/* * PROJECT: GMpro87 Professional Penetration Tool
 * SSID: GMpro | PASS: Sangkur87
 */

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

// SDK Deauther Core
#include <A_Config.h>
#include <Attack.h>
#include <Scan.h>

// --- KONFIGURASI ---
// Variabel harus global agar bisa diakses oleh loop dan lambda
const char* apSSID = "GMpro";
const char* apPASS = "Sangkur87";
const int LED_PIN = 2;

AsyncWebServer server(80);
DNSServer dnsServer;
Attack attack;
Scan scanObj;

// Global State
bool massDeauth = false;
bool scanHidden = false;
int blinkInterval = 1000;
unsigned long lastHop = 0;
unsigned long prevBlink = 0;
int currentCh = 1;

// --- UI WEB (HTML & CSS) ---
const char INDEX_HTML[] PROGMEM = R"raw(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { background:#050505; color:#0f0; font-family:monospace; text-align:center; margin:0; }
        .header { background:#111; padding:20px; border-bottom:2px solid #0f0; box-shadow:0 0 15px #0f0; }
        .tabs { display:flex; background:#1a1a1a; position:sticky; top:0; }
        .tab { flex:1; padding:15px; cursor:pointer; border-bottom:2px solid #333; }
        .tab.active { background:#0f0; color:#000; font-weight:bold; }
        .content { display:none; padding:15px; }
        .content.active { display:block; }
        .btn { background:#111; border:1px solid #0f0; color:#0f0; padding:14px; width:100%; margin:8px 0; border-radius:5px; font-weight:bold; cursor:pointer; }
        .btn.on { background:#f00; border-color:#f00; color:#fff; box-shadow:0 0 15px #f00; }
        .wifi-row { display:grid; grid-template-columns: 2fr 1fr 1fr 1fr; background:#111; padding:10px; margin:5px 0; font-size:11px; border-left:3px solid #0f0; text-align:left;}
        .log-box { background:#000; border:1px solid #333; height:130px; overflow-y:scroll; padding:10px; text-align:left; font-size:11px; color:#0f0; margin-top:15px; }
    </style>
</head>
<body>
    <div class="header"><div>GMPRO87</div><small>ADMIN SECURE: ON CH 1</small></div>
    <div class="tabs">
        <div id="bt1" class="tab active" onclick="sh(1)">CONSOLE</div>
        <div id="bt2" class="tab" onclick="sh(2)">SETTING</div>
    </div>
    <div id="t1" class="content active">
        <button class="btn" onclick="sc()">
