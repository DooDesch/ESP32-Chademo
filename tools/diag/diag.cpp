// Bench diagnostics for the CHAdeMO box: switch each relay by hand, watch the optocoupler inputs
// and see whether anything arrives on the CAN bus.
//
// Deliberately a separate binary from the CHAdeMO firmware. Manual relay control has no place in the
// build that runs a charge session, where a stray click would close the contactors mid-sequence.
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ACAN_ESP32.h>

#define AP_SSID "ESP32-CHADEMO"
#define AP_PASSWORD "ChadMeO1"

const int RELAY_PINS[4] = {32, 33, 25, 26};
const char *RELAY_NAMES[4] = {"RY1 charge permission", "RY2 contactor coils", "RY3 spare", "RY4 spare"};
const int IN1_PIN = 34;
const int IN2_PIN = 35;
const int LED_PIN = 2;

AsyncWebServer server(80);
bool relayState[4] = {false, false, false, false};
uint32_t canFrames = 0;
uint32_t canLastId = 0;
uint32_t canLastMillis = 0;
bool canReady = false;

static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Chademo Diagnose</title><style>
body{font-family:sans-serif;margin:0;background:#fff;color:#111}
header{background:#7a1010;color:#fff;padding:10px 14px}
header b{display:block;font-size:1.1em}
header span{font-size:.85em}
h2{margin:18px 14px 6px;font-size:.8em;text-transform:uppercase;letter-spacing:.08em;color:#666;font-weight:400}
.row{display:flex;align-items:center;gap:10px;padding:8px 14px}
.row .name{flex:1}
button{min-width:92px;min-height:46px;font-size:1em;border-radius:8px;border:1px solid #bbb;background:#eee}
button.on{background:#1f7a1f;color:#fff;border-color:#1f7a1f}
.cards{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;padding:0 14px}
.card{background:#f0f0f0;border-radius:10px;padding:10px 12px}
.card .l{display:block;font-size:.75em;color:#666}
.card .v{display:block;font-size:1.4em;font-weight:700}
</style></head><body>
<header><b>Diagnose</b><span>Relais schalten sofort. Nur ohne Hochspannung benutzen.</span></header>
<h2>Relais</h2><div id="relays"></div>
<h2>Eingaenge</h2><div class="cards">
<div class="card"><span class="l">IN 1 (d1, GPIO34)</span><span class="v" id="in1">-</span></div>
<div class="card"><span class="l">IN 2 (d2, GPIO35)</span><span class="v" id="in2">-</span></div></div>
<h2>CAN</h2><div class="cards">
<div class="card"><span class="l">Bus</span><span class="v" id="canok">-</span></div>
<div class="card"><span class="l">Frames</span><span class="v" id="frames">0</span></div>
<div class="card"><span class="l">Letzte ID</span><span class="v" id="lastid">-</span></div>
<div class="card"><span class="l">Letzter Empfang</span><span class="v" id="age">-</span></div></div>
<script>
var names=[];
function draw(s){
 if(!names.length){names=s.names;var h='';
  for(var i=0;i<4;i++){h+='<div class="row"><span class="name">'+names[i]+'</span>'+
   '<button id="b'+i+'" onclick="tog('+i+')">-</button></div>';}
  document.getElementById('relays').innerHTML=h;}
 for(var i=0;i<4;i++){var b=document.getElementById('b'+i);
  b.textContent=s.relays[i]?'AN':'AUS';b.className=s.relays[i]?'on':'';}
 document.getElementById('in1').textContent=s.in1?'aktiv':'ruhig';
 document.getElementById('in2').textContent=s.in2?'aktiv':'ruhig';
 document.getElementById('canok').textContent=s.canReady?'bereit':'Fehler';
 document.getElementById('frames').textContent=s.frames;
 document.getElementById('lastid').textContent=s.frames?('0x'+s.lastId.toString(16)):'-';
 document.getElementById('age').textContent=s.frames?(s.age+' ms'):'-';
}
function poll(){fetch('/api').then(r=>r.json()).then(draw).catch(()=>{});}
function tog(i){fetch('/set?relay='+i+'&on='+(document.getElementById('b'+i).className?0:1)).then(poll);}
setInterval(poll,500);poll();
</script></body></html>)HTML";

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("DIAG BUILD: manual relay control, no CHAdeMO sequence");

  for (int i = 0; i < 4; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);
  }
  pinMode(IN1_PIN, INPUT);
  pinMode(IN2_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  ACAN_ESP32_Settings canSettings(500000);
  canSettings.mRxPin = GPIO_NUM_16;
  canSettings.mTxPin = GPIO_NUM_17;
  canReady = ACAN_ESP32::can.begin(canSettings) == 0;
  Serial.println(canReady ? "CAN ready" : "CAN configuration error");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", PAGE);
  });

  server.on("/api", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"names\":[";
    for (int i = 0; i < 4; i++) {
      json += "\"" + String(RELAY_NAMES[i]) + "\"";
      if (i < 3) json += ",";
    }
    json += "],\"relays\":[";
    for (int i = 0; i < 4; i++) {
      json += relayState[i] ? "true" : "false";
      if (i < 3) json += ",";
    }
    //The optocouplers pull the pin low when the charge sequence line carries voltage.
    json += "],\"in1\":" + String(!digitalRead(IN1_PIN) ? "true" : "false");
    json += ",\"in2\":" + String(!digitalRead(IN2_PIN) ? "true" : "false");
    json += ",\"canReady\":" + String(canReady ? "true" : "false");
    json += ",\"frames\":" + String(canFrames);
    json += ",\"lastId\":" + String(canLastId);
    json += ",\"age\":" + String(canFrames ? millis() - canLastMillis : 0);
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("relay") && request->hasParam("on")) {
      int i = request->getParam("relay")->value().toInt();
      bool on = request->getParam("on")->value().toInt() != 0;
      if (i >= 0 && i < 4) {
        relayState[i] = on;
        digitalWrite(RELAY_PINS[i], on ? HIGH : LOW);
        Serial.printf("%s -> %s\n", RELAY_NAMES[i], on ? "ON" : "OFF");
      }
    }
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.begin();
}

void loop() {
  CANMessage frame;
  while (ACAN_ESP32::can.receive(frame)) {
    canFrames++;
    canLastId = frame.id;
    canLastMillis = millis();
  }
  digitalWrite(LED_PIN, (millis() / 500) % 2);
}
