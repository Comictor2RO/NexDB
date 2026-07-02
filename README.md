# NexDB

A lightweight relational database engine written in **C++23**, built for use in MVPs and small projects where you need a simple, embedded database with an interactive GUI and remote access over TCP.

---

## What is this?

NexDB is a self-contained SQL database engine you can drop into a project and run locally. It exposes a **TCP server** so any application — regardless of language — can send SQL queries and receive results over a plain text protocol. It also ships with a **Raylib-based GUI** for interactive query execution and visual result browsing.

It is intentionally minimal. No heavy dependencies, no installation. Compile and run.

---

## Why use it in an MVP?

- Zero infrastructure — runs as a single binary on any Windows machine
- Remote access via TCP — connect from C#, Python, Node, or anything that can open a socket
- Persistent storage with WAL-based crash recovery and fsync durability
- B+ tree indexing for fast primary key lookups
- Thread-safe — concurrent reads and serialized writes via shared mutexes
- Challenge-response authentication with rate limiting and IP banning
- Configurable via `config.json` — no recompilation needed
- GUI for quick manual queries during development

---

## Interface

The GUI is intentionally simple and split into 3 sections: a **query input** area where you type SQL, a **result panel** that displays the returned rows, and a **logs** section that shows server activity and execution messages.

![Main Interface](docs/screenshots/gui_main.png)
If the command succeeds, the result will appear in the **result panel** and a confirmation message will be written to the **logs** section.

![Query Results](docs/screenshots/gui_results.png)
Once you press the **Start Server** button, the **logs** section will display the port the TCP server has opened on — use that port to connect from your application.

![Server Status](docs/screenshots/gui_server.png)

---

## What's new in this version

### Multi-table database files
Previously each table was stored in its own `.db` file. Now all tables share a single `.db` file per database. Pages are tagged with a `tableId` in the page header — the storage layer allocates and reclaims pages per table automatically.

### `databases/` folder
All database files (`*.db`, `*.cat`, `*.wal`) are now stored inside a `databases/` subdirectory, created automatically on startup. The active database is selected via `config.json`.

### `CREATE DATABASE` / `USE DATABASE` commands
Two new SQL commands for managing multiple databases:

```sql
CREATE DATABASE myproject
USE DATABASE myproject
USE myproject
```

`CREATE DATABASE` creates the database files on disk. `USE DATABASE` signals the client to switch context (returns `SWITCH <name>` over TCP — the client is expected to reconnect targeting that database).

### Localhost bypass
Connections from `127.0.0.1` or `::1` skip the challenge-response handshake and receive `AUTH OK` immediately. This makes local tooling (Python scripts, GUI) simpler. Remote connections still require full authentication.

### FLOAT and BOOL column types
Two new column types are now supported in addition to `INT` and `STRING`:

- `FLOAT` — stores decimal numbers (e.g. `3.14`, `-0.5`)
- `BOOL` — stores boolean values: `true`, `false`, `1`, `0`, `TRUE`, `FALSE`

NULL values (`NULL`) can be inserted into any column regardless of type.

### Concurrent connections
The TCP server now runs an ASIO I/O thread pool. The number of threads is set via `thread_count` in `config.json` (default 4). Multiple clients can be handled simultaneously; engine access is serialized internally via a mutex.

### Atomic DELETE and UPDATE (WAL)
DELETE and UPDATE operations are now fully atomic. The WAL logs the complete post-operation state before modifying storage using a `REPLACE_BEGIN` / `REPLACE_ROW` pattern. On crash recovery, the engine can always restore the correct final state without data loss or partial writes. WAL values are percent-encoded to safely handle any delimiter characters inside stored data.

---

## Configuration

NexDB reads `config.json` from the same directory as the executable at startup. If the file is missing, built-in defaults are used.

```json
{
  "port": 3000,
  "page_size": 4096,
  "cache_capacity": 128,
  "aux_max_failures": 3,
  "aux_timeout": 30,
  "thread_count": 4,
  "database": "mydb"
}
```

| Field | Default | Description |
|-------|---------|-------------|
| `port` | `3000` | TCP port the server binds to |
| `page_size` | `4096` | Storage page size in bytes (must match build) |
| `cache_capacity` | `128` | Number of pages held in LRU cache |
| `aux_max_failures` | `3` | Failed auth attempts before IP ban |
| `aux_timeout` | `30` | Ban duration in seconds |
| `thread_count` | `4` | Number of threads in the ASIO I/O thread pool |
| `database` | `"mydb"` | Active database name (files stored in `databases/`) |

---

