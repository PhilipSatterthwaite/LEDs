/// @file    DemoReel100.ino
/// @brief   FastLED "100 lines of code" demo reel, showing off some effects
/// @example DemoReel100.ino

#include <FastLED.h>

FASTLED_USING_NAMESPACE

// FastLED "100-lines-of-code" demo reel, showing just a few 
// of the kinds of animation patterns you can quickly and easily 
// compose using FastLED.  
//
// This example also shows one easy way to define multiple 
// animations patterns and have them automatically rotate.
//
// -Mark Kriegsman, December 2014


#define DATA_PIN    5
//#define CLK_PIN   4
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define NUM_LEDS    100
//CRGB leds[NUM_LEDS];
CRGB* leds;

#define BRIGHTNESS          15 //don't set over 150: 10,000 mA/300 LED = 33.3 mA -- > < 170 brightness (keep it below 150 for safety): note, setting above 50 will cause color to fade over length of strip


//unsigned long lastUpdate = 0; // Variable to track the last update time
//unsigned long updateInterval = 1000; // Interval in milliseconds (1 second)


void setup() {
  delay(3000); // 3 second delay for recovery
  
  // tell FastLED about the LED strip configuration
  //FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  //FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  
  
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  
   //for (int i = 0; i < NUM_LEDS; i++) {
    //leds[i] = CRGB(255, 0, 0);
  //}

  
  for (int i = 0; i < 600; i++) {
    leds[i] = CRGB(255, 0, 0);
  }
  leds[500] = CRGB(255, 0, 0);
  /*
  for (int i = 0; i < 300; i++) {
    leds[i] = CRGB(255, 0, 0);
  }
  */
  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);
}



void loop()
{
  // Call the current pattern function once, updating the 'leds' array
  //gPatterns[gCurrentPatternNumber]();

  // send the 'leds' array out to the actual LED strip
  FastLED.show();  
  // insert a delay to keep the framerate modest
  FastLED.delay(1000); 

  // do some periodic updates
  //EVERY_N_MILLISECONDS( 20 ) { gHue++; } // slowly cycle the "base color" through the rainbow
  //EVERY_N_SECONDS( 10 ) { nextPattern(); } // change patterns periodically
  //warm();
}


void warm() {
  // Adjust these values to achieve the desired warm white color
  CRGB warmWhiteColor = CRGB(255, 162, 57);  // Red, Green, Blue
  //CRGB warmWhiteColor = CRGB(255, 255, 255);
  
  // Check if it's time to update the LEDs
  /*
  if (millis() - lastUpdate >= updateInterval) {
    // Update the last update time
    lastUpdate = millis();
  // Set all LEDs to the randomly adjusted warm white color
    */
  for (int i = 0; i < NUM_LEDS; i++) {
   
    // Set all LEDs to the warm white color

  int red = warmWhiteColor.r + random(-20, 21);
  int green = warmWhiteColor.g + random(-20, 21);
  int blue = warmWhiteColor.b + random(-20, 21);

  // Ensure the adjusted values stay within the valid range (0-255)
  red = constrain(red, 0, 255);
  green = constrain(green, 0, 255);
  blue = constrain(blue, 0, 255);

    leds[i] = CRGB(red, green, blue);
  
  }

  //FastLED.show();  // Update the LED strip with the new color
}