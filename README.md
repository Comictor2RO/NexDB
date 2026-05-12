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

## Configuration

NexDB reads `config.json` from the same directory as the executable at startup. If the file is missing, built-in defaults are used.

```json
{
  "port": 3000,
  "page_size": 4096,
  "cache_capacity": 128,
  "aux_max_failures": 3,
  "aux_timeout": 30
}
```

| Field | Default | Description |
|-------|---------|-------------|
| `port` | `3000` | TCP port the server binds to |
| `page_size` | `4096` | Storage page size in bytes (must match build) |
| `cache_capacity` | `128` | Number of pages held in LRU cache |
| `aux_max_failures` | `3` | Failed auth attempts before IP ban |
| `aux_timeout` | `30` | Ban duration in seconds |

---

## TCP Protocol

The server listens on the port configured in `config.json` (default 3000).

### Authentication

Every new connection must complete a challenge-response handshake before sending SQL:

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

**Response formats:**
| Case | Response |
|------|----------|
| Success, no rows | `OK\n` |
| Success, with rows | `col1\|col2\|col3\n` per row |
| Error | `ERROR: <message>\n` |

---

## Connecting from C#

### Basic connection helper (with auth)

```csharp
using System.Net.Sockets;
using System.Text;
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

        var rows = new List<string[]>();
        string? line;
        while ((line = _reader.ReadLine()) != null)
        {
            if (line == "OK") break;
            if (line.StartsWith("ERROR:"))
                throw new Exception(line);
            rows.Add(line.Split('|'));
        }
        return rows;
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

db.Query("CREATE TABLE users (id INT, name TEXT, age INT)");
db.Query("INSERT INTO users VALUES (1, Alice, 30)");
db.Query("INSERT INTO users VALUES (2, Bob, 25)");

var rows = db.Query("SELECT * FROM users WHERE age > 20");
foreach (var row in rows)
    Console.WriteLine(string.Join(" | ", row));

db.Query("UPDATE users SET age = 31 WHERE id = 1");
db.Query("DELETE FROM users WHERE id = 2");
```

---

## Supported SQL

```sql
CREATE TABLE products (id INT, name TEXT, price INT)

INSERT INTO products VALUES (1, Widget, 99)

SELECT * FROM products WHERE price > 50

UPDATE products SET price = 79 WHERE id = 1

DELETE FROM products WHERE id = 1

DROP TABLE products
```

Supported types: `INT`, `STRING`, `TEXT`

Supported operators in WHERE: `=`, `!=`, `<`, `>`, `<=`, `>=`

---

## Building

Requirements: CMake 3.20+, MinGW with C++23 support.

```bash
cmake -B cmake-build-debug -G "MinGW Makefiles"
cmake --build cmake-build-debug
./cmake-build-debug/bin/NexDB.exe
```

Dependencies (ASIO, Raylib, GTest) are fetched automatically via CMake FetchContent.

`config.json` is copied automatically to the build output directory on every build.

---

## Architecture

```
NexDB/
├── Config/         JSON config loader (port, cache, auth settings)
├── Frontend/       SQL lexer + parser → AST
├── Engine/         Query executor
├── Storage/
│   ├── Page/       Fixed-size page (4096 bytes)
│   ├── PageHeader/ Page metadata
│   ├── PageManager/ Read/write pages with thread-safe LRU cache
│   └── LRUCache/   LRU eviction with dirty-page tracking
├── Indexing/       B+ tree index on first INT column
├── Catalog/        Table schema registry (persisted to catalog.dat)
├── WALManager/     Write-ahead log — fsync on commit for crash safety
├── Networking/     ASIO TCP server with challenge-response auth
├── GUI/            Raylib interactive interface
└── tests/          Google Test suite
```

### Thread safety

`PageManager` uses a `std::shared_mutex` for the LRU cache (multiple concurrent readers, exclusive writes) and separate `std::mutex` instances for file I/O and insert serialization. `NetworkServer` protects the engine with a `std::mutex` and the rate-limit map with its own `std::mutex`.

### Storage

Data is stored in fixed-size pages (4096 bytes). Each page has a header tracking free space and row count. Rows are serialized as pipe-delimited strings. The LRU cache holds up to `cache_capacity` pages in memory (configurable); dirty pages are flushed to disk on eviction.

### WAL & Durability

All mutating operations (INSERT, UPDATE, DELETE) are logged to `engine.wal` before execution. On commit, the log entry is marked committed and `FlushFileBuffers` (Windows) / `fsync` (Linux) is called to guarantee the log is on physical disk before returning. On startup, uncommitted entries are replayed automatically.

---
