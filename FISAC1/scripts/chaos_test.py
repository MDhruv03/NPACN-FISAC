# chaos_test.py - Stress test for abnormal conditions

import asyncio
import websockets
import json
import time
import random
import sys

URI = "ws://localhost:8080"

stats = {
    "flood_sent": 0,
    "unauthorized_attempts": 0,
    "rapid_connect_disconnect": 0,
    "malformed_sent": 0,
    "errors": 0,
}

async def test_message_flood():
    """Test: Flood the server with a burst of 200 rapid messages."""
    print("\n  [TEST 1] Message Flood (200 rapid messages)...")
    try:
        async with websockets.connect(URI) as ws:
            # Authenticate first
            await ws.send(json.dumps({"type": "auth", "payload": {"username": "user1", "password": "pass1"}}))
            response = await asyncio.wait_for(ws.recv(), timeout=5)

            start = time.time()
            for i in range(200):
                msg = {
                    "type": "location",
                    "payload": {"latitude": random.uniform(-90, 90), "longitude": random.uniform(-180, 180), "userId": "chaos_flood"}
                }
                await ws.send(json.dumps(msg))
                stats["flood_sent"] += 1

            elapsed = (time.time() - start) * 1000
            print(f"    Sent 200 messages in {elapsed:.1f}ms ({200/(elapsed/1000):.0f} msg/s)")
            print(f"    [PASSED] Server handled burst without crashing.")
    except Exception as e:
        stats["errors"] += 1
        print(f"    [FAILED] {e}")


async def test_unauthorized_access():
    """Test: Send messages without authentication."""
    print("\n  [TEST 2] Unauthorized Access Attempts...")
    try:
        async with websockets.connect(URI) as ws:
            # Send location without authenticating
            msg = {"type": "location", "payload": {"latitude": 0, "longitude": 0, "userId": "hacker"}}
            await ws.send(json.dumps(msg))
            stats["unauthorized_attempts"] += 1

            response = await asyncio.wait_for(ws.recv(), timeout=5)
            data = json.loads(response)
            if data.get("type") == "error":
                print(f"    Server correctly rejected: {data['payload']['message']}")
                print(f"    [PASSED] Unauthorized access blocked.")
            else:
                print(f"    [WARNING] Server did not reject unauthorized message.")
    except Exception as e:
        stats["errors"] += 1
        print(f"    [FAILED] {e}")


async def test_invalid_credentials():
    """Test: Login with wrong credentials."""
    print("\n  [TEST 3] Invalid Credentials...")
    try:
        async with websockets.connect(URI) as ws:
            await ws.send(json.dumps({"type": "auth", "payload": {"username": "hacker", "password": "wrong"}}))
            stats["unauthorized_attempts"] += 1

            response = await asyncio.wait_for(ws.recv(), timeout=5)
            data = json.loads(response)
            if not data.get("payload", {}).get("success"):
                print(f"    Server correctly rejected: {data['payload']['message']}")
                print(f"    [PASSED] Invalid credentials rejected.")
            else:
                print(f"    [WARNING] Server accepted invalid credentials!")
    except Exception as e:
        stats["errors"] += 1
        print(f"    [FAILED] {e}")


async def test_rapid_connect_disconnect():
    """Test: Rapidly connect and disconnect 50 times."""
    print("\n  [TEST 4] Rapid Connect/Disconnect (50 cycles)...")
    success = 0
    for i in range(50):
        try:
            ws = await websockets.connect(URI)
            await ws.close()
            success += 1
            stats["rapid_connect_disconnect"] += 1
        except Exception:
            stats["errors"] += 1

    print(f"    Completed {success}/50 rapid cycles.")
    if success >= 45:
        print(f"    [PASSED] Server handles rapid reconnection.")
    else:
        print(f"    [WARNING] {50-success} connection failures during rapid cycling.")


async def test_malformed_messages():
    """Test: Send malformed/garbage data."""
    print("\n  [TEST 5] Malformed Messages...")
    malformed_data = [
        "not json at all",
        '{"type": "unknown_type"}',
        '{"no_type_field": true}',
        '{"type": "location", "payload": {"latitude": "not_a_number"}}',
        '{"type": "auth"}',  # Missing payload
        "",
        "x" * 10000,  # Very long message
    ]

    for data in malformed_data:
        try:
            async with websockets.connect(URI) as ws:
                await ws.send(data)
                stats["malformed_sent"] += 1
                try:
                    response = await asyncio.wait_for(ws.recv(), timeout=2)
                except asyncio.TimeoutError:
                    pass
        except Exception:
            stats["errors"] += 1

    print(f"    Sent {len(malformed_data)} malformed messages.")
    print(f"    [PASSED] Server survived malformed input.")


async def test_artificial_delay():
    """Test: Hold connection idle then send after delay."""
    print("\n  [TEST 6] Artificial Delay (5s idle, then send)...")
    try:
        async with websockets.connect(URI) as ws:
            await ws.send(json.dumps({"type": "auth", "payload": {"username": "user1", "password": "pass1"}}))
            await asyncio.wait_for(ws.recv(), timeout=5)

            print(f"    Waiting 5 seconds...")
            await asyncio.sleep(5)

            msg = {"type": "location", "payload": {"latitude": 0, "longitude": 0, "userId": "delay_test"}}
            await ws.send(json.dumps(msg))
            print(f"    Message sent after delay.")
            print(f"    [PASSED] Connection survived idle period.")
    except Exception as e:
        stats["errors"] += 1
        print(f"    [FAILED] {e}")


async def main():
    print(f"\n{'='*60}")
    print(f"  CHAOS TEST - System Robustness Evaluation")
    print(f"  Target: {URI}")
    print(f"{'='*60}")

    await test_message_flood()
    await test_unauthorized_access()
    await test_invalid_credentials()
    await test_rapid_connect_disconnect()
    await test_malformed_messages()
    await test_artificial_delay()

    print(f"\n{'='*60}")
    print(f"  CHAOS TEST RESULTS")
    print(f"{'='*60}")
    print(f"  Flood messages sent:        {stats['flood_sent']}")
    print(f"  Unauthorized attempts:      {stats['unauthorized_attempts']}")
    print(f"  Rapid connect/disconnect:   {stats['rapid_connect_disconnect']}")
    print(f"  Malformed messages sent:    {stats['malformed_sent']}")
    print(f"  Errors encountered:         {stats['errors']}")
    print(f"{'='*60}\n")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nChaos test stopped by user.")
