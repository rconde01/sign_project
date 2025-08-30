#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>

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
const char * RECEIVER_HOST = "sophia.signbox";
const char * REMOTE_HOST = "sophia.remote";
const uint16_t LISTEN_PORT = 4210;

WiFiUDP udp;

void connectWiFi() {
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

void setupMDNS() {
  if (MDNS.begin(RECEIVER_HOST)) {    // reachable as signbox.local
    Serial.println("mDNS responder started: signbox.local");
    // Optional: advertise a UDP service for discovery tools
    MDNS.addService("msg", "udp", LISTEN_PORT);
  } else {
    Serial.println("mDNS start failed.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  connectWiFi();
  setupMDNS();

  if (udp.begin(LISTEN_PORT)) {
    Serial.print("Listening UDP on port ");
    Serial.println(LISTEN_PORT);
  } else {
    Serial.println("UDP begin failed!");
  }  

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show(); // Initialize all pixels to 'off'

  pinMode(YES_BUTTON, INPUT_PULLUP);
  pinMode(NO_BUTTON, INPUT_PULLUP);
}

enum class Action {
  unknown,
  cat,
  dog,
  sophia,
  coffee
}

Action getAction(const char * message){
  if (strcmp(buf, "cat") == 0)
    return Action::cat;

  if (strcmp(buf, "dog") == 0)
    return Action::cat;

  if (strcmp(buf, "sophia") == 0)
    return Action::cat;

  if (strcmp(buf, "coffee") == 0)
    return Action::coffee;

  return Action::unknown;    
}

void performAction(Action action){
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

void listenForMessage(){
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char buf[128];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len < 0)
      return;
    buf[len] = '\0';

    IPAddress fromIP = udp.remoteIP();
    uint16_t fromPort = udp.remotePort();
    Serial.printf("RX from %s:%u -> \"%s\"\n",
                  fromIP.toString().c_str(), fromPort, buf);

    auto action = getAction(buf);

    // Send ACK back to sender (same port it used)
    udp.beginPacket(fromIP, fromPort);
    udp.print("ACK:");
    udp.print(buf);
    udp.endPacket();

    performAction(action);
  }
}

void loop() {
  listenForMessage();

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