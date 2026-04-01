#include "KombuchaDashboardApp.h"

#include <WiFi.h>

const int KombuchaDashboardApp::ECHO_PINS[2] = {16, 17};

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Kombucha Actuator Demo</title>
  <style>
    body { font-family: Arial, sans-serif; background:#0f172a; color:#e2e8f0; margin:0; }
    header { background:#1e293b; padding:12px 16px; }
    .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(280px,1fr)); gap:12px; padding:12px; }
    .card { background:#1e293b; border-radius:8px; padding:12px; }
    .row { display:flex; justify-content:space-between; margin:6px 0; }
    button { padding:8px 10px; border:1px solid #334155; background:#0b1220; color:#e2e8f0; border-radius:6px; cursor:pointer; }
    .log { max-height:220px; overflow-y:auto; font-family:monospace; font-size:12px; background:#0b1220; border-radius:6px; padding:8px; }
    .muted { color:#94a3b8; font-size:12px; }
  </style>
</head>
<body>
<header>
  <h2>Kombucha Breadboard Demo (Actuators + US)</h2>
  <div class="muted">Pumps default to US-based autopilot. Manual web commands override temporarily.</div>
</header>
<section class="grid">
  <div class="card">
    <h3>Ultrasonic Levels</h3>
    <div class="row"><span>Jar 0</span><span id="level0">--</span></div>
    <div class="row"><span>Jar 1</span><span id="level1">--</span></div>
  </div>

  <div class="card">
    <h3>Actuator Control</h3>
    <div class="row"><span>Heater</span><button id="heaterBtn" onclick="toggleHeater()">Toggle</button></div>
    <div class="row"><span>Oxygen</span><button id="oxygenBtn" onclick="toggleOxygen()">Toggle</button></div>
    <div class="row"><span>Pump A</span><button onclick="setPumpA(true)">Manual ON</button></div>
    <div class="row"><span></span><button onclick="setPumpA(false)">Manual OFF</button></div>
    <div class="row"><span>Pump B</span><button onclick="setPumpB(true)">Manual ON</button></div>
    <div class="row"><span></span><button onclick="setPumpB(false)">Manual OFF</button></div>
    <div style="margin-top:10px;"><button onclick="returnPumpsAuto()">Return Pumps to Auto</button></div>
  </div>

  <div class="card">
    <h3>Current States</h3>
    <div class="row"><span>Heater</span><span id="heaterState">--</span></div>
    <div class="row"><span>Oxygen</span><span id="oxygenState">--</span></div>
    <div class="row"><span>Pump A Duty</span><span id="pumpA">--</span></div>
    <div class="row"><span>Pump B Duty</span><span id="pumpB">--</span></div>
    <div class="row"><span>Pump A Mode</span><span id="pumpAMode">--</span></div>
    <div class="row"><span>Pump B Mode</span><span id="pumpBMode">--</span></div>
  </div>

  <div class="card" style="grid-column:1 / -1;">
    <h3>Status Logs</h3>
    <div class="log" id="logs"></div>
  </div>
</section>
<script>
  let state = null;

  function postControl(data){
    return fetch('/api/control', {
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams(data)
    });
  }

  async function toggleHeater(){ if(!state) return; await postControl({heaterOn: state.actuators.heaterOn ? 0 : 1}); await refresh(); }
  async function toggleOxygen(){ if(!state) return; await postControl({oxygenOn: state.actuators.oxygenOn ? 0 : 1}); await refresh(); }
  async function setPumpA(on){ await postControl({pumpDutyA: on ? 140 : 0}); await refresh(); }
  async function setPumpB(on){ await postControl({pumpDutyB: on ? 140 : 0}); await refresh(); }
  async function returnPumpsAuto(){ await postControl({autoPumps: 1}); await refresh(); }

  async function refresh(){
    const [s, l] = await Promise.all([fetch('/api/state'), fetch('/api/logs')]);
    state = await s.json();
    const logs = await l.json();

    const lvl0 = state.sensors.level0Ok ? state.sensors.level0Cm.toFixed(1) + ' cm' : 'timeout';
    const lvl1 = state.sensors.level1Ok ? state.sensors.level1Cm.toFixed(1) + ' cm' : 'timeout';
    document.getElementById('level0').textContent = lvl0;
    document.getElementById('level1').textContent = lvl1;

    document.getElementById('heaterState').textContent = state.actuators.heaterOn ? 'ON' : 'OFF';
    document.getElementById('oxygenState').textContent = state.actuators.oxygenOn ? 'ON' : 'OFF';
    document.getElementById('pumpA').textContent = state.actuators.pumpDutyA;
    document.getElementById('pumpB').textContent = state.actuators.pumpDutyB;
    document.getElementById('pumpAMode').textContent = state.actuators.pumpAutoA ? 'AUTO' : 'MANUAL';
    document.getElementById('pumpBMode').textContent = state.actuators.pumpAutoB ? 'AUTO' : 'MANUAL';

    document.getElementById('heaterBtn').textContent = state.actuators.heaterOn ? 'Turn OFF' : 'Turn ON';
    document.getElementById('oxygenBtn').textContent = state.actuators.oxygenOn ? 'Turn OFF' : 'Turn ON';

    document.getElementById('logs').innerHTML = logs.map(x => `<div>${x}</div>`).join('');
  }

  refresh();
  setInterval(refresh, 1000);
</script>
</body>
</html>
)HTML";

String KombuchaDashboardApp::boolToJson(bool value) {
  return value ? "true" : "false";
}

void KombuchaDashboardApp::addLog(const String& msg) {
  String line = String(millis()) + " ms: " + msg;
  _eventLog[_eventWriteIndex] = line;
  _eventWriteIndex = (_eventWriteIndex + 1) % STATUS_LOG_MAX;
  if (_eventCount < STATUS_LOG_MAX) _eventCount++;
  Serial.println(line);
}

void KombuchaDashboardApp::setOxygen(bool on) {
  _actuatorState.oxygenOn = on;
  digitalWrite(OXYGEN_RELAY_PIN, on ? HIGH : LOW);
  addLog(String("oxygen pump turned ") + (on ? "on" : "off"));
}

void KombuchaDashboardApp::setHeater(bool on) {
  _actuatorState.heaterOn = on;
  digitalWrite(HEATER_RELAY_PIN, on ? HIGH : LOW);
  addLog(String("heater turned ") + (on ? "on" : "off"));
}

void KombuchaDashboardApp::setPumpDutyA(int duty, bool manualCommand) {
  _actuatorState.pumpDutyA = duty;
  if (duty > 0) _pumps.onA(duty);
  else _pumps.offA();

  if (manualCommand) {
    _manualOverrideUntilA = millis() + MANUAL_OVERRIDE_MS;
    _actuatorState.pumpAutoA = false;
    addLog(String("peristaltic pump A manual duty set to ") + duty);
  } else {
    addLog(String("peristaltic pump A auto duty set to ") + duty);
  }
}

void KombuchaDashboardApp::setPumpDutyB(int duty, bool manualCommand) {
  _actuatorState.pumpDutyB = duty;
  if (duty > 0) _pumps.onB(duty);
  else _pumps.offB();

  if (manualCommand) {
    _manualOverrideUntilB = millis() + MANUAL_OVERRIDE_MS;
    _actuatorState.pumpAutoB = false;
    addLog(String("peristaltic pump B manual duty set to ") + duty);
  } else {
    addLog(String("peristaltic pump B auto duty set to ") + duty);
  }
}

void KombuchaDashboardApp::updateSensors() {
  _currentSensors.timestampMs = millis();

  for (int i = 0; i < LEVEL_SENSOR_COUNT; i++) {
    USReading r = _us.readOne(i);
    _currentSensors.levelOk[i] = r.ok;
    _currentSensors.levelCm[i] = r.ok ? r.cm : NAN;
  }
}

void KombuchaDashboardApp::applyPumpAutopilot() {
  const unsigned long now = millis();

  if (now >= _manualOverrideUntilA) {
    if (!_actuatorState.pumpAutoA) {
      _actuatorState.pumpAutoA = true;
      addLog("peristaltic pump A returned to auto mode");
    }

    if (_currentSensors.levelOk[0]) {
      if (_currentSensors.levelCm[0] < PUMP_A_ON_BELOW_CM && _actuatorState.pumpDutyA == 0) {
        setPumpDutyA(DEFAULT_PUMP_DUTY, false);
      } else if (_currentSensors.levelCm[0] > PUMP_A_OFF_ABOVE_CM && _actuatorState.pumpDutyA > 0) {
        setPumpDutyA(0, false);
      }
    }
  }

  if (now >= _manualOverrideUntilB) {
    if (!_actuatorState.pumpAutoB) {
      _actuatorState.pumpAutoB = true;
      addLog("peristaltic pump B returned to auto mode");
    }

    if (_currentSensors.levelOk[1]) {
      if (_currentSensors.levelCm[1] < PUMP_B_ON_BELOW_CM && _actuatorState.pumpDutyB == 0) {
        setPumpDutyB(DEFAULT_PUMP_DUTY, false);
      } else if (_currentSensors.levelCm[1] > PUMP_B_OFF_ABOVE_CM && _actuatorState.pumpDutyB > 0) {
        setPumpDutyB(0, false);
      }
    }
  }
}

String KombuchaDashboardApp::makeStateJson() {
  String json = "{";
  json += "\"timestampMs\":" + String(_currentSensors.timestampMs) + ",";

  json += "\"sensors\":{";
  json += "\"level0Ok\":" + boolToJson(_currentSensors.levelOk[0]) + ",";
  json += "\"level0Cm\":" + String(_currentSensors.levelCm[0], 2) + ",";
  json += "\"level1Ok\":" + boolToJson(_currentSensors.levelOk[1]) + ",";
  json += "\"level1Cm\":" + String(_currentSensors.levelCm[1], 2);
  json += "},";

  json += "\"actuators\":{";
  json += "\"heaterOn\":" + boolToJson(_actuatorState.heaterOn) + ",";
  json += "\"oxygenOn\":" + boolToJson(_actuatorState.oxygenOn) + ",";
  json += "\"pumpDutyA\":" + String(_actuatorState.pumpDutyA) + ",";
  json += "\"pumpDutyB\":" + String(_actuatorState.pumpDutyB) + ",";
  json += "\"pumpAutoA\":" + boolToJson(_actuatorState.pumpAutoA) + ",";
  json += "\"pumpAutoB\":" + boolToJson(_actuatorState.pumpAutoB);
  json += "}";

  json += "}";
  return json;
}

String KombuchaDashboardApp::makeLogJson() {
  String json = "[";
  if (_eventCount == 0) return "[]";

  size_t start = (_eventWriteIndex + STATUS_LOG_MAX - _eventCount) % STATUS_LOG_MAX;
  for (size_t i = 0; i < _eventCount; i++) {
    size_t idx = (start + i) % STATUS_LOG_MAX;
    if (i > 0) json += ",";
    String line = _eventLog[idx];
    line.replace("\\", "\\\\");
    line.replace("\"", "\\\"");
    json += "\"" + line + "\"";
  }
  json += "]";
  return json;
}

void KombuchaDashboardApp::handleRoot() {
  _server.send_P(200, "text/html", DASHBOARD_HTML);
}

void KombuchaDashboardApp::handleApiState() {
  _server.send(200, "application/json", makeStateJson());
}

void KombuchaDashboardApp::handleApiLogs() {
  _server.send(200, "application/json", makeLogJson());
}

void KombuchaDashboardApp::handleApiControl() {
  if (_server.hasArg("oxygenOn")) {
    setOxygen(_server.arg("oxygenOn") == "1");
  }
  if (_server.hasArg("heaterOn")) {
    setHeater(_server.arg("heaterOn") == "1");
  }
  if (_server.hasArg("pumpDutyA")) {
    setPumpDutyA(constrain(_server.arg("pumpDutyA").toInt(), 0, 255), true);
  }
  if (_server.hasArg("pumpDutyB")) {
    setPumpDutyB(constrain(_server.arg("pumpDutyB").toInt(), 0, 255), true);
  }
  if (_server.hasArg("autoPumps") && _server.arg("autoPumps") == "1") {
    _manualOverrideUntilA = 0;
    _manualOverrideUntilB = 0;
    _actuatorState.pumpAutoA = true;
    _actuatorState.pumpAutoB = true;
    addLog("manual pump override cleared by web command");
  }

  _server.send(200, "application/json", "{\"ok\":true}");
}

void KombuchaDashboardApp::setupWebRoutes() {
  _server.on("/", HTTP_GET, [this]() { handleRoot(); });
  _server.on("/api/state", HTTP_GET, [this]() { handleApiState(); });
  _server.on("/api/logs", HTTP_GET, [this]() { handleApiLogs(); });
  _server.on("/api/control", HTTP_POST, [this]() { handleApiControl(); });
  _server.begin();
}

void KombuchaDashboardApp::connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(_wifiSsid, _wifiPassword);

  addLog("system boot / reconnect messages: Wi-Fi connecting");
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000UL) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    addLog("system boot / reconnect messages: Wi-Fi connected");
    addLog(String("dashboard URL: http://") + WiFi.localIP().toString());
  } else {
    addLog("alert conditions: Wi-Fi connection timeout");
  }
}

void KombuchaDashboardApp::begin() {
  Serial.begin(115200);
  delay(200);

  pinMode(OXYGEN_RELAY_PIN, OUTPUT);
  pinMode(HEATER_RELAY_PIN, OUTPUT);
  setOxygen(false);
  setHeater(false);

  _us.begin();
  _us.setTiming(20000UL, 60UL);

  if (!_pumps.begin()) {
    addLog("alert conditions: failed to initialize pump PWM");
  }
  setPumpDutyA(0, false);
  setPumpDutyB(0, false);

  connectWiFi();
  setupWebRoutes();
  updateSensors();
  addLog("system boot / reconnect messages: actuator demo ready");
}

void KombuchaDashboardApp::loop() {
  _server.handleClient();

  const unsigned long now = millis();
  if ((now - _lastSensorUpdateMs) >= SENSOR_UPDATE_MS) {
    _lastSensorUpdateMs = now;
    updateSensors();
    applyPumpAutopilot();
  }
}
