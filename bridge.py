import asyncio
import json
import logging
import threading
import time

import serial
import websockets

SERIAL_URL = "rfc2217://127.0.0.1:4000"
BAUD_RATE = 9600
WEBSOCKET_HOST = "127.0.0.1"
WEBSOCKET_PORT = 8787

logging.basicConfig(level=logging.INFO, format="[%(asctime)s] %(message)s", datefmt="%H:%M:%S")

clients = set()
clients_lock = threading.Lock()
serial_connection = None
serial_lock = threading.Lock()
loop = None
stop_event = threading.Event()


def get_serial():
    global serial_connection
    with serial_lock:
        if serial_connection is not None and serial_connection.is_open:
            return serial_connection
        try:
            logging.info("Connecting to Wokwi RFC2217 at %s ...", SERIAL_URL)
            serial_connection = serial.serial_for_url(
                SERIAL_URL,
                baudrate=BAUD_RATE,
                timeout=0.2,
                write_timeout=1,
            )
            logging.info("Connected to Wokwi serial port.")
            return serial_connection
        except Exception as exc:
            serial_connection = None
            logging.warning("Wokwi serial unavailable: %s", exc)
            return None


def send_to_arduino(text):
    port = get_serial()
    if port is None:
        return False
    try:
        with serial_lock:
            port.write(text.encode("utf-8"))
            port.flush()
        logging.info("Dashboard -> Arduino: %r", text.rstrip("\\r\\n"))
        return True
    except Exception as exc:
        logging.warning("Could not write to Wokwi serial: %s", exc)
        with serial_lock:
            try:
                port.close()
            except Exception:
                pass
            serial_connection = None
        return False


async def broadcast_sensor(packet):
    if not clients:
        return
    message = json.dumps({"type": "sensor", "data": packet}, separators=(",", ":"))
    with clients_lock:
        current = list(clients)
    dead = []
    for client in current:
        try:
            await client.send(message)
        except Exception:
            dead.append(client)
    if dead:
        with clients_lock:
            for client in dead:
                clients.discard(client)


def serial_reader():
    while not stop_event.is_set():
        port = get_serial()
        if port is None:
            time.sleep(1.0)
            continue
        try:
            raw = port.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="ignore").strip()
            if not line:
                continue
            try:
                packet = json.loads(line)
            except json.JSONDecodeError:
                # Human-readable Arduino output is deliberately ignored.
                continue
            required = ("soilMoisture", "ph", "temperature", "humidity", "light")
            if not all(k in packet for k in required):
                continue
            logging.info("Arduino -> Dashboard: %s", line)
            if loop is not None:
                asyncio.run_coroutine_threadsafe(broadcast_sensor(packet), loop)
        except Exception as exc:
            logging.warning("Serial read error: %s", exc)
            with serial_lock:
                try:
                    port.close()
                except Exception:
                    pass
                serial_connection = None
            time.sleep(0.5)


async def websocket_handler(websocket):
    with clients_lock:
        clients.add(websocket)
    logging.info("Dashboard connected. Clients: %d", len(clients))
    try:
        async for raw_message in websocket:
            try:
                message = json.loads(raw_message)
            except json.JSONDecodeError:
                continue
            if message.get("type") == "crop":
                crop = str(message.get("crop", "")).strip()
                if crop:
                    send_to_arduino(crop + "\\n")
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        with clients_lock:
            clients.discard(websocket)
        logging.info("Dashboard disconnected. Clients: %d", len(clients))


async def main():
    global loop
    loop = asyncio.get_running_loop()
    thread = threading.Thread(target=serial_reader, daemon=True)
    thread.start()
    logging.info("Bridge WebSocket listening at ws://%s:%d", WEBSOCKET_HOST, WEBSOCKET_PORT)
    logging.info("Keep Wokwi running so RFC2217 port 4000 is available.")
    async with websockets.serve(websocket_handler, WEBSOCKET_HOST, WEBSOCKET_PORT):
        await asyncio.to_thread(stop_event.wait)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        stop_event.set()
        logging.info("Bridge stopped.")
