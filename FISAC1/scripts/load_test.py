# load_test.py - WebSocket load test for the location sharing server

import asyncio
import websockets
import json
import time
import random
import sys
import argparse

NUM_CLIENTS = 50
UPDATE_INTERVAL = 0.2
TEST_DURATION = 30
DISCONNECT_CHANCE = 0.05
OUTPUT_JSON = None

# Pre-created test users
TEST_USERS = [f"loadtest_{i}" for i in range(NUM_CLIENTS)]

stats = {
    "connected": 0,
    "disconnected": 0,
    "messages_sent": 0,
    "messages_received": 0,
    "errors": 0,
    "latency_samples": [],
    "auth_success": 0,
    "auth_fail": 0,
}

async def register_user(uri, username, password):
    """Register a test user account."""
    try:
        async with websockets.connect(uri) as ws:
            msg = {"type": "register", "payload": {"username": username, "password": password}}
            await ws.send(json.dumps(msg))
            response = await asyncio.wait_for(ws.recv(), timeout=5)
            data = json.loads(response)
            return data.get("payload", {}).get("success", False)
    except Exception:
        return False

async def client_handler(client_id, uri, start_event):
    """Simulate a single client that authenticates and sends location updates."""
    username = TEST_USERS[client_id]
    password = "loadtest"

    # Wait for all clients to be ready
    await start_event.wait()

    reconnect_attempts = 0
    end_time = time.time() + TEST_DURATION

    while time.time() < end_time:
        try:
            async with websockets.connect(uri) as ws:
                stats["connected"] += 1
                reconnect_attempts = 0

                # Authenticate
                auth_msg = {"type": "auth", "payload": {"username": username, "password": password}}
                await ws.send(json.dumps(auth_msg))
                stats["messages_sent"] += 1

                response = await asyncio.wait_for(ws.recv(), timeout=5)
                data = json.loads(response)
                stats["messages_received"] += 1

                if data.get("payload", {}).get("success"):
                    stats["auth_success"] += 1
                else:
                    stats["auth_fail"] += 1
                    # Try registering
                    break

                # Send location updates
                while time.time() < end_time:
                    # Random disconnect
                    if random.random() < DISCONNECT_CHANCE:
                        stats["disconnected"] += 1
                        await ws.close()
                        break

                    lat = 28.6139 + random.uniform(-1, 1)
                    lon = 77.2090 + random.uniform(-1, 1)

                    message = {
                        "type": "location",
                        "payload": {
                            "latitude": lat,
                            "longitude": lon,
                            "userId": username
                        }
                    }

                    send_start = time.time()
                    await ws.send(json.dumps(message))
                    send_time = (time.time() - send_start) * 1000
                    stats["messages_sent"] += 1
                    stats["latency_samples"].append(send_time)

                    # Try to receive broadcasts
                    try:
                        while True:
                            msg = await asyncio.wait_for(ws.recv(), timeout=0.01)
                            stats["messages_received"] += 1
                    except asyncio.TimeoutError:
                        pass

                    await asyncio.sleep(UPDATE_INTERVAL)

        except (websockets.exceptions.ConnectionClosedError, ConnectionRefusedError) as e:
            stats["disconnected"] += 1
            reconnect_attempts += 1
            await asyncio.sleep(min(1.0 * reconnect_attempts, 5.0))
        except Exception as e:
            stats["errors"] += 1
            await asyncio.sleep(1.0)


