import os
import json
import requests
from dotenv import load_dotenv
import paho.mqtt.client as mqtt
from supabase import create_client

load_dotenv()

EMQX_HOST = os.getenv("EMQX_HOST")
EMQX_PORT = int(os.getenv("EMQX_PORT", "8883"))
EMQX_USER = os.getenv("EMQX_USER")
EMQX_PASS = os.getenv("EMQX_PASS")

SUPABASE_URL = os.getenv("SUPABASE_URL")
SUPABASE_SECRET_KEY = os.getenv("SUPABASE_SECRET_KEY")

DISCORD_WEBHOOK_URL = os.getenv("DISCORD_WEBHOOK_URL")

if not all([EMQX_HOST, EMQX_USER, EMQX_PASS, SUPABASE_URL, SUPABASE_SECRET_KEY]):
    raise RuntimeError("Missing required env vars. Check your .env file.")

sb = create_client(SUPABASE_URL, SUPABASE_SECRET_KEY)

TOPICS = [
    ("kombucha/+/telemetry", 1),
    ("kombucha/+/status", 1),
    ("kombucha/+/alerts", 1),
    ("kombucha/+/command/ack", 1),
    ("kombucha/+/config/ack", 1),
    ("kombucha/+/batch/set", 1),
]

# In-memory cache so every telemetry/event row gets the current active batch.
# If ingest.py restarts, get_active_batch_id() reloads active batch from Supabase.
current_batch_by_device = {}


def parse_device_id(topic: str, payload):
    if isinstance(payload, dict) and payload.get("device"):
        return payload["device"]

    parts = topic.split("/")
    return parts[1] if len(parts) > 1 else "unknown"


def safe_get_dict(payload, key):
    value = payload.get(key, {})
    return value if isinstance(value, dict) else {}


def send_discord_alert(device_id: str, payload: dict):
    if not DISCORD_WEBHOOK_URL:
        print("[DISCORD] webhook not configured, skipping")
        return

    code = payload.get("code", "ALERT")
    message = payload.get("message", "No message")
    ms = payload.get("ms")

    embed = {
        "title": f"Kombucha Alert: {code}",
        "description": str(message),
        "fields": [
            {"name": "Device", "value": str(device_id), "inline": True},
            {"name": "Device ms", "value": str(ms) if ms is not None else "--", "inline": True},
        ],
    }

    try:
        response = requests.post(
            DISCORD_WEBHOOK_URL,
            json={
                "username": "Kombucha Alerts",
                "embeds": [embed],
            },
            timeout=10,
        )
        response.raise_for_status()
        print(f"[DISCORD] alert sent ({code}, {device_id})")
    except Exception as e:
        print(f"[DISCORD] send failed: {e}")


def get_active_batch_id(device_id: str) -> str:
    """
    Returns the active batch for this device.
    Uses memory first, then Supabase, then falls back to default-batch.
    """
    cached = current_batch_by_device.get(device_id)
    if cached:
        return cached

    try:
        result = (
            sb.table("batches")
            .select("batch_id")
            .eq("device_id", device_id)
            .eq("is_active", True)
            .order("started_at", desc=True)
            .limit(1)
            .execute()
        )

        rows = result.data or []

        if rows:
            batch_id = rows[0]["batch_id"]
            current_batch_by_device[device_id] = batch_id
            return batch_id

    except Exception as e:
        print(f"[BATCH] Could not load active batch for {device_id}: {e}")

    return "default-batch"


