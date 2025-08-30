#include <Adafruit_NeoPixel.h>

#define LED_PIN    4      // A5 = GPIO 4
#define NUM_LEDS   32    // change this to the total LEDs in all 4 strips
#define BRIGHTNESS 10     // 0-255

#define YES_BUTTON 33
#define NO_BUTTON 14

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show(); // Initialize all pixels to 'off'

  pinMode(YES_BUTTON, INPUT_PULLUP);
  pinMode(NO_BUTTON, INPUT_PULLUP);
}

void loop() {
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

  int yes = digitalRead(YES_BUTTON);
  int no = digitalRead(NO_BUTTON);
  if(!yes){
    Serial.println("Yes!");
  }

  if(!no){
    Serial.println("No!");
  }
}