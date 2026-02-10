#ifndef WEB_H
#define WEB_H

// ==========================================
// CSS & STYLING (GMpro87 UI)
// ==========================================
const char style[] PROGMEM = R"=====(
body { font-family: 'Segoe UI', sans-serif; background-color: #0d0d0d; color: #efefef; margin: 0; padding: 0; }
.header { background: linear-gradient(135deg, #b92b27 0%, #1565C0 100%); color: white; padding: 25px; text-align: center; box-shadow: 0 4px 15px rgba(0,0,0,0.7); border-bottom: 3px solid #ff4d4d; }
.header h1 { margin: 0; font-size: 28px; letter-spacing: 2px; }
.header p { margin: 5px 0 0; opacity: 0.9; font-size: 12px; text-transform: uppercase; }
.container { padding: 15px; max-width: 500px; margin: auto; }
.card { background: #1a1a1a; padding: 18px; margin-bottom: 15px; border-radius: 12px; border-left: 5px solid #b92b27; box-shadow: 0 10px 20px rgba(0,0,0,0.5); }
.card-blue { border-left-color: #1565C0; }
.card-title { font-weight: bold; margin-bottom: 15px; color: #ff4d4d; font-size: 16px; border-bottom: 1px solid #333; padding-bottom: 8px; display: flex; justify-content: space-between; }
.wifi-item { display: flex; justify-content: space-between; align-items: center; background: #222; padding: 10px; margin-bottom: 8px; border-radius: 8px; border: 1px solid #333; font-size: 14px; }
.btn { display: block; width: 100%; padding: 12px; margin: 8px 0; border: none; border-radius: 6px; font-weight: bold; cursor: pointer; text-align: center; text-decoration: none; font-size: 13px; transition: 0.3s; box-sizing: border-box; color: white; }
.btn-red { background: #b92b27; }
.btn-blue { background: #1565C0; }
.btn-dark { background: #333; color: #ccc; }
.btn-green { background: #28a745; }
input, select { width: 100%; padding: 12px; margin: 8px 0; border-radius: 6px; border: 1px solid #333; background: #2a2a2a; color: white; box-sizing: border-box; }
label { font-size: 11px; color: #aaa; text-transform: uppercase; }
)=====";

// ==========================================
// HEADER & FOOTER
// ==========================================
const char Header[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>
<style>{style}</style><title>GMpro87 PRO</title></head><body>
<div class='header'><h1>GMPRO87</h1><p>Advanced WiFi Audit Suite</p></div><div class='container'>
)=====";

const char Footer[] PROGMEM = R"=====(
<div style='text-align:center;color:#444;font-size:11px;margin-top:20px;'>
GMpro Project &copy; 2026 | No More Bacot</div></div></body></html>
)=====";

// ==========================================
// HALAMAN UTAMA (DASHBOARD)
// ==========================================
const char attack_html[] PROGMEM = R"=====(
<div class='card'>
    <div class='card-title'>WIFI SCANNER <span>Target: {target_ssid}</span></div>
    {scanned}
    <a href='/scan' class='btn btn-blue'>RESCAN NETWORKS</a>
</div>

<div class='card'>
    <div class='card-title'>ATTACK MODES</div>
    <a href='/start?mode=deauth' class='btn btn-red'>DEAUTH TARGET {deauth_s}</a>
    <a href='/mass_deauth' class='btn btn-red' style='background:#8b0000'>MASS DEAUTH ALL</a>
    <a href='/start?mode=mdk4' class='btn btn-blue'>MDK4 AGGRESSIVE</a>
</div>

<div class='card card-blue'>
    <div class='card-title'>BEACON & PROBE SPAM</div>
    <a href='/start?mode=clone' class='btn btn-dark'>CLONE TARGET BEACON</a>
    <a href='/start?mode=spam' class='btn btn-dark'>RANDOM BEACON SPAM</a>
    <a href='/start?mode=probe' class='btn btn-green'>PROBE REQUEST ATTACK</a>
</div>

<div class='card card-blue'>
    <div class='card-title'>PHISHING & SYSTEM</div>
    <a href='/start?mode=et' class='btn btn-blue'>START EVIL TWIN</a>
    <a href='/log' class='btn btn-green'>VIEW PASSWORD LOGS</a>
    <div style='display:flex; gap:10px;'>
        <a href='/setting' class='btn btn-dark' style='flex:1'>SETTINGS</a>
        <a href='/reboot' class='btn btn-dark' style='flex:1'>REBOOT</a>
    </div>
</div>
)=====";

// ==========================================
// HALAMAN SETTINGS
// ==========================================
const char setting_html[] PROGMEM = R"=====(
<div class='card'>
    <div class='card-title'>DEVICE CONFIGURATION</div>
    <form action='/networksetting' method='POST'>
        <label>SSID Alat</label><input type='text' name='ssid' value='{ssid}'>
        <label>Password Menu</label><input type='text' name='password' value='{password}'>
        <label>Channel</label><input type='number' name='channel' value='{chnl}' min='1' max='13'>
        <button type='submit' class='btn btn-green'>SAVE NETWORK</button>
    </form>
</div>

<div class='card card-blue'>
    <div class='card-title'>ATTACK SETTINGS</div>
    <form action='/attacksetting' method='POST'>
        <label>Deauth Speed (ms)</label><input type='number' name='deauth_speed' value='{speed}'>
        <label>Phishing Path</label>
        <select name='pishingpath'>
            <option value='/login.html'>Google Login</option>
            <option value='/wifi.html'>WiFi Password</option>
        </select>
        <button type='submit' class='btn btn-green'>SAVE ATTACK CONFIG</button>
    </form>
</div>
<a href='/' class='btn btn-dark'>BACK TO DASHBOARD</a>
)=====";

// Variabel Global (Akan diisi oleh .ino)
char ssid[32] = "GMpro";
char password[64] = "Sangkur87";
char rogueap[32] = "GMpro_Free_WiFi";
int chnl = 1;
char eviltwinpath[32] = "/login.html";
char pishingpath[32] = "/pish.html";
char loadingpath[32] = "/load.html";
int loading_enable = 0;
int total_spiffs, used_spiffs, free_spiffs;
int deauth_speed = 100;

#endif
