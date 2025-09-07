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
  HardwareSerial    mp3{1};
  Adafruit_NeoPixel strip{NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800};
  WiFiServer        server{TCP_PORT};
  WiFiClient        client{};
};

void onWiFiEvent(WiFiEvent_t event) {
  Serial.printf("[WiFi-event] event: %d\n", event);

  switch (event) {
    case ARDUINO_EVENT_WIFI_READY:               Serial.println("WiFi interface ready"); break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:           Serial.println("Completed scan for access points"); break;
    case ARDUINO_EVENT_WIFI_STA_START:           Serial.println("WiFi client started"); break;
    case ARDUINO_EVENT_WIFI_STA_STOP:            Serial.println("WiFi clients stopped"); break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:       Serial.println("Connected to access point"); break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:    Serial.println("Disconnected from WiFi access point"); break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE: Serial.println("Authentication mode of access point has changed"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      logLine("WiFi", "GOT_IP " + WiFi.localIP().toString());

      if (MDNS.begin(SIGN_HOSTNAME)) {
        MDNS.addService("sign","tcp",TCP_PORT); // _sign._tcp
        logLine("MDNS", "sign.local up");
      }
      else {
        logLine("MDNS","begin failed");
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:        Serial.println("Lost IP address and IP address is reset to 0"); break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:          Serial.println("WiFi Protected Setup (WPS): succeeded in enrollee mode"); break;
    case ARDUINO_EVENT_WPS_ER_FAILED:           Serial.println("WiFi Protected Setup (WPS): failed in enrollee mode"); break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:          Serial.println("WiFi Protected Setup (WPS): timeout in enrollee mode"); break;
    case ARDUINO_EVENT_WPS_ER_PIN:              Serial.println("WiFi Protected Setup (WPS): pin code in enrollee mode"); break;
    case ARDUINO_EVENT_WIFI_AP_START:           Serial.println("WiFi access point started"); break;
    case ARDUINO_EVENT_WIFI_AP_STOP:            Serial.println("WiFi access point  stopped"); break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:    Serial.println("Client connected"); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: Serial.println("Client disconnected"); break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:   Serial.println("Assigned IP address to client"); break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:  Serial.println("Received probe request"); break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6:         Serial.println("AP IPv6 is preferred"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:        Serial.println("STA IPv6 is preferred"); break;
    case ARDUINO_EVENT_ETH_GOT_IP6:             Serial.println("Ethernet IPv6 is preferred"); break;
    case ARDUINO_EVENT_ETH_START:               Serial.println("Ethernet started"); break;
    case ARDUINO_EVENT_ETH_STOP:                Serial.println("Ethernet stopped"); break;
    case ARDUINO_EVENT_ETH_CONNECTED:           Serial.println("Ethernet connected"); break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:        Serial.println("Ethernet disconnected"); break;
    case ARDUINO_EVENT_ETH_GOT_IP:              Serial.println("Obtained IP address"); break;
    default:                                    break;
  }
}

void setup_wifi(){
  WiFi.onEvent(onWiFiEvent);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void setup_server(Data & data){
  data.server.begin();
  data.server.setNoDelay(true);
  logLine("TCP", "Server listening :" + String(TCP_PORT));
}

void setup_buttons(){
  pinMode(BTN_YES, INPUT_PULLUP);
  pinMode(BTN_NO, INPUT_PULLUP);
}

void setup_leds(Data & data){
  data.strip.begin();
  data.strip.setBrightness(BRIGHTNESS);
  data.strip.show(); // Initialize all pixels to 'off'
}

void setup_mp3(Data & data){
  data.mp3.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX);
  mp3SetVolume(data.mp3, 25);
}

void light_sign(Data & data, int sign_number){
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
      data.strip.fill(data.strip.Color(color.r,color.g,color.b),i,1);
    }
    else {
      data.strip.fill(data.strip.Color(0,0,0),i,1);
    }
  }

  data.strip.show();
}

void execute_command(Data & data, String cmd){
  if(cmd == "cat"){
    light_sign(data, CAT_SIGN);
    mp3PlayIndex(data.mp3,  CAT_MP3);
  }
  else if(cmd == "dog"){
    light_sign(data, DOG_SIGN);
    mp3PlayIndex(data.mp3,  DOG_MP3);
  }
  else if(cmd == "sophia"){
    light_sign(data, SOPHIA_SIGN);
    mp3PlayIndex(data.mp3,  SOPHIA_MP3);
  }
  else if(cmd == "coffee"){
    light_sign(data, COFFEE_SIGN);
    mp3PlayIndex(data.mp3,  COFFEE_MP3);
  }
}

void handleCommand(Data & data, const String& line) {
  logLine("CMD", line);

  if (line.startsWith("PING ")) {
    sendLine(data.client, "PONG " + line.substring(5));
    return;
  }

  if (line.startsWith("CMD ")) {
    auto cmd = line.substring(4);

    execute_command(data, cmd);

    sendLine(data.client, "ACK " + line.substring(4));
  }
}

Data g_data{};

void setup(){
  Serial.begin(115200);
  delay(2000);

  logLine("BOOT","SIGN id=" + deviceId());

  setup_buttons();
  setup_leds(g_data);
  setup_mp3(g_data);
  setup_wifi();
  setup_server(g_data);
}

void loop(){
  // Accept new client
  if (WiFiClient inc = g_data.server.available()){
    inc.setNoDelay(true);

    if (!g_data.client || !g_data.client.connected()) {
      logLine("TCP", " from " + inc.remoteIP().toString());
    }
    else {
      inc.stop();
      logLine("TCP", "rejected client from " + inc.remoteIP().toString() + " (busy)");
      return;
    }
  }

  if(g_data.client && g_data.client.connected()){
    String line;
    while (readLine(g_data.client,line)){
      handleCommand(g_data,line);
      line="";
    }
  }

  int yes_btn = !digitalRead(BTN_YES);
  int no_btn = !digitalRead(BTN_NO);  

  // if(yes_btn){
  //   handle_button_press(1);
  // }
  // else if(no_btn){
  //   handle_button_press(0);
  // }  

  delay(2);
}


// void handle_button_press(int button){
//   if(button == 0)
//     send_message_and_wait_ack(g_data.comm, "no");
//   else if(button == 1)
//     send_message_and_wait_ack(g_data.comm, "yes");

//   light_sign(-1);
//   g_data.is_active = false;
// }
