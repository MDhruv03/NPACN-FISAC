# GeoSync (Group 12) - Full Project Presentation Explanation

Version: 2026-04-06
Project: Real-Time Location Sharing Web Application

---

## 1. One-Line Project Summary

GeoSync is a real-time location sharing system where browser clients communicate over WebSocket/TCP with a C (WinSock2) server, and persistence/auth/logging are handled by a local Flask + SQLite backend.

---

## 2. Problem Statement and Assignment Alignment

We needed to build and evaluate a socket-programming based, concurrent, real-time application that demonstrates:

- TCP + WebSocket communication
- Concurrent multi-client handling
- Authentication and authorization
- Persistent storage and event logs
- Real-time broadcast behavior
- Socket option tuning and measurable impact
- Robustness against common failures and abnormal traffic

This implementation addresses all four major evaluation questions:

- Q1: Workflow and concurrency model
- Q2: Server/auth/storage/broadcast pipeline
- Q3: Socket options and performance profile comparison
- Q4: Robustness and failure behavior under stress

---

## 3. Current Runtime Architecture (Important)

### 3.1 Effective runtime architecture in this branch

```text
[Browser Frontend]
    |  WebSocket (ws://localhost:8080)
    v
[C Server: WinSock2 + select()]
    |  HTTP POST (/auth, /register, /location, /log)
    v
[Flask Backend: localhost:5000]
    |
    v
[SQLite DB: fisac.db]
```

### 3.2 Why this note matters

Some older documentation text describes a pure C + embedded SQLite runtime. In the currently runnable setup:

- C server is still the real-time socket core
- Python Flask is active for auth/storage/log endpoints
- `src/database.c` exists, but is not linked in current `Makefile`

So during presentation, describe this as a **hybrid architecture**.

---

## 4. Component Responsibilities

### 4.1 C server side

- `src/main.c`
  - Starts WinSock2
  - Initializes and runs server on port 8080
- `src/server.c`
  - `select()` based multi-client event loop
  - Accepts sockets, handshakes, receives frames, dispatches messages
  - Broadcasts location updates to authorized peers
- `src/websocket.c`
  - RFC6455 handshake and frame parsing/sending
  - Handles masking, extended payload lengths, control frames
- `src/network.c`
  - `robust_send` and `robust_recv` wrappers for partial I/O and socket errors
- `src/socket.c`
  - Socket options: SO_REUSEADDR, SO_KEEPALIVE, SO_RCVBUF, TCP_NODELAY
  - Baseline vs tuned profile via `FISAC_SOCKOPTS_PROFILE`
- `src/protocol.c`
  - Message routing (`auth`, `register`, `location`, `subscribe`, `error`)
  - Auth gate for non-authenticated clients
  - Forwards DB-related work to backend endpoints via HTTP
- `src/http_client.c`
  - Blocking localhost HTTP client for backend calls

### 4.2 Backend side

- `scripts/service.py`
  - Serves frontend page (`/`)
  - Endpoints: `/auth`, `/register`, `/location`, `/log`
  - New endpoint for presentation evidence: `/stats`
  - SQLite schema creation and demo-user seeding

### 4.3 Frontend side

- `frontend/index.html`
  - UI sections for map, auth, metrics, protocol inspector, evidence panels
- `frontend/app.js`
  - WebSocket client logic
  - Geolocation watch and location publishing
  - Peer rendering, trails, and presentation counters
  - Demo actions (burst, malformed payload, reconnect drill, snapshot export)
- `frontend/style.css`
  - Dark responsive dashboard styling

### 4.4 Orchestration

- `run_all.bat`
  - Kills old processes, builds C server, starts backend + server
  - Passes socket profile to both processes
  - Opens frontend with profile in query string

---

## 5. End-to-End Runtime Flow

