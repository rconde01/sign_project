#pragma once

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>

//
// ---------- USER CONFIG ----------
//
const char*    WIFI_SSID      = "IronReverseSoulSteeler";
const char*    WIFI_PASSWORD  = "dekuposh84";
const uint16_t TCP_PORT       = 50001;
const char*    SIGN_HOSTNAME  = "sophia-sign"; // sophia-sign.local

// Heartbeat period while connected (ms)
const uint32_t HEARTBEAT_MS   = 4000;

const uint32_t DEBOUNCE_MS = 30;

/*** Shared bits ***/
String deviceId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);

  char b[16];
  snprintf(b,sizeof(b),"%02X%02X%02X",mac[3],mac[4],mac[5]);

  return String(b);
}

void logLine(const char* t, const String& s){
  Serial.printf("[%s] %s\n", t, s.c_str());
}

void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);               // low-latency & stable sockets
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void startMDNS(const char* host) {
  if (!MDNS.begin(host)) {
    logLine("MDNS","begin failed");
    return;
  }

  logLine("MDNS", String("up as ") + host + ".local");
}

bool readLine(WiFiClient& c, String& out){
  while (c.available()) {
    char ch = (char)c.read();

    if (ch == '\n')
      return true;

    if (ch != '\r')
      out += ch;
  }

  return false;
}

bool sendLine(WiFiClient& c, const String& s){
  if (!c.connected())
    return false;

  size_t n = c.print(s);
  n += c.print("\n");
  return n == (s.length() + 1);
}

