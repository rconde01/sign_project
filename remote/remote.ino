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
  waiting_for_reply,
  yes,
  no,
  shutdown
};

enum class LightState {
  off,
  indicator
};

std::array<int,4> BTN_PINS = {BTN_SOPHIA, BTN_COFFEE, BTN_DOG, BTN_CAT};

struct Color {
  bool operator==(Color const & rhs) const = default;

  uint8_t r;
  uint8_t g;
  uint8_t b;
};

auto const yellow       = Color{255,255,0};
auto const blue         = Color{0,0,255};
auto const purple       = Color{255,0,255};
auto const orange       = Color{255,50,0};
auto const red          = Color{255,0,0};
auto const green        = Color{0,255,0};
auto const black        = Color{0,0,0};
auto const white        = Color{255,255,255};

struct Data {
  MyAtomic<LightState>  light_state{LightState::off};
  MyAtomic<Color>       current_color{white};
  MyAtomic<State>       state{State::idle};
  int                   active_command_button_index{-1};
  unsigned long         button_press_time{};
  bool                  esp_up{false};
};

void setup_buttons(){
  for(auto pin : BTN_PINS)
    pinMode(pin, INPUT_PULLUP);
}

void set_color(Data & data, Color color){
  if(color == data.current_color)
    return;

  ledcWrite(BACKLIGHT1_RED,   255 - color.r);
  ledcWrite(BACKLIGHT1_GREEN, 255 - color.g);
  ledcWrite(BACKLIGHT1_BLUE,  255 - color.b);
  ledcWrite(BACKLIGHT2_RED,   255 - color.r);
  ledcWrite(BACKLIGHT2_GREEN, 255 - color.g);
  ledcWrite(BACKLIGHT2_BLUE,  255 - color.b);
  ledcWrite(BACKLIGHT3_RED,   255 - color.r);
  ledcWrite(BACKLIGHT3_GREEN, 255 - color.g);
  ledcWrite(BACKLIGHT3_BLUE,  255 - color.b);
  
  data.current_color = color;
}

void setup_leds(Data & data){
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

  set_color(data, black);
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

void long_pulse_color_blocking(Data & data, Color color){
  int num_steps = 5*1024;
  int delay_value = 2;
  int num_cycles = 10;

  for(int i = 0; i < num_steps; ++i){
    float step = 1.0f - (float)i / (float)(num_steps - 1);

    auto level = 1.0f - (1.0f + cosf(num_cycles*step*2*M_PI))/2.0;

    int red = (int)(color.r*level);
    int green = (int)(color.g*level);
    int blue = (int)(color.b*level);

    set_color(data, Color{red,green,blue});

    delay(delay_value);
  }
}

bool handle_button_press(Data & data, int button_index){
  if(data.active_command_button_index != -1)
    return false; // already in a transaction

  auto color = get_button_color(button_index);
  set_color(data, color);

  data.active_command_button_index = button_index;
  data.button_press_time = millis();

  return true;
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
      return handle_button_press(data, i);
    }
  }

  return false;
}

Data g_data{};

void on_receive(const esp_now_recv_info_t* recv_info, const unsigned char* incomingData, int len){
  auto command = get_command(recv_info,incomingData,len);

  if(command){
    if(*command == "yes"){
      g_data.state = State::yes;
    }
    else if(*command == "no"){
      g_data.state = State::no;
    }
  }
}

void sleep(){
  if(g_data.esp_up)
    teardown_esp_now();

  esp_light_sleep_start();
}

void setup(){
  Serial.begin(115200);
  delay(2000);

  setCpuFrequencyMhz(80);
  Serial.printf("After: %d MHz\n", getCpuFrequencyMhz());

  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_ext1_wakeup(
    (1 << BTN_CAT) | (1 << BTN_COFFEE) | (1 << BTN_DOG) | (1 << BTN_SOPHIA),
    ESP_EXT1_WAKEUP_ANY_HIGH);

  setup_buttons();
  setup_leds(g_data);

  sleep();
}

void loop(){
  auto const int request_timeout_milliseconds = 90*1000;

  switch(g_data.state){
    case State::idle:
      if(pollButtons(g_data)){
        g_data.state = State::button_pressed;
      }
      break;

    case State::button_pressed:
      if(setup_esp_now(sign_mac,on_receive) && 
         send_button_command(g_data,g_data.active_command_button_index)){
        g_data.esp_up = true;
        g_data.state = State::waiting_for_reply;        
      }
      else {
        g_data.state = State::shutdown;
      }
      break;

    case State::waiting_for_reply:
      if(millis() - g_data.button_press_time > request_timeout_milliseconds){
        send_command(sign_mac, "disable");
        g_data.state = State::shutdown;
      }
      break;

    case State::yes:
      long_pulse_color_blocking(g_data, green);
      g_data.state = State::shutdown;
      break;

    case State::no:
      long_pulse_color_blocking(g_data, red);
      g_data.state = State::shutdown;
      break;

    case State::shutdown:
      Serial.println("Shutting Down");      
      g_data.active_command_button_index = -1;      
      set_color(g_data, black);
      g_data.light_state = LightState::off;
      g_data.state = State::idle;
      sleep();      
      break;
  }
}