def handle_batch_set(device_id: str, payload: dict):
    """
    Handles MQTT messages on:
      kombucha/<device_id>/batch/set

    Supported payloads:

    Start/set active batch:
      {
        "action": "start_batch",
        "batchId": "batch-demo-1",
        "name": "Demo Batch 1",
        "notes": "optional"
      }

    Set existing batch active:
      {
        "action": "set_active_batch",
        "batchId": "batch-demo-1"
      }

    End current batch:
      {
        "action": "end_batch"
      }
    """
    action = payload.get("action", "start_batch")
    batch_id = payload.get("batchId") or payload.get("batch_id")
    name = payload.get("name") or batch_id
    notes = payload.get("notes")

    if action == "end_batch":
        try:
            # Get current active batch before ending it, for event logging.
            ended_batch_id = get_active_batch_id(device_id)

            sb.table("batches").update({
                "is_active": False,
            }).eq("device_id", device_id).eq("is_active", True).execute()

            current_batch_by_device.pop(device_id, None)

            event_row = {
                "device_id": device_id,
                "batch_id": ended_batch_id,
                "event_type": "batch",
                "code": "BATCH_ENDED",
                "message": f"Active batch ended: {ended_batch_id}",
                "device_ms": payload.get("ms"),
                "payload": payload,
            }
            sb.table("events").insert(event_row).execute()

            print(f"[BATCH] Ended active batch for {device_id}: {ended_batch_id}")

        except Exception as e:
            print(f"[ERROR] Failed to end batch for {device_id}: {e}")

        return

    if not batch_id:
        print(f"[BATCH] Missing batchId for {device_id}")
        return

    try:
        # Mark existing active batches inactive for this device.
        sb.table("batches").update({
            "is_active": False,
        }).eq("device_id", device_id).eq("is_active", True).execute()

        # Insert/update the new active batch.
        # on_conflict requires batch_id to be unique in Supabase.
        sb.table("batches").upsert({
            "batch_id": batch_id,
            "device_id": device_id,
            "name": name,
            "notes": notes,
            "is_active": True,
            "ended_at": None,
        }, on_conflict="batch_id").execute()

        current_batch_by_device[device_id] = batch_id

        event_row = {
            "device_id": device_id,
            "batch_id": batch_id,
            "event_type": "batch",
            "code": "BATCH_STARTED" if action == "start_batch" else "BATCH_SET_ACTIVE",
            "message": f"Active batch set to {batch_id}",
            "device_ms": payload.get("ms"),
            "payload": payload,
        }
        sb.table("events").insert(event_row).execute()

        print(f"[BATCH] Active batch for {device_id}: {batch_id}")

    except Exception as e:
        print(f"[ERROR] Failed to set active batch for {device_id}: {e}")


def on_connect(client, userdata, flags, rc, properties=None):
    print(f"[MQTT] Connected rc={rc}")

    for topic, qos in TOPICS:
        client.subscribe(topic, qos=qos)
        print(f"[MQTT] Subscribed: {topic}")


