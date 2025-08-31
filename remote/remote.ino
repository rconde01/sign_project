#include <WiFi.h>
#include <ESPmDNS.h> // Or <MDNS.h> for ESP8266
#include <WiFiUdp.h>

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
auto const local_port   = uint16_t{4211};
auto const yellow       = Color{255,255,0};
auto const blue         = Color{0,0,255};
auto const purple       = Color{255,0,255};
auto const orange       = Color{255,165,0};

struct PulseData {
  Color color{};
};

class FrtosMutex {
public:
  FrtosMutex(){
    m_mux = portMUX_INITIALIZER_UNLOCKED;
  }

  void lock(){
    portENTER_CRITICAL(&m_mux);
  }

  void unlock(){
    portEXIT_CRITICAL(&m_mux);
  }
private:
  portMUX_TYPE m_mux;
};

class FrtosMutexLock {
public:
  FrtosMutexLock(FrtosMutex & mutex):m_mutex{mutex}{
    m_mutex.lock();
  }

  ~FrtosMutexLock(){
    m_mutex.unlock();
  }

private:
  FrtosMutex & m_mutex;
};

template<typename T>
class MyAtomic {
public:
  MyAtomic(T value):m_mutex{},m_value{value}{}

  MyAtomic & operator=(T value){
    FrtosMutexLock lock(m_mutex);
    m_value = value;

    return *this;
  }

  operator T() const {
    FrtosMutexLock lock(m_mutex);
    return m_value;
  }

private:
  mutable FrtosMutex m_mutex;
  T m_value;
};

struct Data {
  WiFiUDP udp{};
  IPAddress sign_ip{};
  PulseData pulse_data{};
  MyAtomic<bool> is_pulsing{false};
  TaskHandle_t send_message_task_handle{};
  TaskHandle_t pulse_color_task_handle{};
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

void setup_buttons(){
  pinMode(BTN_SOPHIA, INPUT_PULLUP);
  pinMode(BTN_COFFEE, INPUT_PULLUP);
  pinMode(BTN_DOG, INPUT_PULLUP);
  pinMode(BTN_CAT, INPUT_PULLUP);
}

void setup_leds(){
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
}

void setup_mdns() {
  // Start mDNS responder (optional, but good for self-discovery)
  if (!MDNS.begin(remote_host)) {
    Serial.println("Error starting mDNS responder!");
  } else {
    Serial.println("mDNS responder started");
  }

  Serial.print("Attempting to resolve ");
  Serial.print(sign_host);
  Serial.println(".local...");

  g_data.sign_ip = MDNS.queryHost(sign_host);

  if (static_cast<uint32_t>(g_data.sign_ip) != 0) {
    Serial.print("Resolved ");
    Serial.print(sign_host);
    Serial.print(".local to: ");
    Serial.println(g_data.sign_ip.toString());
  } else {
    Serial.print("Failed to resolve ");
    Serial.print(sign_host);
    Serial.println(".local");
  }
}

void setup_udp(){
  if (!g_data.udp.begin(local_port)) {
    Serial.println("UDP begin failed!");
  } else {
    Serial.print("UDP ready on local port ");
    Serial.println(local_port);
  }
}

bool send_message_and_wait_ack(IPAddress dest, const char * msg, uint32_t timeout_ms = 400){
  auto & udp = g_data.udp;

  // Send
  if (udp.beginPacket(dest, sign_port) == 0)
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

void send_button_message(void * data){
  auto button = reinterpret_cast<int>(data);
  auto & sign_ip = g_data.sign_ip;

  switch(button){
  case 0:
    send_message_and_wait_ack(sign_ip, "sophia");
    break;
  case 1:
    send_message_and_wait_ack(sign_ip, "coffee");
    break;
  case 2:
    send_message_and_wait_ack(sign_ip, "dog");
    break;
  case 3:
    send_message_and_wait_ack(sign_ip, "cat");
    break;
  }

  vTaskDelete(NULL);
}


void create_send_message_task(int button){
  xTaskCreate(
    send_button_message,
    "send message task",
    4096,
    reinterpret_cast<void*>(button),
    1,
    &g_data.send_message_task_handle);
}

void pulse_color(void * data){
  auto pulse_data = reinterpret_cast<PulseData *>(data);

  float rf = pulse_data->color.r / 255.0;
  float gf = pulse_data->color.g / 255.0;
  float bf = pulse_data->color.b / 255.0;

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

  g_data.is_pulsing = false;

  vTaskDelete(NULL);
}

void create_pulse_task(Color color){
  g_data.pulse_data.color = color;

  xTaskCreate(pulse_color, "pulse task", 2048, &g_data.pulse_data, 1, &g_data.pulse_color_task_handle);
}

void setup() {
  Serial.begin(115200);

  setup_buttons();
  setup_leds();
  connect_wifi();
  setup_mdns();
  // For whatever reason, we need to do this after mdns or we'll get a crash
  setup_udp();
}

void handle_button(int num, Color color){
  Serial.printf("button %d\n", num);
  g_data.is_pulsing = true;
  Serial.println("pulse");
  create_pulse_task(color);
  Serial.println("send message");
  create_send_message_task(num);
  Serial.println("done");
}

void loop() {
  // put your main code here, to run repeatedly:
  int sophia = !digitalRead(BTN_SOPHIA);
  int coffee = !digitalRead(BTN_COFFEE);
  int dog = !digitalRead(BTN_DOG);
  int cat = !digitalRead(BTN_CAT);

  bool is_pulsing = g_data.is_pulsing;

  if(sophia && !is_pulsing){
    handle_button(0, purple);
  }
  else if(coffee && !is_pulsing){
    handle_button(1, orange);
  }
  else if(dog && !is_pulsing){
    handle_button(2, blue);
  }
  else if(cat && !is_pulsing){
    handle_button(3, yellow);
  }    
}
