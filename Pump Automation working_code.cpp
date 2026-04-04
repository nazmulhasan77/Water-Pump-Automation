#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <Wire.h>
#include <RTClib.h>

// ================= WIFI =================
const char* ssid = "Pump_Controller";
const char* password = "12345678";

// ================= PINS =================
#define RELAY D5
#define SWITCH D6
#define LED D7

// ================= OBJECTS =================
ESP8266WebServer server(80);
RTC_DS3231 rtc;

// ================= SCHEDULE =================
#define MAX_SCHEDULE 5
struct Schedule {
  int hour;
  int minute;
  int duration;  // in minutes
};
Schedule schedules[MAX_SCHEDULE];
int scheduleCount = 0;

// ================= RUNTIME =================
bool pumpRunning = false;
unsigned long pumpStart = 0;
int currentDuration = 0;
int lastMinute = -1;
bool wifiOn = true;
unsigned long wifiStart = 0;

// ================= RELAY CONTROL =================
void setPump(bool state) {
  digitalWrite(RELAY, state ? HIGH : LOW);
  digitalWrite(LED, state ? HIGH : LOW);
  pumpRunning = state;
  if (!state) {
    pumpStart = 0;
    currentDuration = 0;
  }
}

// ================= EEPROM =================
void saveSchedules() {
  EEPROM.put(0, scheduleCount);
  EEPROM.put(10, schedules);
  EEPROM.commit();
}

