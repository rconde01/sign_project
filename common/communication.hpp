#pragma once

// ===== Robust Remote/Sign Link for ESP32 (HUZZAH32) =====
// Set one of these:
// #define DEVICE_ROLE_REMOTE  // comment this for SIGN
// #define DEVICE_ROLE_SIGN

#include <WiFi.h>
#include <AsyncUDP.h>

//
// ---------- USER CONFIG ----------
//
const char* WIFI_SSID     = "IronReverseSoulSteeler";
const char* WIFI_PASSWORD = "dekuposh84";

// Discovery multicast group/port
static const IPAddress DISC_GROUP(239, 255, 0, 1);
static const uint16_t  DISC_PORT = 40000;

// Unicast messaging port
static const uint16_t  MSG_PORT = 40001;

// Timings (ms)
static const uint32_t HELLO_PERIOD_MS     = 1000;
static const uint32_t HEARTBEAT_PERIOD_MS = 1500;
static const uint32_t HEARTBEAT_GRACE_MS  = 500;   // extra time before counting as missed
static const uint8_t  HEARTBEAT_MISSES_MAX = 3;

// ---------------------------------

// Build-time role string
#if defined(DEVICE_ROLE_REMOTE)
  const char* ROLE = "SOPHIA-REMOTE";
#elif defined(DEVICE_ROLE_SIGN)
  const char* ROLE = "SOPHIA-SIGN";
#else
  #error "Define DEVICE_ROLE_REMOTE or DEVICE_ROLE_SIGN at the top."
#endif

// Globals
AsyncUDP udpDisc;     // discovery (multicast)
AsyncUDP udpMsg;      // unicast messages
IPAddress peerIP;     // learned peer IP
bool peerKnown = false;

uint32_t lastHello  = 0;
uint32_t lastBeatTx = 0;
uint32_t lastBeatRx = 0;
uint8_t  missedBeats = 0;
uint32_t lastWiFiAttempt = 0;
uint32_t wifiRetryMs = 1000;  // backoff (caps later)

String deviceId; // last 3 bytes of MAC

// Simple debounced button for REMOTE example
#if defined(DEVICE_ROLE_REMOTE)
  const int BTN_PINS[] = { 27, 33, 14, 32 }; // matches your earlier wiring
  const int NUM_BTNS = sizeof(BTN_PINS)/sizeof(BTN_PINS[0]);
  bool lastBtnState[4] = {false,false,false,false};
  uint32_t lastBtnChange[4] = {0,0,0,0};
  const uint32_t DEBOUNCE_MS = 30;
#endif

// Simple LEDs for SIGN example (you can replace with your PWM code)
#if defined(DEVICE_ROLE_SIGN)
  const int LED_PINS[] = { 22, 25, 26, 23, 5, 4, 16, 21, 17 }; // 3x RGB as you had
  const int NUM_LEDS = sizeof(LED_PINS)/sizeof(LED_PINS[0]);
#endif

// ---------- Helpers ----------
String ipToStr(const IPAddress& ip) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return String(buf);
}

String macSuffix() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(buf);
}

void logLine(const char* tag, const String& msg) {
  Serial.printf("[%s] %s\n", tag, msg.c_str());
}

// ---------- Wi-Fi ----------
void connectWiFiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) return;

  uint32_t now = millis();
  if (now - lastWiFiAttempt < wifiRetryMs) return;

  lastWiFiAttempt = now;
  logLine("WiFi", "Connecting to " + String(WIFI_SSID) + " ...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // backoff (1s → 2s → 4s → 8s → cap 10s)
  wifiRetryMs = min<uint32_t>(wifiRetryMs * 2, 10000);
}

void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      logLine("WiFi", "STA_CONNECTED");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      logLine("WiFi", "GOT_IP " + ipToStr(WiFi.localIP()));
      wifiRetryMs = 1000; // reset backoff
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      logLine("WiFi", "DISCONNECTED, will retry");
      peerKnown = false; // force rediscovery after we come back
      missedBeats = 0;
      udpDisc.close();
      udpMsg.close();
      break;
    default:
      break;
  }
}

