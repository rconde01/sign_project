#include <Adafruit_NeoPixel.h>

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
};

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

Data g_data{};

void on_receive(const esp_now_recv_info_t* recv_info, const unsigned char* incomingData, int len){
  auto command = get_command(recv_info,incomingData,len);

  if(command){
    execute_command(g_data,*command);
  }
}

void setup(){
  Serial.begin(115200);
  delay(2000);

  setup_buttons();
  setup_leds(g_data);
  setup_mp3(g_data);

  setup_esp_now(remote_mac,on_receive);
}

void loop(){
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