void loadSchedules() {
  EEPROM.get(0, scheduleCount);
  EEPROM.get(10, schedules);
  if (scheduleCount < 0 || scheduleCount > MAX_SCHEDULE) {
    scheduleCount = 0;
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  loadSchedules();

  pinMode(RELAY, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(SWITCH, INPUT_PULLUP);

  setPump(false);

  Wire.begin();
if (!rtc.begin()) {
    Serial.println("Couldn't find RTC! Continuing without time...");
    // Optionally blink LED fast to indicate error
    for(int i=0; i<40; i++) {
        digitalWrite(LED, !digitalRead(LED));
        delay(1000);
    }
    // Do NOT halt the whole device
}

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting default time!");
    // Set to a reasonable default (you can change this)
    rtc.adjust(DateTime(2026, 4, 4, 12, 0, 0));
  }

  WiFi.softAP(ssid, password);
  wifiStart = millis();

  // Web routes
  server.on("/", handleRoot);
  server.on("/add", addSchedule);
  server.on("/del", deleteSchedule);
  server.on("/list", getSchedules);
  server.on("/toggle", togglePump);
  server.on("/status", getStatus);
  server.on("/setTime", setRTCTime);
  server.on("/time", getTime);

  server.begin();
  Serial.println("Smart Pump Controller Started!");
  Serial.print("Connect to WiFi: ");
  Serial.println(ssid);
}

// ================= LOOP =================
void loop() {
  server.handleClient();

  DateTime now = rtc.now();
  checkSchedule(now.hour(), now.minute());
  checkPumpTimer();
  checkManualSwitch();

  // Auto turn OFF WiFi after 5 minutes for power saving
  if (wifiOn && millis() - wifiStart > 300000UL) {
    WiFi.softAPdisconnect(true);
    wifiOn = false;
    Serial.println("WiFi turned OFF for power saving");
  }
}

// ================= MANUAL SWITCH =================
void checkManualSwitch() {
  static bool lastState = HIGH;
  bool current = digitalRead(SWITCH);

  if (lastState == HIGH && current == LOW) {  // Button pressed
    setPump(!pumpRunning);
    delay(300);  // simple debounce
  }
  lastState = current;
}

// ================= SCHEDULE CHECK =================
void checkSchedule(int h, int m) {
  if (m == lastMinute) return;
  lastMinute = m;

  for (int i = 0; i < scheduleCount; i++) {
    if (h == schedules[i].hour && m == schedules[i].minute) {
      setPump(true);
      pumpStart = millis();
      currentDuration = schedules[i].duration;
      Serial.printf("Schedule triggered: %02d:%02d for %d min\n", h, m, currentDuration);
    }
  }
}

// ================= PUMP TIMER =================
void checkPumpTimer() {
  if (pumpRunning && currentDuration > 0) {
    if (millis() - pumpStart >= (unsigned long)currentDuration * 60000UL) {
      setPump(false);
      Serial.println("Pump turned OFF by timer");
    }
  }
}

// ================= WEB API =================
void togglePump() {
  setPump(!pumpRunning);
  server.send(200, "text/plain", "OK");
}

void getStatus() {
  String json = "{\"pump\":" + String(pumpRunning ? 1 : 0) + "}";
  server.send(200, "application/json", json);
}

void setRTCTime() {
  if (server.hasArg("year") && server.hasArg("month") && server.hasArg("day") &&
      server.hasArg("h") && server.hasArg("m") && server.hasArg("s")) {

    int y = server.arg("year").toInt();
    int mo = server.arg("month").toInt();
    int d = server.arg("day").toInt();
    int h = server.arg("h").toInt();
    int mi = server.arg("m").toInt();
    int se = server.arg("s").toInt();

    rtc.adjust(DateTime(y, mo, d, h, mi, se));
    server.send(200, "text/plain", "Time Set Successfully");
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void getTime() {
  DateTime now = rtc.now();
  String json = "{\"h\":" + String(now.hour()) +
                ",\"m\":" + String(now.minute()) +
                ",\"s\":" + String(now.second()) +
                ",\"year\":" + String(now.year()) +
                ",\"month\":" + String(now.month()) +
                ",\"day\":" + String(now.day()) + "}";
  server.send(200, "application/json", json);
}

// ================= SCHEDULE API =================
void addSchedule() {
  if (scheduleCount >= MAX_SCHEDULE) {
    server.send(400, "text/plain", "Schedule limit reached");
    return;
  }

  int h = server.arg("h").toInt();
  int m = server.arg("m").toInt();
  int d = server.arg("d").toInt();

  if (h < 0 || h > 23 || m < 0 || m > 59 || d < 1 || d > 1440) {
    server.send(400, "text/plain", "Invalid values");
    return;
  }

  schedules[scheduleCount++] = {h, m, d};
  saveSchedules();
  server.send(200, "text/plain", "Schedule Added");
}

void deleteSchedule() {
  int id = server.arg("id").toInt();
  if (id >= 0 && id < scheduleCount) {
    for (int i = id; i < scheduleCount - 1; i++) {
      schedules[i] = schedules[i + 1];
    }
    scheduleCount--;
    saveSchedules();
    server.send(200, "text/plain", "Schedule Deleted");
  } else {
    server.send(400, "text/plain", "Invalid ID");
  }
}

void getSchedules() {
  String json = "[";
  for (int i = 0; i < scheduleCount; i++) {
    json += "{\"h\":" + String(schedules[i].hour) +
            ",\"m\":" + String(schedules[i].minute) +
            ",\"d\":" + String(schedules[i].duration) + "}";
    if (i < scheduleCount - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

// ================= WEB PAGE =================
void handleRoot() {
  String html = R"====(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { font-family: Arial, sans-serif; background:#0f172a; color:white; text-align:center; margin:0; padding:10px;}
  .card { background:#1e293b; padding:20px; margin:15px auto; max-width:600px; border-radius:18px;}
  button { padding:12px 20px; margin:8px; border:none; border-radius:10px; font-size:16px; cursor:pointer;}
  .on { background:#22c55e; }
  .off { background:#ef4444; }
  input { padding:10px; margin:5px; width:60px; text-align:center;}
</style>
</head>
<body>
<h2>Smart Pump Controller</h2>

<div class="card">
  <h3>Current Time</h3>
  <h2 id="clock">--:--:--</h2>
</div>

<div class="card">
  <h3>Pump Status</h3>
  <h2 id="status">OFF</h2>
  <button onclick="toggle()" id="toggleBtn">Toggle Pump</button>
</div>

<div class="card">
  <h3>Set RTC Time</h3>
  <input id="year" type="number" placeholder="Year" value="2026">
  <input id="month" type="number" placeholder="Month" value="4">
  <input id="day" type="number" placeholder="Day" value="4"><br>
  <input id="h" type="number" placeholder="Hour" value="12">
  <input id="m" type="number" placeholder="Min" value="0">
  <input id="s" type="number" placeholder="Sec" value="0">
  <button onclick="setTime()">Set Time</button>
</div>

<div class="card">
  <h3>Add Schedule</h3>
  <input id="sh" type="number" placeholder="Hour (0-23)">
  <input id="sm" type="number" placeholder="Minute (0-59)">
  <input id="sd" type="number" placeholder="Duration (min)">
  <button onclick="addSchedule()">Add</button>
</div>

<div class="card">
  <h3>Saved Schedules</h3>
  <div id="list"></div>
</div>

<script>
function toggle() {
  fetch('/toggle').then(() => updateStatus());
}

function setTime() {
  const y = document.getElementById('year').value;
  const mo = document.getElementById('month').value;
  const d = document.getElementById('day').value;
  const h = document.getElementById('h').value;
  const m = document.getElementById('m').value;
  const s = document.getElementById('s').value;

  fetch(`/setTime?year=${y}&month=${mo}&day=${d}&h=${h}&m=${m}&s=${s}`)
    .then(r => r.text())
    .then(txt => alert(txt));
}

function addSchedule() {
  const h = document.getElementById('sh').value;
  const m = document.getElementById('sm').value;
  const d = document.getElementById('sd').value;

  fetch(`/add?h=${h}&m=${m}&d=${d}`)
    .then(() => loadSchedules());
}

function del(i) {
  fetch(`/del?id=${i}`).then(() => loadSchedules());
}

function loadSchedules() {
  fetch('/list').then(r => r.json()).then(data => {
    let html = "";
    data.forEach((s, i) => {
      html += `<p>${s.h.toString().padStart(2,'0')}:${s.m.toString().padStart(2,'0')} → ${s.d} min 
               <button onclick="del(${i})">Delete</button></p>`;
    });
    document.getElementById('list').innerHTML = html || "<p>No schedules added yet.</p>";
  });
}

function updateClock() {
  fetch('/time').then(r => r.json()).then(t => {
    const timeStr = `${t.h.toString().padStart(2,'0')}:${t.m.toString().padStart(2,'0')}:${t.s.toString().padStart(2,'0')}`;
    document.getElementById('clock').innerHTML = timeStr;
  });
}

function updateStatus() {
  fetch('/status').then(r => r.json()).then(s => {
    const st = document.getElementById('status');
    st.innerHTML = s.pump ? "ON" : "OFF";
    st.style.color = s.pump ? "#22c55e" : "#ef4444";
  });
}

setInterval(updateClock, 1000);
setInterval(updateStatus, 1000);
setInterval(loadSchedules, 3000);

updateClock();
updateStatus();
loadSchedules();
</script>
</body>
</html>
)====";

  server.send(200, "text/html", html);
}