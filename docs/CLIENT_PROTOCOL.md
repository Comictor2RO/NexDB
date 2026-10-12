# NexDB — Client Implementer's Reference

Everything a client library needs to know, extracted from the server source rather than
from the README. Where the two disagree, this document follows the code and says so.

Source of truth per section:
`Networking/NetworkServer.cpp`, `Engine/Engine.cpp`, `Frontend/Lexer/Lexer.cpp`,
`Frontend/Parser/Parser.cpp`, `Table/Table.cpp`, `Catalog/Catalog.cpp`, `Config/Config.cpp`.

---

## 1. Transport and lifecycle

| Property | Value |
|---|---|
| Transport | Plain TCP, IPv4 only (`acceptor.open(tcp::v4())`) |
| TLS | None. Do not send this across an untrusted network. |
| Encoding | UTF-8, no BOM |
| Framing | Newline (`\n`) delimited, in both directions |
| Model | Strict request/response, one query per line, one response line per query |
| Port | `config.json` → `port`, default `3000` |
| Concurrency | ASIO pool of `thread_count` threads, but all query execution is serialized by a single engine mutex |

**The server is not running by default.** `main.cpp` starts the Raylib GUI only; the TCP
listener is opened when the user presses *Start Server*, which calls
`NetworkServer::prepare()`. A client that gets `ECONNREFUSED` most likely faces an app
where nobody pressed the button — worth saying so in the error message.

Once authenticated, the server never closes an idle connection and never sends unsolicited
data. There is no ping, no keepalive, no server-side idle timeout, and no `QUIT` command —
closing the socket is how a client disconnects. The one timeout is during the handshake
(§2.3).

### Line endings

The server strips a single trailing `\r`, so `\r\n` from a client is accepted. The server
always terminates its own lines with a bare `\n`.

---

## 2. Handshake

Three lines at most, always in this order. Read them **before** sending anything.

### 2.1 Protocol banner

The first line on every connection, written before any auth logic:

```
S → C:  NEXDB/1.0.0\n
```

The constant is `NetworkServer::PROTOCOL_VERSION`. Check the **major** version with a
prefix test (`NEXDB/1.`) and refuse anything else — the version is bumped whenever the
wire format changes. Do not compare the full string, or your client breaks on `1.1.0`.

### 2.2 Localhost bypass

When `bypass_localhost` is `true` (default) and the peer is `127.0.0.1` or `::1`, the
server skips the challenge entirely:

```
S → C:  NEXDB/1.0.0\n
S → C:  AUTH OK\n
```

Note this is the *only* difference. The banner is still sent. A client that reads one line
and assumes it is the auth result will be desynchronized by exactly one line for the rest
of the session — every response will look like the answer to the previous query.

### 2.3 Challenge-response

For everything else:

```
S → C:  NEXDB/1.0.0\n
S → C:  CHALLENGE <nonce>\n
C → S:  AUTH <hex>\n
S → C:  AUTH OK\n        |  AUTH FAIL\n  |  AUTH BANNED\n
```

- `<nonce>` is 32 lowercase hex characters (16 bytes, generated per connection).
- `<hex>` is `lowercase_hex(SHA256(secret + nonce))` — plain concatenation of the two
  ASCII strings, no separator, no HMAC, no salt.
- The comparison is an exact string match against `"AUTH " + expected`.

On `AUTH FAIL` and `AUTH BANNED` the server writes the line and drops the connection
without reading further. Treat both as fatal; do not retry on the same socket.

The `AUTH` line must arrive within `handshake_timeout` seconds (default 5) of the
`CHALLENGE`. Otherwise the server closes the socket **without sending anything** — the
client sees EOF where it expected the auth result. A timeout does not count toward the ban
(§2.5). Compute the hash before connecting, or right after reading `CHALLENGE`; do not
wait on user input in between.

`AUTH BANNED` is also sent *immediately* — before any `CHALLENGE` — when the IP is
already banned. So after the banner the second line can be any of `AUTH OK`,
`CHALLENGE …`, or `AUTH BANNED`. Parse defensively.

### 2.4 Where the secret comes from

The server resolves it once per *Start Server*, in this order
(`NetworkServer::loadOrCreateSecret`):

