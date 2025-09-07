#include <Adafruit_NeoPixel.h>
#include <ESPmDNS.h>

#include "action.hpp"
#include "mp3.hpp"
#include "../common/communication.hpp"

#define LED_PIN    4      // A5 = GPIO 4
#define NUM_LEDS   32
#define BRIGHTNESS 200     // 0-255

#define BTN_YES 14
#define BTN_NO 33

#define PIN_RX 16
#define PIN_TX 17

#define CAT_MP3 2
#define DOG_MP3 4
#define COFFEE_MP3 5
#define SOPHIA_MP3 1

#define COFFEE_SIGN 0
#define DOG_SIGN 1
#define CAT_SIGN 2
#define SOPHIA_SIGN 3

WiFiServer server(TCP_PORT);
static const int MAX_CLIENTS = 4;
WiFiClient clients[MAX_CLIENTS];

const int LED_PINS[] = {22,25,26, 23,5,4, 16,21,17};

bool readLine(WiFiClient& c, String& out) {
  while (c.available()) { char ch=(char)c.read(); if (ch=='\n') return true; if (ch!='\r') out+=ch; }
  return false;
}
bool sendLine(WiFiClient& c, const String& s) {
  if (!c.connected()) return false;
  size_t n = c.print(s); n += c.print("\n"); return n==(s.length()+1);
}

void handleCommand(WiFiClient& c, const String& line) {
  if (line.startsWith("PING ")) { sendLine(c, "PONG " + line.substring(5)); return; }
  if (line.startsWith("CMD ")) {
    // demo behavior — replace with real sign logic
    if (line.indexOf("LIGHT")>=0) digitalWrite(LED_PINS[0], HIGH);
    if (line.indexOf("OFF")  >=0) digitalWrite(LED_PINS[0], LOW);
    sendLine(c, "ACK " + line.substring(4));
  }
}

void onWiFiEvent(WiFiEvent_t e){
  if (e==ARDUINO_EVENT_WIFI_STA_GOT_IP){
    logLine("WiFi", "GOT_IP "+WiFi.localIP().toString());
    if (MDNS.begin(SIGN_HOSTNAME)) {
      MDNS.addService("sign","tcp",TCP_PORT); // _sign._tcp
      logLine("MDNS", "sign.local up");
    } else logLine("MDNS","begin failed");
  } else if (e==ARDUINO_EVENT_WIFI_STA_DISCONNECTED){
    logLine("WiFi","DISCONNECTED");
  }
}

void setup(){
  Serial.begin(115200); delay(150);
  logLine("BOOT","SIGN id="+deviceId());
  for (int i=0;i<9;i++){ pinMode(LED_PINS[i],OUTPUT); digitalWrite(LED_PINS[i],LOW); }

  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  server.begin(); server.setNoDelay(true);
  logLine("TCP", "Server listening :"+String(TCP_PORT));
}

void loop(){
  // Accept new client
  if (WiFiClient inc = server.available()){
    inc.setNoDelay(true);
    int idx=-1; for (int i=0;i<MAX_CLIENTS;i++){ if (!clients[i] || !clients[i].connected()){ idx=i; break; } }
    if (idx>=0){ clients[idx]=inc; logLine("TCP","client#"+String(idx)+" from "+inc.remoteIP().toString());
                 sendLine(clients[idx],"HELLO SIGN"); }
    else inc.stop();
  }
  // Service existing
  for (int i=0;i<MAX_CLIENTS;i++){
    auto &c = clients[i];
    if (!c || !c.connected()) continue;
    String line;
    while (readLine(c,line)){ handleCommand(c,line); line=""; }
  }
  delay(2);
}

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
  comm{4210,4211,"sophia-sign","sophia-remote"},
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
    send_message_and_wait_ack(g_data.comm, "no");
  else if(button == 1)
    send_message_and_wait_ack(g_data.comm, "yes");

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

void setup() {
  Serial.begin(115200);
  delay(2000);

  setup_buttons();
  setup_leds();
  setup_mp3();
  setup_wifi(g_data.comm);
}

void loop() {
  if(!g_data.comm.mdns_initialized)
    return;

  int yes_btn = !digitalRead(BTN_YES);
  int no_btn = !digitalRead(BTN_NO);

  listen_for_message(g_data.comm, execute_command);

  if(yes_btn){
    handle_button_press(1);
  }
  else if(no_btn){
    handle_button_press(0);
  }
}