// ---------- Discovery ----------
void startDiscovery() {
  if (udpDisc.listenMulticast(DISC_GROUP, DISC_PORT)) {
    logLine("DISC", "Listening multicast on " + ipToStr(DISC_GROUP) + ":" + String(DISC_PORT));
    udpDisc.onPacket([](AsyncUDPPacket p) {
      String msg = String((const char*)p.data(), p.length());
      // Expect: HELLO <ROLE> <deviceId> <ip>
      if (!msg.startsWith("HELLO ")) return;

      // Parse quickly
      // Tokenize: [HELLO][role][id][ip]
      int p1 = msg.indexOf(' ');
      int p2 = msg.indexOf(' ', p1+1);
      int p3 = msg.indexOf(' ', p2+1);
      if (p1 < 0 || p2 < 0 || p3 < 0) return;

      String theirRole = msg.substring(p1+1, p2);
      String theirId   = msg.substring(p2+1, p3);
      String theirIP   = msg.substring(p3+1);

      // We want the *other* role
      bool roleOk = false;

      // Accept only opposite role:
      #if defined(DEVICE_ROLE_REMOTE)
        roleOk = (theirRole == "SOPHIA-SIGN");
      #elif defined(DEVICE_ROLE_SIGN)
        roleOk = (theirRole == "SOPHIA-REMOTE");
      #endif

      if (!roleOk) return;

      IPAddress ip;
      if (!ip.fromString(theirIP)) return;

      peerIP = ip;
      peerKnown = true;
      missedBeats = 0;
      lastBeatRx = millis();
      logLine("DISC", "Peer found: " + theirRole + " id=" + theirId + " ip=" + ipToStr(peerIP));

      // Try to bind unicast socket (for messages/heartbeats)
      if (!udpMsg.listen(MSG_PORT)) {
        logLine("MSG", "Failed to listen on port " + String(MSG_PORT));
      } else {
        logLine("MSG", "Listening unicast on port " + String(MSG_PORT));
        udpMsg.onPacket([](AsyncUDPPacket p) {
          String msg = String((const char*)p.data(), p.length());

          if (msg.startsWith("PING ")) {
            // echo back PONG with same seq
            udpMsg.writeTo((const uint8_t*)("PONG " + msg.substring(5)).c_str(),
                           msg.length(), p.remoteIP(), MSG_PORT);
          } else if (msg.startsWith("PONG ")) {
            lastBeatRx = millis();
            missedBeats = 0;
          } else if (msg.startsWith("CMD ")) {
            // Handle command (REMOTE -> SIGN) or ack (SIGN -> REMOTE)
            // Format: CMD <name> [arg]
            #if defined(DEVICE_ROLE_SIGN)
              if (msg.indexOf("LIGHT") > 0) {
                // Tiny demo: pulse an LED briefly on any LIGHT command
                digitalWrite(LED_PINS[0], HIGH);
              } else if (msg.indexOf("OFF") > 0) {
                digitalWrite(LED_PINS[0], LOW);
              }
            #endif
            // Optionally send ACK
            String ack = "ACK " + msg.substring(4);
            udpMsg.writeTo((const uint8_t*)ack.c_str(), ack.length(), p.remoteIP(), MSG_PORT);
          } else if (msg.startsWith("ACK ")) {
            // could track per-command acks if you want
          }
        });
      }
    });
  } else {
    logLine("DISC", "Multicast listen failed; will retry");
  }
}

void sendHelloIfNeeded() {
  uint32_t now = millis();
  if (now - lastHello < HELLO_PERIOD_MS) return;
  lastHello = now;

  if (WiFi.status() != WL_CONNECTED) return;
  if (!udpDisc.connected()) return; // not bound yet

  String hello = String("HELLO ") + ROLE + " " + deviceId + " " + ipToStr(WiFi.localIP());
  udpDisc.writeTo((const uint8_t*)hello.c_str(), hello.length(), DISC_GROUP, DISC_PORT);
}

// ---------- Heartbeats & link supervision ----------
void heartbeatTick() {
  if (!peerKnown) return;
  uint32_t now = millis();

  // transmit heartbeat
  if (now - lastBeatTx >= HEARTBEAT_PERIOD_MS) {
    lastBeatTx = now;
    static uint32_t seq = 1;
    String ping = String("PING ") + String(seq++);
    udpMsg.writeTo((const uint8_t*)ping.c_str(), ping.length(), peerIP, MSG_PORT);
  }

  // monitor receive; allow grace
  if (now > lastBeatRx + HEARTBEAT_PERIOD_MS + HEARTBEAT_GRACE_MS) {
    lastBeatRx = now; // move window, count miss
    if (++missedBeats >= HEARTBEAT_MISSES_MAX) {
      logLine("LINK", "Peer timed out; returning to discovery");
      peerKnown = false;
      missedBeats = 0;
      udpMsg.close();
      // keep udpDisc open; HELLOs will resume
    }
  }
}

// ---------- App Logic ----------
#if defined(DEVICE_ROLE_REMOTE)
void setupButtons() {
  for (int i = 0; i < NUM_BTNS; ++i) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
  }
}
void pollButtons() {
  uint32_t now = millis();
  for (int i = 0; i < NUM_BTNS; ++i) {
    bool pressed = (digitalRead(BTN_PINS[i]) == LOW);
    if (pressed != lastBtnState[i] && now - lastBtnChange[i] > DEBOUNCE_MS) {
      lastBtnChange[i] = now;
      lastBtnState[i] = pressed;
      if (pressed && peerKnown) {
        // Example: send a LIGHT command referencing button index
        String cmd = "CMD LIGHT " + String(i);
        udpMsg.writeTo((const uint8_t*)cmd.c_str(), cmd.length(), peerIP, MSG_PORT);
      }
    }
  }
}
#endif

#if defined(DEVICE_ROLE_SIGN)
void setupLeds() {
  for (int i = 0; i < NUM_LEDS; ++i) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }
}
#endif

// ---------- Arduino Basics ----------
void setup() {
  Serial.begin(115200);
  delay(200);

  deviceId = macSuffix();
  logLine("BOOT", String(ROLE) + " " + deviceId);

  WiFi.onEvent(onWiFiEvent);
  connectWiFiIfNeeded();

  // Start discovery now; if Wi-Fi not ready, we’ll reopen later on GOT_IP
  startDiscovery();

  #if defined(DEVICE_ROLE_REMOTE)
    setupButtons();
  #elif defined(DEVICE_ROLE_SIGN)
    setupLeds();
  #endif
}

uint32_t lastCheckUDP = 0;

void loop() {
  // Wi-Fi reconnect if needed
  connectWiFiIfNeeded();

  // If Wi-Fi just came back and discovery socket isn’t up, (re)start it
  if (WiFi.status() == WL_CONNECTED && !udpDisc.connected()) {
    startDiscovery();
  }

  // Advertise presence until we know the peer
  if (!peerKnown) {
    sendHelloIfNeeded();
  } else {
    heartbeatTick();
  }

  // Demo app logic
  #if defined(DEVICE_ROLE_REMOTE)
    pollButtons();
  #elif defined(DEVICE_ROLE_SIGN)
    // (Optional) add local effects/timeout; LEDs are driven by incoming CMDs
  #endif

  // Slight yield
  delay(5);
}
