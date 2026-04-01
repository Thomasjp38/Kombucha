// Backup copy of internet-capable test sketch (Wi-Fi + WPA2-Enterprise options).
// Keep this file as a restore point when using an offline-only test.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#include <esp_wifi.h>
#if __has_include("esp_eap_client.h")
  #include <esp_eap_client.h>
  #define HAS_EAP_CLIENT_API 1
  #define HAS_WPA2_LEGACY_API 0
#elif __has_include("esp_wpa2.h")
  #include <esp_wpa2.h>
  #define HAS_EAP_CLIENT_API 0
  #define HAS_WPA2_LEGACY_API 1
#else
  #define HAS_EAP_CLIENT_API 0
  #define HAS_WPA2_LEGACY_API 0
#endif

#include "UltrasonicArray.h"

// -----------------------------------------------------------------------------
// test.ino
// Single-file web demo sketch so you can flash one file and preview dashboard UI.
// -----------------------------------------------------------------------------

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* WIFI_IDENTITY = "YOUR_WIFI_IDENTITY";
const char* WIFI_USERNAME = "YOUR_WIFI_USERNAME";

// Set true for school/company WPA2-Enterprise Wi-Fi.
// Keep false for normal home/hotspot SSID+password.
constexpr bool WIFI_USE_WPA2_ENTERPRISE = false;

constexpr int TRIG_PIN = 14;
constexpr int ECHO_PINS[] = {25, 26, 27};
constexpr int SENSOR_COUNT = sizeof(ECHO_PINS) / sizeof(ECHO_PINS[0]);

constexpr int HEATER_PIN = 32;
constexpr int OXYGEN_PIN = 33;
constexpr int PUMP_A_PIN = 18;
constexpr int PUMP_B_PIN = 19;

UltrasonicArray us(TRIG_PIN, ECHO_PINS, SENSOR_COUNT);
WebServer server(80);

struct DemoState {
  float levelCm[SENSOR_COUNT] = {NAN, NAN, NAN};
  bool levelOk[SENSOR_COUNT] = {false, false, false};
  bool heaterOn = false;
  bool oxygenOn = false;
  bool pumpAOn = false;
  bool pumpBOn = false;
  unsigned long tMs = 0;
};

DemoState state;
String logs[30];
size_t logCount = 0;
size_t logWrite = 0;