1. User runs `run_all.bat`
2. Flask backend starts on `127.0.0.1:5000`
3. C server starts on `0.0.0.0:8080`
4. Browser opens frontend and creates WebSocket connection to `ws://localhost:8080`
5. HTTP Upgrade handshake completes (`101 Switching Protocols`)
6. Client sends `auth` or `register` JSON frame
7. C server forwards credentials to Flask `/auth` or `/register`
8. Flask validates against SQLite and returns JSON result
9. C server sends `auth_response` frame back to browser
10. Authenticated client sends `location` updates
11. C server forwards each location to backend `/location` for persistence
12. C server broadcasts location updates to other authenticated clients
13. Event logs are written via backend `/log`

---

## 6. Q1 Explanation (Workflow + Concurrency)

### 6.1 Workflow proof points

From `docs/q1_workflow_capture.txt`:

- TCP connect succeeded
- WebSocket handshake succeeded with `HTTP/1.1 101 Switching Protocols`
- Auth request and successful `auth_response` captured
- TCP state snapshot confirms listening + established connections during run

### 6.2 Concurrency model

- Single-threaded `select()` multiplexing
- One thread monitors all active sockets
- No mutex/lock complexity for shared client state
- Suitable for I/O-bound real-time relay workloads

### 6.3 Why this model was chosen

- Predictable behavior
- Lower concurrency bug risk than ad-hoc multi-threading
- Adequate for project scale (tens of concurrent clients)

---

## 7. Q2 Explanation (Server + Auth + Storage + Broadcast)

### 7.1 Auth pipeline

- Browser sends `auth` / `register` over WebSocket
- C server forwards credentials to Flask
- Flask validates or inserts user in SQLite
- C server returns `auth_response`

### 7.2 Data persistence path

- Authenticated client sends `location`
- C server forwards payload to `/location`
- Flask inserts into `locations` table with timestamp

### 7.3 Broadcast path

- After processing an incoming location message,
- C server relays it to other authenticated sockets
- Unauthorized clients are rejected with error response

### 7.4 DB/log evidence

From `docs/evidence/q2_db_snapshot.txt`:

- users: 32
- locations: 1978
- logs: 1764

This verifies sustained insert activity and event correlation.

---

## 8. Q3 Explanation (Socket Options + Performance)

### 8.1 Socket options configured

- SO_REUSEADDR
- SO_KEEPALIVE
- SO_RCVBUF
- TCP_NODELAY

Profiles:

- Baseline: lower/default behavior
- Tuned: optimized options enabled

### 8.2 Measured results (from evidence)

Source: `docs/evidence/q3_q4_summary.json`

| Metric | Baseline | Tuned |
|---|---:|---:|
| Throughput (msg/s) | 10.73 | 13.84 |
| p95 latency (ms) | 1.16 | 1.07 |
| Errors | 2 | 2 |
| Messages sent | 215 | 278 |
| Messages received | 429 | 646 |

Interpretation:

- Tuned profile increased throughput significantly
- Latency p95 improved slightly
- Error count remained stable

---

## 9. Q4 Explanation (Robustness)

### 9.1 Scenarios tested

- Message flood
- Unauthorized message attempts
- Invalid credential attempts
- Rapid connect/disconnect cycles
- Malformed payload injection
- Idle delay then resumed traffic

### 9.2 Outcomes (from evidence)

Source: `docs/evidence/chaos_tuned.json`

- flood_sent: 200
- unauthorized_attempts: 2
- rapid_connect_disconnect: 45
- malformed_sent: 0
- errors: 13

### 9.3 TCP state sanity checks

Source: `docs/evidence/q3_q4_summary.json`

- time_wait_8080: 1 (expected transient behavior)
- close_wait_8080: 0 (good cleanup behavior)

---

## 10. Frontend Demonstration Layer (Presentation Features)

The UI now includes assignment-aligned evidence panels:

- Q1-Q4 Alignment board
- Workflow timeline (connection/auth/subscribe/streaming states)
- Backend evidence cards (users/locations/logs/profile/sync)
- Robustness snapshot counters
- Protocol inspector (last TX and RX JSON)
- Demo controls:
  - Start/Stop demo route
  - Burst x25
  - Send malformed payload
  - Reconnect drill
  - Export snapshot

