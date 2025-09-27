#include <cmath>
#include <array>

#include "action.hpp"
#include "../common/communication.hpp"
#include "../common/atomic.hpp"

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

// Linger time on the REMOTE after a press (ms)
static const uint32_t REMOTE_LINGER_MS = 120000;  // 2 minutes
// If no data at all for this long, the REMOTE closes early
static const uint32_t REMOTE_INACTIVITY_MS = 120000; // 2 minutes

enum class State {
  idle,
  button_pressed,
  waiting_for_reply
};

std::array<int,4> BTN_PINS = {BTN_SOPHIA, BTN_COFFEE, BTN_DOG, BTN_CAT};

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

struct Data {
  PulseData       pulse_data{};
  MyAtomic<bool>  is_pulsing{false};
  TaskHandle_t    send_message_task_handle{};
  TaskHandle_t    pulse_color_task_handle{};
  State           state{State::idle};
  
  int             active_command_button_index{-1};
  std::array<uint32_t,BTN_PINS.size()> lastEdge{0,0,0,0};
  std::array<uint32_t,BTN_PINS.size()> lastState{0,0,0,0};
};

void setup_buttons(){
  for(auto pin : BTN_PINS)
    pinMode(pin, INPUT_PULLUP);
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

Color get_button_color(int button){
  switch(button){
  case 0:
    return purple;
  case 1:
    return orange;
  case 2:
    return blue;
  case 3:
    return yellow;
  default:
    return {0,0,0};
  }
}

void set_color(int red, int green, int blue){
  ledcWrite(BACKLIGHT1_RED,   red);
  ledcWrite(BACKLIGHT1_GREEN, green);
  ledcWrite(BACKLIGHT1_BLUE,  blue);
  ledcWrite(BACKLIGHT2_RED,   red);
  ledcWrite(BACKLIGHT2_GREEN, green);
  ledcWrite(BACKLIGHT2_BLUE,  blue);
  ledcWrite(BACKLIGHT3_RED,   red);
  ledcWrite(BACKLIGHT3_GREEN, green);
  ledcWrite(BACKLIGHT3_BLUE,  blue);
}

void pulse_color(Data & data, Color color){
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

    set_color(red,green,blue);

    vTaskDelay(pdMS_TO_TICKS(delay_value));
  }

  data.is_pulsing = false;
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
  auto the_data = reinterpret_cast<Data *>(data);

  pulse_color(*the_data, the_data->pulse_data.color);

  vTaskDelete(NULL);
}

void create_pulse_task(Data & data, Color color){
  data.pulse_data.color = color;

  xTaskCreate(
    pulse_color_task,
    "pulse task",
    2048,
    &data,
    1,
    &data.pulse_color_task_handle);
}

void handle_button_press(Data & data, int button_index){
  if(data.active_command_button_index != -1)
    return; // already in a transaction

  data.is_pulsing = true;
  auto color = get_button_color(button_index);
  create_pulse_task(data, color);

  data.active_command_button_index = button_index;
}

bool send_button_command(Data & data, int button_index){
  Serial.println(("Sending Button Commmand"));

  if (button_index < 0 || button_index >= BTN_PINS.size()){
    Serial.println(("Sending Button Commmand: Failure: Unknown Button"));
    return false;
  }

  String cmd;
  switch(button_index){
  case 0:
    cmd = "coffee";
    break;
  case 1:
    cmd = "sophia";
    break;
  case 2:
    cmd = "dog";
    break;
  case 3:
    cmd = "cat";
    break;
  default:
    Serial.println(("Sending Button Commmand: Failure: Unknown Button"));
    return false;
  }

  return send_command(sign_mac, cmd);
}

bool pollButtons(Data & data){
  uint32_t now = millis();

  for (int i = 0; i < BTN_PINS.size(); i++){
    auto s = digitalRead(BTN_PINS[i]) == LOW;

    if (s){ // pressed
      handle_button_press(data, i);
      return true;
    }
  }

  return false;
}

Data g_data{};

void on_receive(const esp_now_recv_info_t* recv_info, const unsigned char* incomingData, int len){
  auto command = get_command(recv_info,incomingData,len);

  if(command){

  }
}

void setup(){
  Serial.begin(115200);
  delay(2000);

  setup_buttons();
  setup_leds();

  setup_esp_now(sign_mac,on_receive);
}

void loop(){
  switch(g_data.state){
    case State::idle:
      if(pollButtons(g_data)){
        g_data.state = State::button_pressed;
      }
      break;

    case State::button_pressed:
      if(send_button_command(g_data,g_data.active_command_button_index)){
        g_data.state = State::waiting_for_reply;
      }
      break;

    case State::waiting_for_reply:
      if(!g_data.is_pulsing){
        auto button_color = get_button_color(g_data.active_command_button_index);
        set_color(button_color.r,button_color.g,button_color.b);
      }
      g_data.state = State::idle;
      break;
  }
}