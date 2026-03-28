# chaos_test.py

import asyncio
import websockets
import json
import time
import random

async def chaos_client():
    uri = "ws://localhost:8080"
    while True:
        try:
            async with websockets.connect(uri) as websocket:
                print("Chaos Client: Connected")
                
                # Flood the server with a burst of messages
                print("Chaos Client: Flooding server with a burst of messages")
                for _ in range(50):
                    lat = random.uniform(-90, 90)
                    lon = random.uniform(-180, 180)
                    message = {
                        "type": "location",
                        "payload": {"latitude": lat, "longitude": lon},
                        "timestamp": int(time.time())
                    }
                    await websocket.send(json.dumps(message))
                
                # Introduce artificial delay
                delay = random.uniform(1, 5)
                print(f"Chaos Client: Introducing artificial delay of {delay:.2f} seconds")
                await asyncio.sleep(delay)

                # Simulate packet drop by just not sending for a while
                drop_duration = random.uniform(1, 5)
                print(f"Chaos Client: Simulating packet drop for {drop_duration:.2f} seconds")
                await asyncio.sleep(drop_duration)

        except (websockets.exceptions.ConnectionClosedError, ConnectionRefusedError) as e:
            print(f"Chaos Client: Connection closed ({e}). Reconnecting...")
            await asyncio.sleep(5)
        except Exception as e:
            print(f"Chaos Client: An error occurred: {e}")
            break

if __name__ == "__main__":
    try:
        asyncio.run(chaos_client())
    except KeyboardInterrupt:
        print("Chaos test stopped.")