1. `$VDT_AUTH_TOKEN` if non-empty
2. First line of `server_auth.conf` in the server's working directory
3. Otherwise: generate 32 hex chars, write them to `server_auth.conf`, log them

A client should mirror steps 1–2 as a convenience default (explicit argument →
`$VDT_AUTH_TOKEN` → `server_auth.conf`) but must not require a secret, because localhost
usually will not need one. Note that `server_auth.conf` lives next to the *server*
executable's working directory (for CLion builds, the build output dir), which is
frequently not the client's cwd — so make the path configurable.

### 2.5 Rate limiting

Per-IP, in-memory, reset when the server restarts.

| Knob | Config key | Default |
|---|---|---|
| Failures before ban | `aux_max_failures` | 3 |
| Ban duration | `aux_timeout` | 30 s |

The counter resets to zero on a successful auth, and the ban lapses on the first
connection attempt after `bannedUntil`. Clients that auto-retry a bad secret will lock
themselves out — fail fast instead.

---

## 3. Requests

```
C → S:  <one SQL statement>\n
```

- **Exactly one statement per line.** There is no multi-statement support; a `\n` inside
  the query text would be read as a frame boundary and the tail parsed as a second query.
  Reject queries containing `\n` or `\r` client-side with a clear error.
- A trailing `;` is harmless — the lexer silently discards characters it does not
  recognize, and `;` is one of them. So is `"`, `-`, `@`, `#`, and anything else outside
  the token alphabet. See §6.1, because this is a sharp edge.
- No query IDs, no cancellation, no timeouts on the server side. A slow query blocks the
  engine mutex for every other client.
- Unbounded line length (`async_read_until` on an unbounded `streambuf`). Practically,
  keep a row under the page size — see §5.5.

Pipelining technically works (the read buffer keeps whatever arrived early) and responses
come back in order, but nothing in the server guarantees it. Build the client as strict
request/response and serialize calls with a lock.

---

## 4. Responses

Every response is **exactly one line of JSON** terminated by `\n`. Four shapes, keyed on
`type`:

| `type` | Emitted for | Payload |
|---|---|---|
| `ok` | INSERT, UPDATE, DELETE, CREATE TABLE, DROP TABLE | — |
| `rows` | SELECT (including a SELECT matching nothing) | `columns`, `rows` |
| `switch` | USE / USE DATABASE | `db` |
| `error` | any failure | `message` |

```json
{"type": "ok"}
{"type": "rows", "columns": ["id", "name"], "rows": [["1", "Alice"], ["2", "Bob"]]}
{"type": "switch", "db": "myproject"}
{"type": "error", "message": "Table users does not exist"}
```

Rules that matter when writing the parser:

- **`rows` and `columns` are always both present** on a `rows` response. An empty result
  is `{"type": "rows", "columns": [...], "rows": []}` — the header survives, so a client
  can still report column names for a zero-row SELECT. Only non-SELECT statements collapse
  to `{"type": "ok"}`.
- **Column order matches value order** in every row, so `dict(zip(columns, row))` is
  always correct. For `SELECT *` the order is the schema's declaration order; for an
  explicit list it is the order the user asked for.
- **Every value is a JSON string.** There are no JSON numbers, booleans, or nulls in a
  `rows` payload — the engine stores everything as text and `jsonEscape()` quotes it
  unconditionally. `42` comes back as `"42"`, `true` as `"true"`, and SQL `NULL` as the
  four-character string `"NULL"`.
- **Values are fully escaped**, including `\n`, `\r`, `\t`, `"`, `\\` and any control byte
  below 0x20 as `\uXXXX`. A value can safely contain `|`, a newline, or the literal text
  `END`. Any standard JSON parser handles the line correctly.
- **An `error` does not close the connection.** `executeQuery` catches every exception and
  the read loop re-arms. The connection stays usable, so map errors to a raised exception
  in the client, not to a disconnect.
- A `switch` response also means the query succeeded — see §7.6 for why it is not
  per-connection.

The only ways the connection actually ends are a socket error, the server being stopped,
or the client closing it.

---

## 5. Values and types

This is where most client bugs will come from. All four rules below are enforced in
`Table::validateValueForType`.

