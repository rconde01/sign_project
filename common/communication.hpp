#pragma once

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

struct Communication {
  Communication(const char * the_role) : role(the_role){}

  AsyncUDP      udpDiscRx{};        // discovery listener (multicast)
  AsyncUDP      udpDiscTx{};        // discovery sender (multicast)
  //AsyncUDP      udpDiscBcastRx{};   // discovery listener (broadcast/unicast fallback)
  AsyncUDP      udpMsg{};           // unicast messages (CMD/ACK/PING/PONG)

  IPAddress     peerIP{};  // learned peer IP
  bool          peerKnown{false};
  uint32_t      lastHello{0};
  uint32_t      lastBeatTx{0};
  uint32_t      lastBeatRx{0};
  uint8_t       missedBeats{0};
  uint32_t      lastWiFiAttempt{0};
  uint32_t      wifiRetryMs{1000};  // backoff (caps later)
  String        deviceId{}; // last 3 bytes of MAC
  String        role{}; // filled in by app
};

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
void connectWiFiIfNeeded(Communication & comm) {
  if (WiFi.status() == WL_CONNECTED) return;

  uint32_t now = millis();
  if (now - comm.lastWiFiAttempt < comm.wifiRetryMs) return;

  comm.lastWiFiAttempt = now;

  logLine("WiFi", "Connecting to " + String(WIFI_SSID) + " ...");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // backoff (1s → 2s → 4s → 8s → cap 10s)
  comm.wifiRetryMs = min<uint32_t>(comm.wifiRetryMs * 2, 10000);
}

void startDiscoveryRx(Communication & comm, std::function<void(String)> on_command);     // fwd
//void startBroadcastRx(Communication & comm, std::function<void(String)> on_command);     // fwd
void bindMsgSocket(Communication & comm, std::function<void(String)> on_command);        // fwd

void onWiFiEvent(Communication & comm, WiFiEvent_t event, std::function<void(String)> on_command) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      logLine("WiFi", "STA_CONNECTED");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      logLine("WiFi", "GOT_IP " + ipToStr(WiFi.localIP()));
      comm.wifiRetryMs = 1000; // reset backoff

      comm.udpDiscTx.close();

      if(!comm.udpDiscTx.connect(DISC_GROUP, DISC_PORT)) {
        logLine("DISC", "TX connect to multicast failed (will still use broadcast fallback)");
      }

      // (Re)join discovery listeners
      startDiscoveryRx(comm, on_command);
      //startBroadcastRx(comm, on_command);
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      logLine("WiFi", "DISCONNECTED, will retry");
      comm.peerKnown = false; // force rediscovery after we come back
      comm.missedBeats = 0;
      comm.udpDiscRx.close();
      //comm.udpDiscBcastRx.close();
      comm.udpDiscTx.close();
      comm.udpMsg.close();
      break;

    default:
      break;
  }
}

void handleHelloPacket(
    Communication & comm,
    const IPAddress& from,
    const uint8_t* data,
    size_t len,
    std::function<void(String)> on_command) {
  String msg((const char*)data, len);
  if (!msg.startsWith("HELLO ")) return;

  int p1 = msg.indexOf(' ');
  int p2 = msg.indexOf(' ', p1+1);
  int p3 = msg.indexOf(' ', p2+1);
  if (p1 < 0 || p2 < 0 || p3 < 0) return;

  String theirRole = msg.substring(p1+1, p2);
  String theirId   = msg.substring(p2+1, p3);
  String theirIP   = msg.substring(p3+1);

  bool roleOk = theirRole != comm.role;

  if (!roleOk) return;

  IPAddress ip;
  if (!ip.fromString(theirIP)) ip = from;

  comm.peerIP = ip;
  if (!comm.peerKnown)
    logLine("DISC", "Peer found: " + theirRole + " id=" + theirId + " ip=" + ipToStr(comm.peerIP));

  comm.peerKnown = true;
  comm.missedBeats = 0;
  comm.lastBeatRx = millis();

  // Make sure unicast socket is bound
  if (!comm.udpMsg.connected()) {
    bindMsgSocket(comm, on_command);
  }
}


void startDiscoveryRx(Communication & comm, std::function<void(String)> on_command) {
  comm.udpDiscRx.close();
  delay(5);
  if (comm.udpDiscRx.listenMulticast(DISC_GROUP, DISC_PORT)) {
    logLine("DISC", "Listening multicast on " + ipToStr(DISC_GROUP) + ":" + String(DISC_PORT));
    comm.udpDiscRx.onPacket([&comm, on_command](AsyncUDPPacket p) {
      if(comm.role == "sophia-remote"){
        //comm.lastUserActivityMs = millis();
      }

      handleHelloPacket(comm, p.remoteIP(), p.data(), p.length(), on_command);
    });
  } else {
    logLine("DISC", "Multicast listen failed; will retry");
  }
}