async def main():
    uri = "ws://localhost:8080"
    print(f"\n{'='*60}")
    print(f"  LOAD TEST: {NUM_CLIENTS} clients, {TEST_DURATION}s duration")
    print(f"  Update interval: {UPDATE_INTERVAL*1000:.0f}ms")
    print(f"  Target: {uri}")
    print(f"{'='*60}\n")

    # First, register all test users
    print("[1/3] Registering test users...")
    for i in range(NUM_CLIENTS):
        await register_user(uri, TEST_USERS[i], "loadtest")
    print(f"  Registered {NUM_CLIENTS} users.\n")

    # Create start event for synchronized launch
    start_event = asyncio.Event()

    # Create all client tasks
    print(f"[2/3] Launching {NUM_CLIENTS} concurrent clients...")
    tasks = [asyncio.create_task(client_handler(i, uri, start_event)) for i in range(NUM_CLIENTS)]

    # Start all clients simultaneously
    start_time = time.time()
    start_event.set()

    print(f"[3/3] Running for {TEST_DURATION} seconds...\n")

    # Progress reporting
    while time.time() - start_time < TEST_DURATION:
        elapsed = time.time() - start_time
        bar_len = 40
        progress = min(elapsed / TEST_DURATION, 1.0)
        filled = int(bar_len * progress)
        bar = '█' * filled + '░' * (bar_len - filled)
        sys.stdout.write(f"\r  [{bar}] {progress*100:.0f}% | TX:{stats['messages_sent']} RX:{stats['messages_received']} ERR:{stats['errors']}")
        sys.stdout.flush()
        await asyncio.sleep(1)

    # Wait for tasks to complete
    for task in tasks:
        task.cancel()
    await asyncio.gather(*tasks, return_exceptions=True)

    elapsed = time.time() - start_time

    # --- Report ---
    print(f"\n\n{'='*60}")
    print("  LOAD TEST RESULTS")
    print(f"{'='*60}")
    print(f"  Duration:         {elapsed:.1f}s")
    print(f"  Clients:          {NUM_CLIENTS}")
    print(f"  Connections:      {stats['connected']}")
    print(f"  Disconnections:   {stats['disconnected']}")
    print(f"  Auth Success:     {stats['auth_success']}")
    print(f"  Auth Failures:    {stats['auth_fail']}")
    print(f"  Messages Sent:    {stats['messages_sent']}")
    print(f"  Messages Recv:    {stats['messages_received']}")
    print(f"  Errors:           {stats['errors']}")

    if stats["latency_samples"]:
        samples = stats["latency_samples"]
        print(f"\n  --- Latency (ms) ---")
        print(f"  Min:     {min(samples):.2f}")
        print(f"  Max:     {max(samples):.2f}")
        print(f"  Mean:    {sum(samples)/len(samples):.2f}")
        sorted_s = sorted(samples)
        p50 = sorted_s[int(len(sorted_s) * 0.5)]
        p95 = sorted_s[int(len(sorted_s) * 0.95)]
        p99 = sorted_s[min(int(len(sorted_s) * 0.99), len(sorted_s)-1)]
        print(f"  P50:     {p50:.2f}")
        print(f"  P95:     {p95:.2f}")
        print(f"  P99:     {p99:.2f}")
        print(f"  Samples: {len(samples)}")

    throughput = stats["messages_sent"] / elapsed if elapsed > 0 else 0
    print(f"\n  Throughput: {throughput:.1f} msg/s")
    print(f"{'='*60}\n")

    if OUTPUT_JSON:
        samples = sorted(stats["latency_samples"]) if stats["latency_samples"] else []
        payload = {
            "clients": NUM_CLIENTS,
            "duration_s": elapsed,
            "connected": stats["connected"],
            "disconnected": stats["disconnected"],
            "auth_success": stats["auth_success"],
            "auth_fail": stats["auth_fail"],
            "messages_sent": stats["messages_sent"],
            "messages_received": stats["messages_received"],
            "errors": stats["errors"],
            "throughput_msg_s": throughput,
            "latency_ms": {
                "min": (min(samples) if samples else None),
                "max": (max(samples) if samples else None),
                "mean": (sum(samples) / len(samples) if samples else None),
                "p50": (samples[int(len(samples) * 0.5)] if samples else None),
                "p95": (samples[int(len(samples) * 0.95)] if samples else None),
                "p99": (samples[min(int(len(samples) * 0.99), len(samples)-1)] if samples else None),
                "samples": len(samples),
            },
        }
        with open(OUTPUT_JSON, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2)
        print(f"[OK] Wrote JSON report: {OUTPUT_JSON}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="WebSocket load test")
    parser.add_argument("--clients", type=int, default=NUM_CLIENTS)
    parser.add_argument("--interval", type=float, default=UPDATE_INTERVAL)
    parser.add_argument("--duration", type=int, default=TEST_DURATION)
    parser.add_argument("--disconnect-chance", type=float, default=DISCONNECT_CHANCE)
    parser.add_argument("--output-json", type=str, default=None)
    args = parser.parse_args()

    NUM_CLIENTS = args.clients
    UPDATE_INTERVAL = args.interval
    TEST_DURATION = args.duration
    DISCONNECT_CHANCE = args.disconnect_chance
    OUTPUT_JSON = args.output_json

    TEST_USERS = [f"loadtest_{i}" for i in range(NUM_CLIENTS)]

    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nLoad test stopped by user.")