### 5.1 The four column types

| Type | Accepted values (exact) |
|---|---|
| `INT` | optional `+`/`-`, then one or more ASCII digits |
| `FLOAT` | optional `+`/`-`, digits and at most one `.` — no exponent, `3` is valid |
| `BOOL` | one of `true`, `false`, `1`, `0`, `TRUE`, `FALSE` — nothing else |
| `STRING` | anything, always accepted |

`BOOLEAN` and `TEXT` are accepted by the validator but rejected by the parser at
`CREATE TABLE`, which only allows `INT`, `STRING`, `FLOAT`, `BOOL`. Treat those four as
the whole type system.

### 5.2 `Python bool` is a trap

`str(True)` is `'True'`, which is **not** in the accepted BOOL list — `TRUE` and `true`
are, `True` is not. Any client that formats values must map booleans explicitly:

```python
'true' if value else 'false'
```

### 5.3 NULL is not universal — the README is wrong here

The README claims *"NULL values can be inserted into any column regardless of type."*
The code does not implement this. There is no NULL special case anywhere in
`insertRow`; `validateValueForType("NULL", "INT")` walks the characters, hits `N`, and
returns false. In practice:

| Column type | `NULL` accepted? |
|---|---|
| `STRING` | yes — stored and returned as the literal string `"NULL"` |
| `INT`, `FLOAT`, `BOOL` | **no** — `Type validation failed (invalid value for column type)` |

Do not advertise NULL support in the client API, and do not translate Python's `None` to
`NULL` silently — it will fail on three of the four types. Either raise on `None` or
require the caller to be explicit.

### 5.4 Empty strings cannot be inserted

`INSERT INTO t VALUES (1, '')` is a **parse error**, not an empty value. `expectToken`
returns `""` both for "no such token" and for "the token's value is the empty string", so
the parser cannot tell them apart and reports `Unexpected token in query`. The same
applies to an empty value in `UPDATE ... SET col = ''` and in a `WHERE` comparison.

If a client needs to represent emptiness, it has to pick a sentinel and document it.

### 5.5 Size limits

A row is serialized as its values joined by `|`, each percent-encoded (`%` → `%25`,
`|` → `%7C`, `~` → `%7E`, `\n` → `%0A`, `\r` → `%0D`), and must fit inside a single page:
`PAGE_SIZE` (4096 by default) minus the page header, minus one byte for the row
terminator. Budget roughly 4 000 bytes for the encoded row and surface
`Page manager full (disk space or page limit reached)` as a distinct error.

The encoding is internal — values round-trip, so a client never sees percent-encoding.

---

## 6. SQL surface

### 6.1 Lexer behaviour

- **Keywords are case-insensitive** (uppercased on read): `SELECT INSERT DELETE FROM WHERE
  INTO VALUES CREATE TABLE INT STRING DROP UPDATE SET USE DATABASE FLOAT BOOL`.
- **Identifiers keep their case** and are compared case-**sensitively** everywhere
  (table names, column names). `Users` and `users` are two different tables.
- Identifiers match `[A-Za-z][A-Za-z0-9_]*` — they must start with a letter.
- Strings use **single quotes**, with `''` as the escape for a literal `'`:
  `'don''t'` stores `don't`. Double quotes are not string delimiters.
- Numbers are digits with at most one `.`.
- **Every unrecognized character is silently dropped.** This is the sharpest edge in the
  whole grammar, because it turns typos into wrong data instead of errors:

  | Input | Actually executed | Effect |
  |---|---|---|
  | `VALUES (1, -5)` | `VALUES (1, 5)` | **the minus sign is discarded** |
  | `VALUES (1, "Alice")` | `VALUES (1, Alice)` | quotes vanish, works by accident |
  | `WHERE price > -0.5` | `WHERE price > 0.5` | silently wrong result set |
  | `SELECT * FROM t;` | `SELECT * FROM t` | harmless |

  **Negative numbers cannot currently be written into a query.** A client cannot work
  around this at the protocol level — either the lexer gains a `-` case, or the client
  documents that negative literals are unsupported and rejects them up front rather than
  letting the server store the wrong sign.

### 6.2 Grammar

