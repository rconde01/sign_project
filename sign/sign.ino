#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <Arduino.h>

#include "action.hpp"

#define LED_PIN    4      // A5 = GPIO 4
#define NUM_LEDS   32
#define BRIGHTNESS 200     // 0-255

#define BTN_YES 33
#define BTN_NO 14

#define PIN_RX 16
#define PIN_TX 17

#define CAT_MP3 2
#define DOG_MP3 4
#define COFFEE_MP3 5
#define SOPHIA_MP3 1

HardwareSerial mp3(1);

// Helper to send a 6-byte frame: 7E, CMD, 00, 02, DH, DL, EF
void sendCmd(uint8_t cmd, uint8_t DH, uint8_t DL) {
  uint8_t frame[7] = {0x7E, cmd, 0x00, 0x02, DH, DL, 0xEF};
  mp3.write(frame, sizeof(frame));
  mp3.flush();
}

// Convenience wrappers
void mp3Play()            { sendCmd(0x0D, 0x00, 0x00); }        // Play
void mp3Pause()           { sendCmd(0x0E, 0x00, 0x00); }        // Pause
void mp3Stop()            { sendCmd(0x16, 0x00, 0x00); }        // Stop
void mp3SetVolume(uint8_t level) {
  if (level > 30) level = 30;
  // 0x06 = set volume; DH=0x00 (no memory), DL=level (0..30)
  sendCmd(0x06, 0x00, level);
}
void mp3PlayIndex(uint16_t index /*1..2999*/) {
  Serial.printf("playing index %d\n",static_cast<uint32_t>(index));

  uint8_t hi = (index >> 8) & 0xFF;
  uint8_t lo = index & 0xFF;
  // 0x03 = play track by global index in ROOT
  sendCmd(0x03, hi, lo);
}


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
auto const sign_port    = uint16_t{4210};
auto const remote_port  = uint16_t{4211};
auto const yellow       = Color{255,255,0};
auto const blue         = Color{0,0,255};
auto const purple       = Color{255,0,255};
auto const orange       = Color{255,165,0};


struct Data {
  WiFiUDP udp{};
  IPAddress remote_ip{};
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
    MDNS.addService("msg", "udp", sign_port);
  } else {
    Serial.println("mDNS start failed.");
  }

  while(static_cast<uint32_t>(g_data.remote_ip) == 0){
    Serial.print("Attempting to resolve ");
    Serial.print(remote_host);
    Serial.println(".local...");
    g_data.remote_ip = MDNS.queryHost(remote_host);
    delay(1000);
  }
}

void setup_udp() {
  if (g_data.udp.begin(sign_port)) {
    Serial.print("Listening UDP on port ");
    Serial.println(sign_port);
  } else {
    Serial.println("UDP begin failed!");
  }  
}

void setup_buttons(){
  pinMode(BTN_YES, INPUT_PULLUP);
  pinMode(BTN_NO, INPUT_PULLUP);
}

void setup_leds(){
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show(); // Initialize all pixels to 'off'
}

void setup_mp3(){
  mp3.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX);
  mp3SetVolume(25);
}

bool send_message_and_wait_ack(IPAddress dest, const char * msg, uint32_t timeout_ms = 400){
  auto & udp = g_data.udp;

  // Send
  if (udp.beginPacket(dest, remote_port) == 0)
    return false;

  udp.print(msg);
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
      String expected = String("ACK:") + msg;
      if (expected == String(buf)) {
        return true;
      }
    }
    delay(5);
  }
  Serial.println("ACK timeout");
  return false;
}

void send_button_message(int button){
  auto & remote_ip = g_data.remote_ip;

  switch(button){
  case 1:
    send_message_and_wait_ack(remote_ip, "yes");
    break;
  case 0:
    send_message_and_wait_ack(remote_ip, "no");
    break;
  }
}

void handle_button_press(int button){
  send_button_message(button);
  light_sign(-1);
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  setup_buttons();
  connect_wifi();
  setup_mdns();
  setup_udp();
  setup_leds();
  setup_mp3();
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
  auto led_start = -1;
  auto led_end = -1;

  switch(sign_number){
  case 0:
    color = yellow;
    led_start = sign_number*leds_per_sign;
    led_end = led_start + leds_per_sign - 1; 
    break;
  case 1:
    color = blue;
    led_start = sign_number*leds_per_sign;
    led_end = led_start + leds_per_sign - 1; 
    break;
  case 2:
    color = purple;
    led_start = sign_number*leds_per_sign;
    led_end = led_start + leds_per_sign - 1; 
    break;    
  case 3:
    color = orange;
    led_start = sign_number*leds_per_sign;
    led_end = led_start + leds_per_sign - 1; 
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
    mp3PlayIndex(CAT_MP3);
    break;
  case Action::dog:
    light_sign(1);
    mp3PlayIndex(DOG_MP3);
    break;
  case Action::sophia:
    light_sign(2);
    mp3PlayIndex(SOPHIA_MP3);
    break;
  case Action::coffee:
    light_sign(3);
    mp3PlayIndex(COFFEE_MP3);
    break;
  }
}

bool starts_with(const char * str, const char * pattern){
  int index = 0;

  for(;;){
    if(str[index] == '\0'){
      return str[index] == pattern[index];
    }

    if(pattern[index] == '\0')
      return true;

    if(str[index] != pattern[index])
      return false;

    ++index;
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
    if(!starts_with(buf,"ACK:")){
      udp.beginPacket(from_ip, from_port);
      udp.print("ACK:");
      udp.print(buf);
      udp.endPacket();
    }

    perform_action(action);
  }
}

void loop() {
  int yes_btn = !digitalRead(BTN_YES);
  int no_btn = !digitalRead(BTN_NO);

  listen_for_message();

  if(yes_btn){
    handle_button_press(1);
  }
  else if(no_btn){
    handle_button_press(0);
  }
}