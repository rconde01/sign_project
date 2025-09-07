#pragma once

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>

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
  Communication(
    uint16_t local_port,
    uint16_t remote_port,
    String   host_name,
    String   remote_host_name) :
  localPort(local_port),
  remotePort(remote_port),
  host_name(host_name),
  remote_host_name(remote_host_name) {}

  WiFiUDP       udp{};
  bool          mdns_initialized{false};
  uint16_t      localPort;
  uint16_t      remotePort;
  IPAddress     remoteIP{};  // learned peer IP
  bool          peerKnown{false};
  uint32_t      lastWiFiAttempt{0};
  uint32_t      wifiRetryMs{1000};  // backoff (caps later)
  String        host_name{};
  String        remote_host_name{};
};

void setup_mdns(Communication & comm) {
  if (MDNS.begin(comm.host_name)) {    // reachable as signbox.local
    Serial.println("mDNS responder started: " + comm.host_name + ".local");
    MDNS.addService("msg", "udp", comm.localPort);
  } else {
    Serial.println("mDNS start failed.");
  }
}

void setup_udp(Communication & comm) {
  if (comm.udp.begin(comm.localPort)) {
    Serial.print("Listening UDP on port ");
    Serial.println(comm.localPort);
  } else {
    Serial.println("UDP begin failed!");
  }  
}

void onWiFiEvent(Communication & comm, WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("WiFi:STA_CONNECTED");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println("WiFi:STA_GOT_IP");
      if(!comm.mdns_initialized){
        setup_mdns(comm);
        setup_udp(comm);
        comm.mdns_initialized = true;
      }
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi:STA_DISCONNECTED");
      comm.mdns_initialized = false;
      break;

    default:
      break;
  }
}

void setup_wifi(Communication & comm){
  WiFi.onEvent([&comm](arduino_event_id_t event, arduino_event_info_t){ onWiFiEvent(comm, event); });
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false); // lower latency
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Connecting to WiFi");

  // If it doesn't connect in 10 seconds, reboot and try again
  int wait = 0;
  int max_wait = 10000;

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(400);
    wait += 400;

    if(wait >= max_wait){
      Serial.println("Failed to connect, restarting...");
      ESP.restart();
    }
  }

  Serial.println();
  Serial.println("WiFi OK, IP: ");
  Serial.println(WiFi.localIP());
}

bool send_message_and_wait_ack(
    Communication & comm,
    String const & message,
    uint32_t timeout_ms = 400){
  auto & udp = comm.udp;

  // Send
  if (udp.beginPacket(comm.remoteIP, comm.remotePort) == 0)
    return false;

  udp.print(message);
  udp.endPacket();

  uint32_t start = millis();
  char buf[128];

  // Wait for ACK on our local UDP socket
  while (millis() - start < timeout_ms) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      int len = udp.read(buf, sizeof(buf) - 1);
      if (len < 0)
        continue;

      buf[len] = '\0';
      Serial.printf("RX ACK: \"%s\"\n", buf);
      String expected = String("ACK:") + message;
      if (expected == String(buf)) {
        return true;
      }
    }
    delay(5);
  }
  Serial.println("ACK timeout");
  return false;
}

void listen_for_message(Communication & comm, std::function<void(String)> execute_command){
  auto is_remote_alive = [&comm](){
    return true;//send_message_and_wait_ack(comm, "ping");
  };

  if(static_cast<uint32_t>(comm.remoteIP) == 0 || !is_remote_alive()){
    while(static_cast<uint32_t>(comm.remoteIP) == 0){
        Serial.print("Attempting to resolve " + comm.remote_host_name + ".local...\n");
        comm.remoteIP = MDNS.queryHost(comm.remote_host_name);

        if(static_cast<uint32_t>(comm.remoteIP) != 0){
          Serial.println("Resolved " + comm.remote_host_name + ".local to " + comm.remoteIP.toString());
        }
    }
  }

  auto & udp = comm.udp;

  int packet_size = udp.parsePacket();
  if (packet_size > 0) {
    char buf[128];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len < 0)
      return;
    buf[len] = '\0';

    auto from_ip = udp.remoteIP();
    auto from_port = udp.remotePort();
    Serial.printf("RX from %s:%u -> \"%s\"\n",
                  from_ip.toString().c_str(), from_port, buf);

    if(!String(buf).startsWith("ACK:")){
      udp.beginPacket(from_ip, from_port);
      udp.print("ACK:");
      udp.print(buf);
      udp.endPacket();
    }

    execute_command(buf);
  }
}

