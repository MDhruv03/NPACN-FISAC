Member 1: Question 1 - Bullet Answer (Workflow + Concurrency)

Write in bullets:

- Transport and setup
	- TCP connection established to 127.0.0.1:8080.
	- WebSocket upgrade performed over HTTP request response.
	- Security note: current demo uses ws:// on localhost; production path is wss:// with TLS termination.

- Handshake steps
	- Client sends Sec-WebSocket-Key in HTTP Upgrade request.
	- Server computes Sec-WebSocket-Accept using SHA1 + Base64 + WebSocket GUID.
	- Server replies HTTP/1.1 101 Switching Protocols.

- Full-duplex communication
	- Client -> server: auth, register, location frames.
	- Server -> client: auth_response, error, broadcast location frames.
	- Control frames (ping/pong) handled without forced disconnect.

- Graceful termination and TCP states
	- Close frame or socket close triggers server cleanup of client slot.
	- Connection/disconnection events recorded in logs.
	- TIME_WAIT expected during reconnect cycles.
	- CLOSE_WAIT growth treated as cleanup defect indicator.

- Concurrency model justification
	- select()-based single-threaded multiplexing implemented.
	- Chosen for I/O-bound workload and predictable behavior.
	- Shared client-state race conditions minimized vs multi-threaded approach.

- Screenshot evidence to attach
	- docs/q1_workflow_capture.txt (TCP connect, handshake response, auth response)
	- .server.log (connection lifecycle)
	- netstat output for port 8080 states