```
CREATE TABLE <table> (<col> <TYPE> [, <col> <TYPE>]*)
DROP TABLE <table>
CREATE DATABASE <name>        -- parses, then refused for remote clients (§7.8)
DROP DATABASE <name>          -- parses, then refused for remote clients (§7.8)
USE [DATABASE] <name>

INSERT INTO <table> [(<col>[, <col>]*)] VALUES (<val>[, <val>]*)
SELECT * | <col>[, <col>]* FROM <table> [WHERE <cond>]
UPDATE <table> SET <col> = <val> [, <col> = <val>]* [WHERE <cond>]
DELETE FROM <table> [WHERE <cond>]

<cond> := <col> <op> <val>
<op>   := = | != | < | > | <= | >=
```

Anything after the end of a statement is `Unexpected tokens after end of statement`.

### 6.3 Not supported

No `AND`/`OR` (exactly **one** condition per `WHERE`), no `ORDER BY`, `LIMIT`, `OFFSET`,
`GROUP BY`, `JOIN`, subqueries, aggregates (`COUNT`, `SUM`, …), `DISTINCT`, `ALTER TABLE`,
`CREATE INDEX`, transactions (`BEGIN`/`COMMIT`/`ROLLBACK`), prepared statements, or
parameter placeholders.

The absence of placeholders is worth calling out in the client's README: since values are
interpolated into a text query, a client that offers a convenience `insert(table, dict)`
helper owns the escaping (double every `'`, reject `\n`, reject `-`).

### 6.4 The INSERT column list is parsed and then ignored

`INSERT INTO users (name, id) VALUES ('Alice', 1)` parses fine — the parser only checks
that the count matches — but `Engine::executeInsert` does `row.values = stmt.getValues()`
and never looks at the column names. Values are always applied **positionally, in schema
order**. So that statement writes `name = 'Alice'` into the `id` column.

A client should either omit the feature or reorder the values itself against a cached
schema before sending.

---

## 7. Execution semantics worth encoding in tests

### 7.1 `=` and `!=` are string comparisons

`evaluateCondition` compares raw stored text for `=` and `!=`, and only converts with
`std::stod` for `<`, `>`, `<=`, `>=`. Consequences:

- `WHERE price = 9.90` does not match a stored `9.9`.
- `WHERE active = TRUE` does not match a stored `true`.
- `WHERE id = 007` does not match a stored `7`.
- but `WHERE price > 9.8` does match `9.9`, numerically.

A non-numeric operand in an ordering comparison throws inside `stod`, is caught, and the
row simply does not match.

### 7.2 An unknown column in WHERE is not an error

If the condition names a column that does not exist, `evaluateCondition` returns false for
every row and the query succeeds with zero results. A typo in a `WHERE` clause is
indistinguishable from "no matching rows". Only the **SELECT list** validates column names
(`Column X does not exist in table Y`).

### 7.3 Equality on the first INT column returns at most one row

If the first column is `INT`, it is indexed by a B+ tree, and `WHERE <firstcol> = <n>`
takes the index path, which returns a single record. Duplicates are not rejected on
insert, so a table with two rows sharing that value will return **one** row for the
equality query but **both** for `SELECT *` or for the same filter expressed as `>=`/`<=`.

Treat the first INT column as an unenforced primary key and document it.

### 7.4 UPDATE fails silently

`previewUpdate` returns an empty vector — and `updateRow` then does nothing — when:

- no row matches the condition,
- an assignment names a column that does not exist,
- an assigned value fails type validation.

All three return `{"type": "ok"}`. The client cannot distinguish "updated 12 rows" from
"the column name was misspelled". There is no affected-row count anywhere in the protocol;
do not invent a `rowcount` that always reads 0. If confirmation matters, follow the UPDATE
with a SELECT.

### 7.5 CREATE TABLE on an existing table is a silent no-op

`Catalog::createTable` returns early if the name is taken, and the response is
`{"type": "ok"}`. It behaves like `CREATE TABLE IF NOT EXISTS`, but with no error if the
existing schema differs from the one requested. A client offering `create_table()` should
verify the schema itself if that matters.

