"""
SQLite Database Manager for Water Tank Monitor V2 Dashboard.
Stores telemetry history, motor events, and configuration state.
"""

import sqlite3
import os
import time
import json
from contextlib import contextmanager

def _resolve_db_path() -> str:
    """Smart DB path resolution for both Docker containers and Host OS execution."""
    if "DB_PATH" in os.environ and os.environ["DB_PATH"]:
        return os.environ["DB_PATH"]
    
    # Check if explicitly inside Docker container
    if os.path.exists("/.dockerenv") and os.path.isdir("/app/data"):
        return "/app/data/water_tank.db"
    
    # Host OS Execution: Use ./data/water_tank.db relative to dashboard root
    backend_dir = os.path.dirname(os.path.abspath(__file__))
    dashboard_root = os.path.dirname(backend_dir)
    local_data_dir = os.path.join(dashboard_root, "data")
    os.makedirs(local_data_dir, exist_ok=True)
    return os.path.join(local_data_dir, "water_tank.db")

DB_PATH = _resolve_db_path()


def get_connection():
    """Create a new SQLite connection with WAL mode for concurrent reads."""
    conn = sqlite3.connect(DB_PATH, timeout=10)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA busy_timeout=5000")
    return conn


@contextmanager
def get_db():
    conn = get_connection()
    try:
        yield conn
        conn.commit()
    finally:
        conn.close()


def init_db():
    """Create tables if they don't exist."""
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)

    with get_db() as conn:
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS telemetry (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp REAL NOT NULL,
                sequence INTEGER,
                distance_cm INTEGER,
                water_percentage INTEGER,
                battery_voltage REAL,
                battery_percentage INTEGER,
                motor_status TEXT,
                config_version INTEGER,
                sensor_status INTEGER
            );

            CREATE TABLE IF NOT EXISTS motor_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp REAL NOT NULL,
                event TEXT NOT NULL,
                motor_status TEXT NOT NULL,
                reason TEXT
            );

            CREATE TABLE IF NOT EXISTS config_state (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp REAL NOT NULL,
                config_version INTEGER,
                water_top_level INTEGER,
                water_bottom_level INTEGER,
                normal_interval INTEGER,
                motor_monitor_interval INTEGER,
                low_water_threshold INTEGER,
                max_motor_runtime INTEGER
            );

            CREATE TABLE IF NOT EXISTS system_logs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp REAL NOT NULL,
                source TEXT NOT NULL,
                message TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS system_settings (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );

            CREATE INDEX IF NOT EXISTS idx_telemetry_ts ON telemetry(timestamp);
            CREATE INDEX IF NOT EXISTS idx_motor_events_ts ON motor_events(timestamp);
            CREATE INDEX IF NOT EXISTS idx_system_logs_ts ON system_logs(timestamp);
        """)
    print(f"[Database] Initialized at {DB_PATH}")


def insert_telemetry(data: dict):
    """Insert a telemetry record from MQTT JSON payload."""
    with get_db() as conn:
        conn.execute("""
            INSERT INTO telemetry (timestamp, sequence, distance_cm, water_percentage,
                                   battery_voltage, battery_percentage, motor_status,
                                   config_version, sensor_status)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            time.time(),
            data.get("sequence"),
            data.get("distance_cm"),
            data.get("water_percentage"),
            data.get("battery_voltage"),
            data.get("battery_percentage"),
            data.get("motor_status"),
            data.get("config_version"),
            data.get("sensor_status"),
        ))


def insert_motor_event(data: dict):
    """Insert a motor event record from MQTT JSON payload."""
    with get_db() as conn:
        conn.execute("""
            INSERT INTO motor_events (timestamp, event, motor_status, reason)
            VALUES (?, ?, ?, ?)
        """, (
            time.time(),
            data.get("event", "UNKNOWN"),
            data.get("motor_status", "UNKNOWN"),
            data.get("reason", ""),
        ))


