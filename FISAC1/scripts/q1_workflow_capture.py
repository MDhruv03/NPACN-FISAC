import base64
import json
import os
import socket
import subprocess
import time

HOST = "127.0.0.1"
PORT = 8080
OUT = os.path.join(os.path.dirname(__file__), "..", "docs", "q1_workflow_capture.txt")


def make_masked_text_frame(payload: bytes) -> bytes:
    mask_key = os.urandom(4)
    n = len(payload)
    b0 = 0x81
    if n <= 125:
        header = bytes([b0, 0x80 | n])
    elif n <= 65535:
        header = bytes([b0, 0x80 | 126]) + n.to_bytes(2, "big")
    else:
        header = bytes([b0, 0x80 | 127]) + n.to_bytes(8, "big")
    masked = bytes([payload[i] ^ mask_key[i % 4] for i in range(n)])
    return header + mask_key + masked


def recv_ws_frame(sock: socket.socket) -> bytes:
    hdr = sock.recv(2)
    if len(hdr) < 2:
        return b""
    b1 = hdr[1]
    n = b1 & 0x7F
    if n == 126:
        n = int.from_bytes(sock.recv(2), "big")
    elif n == 127:
        n = int.from_bytes(sock.recv(8), "big")
    payload = b""
    while len(payload) < n:
        payload += sock.recv(n - len(payload))
    return payload


def netstat_8080() -> str:
    try:
        out = subprocess.check_output("netstat -ano | findstr :8080", shell=True, text=True)
        return out.strip()
    except subprocess.CalledProcessError:
        return "(no entries for :8080)"


def main() -> None:
    key = base64.b64encode(os.urandom(16)).decode()
    upgrade = (
        "GET / HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    ).encode()

    auth = {
        "type": "auth",
        "payload": {"username": "user1", "password": "pass1"},
    }

    lines = []
    lines.append("Q1 Workflow Capture")
    lines.append(time.strftime("Generated: %Y-%m-%d %H:%M:%S"))
    lines.append("")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(8)
    sock.connect((HOST, PORT))

    lines.append("1) TCP connect: SUCCESS")
    sock.sendall(upgrade)
    hs_resp = sock.recv(4096).decode(errors="replace")
    lines.append("2) WebSocket handshake response:")
    lines.append(hs_resp.strip())

    frame = make_masked_text_frame(json.dumps(auth).encode())
    sock.sendall(frame)
    lines.append("3) Auth request sent over WS text frame")

    payload = recv_ws_frame(sock)
    lines.append("4) Auth response payload:")
    lines.append(payload.decode(errors="replace"))

    # Graceful close from client side
    sock.sendall(bytes([0x88, 0x80, 1, 2, 3, 4]))
    sock.close()
    lines.append("5) Client close frame sent and socket closed")
    lines.append("")
    lines.append("6) TCP state snapshot (:8080):")
    lines.append(netstat_8080())

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print(f"[OK] Wrote workflow evidence: {OUT}")


if __name__ == "__main__":
    main()
