# Viva Prep

This document contains a list of potential viva questions related to the project, along with detailed answers.

---

### 1. TCP States: `TIME_WAIT` vs `CLOSE_WAIT`

**Question:** Explain the difference between the `TIME_WAIT` and `CLOSE_WAIT` TCP states. What are the potential problems associated with each, and how does our server handle them?

**Answer:**

*   **`CLOSE_WAIT`**: This state indicates that the remote end of the connection has closed its side of the connection (sent a FIN), and the local end is waiting for the local application to close its socket.
    *   **Problem**: A large number of sockets in `CLOSE_WAIT` is a classic sign of a resource leak. It means that the application is not properly closing its sockets after the client has disconnected. This can lead to the server running out of file descriptors.
    *   **Our Server's Handling**: Our server's `select()` loop constantly monitors sockets for readability. When a client disconnects, `robust_recv()` will return 0. The server interprets this as a disconnection, calls `close(sd)`, and removes the socket from its list of active clients. This ensures that we never have sockets lingering in `CLOSE_WAIT`.

*   **`TIME_WAIT`**: This state (also known as `2MSL wait state`) occurs on the end that initiates the active close. It is a mechanism to ensure that any stray, delayed packets from the old connection do not interfere with a new connection that might be established on the same port. The socket is kept in this state for twice the Maximum Segment Lifetime (MSL).
    *   **Problem**: If a server is handling a very high rate of short-lived connections, a large number of sockets can accumulate in the `TIME_WAIT` state. This can prevent the server from opening new connections on those ports, as the port is temporarily unavailable.
    *   **Our Server's Handling**: We use the `SO_REUSEADDR` socket option. This option tells the kernel that it's okay to reuse a local address (port) that is in the `TIME_WAIT` state. This is crucial for server applications, as it allows us to restart the server quickly without being blocked by sockets from the previous run that are still in `TIME_WAIT`.

---

### 2. `select()` vs Threads

**Question:** Our server uses a single-threaded, `select()`-based concurrency model. What are the advantages and disadvantages of this approach compared to a multi-threaded model where each client is handled by a separate thread?

**Answer:**

*   **Advantages of `select()`**:
    *   **Lower Overhead**: `select()` has much lower overhead compared to creating and managing threads. Threads require their own stack space and involve context switching, which can be expensive.
    *   **Simpler Synchronization**: In a single-threaded model, we don't need to worry about race conditions or deadlocks when accessing shared data (like the list of clients). This makes the code simpler and less error-prone.
    *   **Scalability for I/O-bound tasks**: For an I/O-bound application like our WebSocket server (which spends most of its time waiting for data from clients), `select()` is very efficient. It can handle a large number of connections with a single thread.

*   **Disadvantages of `select()`**:
    *   **No True Parallelism**: A `select()`-based server cannot take advantage of multiple CPU cores. All processing happens in a single thread.
    *   **Blocking Operations**: If any part of the server's logic performs a blocking operation (other than the `select()` call itself), the entire server will freeze. In our case, the `http_post` is a blocking call, which is a weakness. A better implementation would use a non-blocking HTTP client or delegate the HTTP request to a separate thread or process.

*   **Why `select()` is a good choice for our server**: Given that our server's primary role is to multiplex I/O between many clients and a backend service, the `select()` model is a very good fit. The work done by the C server is lightweight, and the single-threaded nature simplifies the code and makes it very efficient at handling a large number of concurrent connections.

---

### 3. WebSocket vs Raw TCP

**Question:** Why did we choose to use WebSockets over raw TCP sockets for communication between the client and the server?

**Answer:**

While WebSockets are built on top of TCP, they provide several key advantages for our web-based application:

*   **Browser Compatibility**: WebSockets are a standard, browser-supported protocol. This means that any modern web browser can act as a client to our server without requiring any special plugins or software. Raw TCP sockets are not directly accessible from browser-based JavaScript.
*   **Firewall Traversal**: WebSocket connections are initiated as a standard HTTP request on port 80 or 443. This allows them to traverse most firewalls and proxies, which might block raw TCP connections on other ports.
*   **Framing and Message-based Communication**: WebSockets provide a message-based communication protocol with a clear framing mechanism. This simplifies the client and server code, as we don't have to deal with the complexities of parsing a continuous stream of data to identify message boundaries. Raw TCP is a stream-based protocol, which would require us to implement our own framing layer.
*   **Protocol Overhead**: While WebSockets do add some overhead compared to raw TCP (for the handshake and frame headers), it is relatively small and is a worthwhile trade-off for the benefits listed above.

---

### 4. Socket Options Impact

**Question:** We've configured several socket options (`SO_REUSEADDR`, `TCP_NODELAY`, `SO_KEEPALIVE`, `SO_RCVBUF`). Choose two of these and explain in detail how they impact the performance and robustness of our specific application.

**Answer:**

*   **`TCP_NODELAY`**: This option has a significant impact on the **latency** of our application. Our application sends small, frequent location updates. Without `TCP_NODELAY`, Nagle's algorithm would likely buffer these small updates, waiting for more data to send in a single packet. This would introduce a noticeable delay between the client sending a location update and the server receiving it. By enabling `TCP_NODELAY`, we ensure that each location update is sent as a separate packet as soon as it's ready, minimizing latency.

*   **`SO_KEEPALIVE`**: This option is crucial for the **robustness** and **resource management** of our server. In a mobile application, clients can lose their network connection without gracefully closing the TCP socket. Without `SO_KEEPALIVE`, these "half-open" connections would remain on the server, consuming a file descriptor and memory in the `clients` array. `SO_KEEPALIVE` enables the kernel to periodically send probes to the client. If the client doesn't respond after a certain number of probes, the kernel will automatically close the connection. This allows our server to detect and clean up dead connections, preventing resource leaks.

---

### 5. Handling Partial `recv()`

**Question:** Describe how a "partial `recv()`" can occur and explain how our server's code is designed to handle it correctly, both for the initial HTTP handshake and for subsequent WebSocket frames.

**Answer:**

A "partial `recv()`" occurs when a call to `recv()` returns with fewer bytes than were requested. This is a normal part of TCP, which is a stream-based protocol, and can happen due to network buffering, packet fragmentation, or the sender simply not having sent all the data yet.

Our server handles this in two key places:

1.  **Initial HTTP Handshake**: The `websocket_handshake` function reads the initial HTTP request from the client. While our current implementation reads into a large buffer in a single call to `robust_recv`, a more robust implementation would loop, calling `recv` until it has received the complete HTTP headers (i.e., until it finds the `

` sequence). Our `robust_recv` itself doesn't loop, but it correctly handles the non-blocking nature of the socket.

2.  **WebSocket Frames**: This is where our handling is more explicit. The `websocket_frame_recv` function is designed to handle partial receives correctly.
    *   First, it reads the initial 2 bytes of the WebSocket frame header to determine the payload length.
    *   If the payload length is 126 or 127, it then reads the next 2 or 8 bytes to get the full payload length.
    *   Crucially, it then enters a loop (implicitly, via repeated calls from the main loop), reading from the socket until it has received the number of bytes specified by the payload length. It will call `robust_recv` as many times as needed to get the full frame payload.

This two-step process of reading the header to determine the length, and then reading until that length is met, ensures that we always process complete WebSocket frames and never misinterpret a partial frame as a complete one.