// void startBroadcastRx(Communication & comm, std::function<void(String)> on_command) {
//   comm.udpDiscBcastRx.close();
//   if (comm.udpDiscBcastRx.listen(DISC_PORT)) {
//     logLine("DISC", "Listening broadcast/unicast on :" + String(DISC_PORT));
//     comm.udpDiscBcastRx.onPacket([&comm, on_command](AsyncUDPPacket p) {
//       if(comm.role == "sophia-remote"){
//         //comm.lastUserActivityMs = millis();
//       }
//       handleHelloPacket(comm, p.remoteIP(), p.data(), p.length(), on_command);
//     });
//   } else {
//     logLine("DISC", "Broadcast listen failed");
//   }
// }

void sendHelloIfNeeded(Communication & comm) {
  uint32_t now = millis();
  if (now - comm.lastHello < HELLO_PERIOD_MS) return;
  comm.lastHello = now;
  if (WiFi.status() != WL_CONNECTED) return;

  String hello = String("HELLO ") + comm.role + " " + comm.deviceId + " " + ipToStr(WiFi.localIP());

  bool ok = false;
  if (comm.udpDiscTx.connected()) {
    ok = comm.udpDiscTx.write((const uint8_t*)hello.c_str(), hello.length());
  }
  if (!ok) {
    // Broadcast fallback (helps when routers/APs filter multicast)
    WiFiUDP bcast;
    bcast.begin(DISC_PORT);
    bcast.beginPacket(IPAddress(255,255,255,255), DISC_PORT);
    bcast.write((const uint8_t*)hello.c_str(), hello.length());
    bcast.endPacket();
    bcast.stop();
  }
}

// ---------- Unicast messages (heartbeats & commands) ----------
void onMsgPacket(Communication & comm, AsyncUDPPacket p, std::function<void(String)> on_command) {
  String msg((const char*)p.data(), p.length());

  if (msg.startsWith("PING ")) {
    String pong = "PONG " + msg.substring(5);
    comm.udpMsg.writeTo((const uint8_t*)pong.c_str(), pong.length(), p.remoteIP(), MSG_PORT);
  } else if (msg.startsWith("PONG ")) {
    comm.lastBeatRx = millis();
    comm.missedBeats = 0;
  } else if (msg.startsWith("CMD ")) {
    // Handle commands
    on_command(msg.substring(4));

    // ACK (optional)
    String ack = "ACK " + msg.substring(4);
    comm.udpMsg.writeTo((const uint8_t*)ack.c_str(), ack.length(), p.remoteIP(), MSG_PORT);
  } else if (msg.startsWith("ACK ")) {
    // handle command ack if desired
  }

  if(comm.role == "sophia-remote"){
    //comm.lastUserActivityMs = millis();
  }
}

void bindMsgSocket(Communication & comm, std::function<void(String)> on_command) {
  comm.udpMsg.close();
  if (!comm.udpMsg.listen(MSG_PORT)) {
    logLine("MSG", "Failed to listen on port " + String(MSG_PORT));
  } else {
    logLine("MSG", "Listening unicast on port " + String(MSG_PORT));
    comm.udpMsg.onPacket([&comm, on_command](AsyncUDPPacket p) {
      onMsgPacket(comm, p, on_command);
    });
  }
}

// ---------- Heartbeat supervision ----------
void heartbeatTick(Communication & comm) {
  if (!comm.peerKnown) return;
  uint32_t now = millis();

  if (now - comm.lastBeatTx >= HEARTBEAT_PERIOD_MS) {
    comm.lastBeatTx = now;
    static uint32_t seq = 1;
    String ping = String("PING ") + String(seq++);
    comm.udpMsg.writeTo((const uint8_t*)ping.c_str(), ping.length(), comm.peerIP, MSG_PORT);
  }

  if (now > comm.lastBeatRx + HEARTBEAT_PERIOD_MS + HEARTBEAT_GRACE_MS) {
    comm.lastBeatRx = now;
    if (++comm.missedBeats >= HEARTBEAT_MISSES_MAX) {
      logLine("LINK", "Peer timeout → rediscover");
      comm.peerKnown = false;
      comm.missedBeats = 0;
      comm.udpMsg.close();
    }
  }
}

void sendCmdToPeer(Communication & comm, String cmd){
  String cmd_string = "CMD " + cmd;

  comm.udpMsg.writeTo(
    reinterpret_cast<const uint8_t*>(cmd_string.c_str()),
    cmd_string.length(),
    comm.peerIP,
    MSG_PORT);
}