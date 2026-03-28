# Failure Analysis

This document outlines potential failure scenarios for the real-time location sharing application and describes the mitigation strategies that have been implemented or could be implemented.

## System Description

The system consists of:
- A C-based WebSocket server that handles client connections.
- A Python service that processes data and interacts with the database.
- A PostgreSQL database for data storage.
- WebSocket clients that send location data.

---

## Failure Scenarios

### 1. Client Crash

*   **Scenario**: A client application crashes or the underlying OS terminates it. The TCP connection is not gracefully closed.
*   **Mitigation**:
    *   **`SO_KEEPALIVE`**: The `SO_KEEPALIVE` socket option is enabled on the server. This allows the server to detect unresponsive clients and close the dead connections, freeing up resources.
    *   **Code-Level Fix**: The server's `select()` loop will eventually detect that the client socket is no longer readable. The `robust_recv()` function will return 0, indicating that the connection has been closed. The server then cleans up the client's resources.

### 2. Network Congestion

*   **Scenario**: The network between the client and server becomes congested, leading to packet loss and delays.
*   **Mitigation**:
    *   **`SO_RCVBUF`**: The server's receive buffer size has been increased to handle bursts of incoming data, which can help during periods of network congestion.
    *   **Robust I/O**: The `robust_send()` and `robust_recv()` functions are designed to handle partial transmissions, which can occur during network congestion. They will continue to read/write until the entire message is transmitted.
    *   **Client-Side Buffering**: Clients could implement a local buffer to store location updates that cannot be sent due to network congestion. They can then be sent later when the network recovers.

### 3. Partial Transmission

*   **Scenario**: The server receives only a part of a WebSocket frame in a single `recv()` call.
*   **Mitigation**:
    *   **`robust_recv()`**: The `robust_recv()` function is designed to handle this scenario. The `websocket_frame_recv()` function will repeatedly call `robust_recv()` until the full frame has been received, based on the payload length specified in the WebSocket frame header.
    *   **Code-Level Fix**: The `websocket_frame_recv` function reads the frame header to determine the expected payload size, and then continues to read from the socket until all the expected bytes have been received.

### 4. Unauthorized Access

*   **Scenario**: An unauthenticated or malicious user attempts to connect to the server or send data.
*   **Mitigation**:
    *   **Authentication Protocol**: The message protocol includes an `auth` message type. The server should implement logic to verify user credentials against the `users` table in the database.
    *   **Session Management**: After a user is authenticated, the server should generate a session token and send it to the client. The client must then include this token in all subsequent messages. The server would validate this token before processing any message.
    *   **Code-Level Fix (Not Yet Implemented)**:
        1.  In `protocol.c`, the `handle_message` function for the `auth` message type should be updated to query the database and verify the password hash.
        2.  A new table for sessions should be used to store and validate session tokens.
        3.  The server should maintain a state for each client, indicating whether they are authenticated or not. Unauthenticated clients should only be allowed to send `auth` messages.

### 5. Database Unavailability

*   **Scenario**: The Python service is unable to connect to the PostgreSQL database.
*   **Mitigation**:
    *   **Connection Pooling**: The Python service uses a connection pool to manage database connections. This makes the system more resilient to transient database connection issues.
    *   **Error Logging**: The Python service logs all database errors. This is crucial for diagnosing and fixing database-related problems.
    *   **Retry Logic**: The Python service could be enhanced with retry logic. If a database write fails, the service could attempt to retry the operation a few times with an exponential backoff.

### 6. C Server to Python Service Communication Failure

*   **Scenario**: The C server is unable to send data to the Python service.
*   **Mitigation**:
    *   **Asynchronous Communication**: The C server sends data to the Python service asynchronously. The `http_post` function is non-blocking, so a failure to connect to the Python service will not block the main server loop.
    *   **Error Logging**: The C server should be updated to log errors from the `http_post` function. This would provide visibility into communication failures between the two services.
    *   **Local Caching/Queueing**: For higher reliability, the C server could implement a local cache or queue for messages that fail to be sent to the Python service. It could then retry sending them later.
