#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>

#include "action.hpp"

#define LED_PIN    4      // A5 = GPIO 4
#define NUM_LEDS   32    // change this to the total LEDs in all 4 strips
#define BRIGHTNESS 10     // 0-255

#define YES_BUTTON 33
#define NO_BUTTON 14

#define yellow_r 255
#define yellow_g 255
#define yellow_b 0

#define blue_r 0
#define blue_g 0
#define blue_b 255

#define purple_r 255
#define purple_g 0
#define purple_b 255

#define orange_r 255
#define orange_g 165
#define orange_b 0

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

const char * ssid = "IronReverseSoulSteeler";
const char * password = "dekuposh84";
const char * SIGN_HOST = "sophia-signbox";
const char * REMOTE_HOST = "sophia-remote";
const uint16_t LISTEN_PORT = 4210;

WiFiUDP udp;

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
  if (MDNS.begin(SIGN_HOST)) {    // reachable as signbox.local
    Serial.println("mDNS responder started: sophia.signbox.local");
    // Optional: advertise a UDP service for discovery tools
    MDNS.addService("msg", "udp", LISTEN_PORT);
  } else {
    Serial.println("mDNS start failed.");
  }
}

void setup_udp() {
  if (udp.begin(LISTEN_PORT)) {
    Serial.print("Listening UDP on port ");
    Serial.println(LISTEN_PORT);
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

void perform_action(Action action){
  switch(action){
  case Action::cat:
    strip.fill(strip.Color(yellow_r, yellow_g, yellow_b));
    strip.show();
    break;
  case Action::dog:
    strip.fill(strip.Color(blue_r, blue_g, blue_b));
    strip.show();
    break;

  case Action::sophia:
    strip.fill(strip.Color(purple_r, purple_g, purple_b));
    strip.show();
    break;

  case Action::coffee:
    strip.fill(strip.Color(orange_r, orange_g, orange_b));
    strip.show();
    break;
  }
}

void listen_for_message(){
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

  // Serial.println("Turning LEDS on");
  // // Turn all LEDs on (white)
  // strip.fill(strip.Color(255, 255, 255));
  // strip.show();
  // delay(500);

  // // Turn all LEDs off
  // Serial.println("Turning LEDS off");
  // strip.clear();
  // strip.show();
  // delay(500);

  // int yes = digitalRead(YES_BUTTON);
  // int no = digitalRead(NO_BUTTON);
  // if(!yes){
  //   Serial.println("Yes!");
  // }

  // if(!no){
  //   Serial.println("No!");
  // }
}