# Real-Time Location Sharing Web Application

A secure real-time location sharing web application built with C (WinSock2), WebSockets, SQLite3, and vanilla JavaScript.

## Architecture

```
[Web Browser] ←→ [WebSocket over TCP] ←→ [C Server (WinSock2)] ←→ [SQLite3 DB]
     ^                                          |
     |                                          v
  Leaflet.js                              fisac.db
  OpenStreetMap                     (users, locations, logs)
```

**No external dependencies** — the server embeds SQLite3 directly. No database installation, no Python service. Just compile and run.

## Quick Start

### Prerequisites
- **GCC** (MinGW recommended) with `mingw32-make`
- **Windows 10/11**
- **Python 3.9+** with `Flask` (backend API)

Install Python dependencies:

```bash
python -m pip install -r scripts/requirements.txt
```

### Build & Run

```batch
run_all.bat
```

Non-interactive mode (for CI or scripted runs):

```batch
run_all.bat --no-pause
```

Or manually:

```bash
mingw32-make
server.exe
# Then open frontend/index.html in your browser
```

### Test Users
| Username | Password |
|----------|----------|
| user1    | pass1    |
| user2    | pass2    |
| user3    | pass3    |
| admin    | admin123 |

## Project Structure

```
FISAC1/
├── src/                    # C Source files
│   ├── main.c              # Entry point, initialization
│   ├── server.c            # select()-based concurrent server
│   ├── socket.c            # WinSock2 socket ops + options
│   ├── network.c           # Robust send/recv with error handling
│   ├── websocket.c         # WebSocket handshake + frame I/O
│   ├── protocol.c          # JSON message handling (auth, location)
│   ├── database.c          # SQLite3 database layer
│   ├── cJSON.c             # JSON parser (3rd party)
│   ├── sha1.c              # SHA-1 for WebSocket handshake
│   ├── base64.c            # Base64 for WebSocket handshake
│   └── sqlite3.c           # SQLite3 amalgamation (3rd party)
├── include/                # Header files
├── frontend/               # Web frontend
│   ├── index.html          # Main page with auth + map
│   ├── style.css           # Dark theme styling
│   └── app.js              # WebSocket client + Leaflet.js map
├── scripts/                # Testing & analysis
│   ├── load_test.py        # 50-client concurrent load test
│   ├── chaos_test.py       # Robustness & edge-case testing
│   ├── log_analyzer.py     # Database log analysis
│   └── schema.sql          # Database schema (reference)
├── docs/                   # Documentation
│   ├── FAILURE_ANALYSIS.md  # Failure scenarios & mitigations
│   └── VIVA_PREP.md        # Viva preparation Q&A
├── Makefile                # Build script
├── run_all.bat             # One-click build & run
└── README.md               # This file
```

## Features

### Core Requirements
- ✅ WebSocket over TCP (WinSock2)
- ✅ Concurrent client handling via `select()` I/O multiplexing
- ✅ User authentication with password hashing
- ✅ Persistent database storage (SQLite3)
- ✅ Location updates with timestamps
- ✅ System event logging (syslogs)
- ✅ Real-time broadcast to authorized subscribers
- ✅ Partial transmission handling (`robust_send`/`robust_recv`)

### Socket Options
- ✅ `SO_REUSEADDR` — Address reuse for rapid server restart
- ✅ `TCP_NODELAY` — Nagle's algorithm disabled for low latency
- ✅ `SO_KEEPALIVE` — Dead connection detection
- ✅ `SO_RCVBUF` — 128KB receive buffer for burst traffic

### Security
- ✅ Authentication required before location updates
- ✅ Password hashing (djb2) stored in database
- ✅ Unauthorized message rejection
- ✅ Session management
- ✅ Input validation

### Robustness
- ✅ Graceful client disconnection handling
- ✅ Signal handler for server shutdown (Ctrl+C)
- ✅ Non-blocking I/O with WSAEWOULDBLOCK handling
- ✅ WSAECONNRESET / WSAECONNABORTED recovery
- ✅ WebSocket close frame (opcode 8) handling

## Running Tests

### Load Test
```bash
pip install websockets
cd scripts
python load_test.py
```

### Chaos Test
```bash
pip install websockets
cd scripts
python chaos_test.py
```

### Log Analysis
```bash
cd scripts
python log_analyzer.py
```

## Key Design Decisions

### Why `select()` over threads?
- No synchronization overhead (no mutexes, no race conditions)
- Lower memory footprint
- Simpler code, fewer concurrency bugs
- Sufficient for I/O-bound relay workloads
- Safe concurrent access to shared client list

### Why SQLite over PostgreSQL?
- Zero-configuration, embedded database
- No external dependencies or services to install
- Single-file database (`fisac.db`)
- WAL mode for concurrent read/write performance
- Perfect for this application's scale

### Why WebSocket over raw TCP?
- Browser-native support (no plugins needed)
- HTTP-compatible (passes through firewalls)
- Message-based framing (vs TCP's stream)
- Standard protocol (RFC 6455)