Same for `DROP TABLE` on a missing table — except that one *does* error, from
`Engine::executeDrop`.

### 7.6 USE switches the database for every client at once

`Engine::executeUseDatabase` calls `switchDatabase()`, which tears down and rebuilds the
catalog, storage, and WAL on the single shared `Engine`. There is one engine per server
process and no per-connection state, so a `USE` issued by one client silently moves every
other connected client to the new database.

The `{"type": "switch"}` response is only a notification that this already happened — the
README's "the client is expected to reconnect" is misleading, since reconnecting changes
nothing and there is no way to select a database at connect time.

Implications for the client API:

- Do not expose a `database=` constructor parameter. It cannot be honoured.
- If `use()` is exposed at all, document it as process-global and not connection-scoped.
- Track the last-seen `db` from `switch` responses as a *hint*, and know it can be
  invalidated by another client at any moment.

### 7.7 DELETE and UPDATE rewrite the entire table

Both call `pageManager.clearAll()` and re-insert the surviving rows, which also rebuilds
the index and can change row ordering. There is no ordering guarantee on `SELECT` at any
time — no `ORDER BY` exists, and physical order changes after any mutation. A client must
not present result order as stable.

`DELETE FROM t` with no `WHERE` deletes every row.

### 7.8 CREATE DATABASE and DROP DATABASE are refused over the network by default

Both statements parse normally. The refusal happens in `Engine::query`, after the parse and
before any execution, on the statement type:

```json
{"type": "error", "message": "CREATE DATABASE/DROP DATABASE is not permitted for remote clients"}
```

Because the check reads the AST and not the query text, no spelling gets around it:
`CREATE DATABASE x`, `create database x` and `   CrEaTe   DaTaBaSe   x` all produce that same
error. Nothing is created, deleted or switched, and the error is an ordinary error frame — the
connection stays open and usable (§4).

**This is a server setting, not a protocol rule.** `allow_remote_db_admin` in `config.json`
(§9) defaults to `false`, and an operator can set it to `true`, at which point both statements
succeed with `{"type": "ok"}` and behave exactly as described at the end of this section. A
client cannot detect which way it is configured except by trying, and there is no capability
negotiation in the handshake.

So do not hard-code either assumption. Rejecting both statements client-side is reasonable as a
default, but make it overridable rather than absolute, and handle the server's error in any
case: a hand-written query, or an older client against a newer server, will hit it.

**The in-process GUI can still issue both**, on the same engine. So a database can appear or
disappear underneath a connected client without any query of its own — the same class of
surprise as `USE` in §7.6. A client cannot treat the set of databases as stable, and the
protocol offers no way to enumerate them anyway.

For the record, the local semantics the block hides from clients: `CREATE DATABASE` is
idempotent — it creates `databases/<name>.db` and `.cat` only if absent and never errors.
`DROP DATABASE` deletes `.wal`, `.cat`, `.db` in that order (a partial failure leaves the
`.db` in place, so the statement can be retried), refuses the currently active database
case-insensitively, and errors on a missing one. It writes no WAL record, so it is never
replayed on recovery and cannot be rolled back. None of it is reachable from a client, which
also makes three of the engine's error messages unreachable (§8).

---

## 8. Error messages

`message` is free text, not a code. Exhaustive list from the sources, useful for tests and
for deciding whether to sub-class exceptions:

**Parse errors**, all prefixed `Parse error: ` (`Frontend/Parser/Parser.hpp`):

```
Unexpected token in query
Missing required keyword
Missing punctuation (e.g. parenthesis or comma)
Invalid identifier
Invalid column type
Empty column or value list
Column count does not match value count
Trailing comma
Unexpected tokens after end of statement
Unexpected end of query
```

An empty line yields `Parse error: Missing required keyword`.

**Engine errors** (`Engine/Engine.cpp`):

```
Table <name> does not exist
Column <name> does not exist in table <table>
Database '<name>' does not exist (use CREATE DATABASE first)
CREATE DATABASE/DROP DATABASE is not permitted for remote clients
Invalid SQL query
Insert failed for table <name>: Column count mismatch (expected N columns)
Insert failed for table <name>: Type validation failed (invalid value for column type)
Insert failed for table <name>: Page manager full (disk space or page limit reached)
Insert failed for table <name>: Index insertion failed (B+Tree error)
```

