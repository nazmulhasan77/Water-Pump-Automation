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

// ================= OBJECTS =================
ESP8266WebServer server(80);
RTC_DS3231 rtc;

// ================= SCHEDULE =================
#define MAX_SCHEDULE 5

struct Schedule {
  int hour;
  int minute;
  int duration;
};

Schedule schedules[MAX_SCHEDULE];
int scheduleCount = 0;

// ================= RUNTIME =================
bool pumpRunning = false;
unsigned long pumpStart = 0;
int currentDuration = 0;
int lastMinute = -1;

// ================= RELAY =================
void setPump(bool state) {
  digitalWrite(RELAY, state ? LOW : HIGH);
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
  setPump(false);

  Wire.begin();
  rtc.begin();

  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // first time only

  WiFi.softAP(ssid, password);

  server.on("/", handleRoot);
  server.on("/add", addSchedule);
  server.on("/del", deleteSchedule);
  server.on("/list", getSchedules);
  server.on("/toggle", togglePump);
  server.on("/setTime", setRTCTime);
  server.on("/time", getTime);

  server.begin();
}

// ================= LOOP =================
void loop() {
  server.handleClient();

  DateTime now = rtc.now();

  checkSchedule(now.hour(), now.minute());
  checkPumpTimer();
}

// ================= SCHEDULE CHECK =================
void checkSchedule(int h, int m) {
  for (int i = 0; i < scheduleCount; i++) {

    if (h == schedules[i].hour && m == schedules[i].minute) {

      if (lastMinute != m) {

        pumpRunning = true;
        pumpStart = millis();
        currentDuration = schedules[i].duration;

        setPump(true);

        lastMinute = m;
      }
    }
  }

  if (m != lastMinute) lastMinute = -1;
}

// ================= TIMER OFF =================
void checkPumpTimer() {
  if (pumpRunning) {
    if (millis() - pumpStart >= currentDuration * 60000UL) {

      pumpRunning = false;
      setPump(false);
    }
  }
}

// ================= MANUAL TOGGLE =================
void togglePump() {
  pumpRunning = !pumpRunning;
  setPump(pumpRunning);

  server.send(200, "text/plain", "OK");
}

// ================= RTC SET =================
void setRTCTime() {
  if (server.hasArg("h") && server.hasArg("m") && server.hasArg("s")) {

    int h = server.arg("h").toInt();
    int m = server.arg("m").toInt();
    int s = server.arg("s").toInt();

    rtc.adjust(DateTime(2024, 1, 1, h, m, s));

    server.send(200, "text/plain", "Time Set");
  }
}

// ================= GET TIME =================
void getTime() {
  DateTime now = rtc.now();

  String json = "{";
  json += "\"h\":" + String(now.hour()) + ",";
  json += "\"m\":" + String(now.minute()) + ",";
  json += "\"s\":" + String(now.second());
  json += "}";

  server.send(200, "application/json", json);
}

// ================= WEB UI =================
void handleRoot() {

String html = R"====(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">

<style>
body { font-family: Arial; background:#0f172a; color:white; text-align:center;}
.card { background:#1e293b; padding:20px; margin:12px; border-radius:18px;}
button { padding:10px; margin:5px; border:none; border-radius:10px; cursor:pointer;}
.on { background:#22c55e; }
.off { background:#ef4444; }
.blue { background:#3b82f6; }
</style>

</head>

<body>

<h2>💧 Smart Pump Controller</h2>

<div class="card">
<h3>⏰ Live Clock</h3>
<h2 id="clock">--:--:--</h2>
</div>

<div class="card">
<h3>🔘 Manual Control</h3>
<button class="blue" onclick="toggle()">Toggle Pump</button>
</div>

<div class="card">
<h3>🕒 Set RTC Time</h3>
<input id="h" type="number" placeholder="Hour">
<input id="m" type="number" placeholder="Min">
<input id="s" type="number" placeholder="Sec">
<br><br>
<button onclick="setTime()">Set Time</button>
</div>

<div class="card">
<h3>📅 Add Schedule</h3>
<input id="sh" type="number" placeholder="Hour">
<input id="sm" type="number" placeholder="Min">
<input id="sd" type="number" placeholder="Duration">
<br><br>
<button onclick="add()">Add</button>
</div>

<div class="card">
<h3>📋 Schedule List</h3>
<div id="list"></div>
</div>

<script>

function toggle(){
  fetch('/toggle');
}

function setTime(){
  let h=document.getElementById("h").value;
  let m=document.getElementById("m").value;
  let s=document.getElementById("s").value;

  fetch(`/setTime?h=${h}&m=${m}&s=${s}`);
}

function add(){
  let h=document.getElementById("sh").value;
  let m=document.getElementById("sm").value;
  let d=document.getElementById("sd").value;

  fetch(`/add?h=${h}&m=${m}&d=${d}`)
  .then(()=>load());
}

function del(i){
  fetch(`/del?id=${i}`)
  .then(()=>load());
}

function load(){
  fetch('/list')
  .then(r=>r.json())
  .then(data=>{
    let html="";
    data.forEach((s,i)=>{
      html+=`<p>${formatTime(s.h,s.m)} → ${s.d} min 
      <button onclick="del(${i})">❌</button></p>`;
    });
    document.getElementById("list").innerHTML=html;
  });
}

function formatTime(h,m){
  let ampm = h>=12?"PM":"AM";
  let hh = h%12;
  if(hh==0) hh=12;
  return hh+":"+String(m).padStart(2,'0')+" "+ampm;
}

function clock(){
  fetch('/time')
  .then(r=>r.json())
  .then(t=>{
    let ampm = t.h>=12?"PM":"AM";
    let hh = t.h%12;
    if(hh==0) hh=12;

    document.getElementById("clock").innerHTML =
      hh+":"+String(t.m).padStart(2,'0')+":"+String(t.s).padStart(2,'0')+" "+ampm;
  });
}

setInterval(clock,1000);
setInterval(load,2000);

load();

</script>

</body>
</html>
)====";

  server.send(200, "text/html", html);
}

// ================= API =================
void addSchedule() {
  if (server.hasArg("h") && server.hasArg("m") && server.hasArg("d")) {

    if (scheduleCount < MAX_SCHEDULE) {

      schedules[scheduleCount].hour = server.arg("h").toInt();
      schedules[scheduleCount].minute = server.arg("m").toInt();
      schedules[scheduleCount].duration = server.arg("d").toInt();

      scheduleCount++;
      saveSchedules();

      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(400, "text/plain", "Error");
}

void deleteSchedule() {
  int id = server.arg("id").toInt();

  if (id >= 0 && id < scheduleCount) {

    for (int i = id; i < scheduleCount - 1; i++) {
      schedules[i] = schedules[i + 1];
    }

    scheduleCount--;
    saveSchedules();

    server.send(200, "text/plain", "Deleted");
    return;
  }

  server.send(400, "text/plain", "Error");
}

void getSchedules() {
  String json = "[";

  for (int i = 0; i < scheduleCount; i++) {
    json += "{";
    json += "\"h\":" + String(schedules[i].hour) + ",";
    json += "\"m\":" + String(schedules[i].minute) + ",";
    json += "\"d\":" + String(schedules[i].duration);
    json += "}";

    if (i < scheduleCount - 1) json += ",";
  }

  json += "]";

  server.send(200, "application/json", json);
}