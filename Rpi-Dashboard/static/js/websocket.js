/**
 * WebSocket Client for Water Tank Monitor V2 Dashboard.
 * Connects to FastAPI backend, receives live telemetry, and sends commands.
 */

const WS_URL = `ws://${location.host}/ws`;

const TankWS = (function () {
  let ws = null;
  let reconnectTimer = null;
  const listeners = {};

  function connect() {
    if (ws && ws.readyState <= 1) return;

    ws = new WebSocket(WS_URL);

    ws.onopen = () => {
      console.log('[WS] Connected');
      emit('connected');
    };

    ws.onmessage = (evt) => {
      try {
        const msg = JSON.parse(evt.data);
        emit(msg.type, msg.data);
      } catch (e) {
        console.warn('[WS] Bad message', e);
      }
    };

    ws.onclose = () => {
      console.log('[WS] Disconnected — reconnecting in 3s');
      scheduleReconnect();
    };

    ws.onerror = () => {
      ws.close();
    };
  }

  function scheduleReconnect() {
    clearTimeout(reconnectTimer);
    reconnectTimer = setTimeout(connect, 3000);
  }

  function send(type, data) {
    if (ws && ws.readyState === 1) {
      ws.send(JSON.stringify({ type, data }));
    }
  }

  function on(event, fn) {
    if (!listeners[event]) listeners[event] = [];
    listeners[event].push(fn);
  }

  function emit(event, data) {
    (listeners[event] || []).forEach((fn) => fn(data));
  }

  // Auto-connect
  connect();

  return { connect, send, on };
})();