def on_message(client, userdata, msg):
    topic = msg.topic
    raw = msg.payload.decode("utf-8", errors="ignore")

    try:
        payload = json.loads(raw)
    except Exception:
        payload = {"raw": raw}

    device_id = parse_device_id(topic, payload)

    # Batch control messages do not come from firmware.
    # They come from the website and tell ingest.py which batch future data belongs to.
    if topic.endswith("/batch/set"):
        if isinstance(payload, dict):
            handle_batch_set(device_id, payload)
        else:
            print(f"[BATCH] Invalid batch payload on {topic}: {raw}")
        return

    try:
        if topic.endswith("/telemetry"):
            color_raw = safe_get_dict(payload, "colorRaw") if isinstance(payload, dict) else {}
            color_hsl = safe_get_dict(payload, "colorHsl") if isinstance(payload, dict) else {}

            batch_id = get_active_batch_id(device_id)

            row = {
                "device_id": device_id,
                "batch_id": batch_id,

                "temp_c": payload.get("tempC"),
                "us_main_cm": payload.get("mainDistanceCm"),
                "us_feed_cm": payload.get("feedDistanceCm"),
                "us_waste_cm": payload.get("wasteDistanceCm"),

                "ph": payload.get("ph"),
                "ph_voltage": payload.get("phVoltage"),
                "ph_cal_voltage_a": payload.get("phCalVoltageA"),
                "ph_cal_voltage_b": payload.get("phCalVoltageB"),

                "r": color_raw.get("r"),
                "g": color_raw.get("g"),
                "b": color_raw.get("b"),
                "c": color_raw.get("c"),

                "h": color_hsl.get("h"),
                "s": color_hsl.get("s"),
                "l": color_hsl.get("l"),

                "heater_on": payload.get("heaterOn"),
                "oxygen_on": payload.get("oxygenOn"),
                "feed_pump_on": payload.get("feedPumpOn"),
                "waste_pump_on": payload.get("wastePumpOn"),

                "auto_level_control": payload.get("autoLevelControl"),
                "auto_heater_control": payload.get("autoHeaterControl"),
                "auto_oxygen_control": payload.get("autoOxygenControl"),

                "oxygen_run_seconds": payload.get("oxygenRunSeconds"),
                "oxygen_wait_seconds": payload.get("oxygenWaitSeconds"),

                "feed_empty_alert": payload.get("feedEmptyAlert"),
                "waste_overflow_alert": payload.get("wasteOverflowAlert"),
                "main_jar_too_full_alert": payload.get("mainJarTooFullAlert"),
                "temp_sensor_fault": payload.get("tempSensorFault"),
                "ph_sensor_fault": payload.get("phSensorFault"),
                "color_sensor_fault": payload.get("colorSensorFault"),

                "feed_empty_latched": payload.get("feedEmptyLatched"),
                "waste_overflow_latched": payload.get("wasteOverflowLatched"),
                "main_jar_too_full_latched": payload.get("mainJarTooFullLatched"),
                "temp_sensor_fault_latched": payload.get("tempSensorFaultLatched"),

                "fluid_system_lockout": payload.get("fluidSystemLockout"),
                "heater_lockout": payload.get("heaterLockout"),

                "main_distance_target_cm": payload.get("mainDistanceTargetCm"),
                "main_distance_band_cm": payload.get("mainDistanceBandCm"),
                "main_too_full_distance_cm": payload.get("mainTooFullDistanceCm"),
                "feed_empty_distance_cm": payload.get("feedEmptyDistanceCm"),
                "waste_overflow_distance_cm": payload.get("wasteOverflowDistanceCm"),
                "temp_low_c": payload.get("tempLowC"),
                "temp_high_c": payload.get("tempHighC"),

                "debug_mode": payload.get("debugMode"),
                "device_ms": payload.get("ms"),
                "payload": payload,
            }

            sb.table("telemetry_raw").insert(row).execute()
            print(f"[DB] telemetry_raw insert OK ({device_id}, batch={batch_id})")

        else:
            if topic.endswith("/status"):
                event_type = "status"
            elif topic.endswith("/alerts"):
                event_type = "alert"
            elif topic.endswith("/config/ack"):
                event_type = "config_ack"
            else:
                event_type = "command_ack"

            batch_id = get_active_batch_id(device_id)

            row = {
                "device_id": device_id,
                "batch_id": batch_id,
                "event_type": event_type,
                "code": payload.get("code") if isinstance(payload, dict) else None,
                "message": (
                    payload.get("message")
                    or payload.get("event")
                    or payload.get("code")
                    or raw
                ) if isinstance(payload, dict) else raw,
                "device_ms": payload.get("ms") if isinstance(payload, dict) else None,
                "payload": payload,
            }

            sb.table("events").insert(row).execute()
            print(f"[DB] events insert OK ({event_type}, {device_id}, batch={batch_id})")

            if topic.endswith("/alerts") and isinstance(payload, dict):
                send_discord_alert(device_id, payload)

    except Exception as e:
        print(f"[ERROR] DB insert failed on topic {topic}: {e}")


def main():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(EMQX_USER, EMQX_PASS)
    client.tls_set()

    client.on_connect = on_connect
    client.on_message = on_message

    print(f"[MQTT] Connecting to {EMQX_HOST}:{EMQX_PORT} ...")
    client.connect(EMQX_HOST, EMQX_PORT, 60)
    client.loop_forever()


if __name__ == "__main__":
    main()