def insert_config_state(data: dict):
    """Insert a configuration state record from MQTT JSON payload."""
    with get_db() as conn:
        conn.execute("""
            INSERT INTO config_state (timestamp, config_version, water_top_level,
                                      water_bottom_level, normal_interval,
                                      motor_monitor_interval, low_water_threshold,
                                      max_motor_runtime)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            time.time(),
            data.get("config_version"),
            data.get("water_top_level"),
            data.get("water_bottom_level"),
            data.get("normal_interval"),
            data.get("motor_monitor_interval"),
            data.get("low_water_threshold"),
            data.get("max_motor_runtime"),
        ))


def insert_system_log(source: str, message: str):
    """Insert a system log entry."""
    with get_db() as conn:
        conn.execute("""
            INSERT INTO system_logs (timestamp, source, message)
            VALUES (?, ?, ?)
        """, (time.time(), source, message))


def get_telemetry_history(hours: float = 24, limit: int = 2000) -> list:
    """Fetch telemetry records from the last N hours."""
    since = time.time() - (hours * 3600)
    with get_db() as conn:
        rows = conn.execute("""
            SELECT timestamp, sequence, distance_cm, water_percentage,
                   battery_voltage, battery_percentage, motor_status,
                   config_version, sensor_status
            FROM telemetry
            WHERE timestamp >= ?
            ORDER BY timestamp ASC
            LIMIT ?
        """, (since, limit)).fetchall()
    return [dict(r) for r in rows]


def get_motor_events_history(hours: float = 168, limit: int = 500) -> list:
    """Fetch motor events from the last N hours (default 7 days)."""
    since = time.time() - (hours * 3600)
    with get_db() as conn:
        rows = conn.execute("""
            SELECT timestamp, event, motor_status, reason
            FROM motor_events
            WHERE timestamp >= ?
            ORDER BY timestamp DESC
            LIMIT ?
        """, (since, limit)).fetchall()
    return [dict(r) for r in rows]


def get_latest_config() -> dict:
    """Fetch the most recent config state."""
    with get_db() as conn:
        row = conn.execute("""
            SELECT * FROM config_state ORDER BY timestamp DESC LIMIT 1
        """).fetchone()
    return dict(row) if row else {}


def get_system_logs(limit: int = 200) -> list:
    """Fetch recent system log entries."""
    with get_db() as conn:
        rows = conn.execute("""
            SELECT timestamp, source, message
            FROM system_logs
            ORDER BY timestamp DESC
            LIMIT ?
        """, (limit,)).fetchall()
    return [dict(r) for r in rows]


def get_motor_runtime_data(hours: float = 24) -> dict:
    """
    Calculate motor runtime buckets and last active timestamp using a unified state timeline.
    Merges motor_events and telemetry status signals to extract active ON intervals regardless of trigger source.
    """
    now = time.time()
    since = now - (hours * 3600)

    with get_db() as conn:
        # 1. Fetch last active timestamp across all history
        last_evt = conn.execute("""
            SELECT timestamp FROM motor_events
            WHERE motor_status = 'ON'
            ORDER BY timestamp DESC LIMIT 1
        """).fetchone()

        last_tel = conn.execute("""
            SELECT timestamp FROM telemetry
            WHERE motor_status = 'ON'
            ORDER BY timestamp DESC LIMIT 1
        """).fetchone()

        last_active = None
        if last_evt and last_tel:
            last_active = max(last_evt["timestamp"], last_tel["timestamp"])
        elif last_evt:
            last_active = last_evt["timestamp"]
        elif last_tel:
            last_active = last_tel["timestamp"]

        # 2. Fetch motor events (look back 24h prior to 'since' to catch cross-boundary runs)
        events = conn.execute("""
            SELECT timestamp, motor_status
            FROM motor_events
            WHERE timestamp >= ?
            ORDER BY timestamp ASC
        """, (since - 86400,)).fetchall()

        # 3. Fetch telemetry records
        telemetry = conn.execute("""
            SELECT timestamp, motor_status
            FROM telemetry
            WHERE timestamp >= ?
            ORDER BY timestamp ASC
        """, (since - 86400,)).fetchall()

    # 4. Merge all status signals into a unified chronological list of (timestamp, status) tuples
    signals = []
    for e in events:
        st = str(e["motor_status"]).upper().strip()
        if st in ("ON", "1", "TRUE"):
            signals.append((e["timestamp"], "ON"))
        elif st in ("OFF", "0", "FALSE"):
            signals.append((e["timestamp"], "OFF"))

    for t in telemetry:
        st = str(t["motor_status"]).upper().strip()
        if st in ("ON", "1", "TRUE"):
            signals.append((t["timestamp"], "ON"))
        elif st in ("OFF", "0", "FALSE"):
            signals.append((t["timestamp"], "OFF"))

    # Sort all signals chronologically by timestamp
    signals.sort(key=lambda x: x[0])

    # 5. Extract continuous ON intervals [(start_ts, end_ts), ...]
    active_intervals = []
    current_on_ts = None

    for ts, status in signals:
        if status == "ON":
            if current_on_ts is None:
                current_on_ts = ts
        elif status == "OFF":
            if current_on_ts is not None:
                active_intervals.append((current_on_ts, max(current_on_ts, ts)))
                current_on_ts = None

    # Handle open interval (motor is currently ON or last state was ON)
    if current_on_ts is not None:
        last_sig_ts = signals[-1][0] if signals else current_on_ts
        end_ts = min(now, max(current_on_ts, last_sig_ts + 3600))
        active_intervals.append((current_on_ts, max(current_on_ts, end_ts)))

    # Determine buckets
    if hours <= 6:
        num_buckets = 12  # 30-min buckets
    elif hours <= 24:
        num_buckets = 24  # 1-hour buckets
    elif hours <= 168:
        num_buckets = 7   # 1-day buckets
    else:
        num_buckets = 30  # 1-day buckets

    bucket_size = (hours * 3600) / num_buckets
    buckets = []

    for i in range(num_buckets):
        b_start = since + (i * bucket_size)
        b_end = b_start + bucket_size
        
        # Sum seconds of overlap between [b_start, b_end] and all active_intervals
        active_secs = 0.0
        for start_ts, end_ts in active_intervals:
            overlap_start = max(b_start, start_ts)
            overlap_end = min(b_end, end_ts)
            if overlap_end > overlap_start:
                active_secs += (overlap_end - overlap_start)

        buckets.append({
            "start": b_start,
            "end": b_end,
            "runtime_minutes": round(active_secs / 60.0, 1),
        })

    total_minutes = sum(b["runtime_minutes"] for b in buckets)

    return {
        "hours": hours,
        "total_runtime_minutes": round(total_minutes, 1),
        "last_active_timestamp": last_active,
        "buckets": buckets,
    }


# ─── System Configuration & Management Functions ───

def get_tank_capacity_liters() -> int:
    """Retrieve stored tank capacity in Liters from SQLite (defaults to 1000)."""
    try:
        with get_db() as conn:
            row = conn.execute("SELECT value FROM system_settings WHERE key = 'tank_capacity_liters'").fetchone()
            if row and row["value"]:
                return int(row["value"])
    except Exception:
        pass
    return 1000


def set_tank_capacity_liters(capacity: int) -> int:
    """Set and persist tank capacity in Liters into SQLite."""
    if capacity <= 0:
        raise ValueError("Tank capacity must be a positive integer greater than 0.")
    init_db()
    with get_db() as conn:
        conn.execute("""
            INSERT INTO system_settings (key, value) VALUES ('tank_capacity_liters', ?)
            ON CONFLICT(key) DO UPDATE SET value = excluded.value
        """, (str(capacity),))
    return capacity


def get_db_stats() -> dict:
    """Return record counts, timestamp range, and database file size."""
    init_db()
    with get_db() as conn:
        tel_count = conn.execute("SELECT COUNT(*) FROM telemetry").fetchone()[0]
        evt_count = conn.execute("SELECT COUNT(*) FROM motor_events").fetchone()[0]
        log_count = conn.execute("SELECT COUNT(*) FROM system_logs").fetchone()[0]
        
        min_ts = conn.execute("SELECT MIN(timestamp) FROM telemetry").fetchone()[0]
        max_ts = conn.execute("SELECT MAX(timestamp) FROM telemetry").fetchone()[0]
        
    capacity = get_tank_capacity_liters()
    file_size_bytes = os.path.getsize(DB_PATH) if os.path.exists(DB_PATH) else 0

    return {
        "db_path": DB_PATH,
        "file_size_bytes": file_size_bytes,
        "tank_capacity_liters": capacity,
        "telemetry_count": tel_count,
        "motor_events_count": evt_count,
        "system_logs_count": log_count,
        "oldest_timestamp": min_ts,
        "newest_timestamp": max_ts,
    }


def clear_db_records(recent_seconds: float = None, older_than_seconds: float = None, clear_all: bool = False) -> dict:
    """
    Clear database records based on time criteria:
    - clear_all: Deletes all rows from telemetry, motor_events, and system_logs.
    - recent_seconds: Deletes records where timestamp >= (now - recent_seconds).
    - older_than_seconds: Deletes records where timestamp <= (now - older_than_seconds).
    """
    now = time.time()
    deleted = {"telemetry": 0, "motor_events": 0, "system_logs": 0}

    init_db()
    with get_db() as conn:
        if clear_all:
            deleted["telemetry"] = conn.execute("DELETE FROM telemetry").rowcount
            deleted["motor_events"] = conn.execute("DELETE FROM motor_events").rowcount
            deleted["system_logs"] = conn.execute("DELETE FROM system_logs").rowcount
        elif recent_seconds is not None:
            cutoff = now - recent_seconds
            deleted["telemetry"] = conn.execute("DELETE FROM telemetry WHERE timestamp >= ?", (cutoff,)).rowcount
            deleted["motor_events"] = conn.execute("DELETE FROM motor_events WHERE timestamp >= ?", (cutoff,)).rowcount
            deleted["system_logs"] = conn.execute("DELETE FROM system_logs WHERE timestamp >= ?", (cutoff,)).rowcount
        elif older_than_seconds is not None:
            cutoff = now - older_than_seconds
            deleted["telemetry"] = conn.execute("DELETE FROM telemetry WHERE timestamp <= ?", (cutoff,)).rowcount
            deleted["motor_events"] = conn.execute("DELETE FROM motor_events WHERE timestamp <= ?", (cutoff,)).rowcount
            deleted["system_logs"] = conn.execute("DELETE FROM system_logs WHERE timestamp <= ?", (cutoff,)).rowcount

    return deleted


def get_water_analytics_data(hours: float = 24) -> dict:
    """
    Returns water level history and water consumption calculations (in Liters).
    Consumption formula:
    - Identifies fill events (Motor ON -> Motor OFF).
    - Filters out micro-interval noise (< 3s duration) caused by in-flight telemetry lag.
    - Delta Water % = (max_water_pct_during_fill - water_pct_at_start).
    - Liters Filled = (Delta Water % / 100.0) * tank_capacity.
    - Locks end_pct to peak level achieved in fill window so post-fill consumption doesn't shrink history.
    - Aggregates consumption into time buckets (1D=24h, 7D=7d, 30D=30d, 1Y=365d).
    """
    tank_capacity = get_tank_capacity_liters()
    now = time.time()
    since = now - (hours * 3600)

    with get_db() as conn:
        telemetry = conn.execute("""
            SELECT timestamp, water_percentage, distance_cm, motor_status
            FROM telemetry
            WHERE timestamp >= ?
            ORDER BY timestamp ASC
        """, (since - 86400,)).fetchall()

        motor_events = conn.execute("""
            SELECT timestamp, event, motor_status
            FROM motor_events
            WHERE timestamp >= ?
            ORDER BY timestamp ASC
        """, (since - 86400,)).fetchall()

    # Filter level history for output to requested window
    level_history = [
        {
            "timestamp": t["timestamp"],
            "water_percentage": t["water_percentage"],
            "distance_cm": t["distance_cm"]
        } for t in telemetry if t["timestamp"] >= since
    ]

    # Priority 1: Extract fill cycles from explicit motor_events ON/OFF pairs
    fill_cycles = []
    on_ts = None
    for e in motor_events:
        st = str(e["motor_status"]).upper().strip()
        ts = e["timestamp"]
        if st in ("ON", "1", "TRUE") and on_ts is None:
            on_ts = ts
        elif st in ("OFF", "0", "FALSE") and on_ts is not None:
            # Filter out micro-interval noise (< 3s duration)
            if (ts - on_ts) >= 3.0:
                fill_cycles.append((on_ts, ts))
            on_ts = None

    if on_ts is not None:
        # Motor is currently ON
        fill_cycles.append((on_ts, max(on_ts, now)))

    # Priority 2: Fallback to telemetry ON contiguous blocks if no motor_events exist in window
    if not fill_cycles and telemetry:
        tel_on = False
        on_t_ts = None
        for t in telemetry:
            st = str(t["motor_status"]).upper().strip()
            ts = t["timestamp"]
            if st in ("ON", "1", "TRUE") and not tel_on:
                tel_on = True
                on_t_ts = ts
            elif st in ("OFF", "0", "FALSE") and tel_on:
                if (ts - on_t_ts) >= 3.0:
                    fill_cycles.append((on_t_ts, ts))
                tel_on = False
        if tel_on and on_t_ts is not None:
            fill_cycles.append((on_t_ts, max(on_t_ts, telemetry[-1]["timestamp"])))

    fill_events = []
    for start_ts, end_ts in fill_cycles:
        # Only include fill events that ended within or overlap the requested window
        if end_ts < since:
            continue

        # Find start_pct: closest telemetry AT OR BEFORE start_ts (or up to start_ts + 10s)
        start_tel = [t for t in telemetry if t["timestamp"] <= (start_ts + 10)]
        if start_tel:
            start_pct = start_tel[-1]["water_percentage"]
        elif telemetry:
            start_pct = telemetry[0]["water_percentage"]
        else:
            start_pct = 0

        # Find end_pct: highest peak percentage recorded during fill window [start_ts - 5s, end_ts + 60s]
        fill_tel = [t for t in telemetry if (start_ts - 5) <= t["timestamp"] <= (end_ts + 60)]
        if fill_tel:
            valid_pcts = [t["water_percentage"] for t in fill_tel if t["water_percentage"] is not None]
            end_pct = max(valid_pcts, default=start_pct) if valid_pcts else start_pct
        else:
            # Fallback to closest telemetry after end_ts
            post_tel = [t for t in telemetry if t["timestamp"] >= end_ts]
            if post_tel:
                end_pct = post_tel[0]["water_percentage"]
            else:
                end_pct = start_pct or 0

        start_val = start_pct if start_pct is not None else 0
        end_val = end_pct if end_pct is not None else start_val
        delta_pct = max(0, end_val - start_val)
        liters = round((delta_pct / 100.0) * tank_capacity, 1)

        fill_events.append({
            "start_timestamp": start_ts,
            "end_timestamp": end_ts,
            "start_pct": start_val,
            "end_pct": end_val,
            "liters": liters
        })

    if hours <= 24:
        num_buckets = 24
    elif hours <= 168:
        num_buckets = 7
    elif hours <= 720:
        num_buckets = 30
    else:
        num_buckets = 12

    bucket_size = (hours * 3600) / num_buckets
    consumption_buckets = []

    for i in range(num_buckets):
        b_start = since + (i * bucket_size)
        b_end = b_start + bucket_size
        liters_in_b = sum(f["liters"] for f in fill_events if b_start <= f["end_timestamp"] < b_end)

        consumption_buckets.append({
            "start": b_start,
            "end": b_end,
            "liters": round(liters_in_b, 1)
        })

    total_liters = sum(f["liters"] for f in fill_events)
    fill_count = len(fill_events)

    return {
        "hours": hours,
        "tank_capacity_liters": tank_capacity,
        "total_consumed_liters": round(total_liters, 1),
        "fill_count": fill_count,
        "level_history": level_history,
        "consumption_buckets": consumption_buckets,
    }


