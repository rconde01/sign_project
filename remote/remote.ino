#include <dummy.h>

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

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
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
}

void fadeColor(int r, int g, int b){
  float rf = r / 255.0;
  float gf = g / 255.0;
  float bf = b / 255.0;

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
    delay(delay_value);
  }  
}

void loop() {
  // put your main code here, to run repeatedly:
  int sophia = !digitalRead(BTN_SOPHIA);
  int coffee = !digitalRead(BTN_COFFEE);
  int dog = !digitalRead(BTN_DOG);
  int cat = !digitalRead(BTN_CAT);

  if(sophia){
    Serial.println("button 1");
    fadeColor(purple_r, purple_g, purple_b);
  }

  if(coffee){
    Serial.println("button 2");
    fadeColor(orange_r, orange_g, orange_b);
  }

  if(dog){
    Serial.println("button 3");
    fadeColor(blue_r, blue_g, blue_b);
  }

  if(cat){
    Serial.println("button 4");
    fadeColor(yellow_r, yellow_g, yellow_b);
  }    
}
