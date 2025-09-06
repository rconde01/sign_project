#include <cmath>
#include "action.hpp"
#include "../common/communication.hpp"

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

auto const yellow       = Color{255,255,0};
auto const blue         = Color{0,0,255};
auto const purple       = Color{255,0,255};
auto const orange       = Color{255,165,0};
auto const red          = Color{255,0,0};
auto const green        = Color{0,255,0};

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
  Data():
  pulse_data{},
  is_pulsing{false},
  send_message_task_handle{},
  pulse_color_task_handle{},
  comm{"sophia-remote"}{}

  PulseData       pulse_data;
  MyAtomic<bool>  is_pulsing;
  TaskHandle_t    send_message_task_handle;
  TaskHandle_t    pulse_color_task_handle;
  Communication   comm;
};

Data g_data{};

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

void send_button_message(void * data){
  auto button = reinterpret_cast<int>(data);

  switch(button){
  case 0:
    sendCmdToPeer(g_data.comm,"sophia");
    break;
  case 1:
    sendCmdToPeer(g_data.comm,"coffee");
    break;
  case 2:
    sendCmdToPeer(g_data.comm,"dog");
    break;
  case 3:
    sendCmdToPeer(g_data.comm,"cat");
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

void pulse_color(Color color){
  float rf = color.r / 255.0;
  float gf = color.g / 255.0;
  float bf = color.b / 255.0;

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
}

void long_pulse_color_blocking(Color color){
  int num_steps = 1024;
  int delay_value = 2;
  int num_cycles = 3;

  for(int i = 0; i < num_steps; ++i){
    float step = 1.0f - (float)i / (float)(num_steps - 1);

    auto level = 1.0f - (1.0f + cosf(num_cycles*step*2*M_PI))/2.0;

    int red = 255 - (int)(color.r*level);
    int green = 255 - (int)(color.g*level);
    int blue = 255 - (int)(color.b*level);

    ledcWrite(BACKLIGHT1_RED,   red);
    ledcWrite(BACKLIGHT1_GREEN, green);
    ledcWrite(BACKLIGHT1_BLUE,  blue);
    ledcWrite(BACKLIGHT2_RED,   red);
    ledcWrite(BACKLIGHT2_GREEN, green);
    ledcWrite(BACKLIGHT2_BLUE,  blue);
    ledcWrite(BACKLIGHT3_RED,   red);
    ledcWrite(BACKLIGHT3_GREEN, green);
    ledcWrite(BACKLIGHT3_BLUE,  blue);
    delay(delay_value);
  }
}

void pulse_color_task(void * data){
  auto pulse_data = reinterpret_cast<PulseData *>(data);

  pulse_color(pulse_data->color);

  vTaskDelete(NULL);
}

void create_pulse_task(Color color){
  g_data.pulse_data.color = color;

  xTaskCreate(
    pulse_color_task,
    "pulse task",
    2048,
    &g_data.pulse_data,
    1,
    &g_data.pulse_color_task_handle);
}

void execute_command(String cmd){
  if(cmd == "yes"){
    long_pulse_color_blocking(green);
  }
  else if(cmd == "no"){
    long_pulse_color_blocking(red);
  }
}

void setup_wifi(){
  g_data.comm.deviceId = macSuffix();
  logLine("BOOT", String(g_data.comm.role) + " " + g_data.comm.deviceId);

  WiFi.onEvent([](WiFiEvent_t event){ onWiFiEvent(g_data.comm, event, execute_command); });
  connectWiFiIfNeeded(g_data.comm);
  startDiscoveryRx(g_data.comm, execute_command);
  //startBroadcastRx(g_data.comm, execute_command);
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

void perform_action(Action action){
  switch(action){
  case Action::yes:
    long_pulse_color_blocking(green);
    break;
  case Action::no:
    long_pulse_color_blocking(red);
    break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  setup_buttons();
  setup_leds();
  setup_wifi();
}

void loop() {
  connectWiFiIfNeeded(g_data.comm);

  if(WiFi.status() == WL_CONNECTED){
    static uint32_t lastTry = 0;
    if (!g_data.comm.udpDiscRx.connected() && millis() - lastTry > 2000) {
      lastTry = millis(); startDiscoveryRx(g_data.comm, execute_command);
    }
    // if (!g_data.comm.udpDiscBcastRx.connected() && millis() - lastTry > 2000) {
    //   lastTry = millis(); startBroadcastRx(g_data.comm, execute_command);
    // }
  }

  if(!g_data.comm.peerKnown){
    sendHelloIfNeeded(g_data.comm);
  }
  else {
    heartbeatTick(g_data.comm);
  }

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
