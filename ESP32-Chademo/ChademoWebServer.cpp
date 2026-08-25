#include "ChademoWebServer.h"
#include <WiFi.h>
#include <SPIFFS.h>
#include <EEPROM.h>
#include <ElegantOTA.h>


AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

ChademoWebServer::ChademoWebServer(EESettings& s) : settings{ s } {
}

AsyncWebSocket& ChademoWebServer::getWebSocket() {
    return ws;
}
void ChademoWebServer::execute() {
    ws.cleanupClients();
    //ElegantOTA reboots the board here once an upload finished. Holding that off while the
    //contactors are closed keeps a reboot from dropping the CHAdeMO session under load.
    if (!chargeInProgress()) {
        ElegantOTA.loop();
    }
}

void ChademoWebServer::broadcast(const char * message) {
    ws.printfAll(message);
}

void ChademoWebServer::setup()
{
    // ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.on("/wifi", [&] (AsyncWebServerRequest *request) {
        bool updated = true;
        if(request->hasParam("apSSID", true) && request->hasParam("apPW", true)) 
        {
            WiFi.softAP(request->arg("apSSID").c_str(), request->arg("apPW").c_str());
        }
        else if(request->hasParam("staSSID", true) && request->hasParam("staPW", true)) 
        {
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(request->arg("staSSID").c_str(), request->arg("staPW").c_str());
        }
        else
        {
            File file = SPIFFS.open("/wifi.html", "r");
            String html = file.readString();
            file.close();
            html.replace("%staSSID%", WiFi.SSID());
            html.replace("%apSSID%", WiFi.softAPSSID());
            html.replace("%staIP%", WiFi.localIP().toString());
            request->send(200, "text/html", html);
            updated = false;
        }

        if (updated)
        {
            request->send(SPIFFS, "/wifi-updated.html");
        } 
    });

    server.on("/settings", HTTP_GET, [&] (AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument json(2048);
        toJson(settings, json);
        serializeJson(json, *response);
        request->send(response);
    });

    server.on(
         "/settings",
         HTTP_POST,
         [](AsyncWebServerRequest * request){},
         NULL,
         [&](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
         Serial.println("settings POST");
         const size_t JSON_DOC_SIZE = 1024U;
         DynamicJsonDocument jsonDoc(JSON_DOC_SIZE);
        
         if (DeserializationError::Ok == deserializeJson(jsonDoc, (const char*)data))
         {
             JsonObject obj = jsonDoc.as<JsonObject>();
             fromJson(settings, obj);
             EEPROM.put(0, settings);
             EEPROM.commit();
             updateTargetAV(); //without this the new limits only take effect after a reboot
             request->send(200, "application/json", "success");

         } else {
             request->send(200, "application/json", "DeserializationError");
         }
     });

     server.on(
         "/start1",
         HTTP_POST,
         [](AsyncWebServerRequest * request){},
         NULL,
         [&](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {

         overrideStart1 = !overrideStart1;
         request->send(200, "application/json", "success");

     });

     server.on(
         "/start2",
         HTTP_POST,
         [](AsyncWebServerRequest * request){},
         NULL,
         [&](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {

         overrideStart2 = !overrideStart2;
         request->send(200, "application/json", "success");

     });

    server.on("/diagstate", HTTP_GET, [&] (AsyncWebServerRequest *request) {
        String json = "{\"relays\":[";
        for (int i = 0; i < 4; i++) {
            json += diagRelayState(i) ? "true" : "false";
            if (i < 3) json += ",";
        }
        //The optocouplers pull the input low while the charge sequence line carries voltage.
        json += "],\"in1\":" + String(!digitalRead(CHADEMO_IN1) ? "true" : "false");
        json += ",\"in2\":" + String(!digitalRead(CHADEMO_IN2) ? "true" : "false");
        json += ",\"frames\":" + String(canFrames);
        json += ",\"lastId\":" + String(canLastId);
        json += ",\"age\":" + String(canFrames ? millis() - canLastMillis : 0);
        json += ",\"active\":" + String(chargeInProgress() ? "true" : "false");
        json += "}";
        request->send(200, "application/json", json);
    });

    server.on("/diagset", HTTP_GET, [&] (AsyncWebServerRequest *request) {
        if (!request->hasParam("relay") || !request->hasParam("on")) {
            request->send(400, "application/json", "{\"error\":\"missing parameter\"}");
            return;
        }
        int index = request->getParam("relay")->value().toInt();
        bool on = request->getParam("on")->value().toInt() != 0;
        if (!diagSetRelay(index, on)) {
            request->send(409, "application/json", "{\"error\":\"charge sequence active\"}");
            return;
        }
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  

  // Start server
  Serial.println("Starting Web Server");
  ElegantOTA.begin(&server);
  server.begin();
}

void ChademoWebServer::toJson(EESettings& settings, DynamicJsonDocument &root) {
    root["useBms"] = settings.useBms;
    root["ampHours"] = settings.ampHours;
    root["packSizeKWH"] = settings.packSizeKWH;
    root["maxChargeVoltage"] = settings.maxChargeVoltage;
    root["targetChargeVoltage"] = settings.targetChargeVoltage;
    root["maxChargeAmperage"] = settings.maxChargeAmperage;
    root["minChargeAmperage"] = settings.minChargeAmperage;
    root["capacity"] = settings.capacity;
    root["debuggingLevel"] = settings.debuggingLevel;
    root["currentMissmatch"] = settings.currentMissmatch;

}

void ChademoWebServer::fromJson(EESettings& settings, JsonObject &doc) {

    settings.useBms = doc["useBms"];
    settings.ampHours = doc["ampHours"];
    settings.packSizeKWH = doc["packSizeKWH"];
    settings.maxChargeVoltage = doc["maxChargeVoltage"];
    settings.targetChargeVoltage = doc["targetChargeVoltage"];
    settings.maxChargeAmperage = doc["maxChargeAmperage"];
    settings.minChargeAmperage = doc["minChargeAmperage"];
    settings.capacity = doc["capacity"];
    settings.debuggingLevel = doc["debuggingLevel"];
    settings.currentMissmatch = doc["currentMissmatch"];

}
