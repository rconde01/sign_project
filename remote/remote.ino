#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>

#define BTN_SOPHIA 27
#define BTN_COFFEE 33
#define BTN_DOG 14
#define BTN_CAT 32

#define BACKLIGHT1_RED 22
#define BACKLIGHT1_GREEN 25
#define BACKLIGHT1_BLUE 26
#define BACKLIGHT2_RED 23
#define BACKLIGHT2_GREEN 5
#define BACKLIGHT2_BLUE 4
#define BACKLIGHT3_RED 16
#define BACKLIGHT3_GREEN 21
#define BACKLIGHT3_BLUE 17

#define LEDC_FREQ            5000    // 5 kHz PWM
#define LEDC_RESOLUTION      8       // 8-bit (0-255)

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

const char * ssid = "IronReverseSoulSteeler";
const char * password = "dekuposh84";
const char * RECEIVER_HOST = "sophia.signbox";
const char * REMOTE_HOST = "sophia.remote";
const uint16_t RECEIVER_PORT = 4210;
const uint16_t LOCAL_PORT = 4211;

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

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  // Give the USB/monitor some time to catch up
  delay(2000); 

  pinMode(BTN_SOPHIA, INPUT_PULLUP);
  pinMode(BTN_COFFEE, INPUT_PULLUP);
  pinMode(BTN_DOG, INPUT_PULLUP);
  pinMode(BTN_CAT, INPUT_PULLUP);

  ledcSetClockSource(LEDC_AUTO_CLK);

  ledcAttachChannel(BACKLIGHT1_RED,   LEDC_FREQ, LEDC_RESOLUTION, 0);
  ledcAttachChannel(BACKLIGHT1_GREEN, LEDC_FREQ, LEDC_RESOLUTION, 1);
  ledcAttachChannel(BACKLIGHT1_BLUE,  LEDC_FREQ, LEDC_RESOLUTION, 2);
  ledcAttachChannel(BACKLIGHT2_RED,   LEDC_FREQ, LEDC_RESOLUTION, 3);
  ledcAttachChannel(BACKLIGHT2_GREEN, LEDC_FREQ, LEDC_RESOLUTION, 4);
  ledcAttachChannel(BACKLIGHT2_BLUE,  LEDC_FREQ, LEDC_RESOLUTION, 5);
  ledcAttachChannel(BACKLIGHT3_RED,   LEDC_FREQ, LEDC_RESOLUTION, 6);
  ledcAttachChannel(BACKLIGHT3_GREEN, LEDC_FREQ, LEDC_RESOLUTION, 7);
  ledcAttachChannel(BACKLIGHT3_BLUE,  LEDC_FREQ, LEDC_RESOLUTION, 8);

  // It's weird but without this the writes below to shut off the leds don't work as expected
  // Some shut off and some don't
  delay(100);

  ledcWrite(BACKLIGHT1_RED,   255);
  ledcWrite(BACKLIGHT1_GREEN, 255);
  ledcWrite(BACKLIGHT1_BLUE,  255);
  ledcWrite(BACKLIGHT2_RED,   255);
  ledcWrite(BACKLIGHT2_GREEN, 255);
  ledcWrite(BACKLIGHT2_BLUE,  255);
  ledcWrite(BACKLIGHT3_RED,   255);
  ledcWrite(BACKLIGHT3_GREEN, 255);
  ledcWrite(BACKLIGHT3_BLUE,  255);

  connectWiFi();

  if (!udp.begin(LOCAL_PORT)) {
    Serial.println("UDP begin failed!");
  } else {
    Serial.print("UDP ready on local port "); Serial.println(LOCAL_PORT);
  }
}



bool sendMessageAndWaitAck(IPAddress dest, const char * msg, uint32_t timeout_ms){
  // Send
  if (udp.beginPacket(dest, RECEIVER_PORT) == 0)
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

TaskHandle_t pulseTaskHandle;

struct PulseData {
  int r;
  int g;
  int b;
};

PulseData pulse_data;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
bool pulsing = false;

void pulseColor(void * data){
  PulseData * pulse_data = (PulseData *)data;

  float rf = pulse_data->r / 255.0;
  float gf = pulse_data->g / 255.0;
  float bf = pulse_data->b / 255.0;

  int num_steps = 256;
  int delay_value = 2;

  for(int i = 0; i < num_steps; ++i){
    float step = 1.0f - (float)i / (float)(num_steps - 1);
    int red = 255 - (int)(rf*step*255);
    int green = 255 - (int)(gf*step*255);
    int blue = 255 - (int)(bf*step*255);

    ledcWrite(BACKLIGHT1_RED,   red);
    ledcWrite(BACKLIGHT1_GREEN, green);
    ledcWrite(BACKLIGHT1_BLUE,  blue);
    ledcWrite(BACKLIGHT2_RED,   red);
    ledcWrite(BACKLIGHT2_GREEN, green);
    ledcWrite(BACKLIGHT2_BLUE,  blue);
    ledcWrite(BACKLIGHT3_RED,   red);
    ledcWrite(BACKLIGHT3_GREEN, green);
    ledcWrite(BACKLIGHT3_BLUE,  blue);
    vTaskDelay(pdMS_TO_TICKS(delay_value));
  }  

  portENTER_CRITICAL(&mux);
  pulsing = false;
  portEXIT_CRITICAL(&mux);

  vTaskDelete(NULL);
}

void createPulseTask(int r, int g, int b){
  pulse_data.r = r;
  pulse_data.g = g;
  pulse_data.b = b;

  xTaskCreate(pulseColor, "pulse task", 2048, &pulse_data, 1, &pulseTaskHandle);
}


void loop() {
  // put your main code here, to run repeatedly:
  int sophia = !digitalRead(BTN_SOPHIA);
  int coffee = !digitalRead(BTN_COFFEE);
  int dog = !digitalRead(BTN_DOG);
  int cat = !digitalRead(BTN_CAT);

  portENTER_CRITICAL(&mux);
  auto is_pulsing = pulsing;
  portEXIT_CRITICAL(&mux);  

  if(sophia && !is_pulsing){
    Serial.println("button 1");
    portENTER_CRITICAL(&mux);
    pulsing = true;  
    portEXIT_CRITICAL(&mux);
    createPulseTask(purple_r, purple_g, purple_b);
  }
  else if(coffee && !is_pulsing){
    Serial.println("button 2");
    portENTER_CRITICAL(&mux);
    pulsing = true;  
    portEXIT_CRITICAL(&mux);
    createPulseTask(orange_r, orange_g, orange_b);
  }
  else if(dog && !is_pulsing){
    Serial.println("button 3");
    portENTER_CRITICAL(&mux);
    pulsing = true;  
    portEXIT_CRITICAL(&mux);  
    createPulseTask(blue_r, blue_g, blue_b);
  }
  else if(cat && !is_pulsing){
    Serial.println("button 4");
    portENTER_CRITICAL(&mux);
    pulsing = true;  
    portEXIT_CRITICAL(&mux);   
    createPulseTask(yellow_r, yellow_g, yellow_b);
  }    
}
