import csv
import json
import os
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import paho.mqtt.client as mqtt


MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
METRICS_PORT = int(os.getenv("METRICS_PORT", "9100"))
DATA_DIR = os.getenv("DATA_DIR", "/data")
MQTT_RETRY_SECONDS = int(os.getenv("MQTT_RETRY_SECONDS", "2"))

state_lock = threading.Lock()
state = {
    "messages": 0,
    "occupancy": 0,
    "present": set(),
    "temperature": 0.0,
    "humidity": 0.0,
    "blocked": 0,
    "entries": 0,
    "exits": 0,
    "denied": 0,
    "timeouts": 0,
    "evacuations": 0,
    "rfid_errors": 0,
    "sensor_errors": 0,
}


def log(message):
    print(f"[{utc_now()}] {message}", flush=True)


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def ensure_data_files():
    os.makedirs(DATA_DIR, exist_ok=True)
    events_path = os.path.join(DATA_DIR, "events.csv")
    presence_path = os.path.join(DATA_DIR, "presence.csv")
    if not os.path.exists(events_path):
        with open(events_path, "w", newline="", encoding="utf-8") as f:
            csv.writer(f).writerow([
                "received_at",
                "topic",
                "event",
                "card_id",
                "direction",
                "result",
                "occupancy",
                "temp",
                "hum",
                "blocked",
                "device_millis",
            ])
    if not os.path.exists(presence_path):
        write_presence_snapshot()


def append_event(topic, payload):
    with open(os.path.join(DATA_DIR, "events.csv"), "a", newline="", encoding="utf-8") as f:
        csv.writer(f).writerow([
            utc_now(),
            topic,
            payload.get("event", ""),
            payload.get("card_id", ""),
            payload.get("direction", ""),
            payload.get("result", ""),
            payload.get("occupancy", ""),
            payload.get("temp", ""),
            payload.get("hum", ""),
            payload.get("blocked", ""),
            payload.get("millis", ""),
        ])


def write_presence_snapshot():
    os.makedirs(DATA_DIR, exist_ok=True)
    with open(os.path.join(DATA_DIR, "presence.csv"), "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["card_id", "observed_at"])
        for card_id in sorted(state["present"]):
            writer.writerow([card_id, utc_now()])


def update_state(topic, payload):
    state["messages"] += 1
    event = payload.get("event", "")
    card_id = payload.get("card_id", "")

    if "temp" in payload:
        state["temperature"] = float(payload["temp"])
    if "hum" in payload:
        state["humidity"] = float(payload["hum"])
    if "blocked" in payload:
        state["blocked"] = int(payload["blocked"])

    if event == "entry":
        state["entries"] += 1
        if card_id:
            state["present"].add(card_id)
    elif event == "exit":
        state["exits"] += 1
        if card_id:
            state["present"].discard(card_id)
    elif event == "denied":
        state["denied"] += 1
    elif event == "timeout":
        state["timeouts"] += 1
    elif event == "evacuated":
        state["evacuations"] += 1
        if card_id:
            state["present"].discard(card_id)
    elif event == "rfid_error":
        state["rfid_errors"] += 1
    elif event == "env_error":
        state["sensor_errors"] += 1

    if "occupancy" in payload and event not in {"entry", "exit", "evacuated"}:
        state["occupancy"] = int(payload["occupancy"])
    else:
        state["occupancy"] = len(state["present"])

    append_event(topic, payload)
    write_presence_snapshot()


def on_connect(client, userdata, flags, reason_code, properties=None):
    log(f"connected to MQTT broker {MQTT_HOST}:{MQTT_PORT} reason={reason_code}")
    result, mid = client.subscribe("ase/access/#")
    log(f"subscribed to ase/access/# result={result} mid={mid}")


def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
    except json.JSONDecodeError:
        log(f"invalid JSON on {msg.topic}: {msg.payload!r}")
        payload = {"event": "raw", "raw": msg.payload.decode("utf-8", errors="replace")}

    with state_lock:
        log(f"message received topic={msg.topic} payload={payload}")
        update_state(msg.topic, payload)


def metric_line(name, value, help_text, kind="gauge"):
    return f"# HELP {name} {help_text}\n# TYPE {name} {kind}\n{name} {value}\n"


class MetricsHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/metrics":
            self.send_response(404)
            self.end_headers()
            return

        with state_lock:
            body = ""
            body += metric_line("access_messages_total", state["messages"], "Total MQTT messages received", "counter")
            body += metric_line("access_occupancy", state["occupancy"], "Current number of cards inside")
            body += metric_line("access_temperature_c", state["temperature"], "Last temperature reading")
            body += metric_line("access_humidity_pct", state["humidity"], "Last humidity reading")
            body += metric_line("access_blocked", state["blocked"], "Environmental access block state")
            body += metric_line("access_entries_total", state["entries"], "Total entry events", "counter")
            body += metric_line("access_exits_total", state["exits"], "Total exit events", "counter")
            body += metric_line("access_denied_total", state["denied"], "Total denied events", "counter")
            body += metric_line("access_timeouts_total", state["timeouts"], "Total RFID timeout events", "counter")
            body += metric_line("access_evacuations_total", state["evacuations"], "Total evacuated cards", "counter")
            body += metric_line("access_rfid_errors_total", state["rfid_errors"], "Total RFID errors", "counter")
            body += metric_line("access_sensor_errors_total", state["sensor_errors"], "Total DHT20 errors", "counter")

        data = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; version=0.0.4")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, fmt, *args):
        return


def run_metrics_server():
    log(f"starting metrics server on 0.0.0.0:{METRICS_PORT}")
    server = ThreadingHTTPServer(("0.0.0.0", METRICS_PORT), MetricsHandler)
    server.serve_forever()


def run_mqtt_client():
    while True:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        client.on_connect = on_connect
        client.on_message = on_message
        try:
            log(f"connecting to MQTT broker {MQTT_HOST}:{MQTT_PORT}")
            client.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
            client.loop_forever()
        except (OSError, mqtt.MQTTException) as exc:
            log(f"MQTT connection failed: {exc}; retrying in {MQTT_RETRY_SECONDS}s")
            time.sleep(MQTT_RETRY_SECONDS)


if __name__ == "__main__":
    ensure_data_files()
    threading.Thread(target=run_metrics_server, daemon=True).start()
    run_mqtt_client()
