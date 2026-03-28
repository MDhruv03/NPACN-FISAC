# load_test.py

import asyncio
import websockets
import json
import time
import random

NUM_CLIENTS = 200
UPDATE_INTERVAL = 0.1  # 100ms
DISCONNECT_CHANCE = 0.1

async def client_handler(client_id):
    uri = "ws://localhost:8080"
    reconnect_attempts = 0
    while True:
        try:
            async with websockets.connect(uri) as websocket:
                print(f"Client {client_id}: Connected")
                reconnect_attempts = 0
                while True:
                    if random.random() < DISCONNECT_CHANCE / (1 / UPDATE_INTERVAL):
                        print(f"Client {client_id}: Randomly disconnecting")
                        await websocket.close()
                        break

                    lat = random.uniform(-90, 90)
                    lon = random.uniform(-180, 180)
                    message = {
                        "type": "location",
                        "payload": {
                            "latitude": lat,
                            "longitude": lon
                        },
                        "timestamp": int(time.time())
                    }
                    
                    start_time = time.time()
                    await websocket.send(json.dumps(message))
                    
                    # In a real test, you would wait for a response to measure latency
                    # For now, we just print the send time
                    send_time = (time.time() - start_time) * 1000
                    print(f"Client {client_id}: Sent location, send time: {send_time:.2f}ms")

                    await asyncio.sleep(UPDATE_INTERVAL)

        except (websockets.exceptions.ConnectionClosedError, ConnectionRefusedError) as e:
            print(f"Client {client_id}: Connection closed ({e}). Reconnecting...")
            reconnect_attempts += 1
            await asyncio.sleep(1 * reconnect_attempts) # Exponential backoff
        except Exception as e:
            print(f"Client {client_id}: An error occurred: {e}")
            break


async def main():
    tasks = [client_handler(i) for i in range(NUM_CLIENTS)]
    await asyncio.gather(*tasks)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Load test stopped.")