## TCP Protocol

The server listens on the port configured in `config.json` (default 3000).

### Authentication

Connections from **localhost** (`127.0.0.1` / `::1`) receive `AUTH OK` immediately — no handshake required.

Remote connections must complete a challenge-response handshake:

```
Server → Client:  CHALLENGE <16-byte-hex-nonce>\n
Client → Server:  AUTH <SHA256(secret + nonce)>\n
Server → Client:  AUTH OK\n   (or AUTH FAIL\n / AUTH BANNED\n)
```

The shared secret is auto-generated on first run and saved to `server_auth.conf`. You can override it by setting the `VDT_AUTH_TOKEN` environment variable.

After 3 failed attempts the IP is banned for 30 seconds (configurable via `config.json`).

### Queries

After `AUTH OK`, send SQL queries terminated with `\n`:

```
SELECT * FROM users WHERE age > 18\n
```

**Response formats:** every response is a **single line of JSON** terminated by `\n`.

| Case | Response |
|------|----------|
| Success, no rows | `{"type": "ok"}` |
| Success, with rows | `{"type": "rows", "rows": [["1", "Alice"], ["2", "Bob"]]}` |
| Database switch (`USE`) | `{"type": "switch", "db": "<dbname>"}` |
| Error | `{"type": "error", "message": "<message>"}` |

All values are JSON-escaped, so any character — including `|`, newlines, or the literal
text `END` — is safe inside a value. Clients read one line and parse it with a standard
JSON parser (`JSON.parse`, `json.loads`, `System.Text.Json`, …).

---

## Connecting from C#

### Basic connection helper (with auth)

```csharp
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Security.Cryptography;

public class NexDBClient : IDisposable
{
    private readonly TcpClient _client;
    private readonly NetworkStream _stream;
    private readonly StreamReader _reader;
    private readonly StreamWriter _writer;

    public NexDBClient(string host, int port, string secret)
    {
        _client = new TcpClient(host, port);
        _stream = _client.GetStream();
        _reader = new StreamReader(_stream, Encoding.UTF8);
        _writer = new StreamWriter(_stream, Encoding.UTF8) { AutoFlush = true };

        // Challenge-response handshake
        string challenge = _reader.ReadLine()!; // "CHALLENGE <nonce>"
        string nonce = challenge.Split(' ')[1];
        string hash = ComputeSha256(secret + nonce);
        _writer.WriteLine("AUTH " + hash);

        string authResult = _reader.ReadLine()!;
        if (authResult != "AUTH OK")
            throw new Exception("Authentication failed: " + authResult);
    }

    private static string ComputeSha256(string input)
    {
        byte[] bytes = SHA256.HashData(Encoding.UTF8.GetBytes(input));
        return Convert.ToHexString(bytes).ToLower();
    }

    public List<string[]> Query(string sql)
    {
        _writer.WriteLine(sql);

        // Each response is a single line of JSON.
        string line = _reader.ReadLine() ?? throw new Exception("Connection closed by server");

        using var doc = JsonDocument.Parse(line);
        string type = doc.RootElement.GetProperty("type").GetString()!;

        switch (type)
        {
            case "ok":
            case "switch":
                return new List<string[]>();
            case "error":
                throw new Exception(doc.RootElement.GetProperty("message").GetString());
            case "rows":
                var rows = new List<string[]>();
                foreach (var row in doc.RootElement.GetProperty("rows").EnumerateArray())
                {
                    var cells = new List<string>();
                    foreach (var cell in row.EnumerateArray())
                        cells.Add(cell.GetString()!);
                    rows.Add(cells.ToArray());
                }
                return rows;
            default:
                throw new Exception("Unknown response type: " + type);
        }
    }

    public void Dispose()
    {
        _reader.Dispose();
        _writer.Dispose();
        _stream.Dispose();
        _client.Dispose();
    }
}
```

### Usage example

```csharp
// Secret is in server_auth.conf next to the executable
string secret = File.ReadAllText("server_auth.conf").Trim();

using var db = new NexDBClient("127.0.0.1", 3000, secret);

db.Query("CREATE TABLE users (id INT, name STRING, age INT, score FLOAT, active BOOL)");
db.Query("INSERT INTO users VALUES (1, Alice, 30, 9.5, true)");
db.Query("INSERT INTO users VALUES (2, Bob, 25, 7.2, false)");

var rows = db.Query("SELECT * FROM users WHERE age > 20");
foreach (var row in rows)
    Console.WriteLine(string.Join(" | ", row));

db.Query("UPDATE users SET age = 31 WHERE id = 1");
db.Query("DELETE FROM users WHERE id = 2");
```

