#include <Adafruit_NeoPixel.h>

#include "action.hpp"
#include "mp3.hpp"
#include "../common/communication.hpp"

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

#define SOPHIA_SIGN 0
#define DOG_SIGN 1
#define CAT_SIGN 2
#define COFFEE_SIGN 3

struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

auto const yellow       = Color{255,255,0};
auto const blue         = Color{0,0,255};
auto const purple       = Color{255,0,255};
auto const orange       = Color{255,165,0};

struct Data {
  Data() : 
  mp3{1},
  strip{NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800},
  comm{"sophia-sign"},
  is_active{false} {}

  HardwareSerial    mp3;
  Adafruit_NeoPixel strip;
  Communication     comm;
  bool              is_active;
};

Data g_data{};

void setup_buttons(){
  pinMode(BTN_YES, INPUT_PULLUP);
  pinMode(BTN_NO, INPUT_PULLUP);
}

void setup_leds(){
  g_data.strip.begin();
  g_data.strip.setBrightness(BRIGHTNESS);
  g_data.strip.show(); // Initialize all pixels to 'off'
}

void setup_mp3(){
  g_data.mp3.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX);
  mp3SetVolume(g_data.mp3, 25);
}

void handle_button_press(int button){
  if(button == 0)
    sendCmdToPeer(g_data.comm, "no");
  else if(button == 1)
    sendCmdToPeer(g_data.comm, "yes");

  light_sign(-1);
  g_data.is_active = false;
}

void light_sign(int sign_number){
  auto const leds_per_sign = 8;
  auto const num_signs = 4;
  auto const num_leds = num_signs*leds_per_sign;

  Color color{};
  auto led_start = -1;
  auto led_end = -1;

  switch(sign_number){
  case CAT_SIGN:
    color = yellow;
    led_start = sign_number*leds_per_sign;
    led_end = led_start + leds_per_sign - 1; 
    break;
  case DOG_SIGN:
    color = blue;
    led_start = sign_number*leds_per_sign;
    led_end = led_start + leds_per_sign - 1; 
    break;
  case COFFEE_SIGN:
    color = purple;
    led_start = sign_number*leds_per_sign;
    led_end = led_start + leds_per_sign - 1; 
    break;    
  case SOPHIA_SIGN:
    color = orange;
    led_start = sign_number*leds_per_sign;
    led_end = led_start + leds_per_sign - 1; 
    break;
  }

  for(int i = 0; i < num_leds; ++i){
    if(i >= led_start && i <= led_end){
      g_data.strip.fill(g_data.strip.Color(color.r,color.g,color.b),i,1);
    }
    else {
      g_data.strip.fill(g_data.strip.Color(0,0,0),i,1);
    }
  }

  g_data.strip.show();
}

void execute_command(String cmd){
  if(cmd == "cat"){
    light_sign(CAT_SIGN);
    mp3PlayIndex(g_data.mp3,  CAT_MP3);
    g_data.is_active = true;
  }
  else if(cmd == "dog"){
    light_sign(DOG_SIGN);
    mp3PlayIndex(g_data.mp3,  DOG_MP3);
    g_data.is_active = true;
  }
  else if(cmd == "sophia"){
    light_sign(SOPHIA_SIGN);
    mp3PlayIndex(g_data.mp3,  SOPHIA_MP3);
    g_data.is_active = true;
  }
  else if(cmd == "coffee"){
    light_sign(COFFEE_SIGN);
    mp3PlayIndex(g_data.mp3,  COFFEE_MP3);
    g_data.is_active = true;
  }
}

void setup_wifi(){
  g_data.comm.deviceId = macSuffix();
  logLine("BOOT", String(g_data.comm.role) + " " + g_data.comm.deviceId);

  WiFi.onEvent([](WiFiEvent_t event){ onWiFiEvent(g_data.comm, event, execute_command); });
  connectWiFiIfNeeded(g_data.comm);
  startDiscoveryRx(g_data.comm, execute_command);
  startBroadcastRx(g_data.comm, execute_command);
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  setup_buttons();
  setup_leds();
  setup_mp3();
  setup_wifi();
}

void loop() {
  connectWiFiIfNeeded(g_data.comm);

  if(WiFi.status() == WL_CONNECTED){
    static uint32_t lastTry = 0;
    if (!g_data.comm.udpDiscRx.connected() && millis() - lastTry > 2000) {
      lastTry = millis(); startDiscoveryRx(g_data.comm, execute_command);
    }
    if (!g_data.comm.udpDiscBcastRx.connected() && millis() - lastTry > 2000) {
      lastTry = millis(); startBroadcastRx(g_data.comm, execute_command);
    }
  }

  if(!g_data.comm.peerKnown){
    sendHelloIfNeeded(g_data.comm);
  }
  else {
    heartbeatTick(g_data.comm);
  }

  int yes_btn = !digitalRead(BTN_YES);
  int no_btn = !digitalRead(BTN_NO);

  if(yes_btn && g_data.is_active){
    handle_button_press(1);
  }
  else if(no_btn && g_data.is_active){
    handle_button_press(0);
  }

  delay(5);
}