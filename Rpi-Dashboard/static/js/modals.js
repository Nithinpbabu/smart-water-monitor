/**
 * Modal Manager + Chart Rendering for Water Tank Monitor V2 Dashboard.
 * Handles glassmorphic modal overlays triggered by clicking landing page elements.
 */

/* ─── Global Chart.js Defaults ─── */
Chart.defaults.color = 'rgba(255,255,255,0.5)';
Chart.defaults.borderColor = 'rgba(255,255,255,0.06)';
Chart.defaults.font.family = 'ui-sans-serif,-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif';

/* ─── Modal Core ─── */
const ModalManager = (function () {
  const backdrop = document.getElementById('modalBackdrop');
  let activeModal = null;

  function open(id) {
    if (activeModal) close();
    const sheet = document.getElementById(id);
    if (!sheet) return;
    activeModal = sheet;
    backdrop.classList.add('active');
    sheet.classList.add('active');
    // Fire open callback
    const cb = openCallbacks[id];
    if (cb) cb();
  }

  function close() {
    if (!activeModal) return;
    backdrop.classList.remove('active');
    activeModal.classList.remove('active');
    activeModal = null;
  }

  // Close on backdrop click
  backdrop.addEventListener('click', close);

  // Close buttons
  document.querySelectorAll('.modal-close').forEach((btn) => {
    btn.addEventListener('click', close);
  });

  const openCallbacks = {};
  function onOpen(id, fn) { openCallbacks[id] = fn; }

  return { open, close, onOpen };
})();

/* ─── Utility: Format Timestamp ─── */
function fmtTime(ts) {
  const d = new Date(ts * 1000);
  return d.toLocaleTimeString('en-IN', { hour: '2-digit', minute: '2-digit', hour12: false });
}
function fmtDate(ts) {
  const d = new Date(ts * 1000);
  return d.toLocaleDateString('en-IN', { day: '2-digit', month: 'short' });
}

/* ═══════════════════════════════════════════════════════════════════════════
   WATER LEVEL HISTORY MODAL
   ═══════════════════════════════════════════════════════════════════════════ */

let waterChart = null;
let waterTimeRange = 24;
let waterViewMode = 'level'; // 'level' or 'consumed'

async function loadWaterChart(hours, mode) {
  if (hours !== undefined) waterTimeRange = hours;
  if (mode !== undefined) waterViewMode = mode;

  // Update active view tabs
  document.querySelectorAll('#waterModal .view-tab').forEach((t) => {
    t.classList.toggle('active', t.dataset.mode === waterViewMode);
  });

  // Update active time tabs
  document.querySelectorAll('#waterModal .time-tab').forEach((t) => {
    t.classList.toggle('active', parseInt(t.dataset.hours) === waterTimeRange);
  });

  const summaryBar = document.getElementById('consumptionSummaryBar');
  if (summaryBar) {
    summaryBar.style.display = waterViewMode === 'consumed' ? 'flex' : 'none';
  }

  const resp = await fetch(`/api/water-analytics?hours=${waterTimeRange}`);
  const data = await resp.json();

  const ctx = document.getElementById('waterChartCanvas').getContext('2d');
  if (waterChart) waterChart.destroy();

  if (waterViewMode === 'level') {
    const levelHist = data.level_history || [];
    const labels = levelHist.map((d) => waterTimeRange <= 24 ? fmtTime(d.timestamp) : fmtDate(d.timestamp));
    const waterPct = levelHist.map((d) => d.water_percentage);
    const distCm = levelHist.map((d) => d.distance_cm);

    waterChart = new Chart(ctx, {
      type: 'line',
      data: {
        labels,
        datasets: [
          {
            label: 'Water %',
            data: waterPct,
            borderColor: '#3d8bff',
            backgroundColor: 'rgba(61,139,255,0.08)',
            fill: true,
            tension: 0.35,
            borderWidth: 2,
            pointRadius: 0,
            pointHitRadius: 8,
          },
          {
            label: 'Distance (cm)',
            data: distCm,
            borderColor: 'rgba(255,255,255,0.25)',
            borderDash: [4, 4],
            tension: 0.35,
            borderWidth: 1,
            pointRadius: 0,
            yAxisID: 'y1',
          },
        ],
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        interaction: { mode: 'index', intersect: false },
        plugins: { legend: { display: true, labels: { boxWidth: 12, padding: 12 } } },
        scales: {
          x: { ticks: { maxTicksLimit: 8, font: { size: 10 } }, grid: { display: false } },
          y: { min: 0, max: 100, ticks: { callback: (v) => v + '%', font: { size: 10 } } },
          y1: { position: 'right', ticks: { font: { size: 10 } }, grid: { display: false } },
        },
      },
    });
  } else {
    // Water Consumed Bar Chart
    const totalValEl = document.getElementById('consTotalVal');
    if (totalValEl) totalValEl.textContent = `${data.total_consumed_liters} L`;
    
    const countValEl = document.getElementById('consFillCountVal');
    if (countValEl) countValEl.textContent = `${data.fill_count} ${data.fill_count === 1 ? 'Cycle' : 'Cycles'}`;

    const buckets = data.consumption_buckets || [];
    const labels = buckets.map((b) => waterTimeRange <= 24 ? fmtTime(b.start) : fmtDate(b.start));
    const liters = buckets.map((b) => b.liters);

    waterChart = new Chart(ctx, {
      type: 'bar',
      data: {
        labels,
        datasets: [
          {
            label: 'Water Consumed (L)',
            data: liters,
            backgroundColor: 'rgba(60,207,130,0.6)',
            borderColor: '#3fcf82',
            borderWidth: 1,
            borderRadius: 4,
          },
        ],
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
          legend: { display: true, labels: { boxWidth: 12, padding: 12 } },
          tooltip: {
            callbacks: {
              label: (item) => `Consumed: ${item.raw} Liters`,
            },
          },
        },
        scales: {
          x: { ticks: { maxTicksLimit: 12, font: { size: 10 } }, grid: { display: false } },
          y: { min: 0, ticks: { callback: (v) => v + ' L', font: { size: 10 } } },
        },
      },
    });
  }
}