---

## Supported SQL

```sql
-- Database management
CREATE DATABASE myproject
USE DATABASE myproject
USE myproject

-- Table management
CREATE TABLE products (id INT, name STRING, price FLOAT, active BOOL)
DROP TABLE products

-- Data manipulation
INSERT INTO products VALUES (1, Widget, 9.99, true)
INSERT INTO products VALUES (2, 'Out of stock', NULL, false)
SELECT * FROM products
SELECT name, price FROM products WHERE price > 5.0
UPDATE products SET price = 7.99 WHERE id = 1
DELETE FROM products WHERE id = 2
```

Supported column types: `INT`, `STRING`, `FLOAT`, `BOOL`

Any column can store `NULL` as a value regardless of its declared type.

Supported operators in WHERE: `=`, `!=`, `<`, `>`, `<=`, `>=`

String values can optionally be wrapped in single quotes: `WHERE name = 'Alice'` and `VALUES (1, 'Alice')` are both valid.

---

## Building

Dependencies (ASIO, Raylib, GTest) are fetched automatically via CMake FetchContent. `config.json` is copied to the build output directory on every build.

### Windows (MinGW)

Requirements: CMake 3.20+, MinGW with C++23 support.

```bash
cmake -B cmake-build-debug -G "MinGW Makefiles"
cmake --build cmake-build-debug
./cmake-build-debug/bin/NexDB.exe
```

### Linux

Requirements: CMake 3.20+, GCC 13+ or Clang 17+ with C++23 support, plus X11 and OpenGL development libraries.

```bash
# Ubuntu/Debian
sudo apt install cmake build-essential libx11-dev libxrandr-dev libxi-dev \
     libxxf86vm-dev libxinerama-dev libxcursor-dev libgl1-mesa-dev

cmake -B cmake-build-debug
cmake --build cmake-build-debug
./cmake-build-debug/bin/NexDB
```

---

## Architecture

```
NexDB/
├── Config/         JSON config loader (port, cache, auth, database name)
├── Frontend/       SQL lexer + parser → AST
├── Engine/         Query executor
├── AST/            Statement types (Select, Insert, Delete, Update,
│                   Create/Drop Table, Create/Use Database)
├── Storage/
│   ├── Page/         Fixed-size page (4096 bytes), tagged with tableId
│   ├── PageHeader/   Page metadata (pageId, tableId, freeSpace, rowCount)
│   ├── StorageFile/  Shared .db file manager — allocates/frees pages per tableId
│   ├── PageManager/  Per-table read/write with thread-safe LRU cache
│   └── LRUCache/     LRU eviction with dirty-page tracking
├── Indexing/       B+ tree index on first INT column
├── Catalog/        Table schema + tableId registry (persisted to .cat file)
├── WALManager/     Write-ahead log — fsync on commit for crash safety
├── Networking/     ASIO TCP server with challenge-response auth
├── GUI/            Raylib interactive interface
└── tests/          Google Test suite
```

### Thread safety

`PageManager` uses a `std::shared_mutex` for the LRU cache (multiple concurrent readers, exclusive writes) and separate `std::mutex` instances for file I/O and insert serialization. `NetworkServer` runs an ASIO I/O thread pool (`thread_count` threads) and protects the engine with a single `std::mutex` to serialize query execution. The rate-limit map has its own `std::mutex`.

### Storage

Data is stored in fixed-size pages (4096 bytes). Each page header contains a `tableId` field — all tables for a given database share a single `.db` file, with pages tagged by owner. A `StorageFile` class manages the shared file: it allocates pages from a free list (pages with `tableId == 0`), rebuilt by scanning headers on startup.

The LRU cache holds up to `cache_capacity` pages in memory (configurable); dirty pages are flushed to disk on eviction. Each table's `PageManager` tracks its own set of global page IDs.

### WAL & Durability

All mutating operations (INSERT, UPDATE, DELETE) are logged to `engine.wal` before execution. On commit, the log entry is marked committed and `FlushFileBuffers` (Windows) / `fsync` (Linux) is called to guarantee the log is on physical disk before returning. On startup, uncommitted entries are replayed automatically.

DELETE and UPDATE use a `REPLACE_BEGIN` / `REPLACE_ROW` pattern: the complete post-operation row set is written to the WAL before any storage is modified. This makes both operations fully atomic — a crash at any point leaves the engine able to recover the correct final state. WAL field values are percent-encoded so delimiter characters (`|`, `~`, `%`) inside stored data never corrupt the log format.

---
