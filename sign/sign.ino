#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>

#include "action.hpp"

#define LED_PIN    4      // A5 = GPIO 4
#define NUM_LEDS   32
#define BRIGHTNESS 10     // 0-255

#define YES_BUTTON 33
#define NO_BUTTON 14

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

auto const ssid         = "IronReverseSoulSteeler";
auto const password     = "dekuposh84";
auto const sign_host    = "sophia-signbox";
auto const remote_host  = "sophia-remote";
auto const listen_port  = uint16_t{4210};
auto const yellow       = Color{255,255,0};
auto const blue         = Color{0,0,255};
auto const purple       = Color{255,0,255};
auto const orange       = Color{255,165,0};


struct Data {
  WiFiUDP udp{};
};

Data g_data{};

void connect_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false); // lower latency
  WiFi.begin(ssid, password);

  Serial.println("Connecting to WiFi");

  // If it doesn't connect in 10 seconds, reboot and try again
  int wait = 0;
  int max_wait = 10000;

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(400);
    wait += 400;

    if(wait >= max_wait){
      Serial.println("Connection Failed. Rebooting.");
      ESP.restart();
    }
  }
  Serial.println();
  Serial.println("WiFi OK, IP: ");
  Serial.println(WiFi.localIP());
}

void setup_mdns() {
  if (MDNS.begin(sign_host)) {    // reachable as signbox.local
    Serial.println("mDNS responder started: sophia.signbox.local");
    // Optional: advertise a UDP service for discovery tools
    MDNS.addService("msg", "udp", listen_port);
  } else {
    Serial.println("mDNS start failed.");
  }
}

void setup_udp() {
  if (g_data.udp.begin(listen_port)) {
    Serial.print("Listening UDP on port ");
    Serial.println(listen_port);
  } else {
    Serial.println("UDP begin failed!");
  }  
}

void setup_buttons(){
  pinMode(YES_BUTTON, INPUT_PULLUP);
  pinMode(NO_BUTTON, INPUT_PULLUP);
}

void setup_leds(){
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show(); // Initialize all pixels to 'off'
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  setup_buttons();
  connect_wifi();
  setup_mdns();
  setup_udp();
  setup_leds();
}

Action get_action(const char * message){
  if (strcmp(message, "cat") == 0)
    return Action::cat;

  if (strcmp(message, "dog") == 0)
    return Action::dog;

  if (strcmp(message, "sophia") == 0)
    return Action::sophia;

  if (strcmp(message, "coffee") == 0)
    return Action::coffee;

  return Action::unknown;    
}

void light_sign(int sign_number){
  auto const leds_per_sign = 8;
  auto const num_signs = 4;
  auto const num_leds = num_signs*leds_per_sign;

  Color color{};
  auto const led_start = sign_number*leds_per_sign;
  auto const led_end = led_start + leds_per_sign - 1; 

  switch(sign_number){
  case 0:
    color = yellow;
    break;
  case 1:
    color = blue;
    break;
  case 2:
    color = purple;
    break;    
  case 3:
    color = orange;
    break;
  }

  for(int i = 0; i < num_leds; ++i){
    if(i >= led_start && i <= led_end){
      strip.fill(strip.Color(color.r,color.g,color.b),i,1);
    }
    else {
      strip.fill(strip.Color(0,0,0),i,1);
    }
  }

  strip.show();
}

void perform_action(Action action){
  switch(action){
  case Action::cat:
    light_sign(0);
    break;
  case Action::dog:
    light_sign(1);
    break;
  case Action::sophia:
    light_sign(2);
    break;
  case Action::coffee:
    light_sign(3);
    break;
  }
}

void listen_for_message(){
  auto & udp = g_data.udp;

  int packet_size = udp.parsePacket();
  if (packet_size > 0) {
    char buf[128];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len < 0)
      return;
    buf[len] = '\0';

    IPAddress from_ip = udp.remoteIP();
    uint16_t from_port = udp.remotePort();
    Serial.printf("RX from %s:%u -> \"%s\"\n",
                  from_ip.toString().c_str(), from_port, buf);

    auto action = get_action(buf);

    // Send ACK back to sender (same port it used)
    udp.beginPacket(from_ip, from_port);
    udp.print("ACK:");
    udp.print(buf);
    udp.endPacket();

    perform_action(action);
  }
}

void loop() {
  listen_for_message();
}