// View tab clicks (Level vs Consumed)
document.querySelectorAll('#waterModal .view-tab').forEach((tab) => {
  tab.addEventListener('click', () => loadWaterChart(waterTimeRange, tab.dataset.mode));
});

// Time tab clicks (1D, 7D, 30D, 1Y)
document.querySelectorAll('#waterModal .time-tab').forEach((tab) => {
  tab.addEventListener('click', () => loadWaterChart(parseInt(tab.dataset.hours), waterViewMode));
});

ModalManager.onOpen('waterModal', () => loadWaterChart(waterTimeRange, waterViewMode));

/* ═══════════════════════════════════════════════════════════════════════════
   BATTERY HEALTH MODAL
   ═══════════════════════════════════════════════════════════════════════════ */

let batteryChart = null;
let batteryTimeRange = 24;

async function loadBatteryChart(hours) {
  batteryTimeRange = hours;
  document.querySelectorAll('#batteryModal .time-tab').forEach((t) => {
    t.classList.toggle('active', parseInt(t.dataset.hours) === hours);
  });

  const resp = await fetch(`/api/telemetry?hours=${hours}`);
  const data = await resp.json();

  const labels = data.map((d) => hours <= 24 ? fmtTime(d.timestamp) : fmtDate(d.timestamp));
  const voltage = data.map((d) => d.battery_voltage);
  const pct = data.map((d) => d.battery_percentage);

  const ctx = document.getElementById('batteryChartCanvas').getContext('2d');
  if (batteryChart) batteryChart.destroy();

  batteryChart = new Chart(ctx, {
    type: 'line',
    data: {
      labels,
      datasets: [
        {
          label: 'Voltage (V)',
          data: voltage,
          borderColor: '#ffd166',
          backgroundColor: 'rgba(255,209,102,0.08)',
          fill: true,
          tension: 0.35,
          borderWidth: 2,
          pointRadius: 0,
        },
        {
          label: 'Battery %',
          data: pct,
          borderColor: '#5affa0',
          tension: 0.35,
          borderWidth: 1.5,
          pointRadius: 0,
          yAxisID: 'y1',
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: 'index', intersect: false },
      plugins: { legend: { display: true, labels: { boxWidth: 12, padding: 12 } } },
      scales: {
        x: { ticks: { maxTicksLimit: 8, font: { size: 10 } }, grid: { display: false } },
        y: { ticks: { callback: (v) => v.toFixed(1) + 'V', font: { size: 10 } } },
        y1: { position: 'right', min: 0, max: 100, ticks: { callback: (v) => v + '%', font: { size: 10 } }, grid: { display: false } },
      },
    },
  });
}

document.querySelectorAll('#batteryModal .time-tab').forEach((tab) => {
  tab.addEventListener('click', () => loadBatteryChart(parseInt(tab.dataset.hours)));
});

ModalManager.onOpen('batteryModal', () => loadBatteryChart(batteryTimeRange));

/* ═══════════════════════════════════════════════════════════════════════════
   MOTOR RUNTIME HISTORY MODAL
   ═══════════════════════════════════════════════════════════════════════════ */

let motorChart = null;
let motorTimeRange = 24;

function fmtLastActive(ts) {
  if (!ts) return 'Never';
  const d = new Date(ts * 1000);
  const now = new Date();
  const isToday = d.toDateString() === now.toDateString();
  const timeStr = d.toLocaleTimeString('en-IN', { hour: '2-digit', minute: '2-digit', hour12: true });
  if (isToday) return timeStr;
  const dateStr = d.toLocaleDateString('en-IN', { day: '2-digit', month: 'short' });
  return `${dateStr} ${timeStr}`;
}

function fmtDuration(totalMinutes) {
  if (!totalMinutes || totalMinutes === 0) return '0 min';
  if (totalMinutes < 60) return `${Math.round(totalMinutes)} min`;
  const hrs = Math.floor(totalMinutes / 60);
  const mins = Math.round(totalMinutes % 60);
  return mins > 0 ? `${hrs} hr ${mins} min` : `${hrs} hr`;
}

async function loadMotorRuntimeChart(hours) {
  motorTimeRange = hours;
  document.querySelectorAll('#motorModal .time-tab').forEach((t) => {
    t.classList.toggle('active', parseInt(t.dataset.hours) === hours);
  });

  const resp = await fetch(`/api/motor-runtime?hours=${hours}`);
  const data = await resp.json();

  // Update summary bar
  document.getElementById('motorTotalVal').textContent = fmtDuration(data.total_runtime_minutes);
  document.getElementById('motorLastActiveVal').textContent = fmtLastActive(data.last_active_timestamp);

  const buckets = data.buckets || [];
  const labels = buckets.map((b) => hours <= 24 ? fmtTime(b.start) : fmtDate(b.start));
  const runtimes = buckets.map((b) => hours > 168 ? (b.runtime_minutes / 60).toFixed(1) : b.runtime_minutes);
  const unitLabel = hours > 168 ? 'hr' : 'min';

  const ctx = document.getElementById('motorChartCanvas').getContext('2d');
  if (motorChart) motorChart.destroy();

  // Create gradient
  const gradient = ctx.createLinearGradient(0, 0, 0, 200);
  gradient.addColorStop(0, 'rgba(61, 139, 255, 0.85)');
  gradient.addColorStop(1, 'rgba(90, 255, 160, 0.25)');

  motorChart = new Chart(ctx, {
    type: 'bar',
    data: {
      labels,
      datasets: [
        {
          label: `Runtime (${unitLabel})`,
          data: runtimes,
          backgroundColor: gradient,
          borderColor: '#3d8bff',
          borderWidth: 1,
          borderRadius: 6,
          borderSkipped: false,
          maxBarThickness: 28,
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: 'index', intersect: false },
      plugins: {
        legend: { display: false },
        tooltip: {
          callbacks: {
            label: (ctx) => `Runtime: ${ctx.raw} ${unitLabel}`,
          },
        },
      },
      scales: {
        x: { ticks: { maxTicksLimit: 8, font: { size: 10 } }, grid: { display: false } },
        y: {
          min: 0,
          ticks: { callback: (v) => v + ' ' + unitLabel, font: { size: 10 } },
          grid: { color: 'rgba(255,255,255,0.06)' },
        },
      },
    },
  });
}

document.querySelectorAll('#motorModal .time-tab').forEach((tab) => {
  tab.addEventListener('click', () => loadMotorRuntimeChart(parseInt(tab.dataset.hours)));
});

ModalManager.onOpen('motorModal', () => loadMotorRuntimeChart(motorTimeRange));

/* ═══════════════════════════════════════════════════════════════════════════
   SETTINGS / CONFIG MODAL
   ═══════════════════════════════════════════════════════════════════════════ */

async function loadConfig() {
  const resp = await fetch('/api/config');
  const cfg = await resp.json();
  if (cfg.water_top_level !== undefined) {
    document.getElementById('cfgTopLevel').value = cfg.water_top_level;
    document.getElementById('cfgBottomLevel').value = cfg.water_bottom_level;
    document.getElementById('cfgNormalInterval').value = cfg.normal_interval;
    document.getElementById('cfgLowThreshold').value = cfg.low_water_threshold;
    document.getElementById('cfgMaxRuntime').value = cfg.max_motor_runtime;
  }
}

// Save config button
document.getElementById('btnSaveConfig').addEventListener('click', () => {
  const data = {
    water_top_level: parseInt(document.getElementById('cfgTopLevel').value),
    water_bottom_level: parseInt(document.getElementById('cfgBottomLevel').value),
    normal_interval: parseInt(document.getElementById('cfgNormalInterval').value),
    low_water_threshold: parseInt(document.getElementById('cfgLowThreshold').value),
    max_motor_runtime: parseInt(document.getElementById('cfgMaxRuntime').value),
  };
  TankWS.send('config_set', data);
  alert('Configuration sent to gateway!');
});

// OTA buttons
document.getElementById('btnOtaStart').addEventListener('click', () => {
  if (confirm('Enter OTA Firmware Update Mode?')) TankWS.send('ota_start', {});
});
document.getElementById('btnOtaExit').addEventListener('click', () => {
  TankWS.send('ota_exit', {});
});

// Pre-Peak toggle
const prePeakToggle = document.getElementById('prePeakToggle');
prePeakToggle.addEventListener('click', () => {
  const isOn = prePeakToggle.classList.toggle('on');
  TankWS.send('prepeak_config', { enabled: isOn });
});

// Save Pre‑Peak config
document.getElementById('btnSavePrePeak').addEventListener('click', () => {
  const data = {
    enabled: prePeakToggle.classList.contains('on'),
    peak_hour: parseInt(document.getElementById('cfgPeakHour').value),
    peak_minute: parseInt(document.getElementById('cfgPeakMinute').value),
    water_threshold: parseInt(document.getElementById('cfgPeakThreshold').value),
  };
  TankWS.send('prepeak_config', data);
  alert('Pre-Peak Fill config sent!');
});

ModalManager.onOpen('settingsModal', loadConfig);

/* ═══════════════════════════════════════════════════════════════════════════
   SYSTEM LOG CONSOLE (live streaming)
   ═══════════════════════════════════════════════════════════════════════════ */

const logConsole = document.getElementById('logConsole');
function appendLog(source, message) {
  const el = document.createElement('div');
  el.className = 'log-entry';
  const now = new Date().toLocaleTimeString('en-IN', { hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false });
  el.innerHTML = `<span class="log-ts">${now}</span><span class="log-src">[${source}]</span>${message}`;
  logConsole.appendChild(el);
  // Keep max 150 entries
  while (logConsole.children.length > 150) logConsole.removeChild(logConsole.firstChild);
  logConsole.scrollTop = logConsole.scrollHeight;
}

/* ═══════════════════════════════════════════════════════════════════════════
   WEBSOCKET EVENT BINDING — Live Telemetry → Landing Page
   ═══════════════════════════════════════════════════════════════════════════ */

function applyState(data) {
  if (!data) return;

  // Handle nested initial_state payload
  if (data.telemetry) applyState(data.telemetry);
  if (data.motor) applyState(data.motor);

  if (data.water_percentage !== undefined) {
    LiquidDashboard.setProgress(data.water_percentage);
  }
  if (data.battery_percentage !== undefined) {
    LiquidDashboard.setBattery(data.battery_percentage);
  }
  if (data.motor_status !== undefined) {
    const isOn = data.motor_status === 'ON' || data.motor_status === 1 || data.motor_status === '1';
    LiquidDashboard.setMotor(isOn);
  }
}

TankWS.on('initial_state', applyState);
TankWS.on('telemetry', applyState);
TankWS.on('motor', applyState);

TankWS.on('motor_event', (data) => {
  applyState(data);
  if (data.event && data.reason) {
    appendLog('MOTOR', `${data.event} — ${data.reason}`);
  }
});

TankWS.on('log', (data) => {
  appendLog(data.node || 'SYSTEM', data.message || JSON.stringify(data));
});

TankWS.on('system_log', (data) => {
  appendLog(data.source || data.node || 'SYSTEM', data.message || JSON.stringify(data));
});

TankWS.on('ota_ack', (data) => {
  appendLog('OTA', JSON.stringify(data));
});

/* ─── Motor Toggle Command ─── */
LiquidDashboard.onMotorToggle((wantsOn) => {
  TankWS.send('motor_command', { action: wantsOn ? 'ON' : 'OFF' });
});

/* ─── Landing Element Click → Modal Bindings ─── */

// Click on the water gauge sphere → open water level history ONLY when already docked in Phase 2
document.getElementById('hero-wrap').addEventListener('click', (e) => {
  if (window.LiquidDashboard && window.LiquidDashboard.isDocked() && !window.LiquidDashboard.wasJustDocked()) {
    e.stopPropagation();
    ModalManager.open('waterModal');
  }
});

// Click on battery stat card → open battery history
document.querySelector('.stat-card:first-child').addEventListener('click', () => {
  ModalManager.open('batteryModal');
});

// Click on runtime stat card → open motor history
document.querySelector('.stat-card:nth-child(2)').addEventListener('click', () => {
  ModalManager.open('motorModal');
});

// Settings gear button → open settings modal
LiquidDashboard.onSettingsClick(() => {
  ModalManager.open('settingsModal');
});