These are useful to show both implementation and evaluation in a single live dashboard.

---

## 11. API Contract Summary

### 11.1 WebSocket message types

- `auth`
- `register`
- `auth_response`
- `location`
- `subscribe`
- `error`

### 11.2 Backend HTTP endpoints

- `POST /auth`
- `POST /register`
- `POST /location`
- `POST /log`
- `GET /stats` (presentation/evidence endpoint)

---

## 12. Build and Run Commands

### 12.1 Main run

```bat
run_all.bat
```

### 12.2 Profile-specific run

```bat
run_all.bat --no-pause baseline
run_all.bat --no-pause tuned
```

### 12.3 Python dependencies

```bash
python -m pip install -r scripts/requirements.txt
```

### 12.4 Evaluation scripts

```bash
python scripts/load_test.py
python scripts/chaos_test.py
python scripts/profile_eval.py
```

---

## 13. 10-Minute Presentation Script (Suggested)

### 0:00 - 1:00 Intro

- Problem: real-time multi-user location sharing with socket programming
- Requirement: prove concurrency, persistence, tuning impact, robustness

### 1:00 - 2:30 Architecture

- Show hybrid architecture diagram
- Explain why C handles realtime socket path, Flask handles persistence API

### 2:30 - 4:00 Q1 workflow live

- Login and show workflow timeline changing state-by-state
- Mention handshake/auth evidence file

### 4:00 - 5:30 Q2 pipeline live

- Show location updates on map
- Show protocol inspector TX/RX and backend evidence counts increasing

### 5:30 - 7:00 Q3 performance

- Show baseline vs tuned metrics table from evidence
- Emphasize throughput gain and p95 latency improvement

### 7:00 - 8:30 Q4 robustness

- Trigger burst test and reconnect drill
- Optionally trigger malformed payload action
- Show robustness counters and stable app behavior

### 8:30 - 9:30 Evidence and traceability

- Open `docs/evidence/*` JSON files
- Explain that results are reproducible via scripts

### 9:30 - 10:00 Wrap-up

- Restate that all assignment questions are addressed with measurable outcomes

---

## 14. Evidence Map (What to show on screen)

- Workflow capture: `docs/q1_workflow_capture.txt`
- Q2 backend/db snapshot: `docs/evidence/q2_db_snapshot.txt`
- Baseline performance: `docs/evidence/load_baseline.json`
- Tuned performance: `docs/evidence/load_tuned.json`
- Chaos results: `docs/evidence/chaos_tuned.json`
- Final summary: `docs/evidence/q3_q4_summary.json`

---

## 15. Known Limitations (Honest but safe to present)

- Current deployment uses `ws://` on localhost (not TLS)
- HTTP bridge from C to Flask is blocking in current form
- No advanced role-based access control
- Malformed payload counter in older evidence shows zero due how that script records events

---

## 16. Future Improvements

- Add TLS (`wss://`) via reverse proxy or native TLS integration
- Add non-blocking backend queue or worker model to isolate DB latency
- Add token-based session auth and stronger password hashing
- Add server-side rate limiting per client
- Add observability dashboard for p50/p95/p99 over time

---

## 17. Talking Points for Viva Questions

### 17.1 Why `select()` instead of threads?

- Lower complexity and fewer race conditions for this workload
- Good fit for I/O multiplexing at project scale

### 17.2 Why WebSocket over raw TCP?

- Browser-native full duplex support
- Standardized framing and handshake
- Easy integration with frontend

### 17.3 How partial send/recv is handled?

- `robust_send` loops until full write
- WebSocket frame parser reads exact lengths including extended payload forms

### 17.4 How do you prove robustness?

- Chaos script outcomes + TCP state checks + no CLOSE_WAIT buildup

---

## 18. Final Closing Statement

This project demonstrates practical socket programming in C with real-time multi-client behavior, measurable tuning impact, persistent storage, and failure-oriented validation. The implementation is not only functional but also backed by reproducible evidence for each assignment question.
