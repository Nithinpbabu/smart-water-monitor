# 🚰 Water Tank Monitor V2 — Raspberry Pi 5 Dashboard & Backend

Modern, glassmorphic real-time dashboard and REST/WebSocket API service for monitoring water tank level, battery stats, motor runtime, and historical consumption analytics on a Raspberry Pi 5.

---

## 🌟 Features

- **Real-Time Telemetry & Controls**: Live water percentage gauge, battery stats, and motor ON/OFF toggle over WebSockets.
- **Water Consumption Analytics**: Automatically calculates Liters consumed per fill cycle ($L = \Delta\% \times \text{Capacity} / 100$) across 1D, 7D, 30D, and 1Y timeframes.
- **Dynamic Tank Capacity Configuration**: Easily adjust tank capacity (e.g. 500L, 1000L) on the fly without restarting the service.
- **Database & Purging CLI Tool**: Built-in CLI management tool (`./manage_db` / `./clear_data`) to view stats, change tank capacity, and purge telemetry data by time range.
- **SQLite WAL Mode**: Thread-safe concurrent database access for background MQTT ingestion and live WebSockets.

---

## 🛠️ CLI Management Tool (`./manage_db` / `./clear_data`)

The `/Rpi-Dashboard` folder includes a management tool to view database status, edit tank capacity, and clear historical telemetry records without interrupting the live server.

### 1. View Database Status & Tank Capacity
Displays configured tank capacity, database file size, record counts, and date range:
```bash
./manage_db --status
```
*(or `./clear_data --status`)*

### 2. Change Tank Capacity (e.g. 500 Liters)
Updates the tank capacity in SQLite. **The running dashboard immediately recalculates consumption graphs with the new capacity on the next chart fetch!**
```bash
./manage_db --tank 500
```

### 3. Clear Data by Time Duration
- **Clear recent data (last 6 hours)**:
  ```bash
  ./clear_data --6h
  ```
- **Clear recent data (last 360 days)**:
  ```bash
  ./clear_data --360d
  ```
- **Clear data older than 30 days**:
  ```bash
  ./manage_db --older-than 30d
  ```
- **Clear ALL historical data**:
  ```bash
  ./manage_db --all
  ```
- **Skip confirmation prompt (`-y` / `--yes`)**:
  ```bash
  ./clear_data --6h -y
  ```

### 4. Running Inside Docker Container
If running inside the Docker container on your Raspberry Pi 5:
```bash
docker exec -it water_tank_dashboard ./manage_db --status
docker exec -it water_tank_dashboard ./clear_data --6h
docker exec -it water_tank_dashboard ./manage_db --tank 500
```

---

## 🚀 Deployment on Raspberry Pi 5

### 1. Build and Run Container
```bash
cd /home/pi/water_tank_v2/Rpi-Dashboard

# Rebuild and start container
docker compose build --no-cache
docker compose up -d
```

### 2. Access Dashboard
Open your browser and navigate to:
```
http://<RASPBERRY_PI_IP>:8001/
```
*(Tip: Press **Ctrl + F5** or **Cmd + Shift + R** to bypass browser cache when deploying updates).*

---

## 🗄️ Architecture & Port Map

- **FastAPI Web Server**: Port `8001`
- **MQTT Listener**: Port `1883` (Connects to Mosquitto on Pi)
- **SQLite Database**: Stored in `/app/data/water_tank.db` inside container (persisted via Docker volume).