Three more exist in `Engine.cpp` but no client can ever observe them, because they are raised
inside `executeDropDatabase`, which the network path refuses before reaching (§7.8):

```
Database '<name>' does not exist
Cannot drop the currently active database '<name>' (USE another database first)
Failed to delete database '<name>': <.ext> is in use
```

Note the first of those: `Engine.cpp` has two near-identical `Database '<name>' does not exist`
messages, and only the `USE` one — which appends `(use CREATE DATABASE first)` — is reachable
from a client. The bare variant belongs to `DROP DATABASE`. Another reason not to match on
these strings.

Plus anything thrown from the storage layer, e.g.
`Index record corrupted: row N not found in page M`.

Since these are prose, a client should expose the raw string and not pattern-match on it —
the messages are not part of the versioned protocol.

---

## 9. Server config that clients can observe

`config.json`, read once at startup from the executable's working directory. A malformed value
— or an unrecognised key — aborts the process at launch, so a running server always has valid
values, and a mistyped key is never silently ignored.

| Key | Default | Range | Client-visible effect |
|---|---|---|---|
| `port` | 3000 | 1–65535 | where to connect |
| `bypass_localhost` | `true` | bool | whether localhost skips auth |
| `allow_remote_db_admin` | `false` | bool | whether `CREATE DATABASE` / `DROP DATABASE` are accepted (§7.8) |
| `aux_max_failures` | 3 | > 0 | failed auths before ban |
| `aux_timeout` | 30 | > 0 | ban seconds |
| `thread_count` | 4 | > 0 | io threads; connections are async, so this does not cap them (execution still serialized) |
| `handshake_timeout` | 5 | > 0 | seconds to send `AUTH` after `CHALLENGE` before the server closes the socket (§2.3) |
| `page_size` | 4096 | > 0 | max row size; must match the compiled constant |
| `cache_capacity` | 128 | > 0 | none |
| `database` | `mydb` | — | which database is active at startup |

There is no way to query any of this over the wire. A client cannot discover the schema
either — there is no `SHOW TABLES` or `DESCRIBE`. The only introspection available is
`SELECT * FROM <table>` and reading the `columns` header.

---

## 10. Client checklist

Minimum for correctness:

- [ ] Read the banner **and** the auth line before the first query
- [ ] Prefix-match `NEXDB/1.` rather than the exact version
- [ ] Handle all three post-banner lines: `AUTH OK`, `CHALLENGE …`, `AUTH BANNED`
- [ ] `sha256(secret + nonce)`, lowercase hex
- [ ] Socket timeout on connect and on every read; a hung server must not hang the caller
- [ ] Read exactly one line per query, parse as JSON, dispatch on `type`
- [ ] Reject queries containing `\n` / `\r` before sending
- [ ] Map `error` to a raised exception, and keep the connection open
- [ ] Serialize concurrent `query()` calls with a lock, or document one-connection-per-thread
- [ ] Treat an empty read as "server closed" and mark the connection dead
- [ ] `close()` plus context-manager support

Worth surfacing in the client's own docs, because they will surprise users:

- [ ] Values always come back as strings — including `"NULL"`
- [ ] Booleans must be sent as `true`/`false`, never Python's `True`
- [ ] `NULL` only works in `STRING` columns
- [ ] Empty strings cannot be stored
- [ ] Negative numbers are silently stripped of their sign by the lexer
- [ ] The `INSERT` column list is ignored — values are positional
- [ ] `USE` affects every connected client
- [ ] `CREATE DATABASE` / `DROP DATABASE` are refused by the server, not only by the client —
      and the GUI can still run them, so databases move under you
- [ ] `UPDATE` reports success even when it changed nothing
- [ ] One condition per `WHERE`, no `AND`/`OR`, no `ORDER BY`/`LIMIT`

### A mock server is enough to test all of it

Every behaviour above is observable over a socket, so the package's test suite does not
need the C++ build: a fixture that writes the banner, optionally challenges, and replays
canned JSON lines covers the handshake, all four response types, desync, malformed JSON,
mid-query disconnect, and timeout paths.
