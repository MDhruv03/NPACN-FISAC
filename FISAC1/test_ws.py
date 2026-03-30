import asyncio
import websockets
import json

async def test_auth():
    uri = "ws://localhost:8080/"
    try:
        print(f"Connecting to {uri}...")
        async with websockets.connect(uri) as websocket:
            print("Connected!")
            msg = {
                "type": "auth",
                "payload": {"username": "user1", "password": "pass1"}
            }
            print(f"Sending: {msg}")
            await websocket.send(json.dumps(msg))
            
            print("Waiting for response...")
            response = await websocket.recv()
            print(f"Received: {response}")
    except Exception as e:
        print(f"Error: {e}")

asyncio.run(test_auth())