const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Kombucha test.ino Web Preview</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 0; background: #0f172a; color: #e2e8f0; }
    header { background: #1e293b; padding: 12px 16px; }
    .muted { color: #94a3b8; font-size: 12px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit,minmax(280px,1fr)); gap: 12px; padding: 12px; }
    .card { background: #1e293b; border-radius: 8px; padding: 12px; }
    .row { display: flex; justify-content: space-between; margin: 8px 0; }
    button { padding: 8px 10px; border-radius: 6px; border: 1px solid #334155; background: #0b1220; color: #e2e8f0; cursor: pointer; }
    .log { max-height: 220px; overflow-y: auto; font-family: monospace; font-size: 12px; background: #0b1220; padding: 8px; border-radius: 6px; }
  </style>
</head>
<body>
<header>
  <h2>Kombucha test.ino Dashboard</h2>
  <div class="muted">Quick UI preview + live ultrasonic reads + manual actuator toggles</div>
</header>
<section class="grid">
  <div class="card">
    <h3>Ultrasonic (TRIG 14)</h3>
    <div class="row"><span>ECHO 25</span><span id="l0">--</span></div>
    <div class="row"><span>ECHO 26</span><span id="l1">--</span></div>
    <div class="row"><span>ECHO 27</span><span id="l2">--</span></div>
  </div>
  <div class="card">
    <h3>Actuators</h3>
    <div class="row"><span>Heater</span><button id="heaterBtn" onclick="toggle('heaterOn')">Toggle</button></div>
    <div class="row"><span>Oxygen</span><button id="oxygenBtn" onclick="toggle('oxygenOn')">Toggle</button></div>
    <div class="row"><span>Pump A</span><button id="pumpABtn" onclick="toggle('pumpAOn')">Toggle</button></div>
    <div class="row"><span>Pump B</span><button id="pumpBBtn" onclick="toggle('pumpBOn')">Toggle</button></div>
  </div>
  <div class="card" style="grid-column: 1 / -1;">
    <h3>Status Logs</h3>
    <div id="logs" class="log"></div>
  </div>
</section>
<script>
let appState = null;
async function postControl(payload){
  await fetch('/api/control', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:new URLSearchParams(payload)});
}
async function toggle(key){
  if(!appState) return;
  await postControl({[key]: appState[key] ? 0 : 1});
  await refresh();
}
function fmt(ok, v){ return ok ? `${v.toFixed(1)} cm` : 'timeout'; }
async function refresh(){
  const [s, l] = await Promise.all([fetch('/api/state'), fetch('/api/logs')]);
  appState = await s.json();
  const logs = await l.json();
  document.getElementById('l0').textContent = fmt(appState.levelOk0, appState.level0);
  document.getElementById('l1').textContent = fmt(appState.levelOk1, appState.level1);
  document.getElementById('l2').textContent = fmt(appState.levelOk2, appState.level2);
  document.getElementById('heaterBtn').textContent = appState.heaterOn ? 'Turn OFF' : 'Turn ON';
  document.getElementById('oxygenBtn').textContent = appState.oxygenOn ? 'Turn OFF' : 'Turn ON';
  document.getElementById('pumpABtn').textContent = appState.pumpAOn ? 'Turn OFF' : 'Turn ON';
  document.getElementById('pumpBBtn').textContent = appState.pumpBOn ? 'Turn OFF' : 'Turn ON';
  document.getElementById('logs').innerHTML = logs.map(x=>`<div>${x}</div>`).join('');
}
refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
)HTML";

void addLog(const String& msg) {
  String line = String(millis()) + " ms: " + msg;
  logs[logWrite] = line;
  logWrite = (logWrite + 1) % 30;
  if (logCount < 30) logCount++;
  Serial.println(line);
}

void setOutput(int pin, bool on) {
  digitalWrite(pin, on ? HIGH : LOW);
}

void handleRoot() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

void handleApiState() {
  String json = "{";
  json += "\"t\":" + String(state.tMs) + ",";
  json += "\"level0\":" + String(state.levelCm[0], 2) + ",";
  json += "\"level1\":" + String(state.levelCm[1], 2) + ",";
  json += "\"level2\":" + String(state.levelCm[2], 2) + ",";
  json += "\"levelOk0\":" + String(state.levelOk[0] ? "true" : "false") + ",";
  json += "\"levelOk1\":" + String(state.levelOk[1] ? "true" : "false") + ",";
  json += "\"levelOk2\":" + String(state.levelOk[2] ? "true" : "false") + ",";
  json += "\"heaterOn\":" + String(state.heaterOn ? "true" : "false") + ",";
  json += "\"oxygenOn\":" + String(state.oxygenOn ? "true" : "false") + ",";
  json += "\"pumpAOn\":" + String(state.pumpAOn ? "true" : "false") + ",";
  json += "\"pumpBOn\":" + String(state.pumpBOn ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleApiLogs() {
  String json = "[";
  size_t start = (logWrite + 30 - logCount) % 30;
  for (size_t i = 0; i < logCount; i++) {
    size_t idx = (start + i) % 30;
    if (i > 0) json += ",";
    String line = logs[idx];
    line.replace("\\", "\\\\");
    line.replace("\"", "\\\"");
    json += "\"" + line + "\"";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleApiControl() {
  if (server.hasArg("heaterOn")) {
    state.heaterOn = (server.arg("heaterOn") == "1");
    setOutput(HEATER_PIN, state.heaterOn);
    addLog(String("heater turned ") + (state.heaterOn ? "on" : "off"));
  }
  if (server.hasArg("oxygenOn")) {
    state.oxygenOn = (server.arg("oxygenOn") == "1");
    setOutput(OXYGEN_PIN, state.oxygenOn);
    addLog(String("oxygen pump turned ") + (state.oxygenOn ? "on" : "off"));
  }
  if (server.hasArg("pumpAOn")) {
    state.pumpAOn = (server.arg("pumpAOn") == "1");
    setOutput(PUMP_A_PIN, state.pumpAOn);
    addLog(String("peristaltic pump A turned ") + (state.pumpAOn ? "on" : "off"));
  }
  if (server.hasArg("pumpBOn")) {
    state.pumpBOn = (server.arg("pumpBOn") == "1");
    setOutput(PUMP_B_PIN, state.pumpBOn);
    addLog(String("peristaltic pump B turned ") + (state.pumpBOn ? "on" : "off"));
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void setupWeb() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleApiState);
  server.on("/api/logs", HTTP_GET, handleApiLogs);
  server.on("/api/control", HTTP_POST, handleApiControl);
  server.begin();
}

void connectWiFi() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_STA);

  if (WIFI_USE_WPA2_ENTERPRISE) {
#if HAS_EAP_CLIENT_API
    addLog("Wi-Fi mode: WPA2-Enterprise (EAP client API)");
    esp_eap_client_set_identity((const uint8_t*)WIFI_IDENTITY, strlen(WIFI_IDENTITY));
    esp_eap_client_set_username((const uint8_t*)WIFI_USERNAME, strlen(WIFI_USERNAME));
    esp_eap_client_set_password((const uint8_t*)WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    esp_wifi_sta_enterprise_enable();
    WiFi.begin(WIFI_SSID);
#elif HAS_WPA2_LEGACY_API
    addLog("Wi-Fi mode: WPA2-Enterprise");
    esp_wifi_sta_wpa2_ent_set_identity((const uint8_t*)WIFI_IDENTITY, strlen(WIFI_IDENTITY));
    esp_wifi_sta_wpa2_ent_set_username((const uint8_t*)WIFI_USERNAME, strlen(WIFI_USERNAME));
    esp_wifi_sta_wpa2_ent_set_password((const uint8_t*)WIFI_PASSWORD, strlen(WIFI_PASSWORD));

    esp_wpa2_config_t config = WPA2_CONFIG_INIT_DEFAULT();
    esp_wifi_sta_wpa2_ent_enable(&config);
    WiFi.begin(WIFI_SSID);
#else
    addLog("alert conditions: WPA2-Enterprise headers unavailable in this ESP32 core");
    addLog("falling back to normal Wi-Fi SSID/password mode");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif
  } else {
    addLog("Wi-Fi mode: WPA2/WPA3 Personal (SSID + password)");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  Serial.print("Connecting WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000UL) {
    Serial.print(".");
    delay(250);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Dashboard URL: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection timeout");
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  pinMode(HEATER_PIN, OUTPUT);
  pinMode(OXYGEN_PIN, OUTPUT);
  pinMode(PUMP_A_PIN, OUTPUT);
  pinMode(PUMP_B_PIN, OUTPUT);
  setOutput(HEATER_PIN, false);
  setOutput(OXYGEN_PIN, false);
  setOutput(PUMP_A_PIN, false);
  setOutput(PUMP_B_PIN, false);

  us.begin();
  us.setTiming(20000UL, 60UL);

  connectWiFi();
  setupWeb();

  addLog("test.ino web dashboard ready");
  addLog("shared TRIG 14; ECHO 25/26/27");
}

void loop() {
  server.handleClient();

  state.tMs = millis();
  for (int i = 0; i < us.count(); i++) {
    USReading r = us.readOne(i);
    state.levelOk[i] = r.ok;
    state.levelCm[i] = r.ok ? r.cm : NAN;
  }

  delay(250);
}
