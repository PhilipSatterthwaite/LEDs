#include <FastLED.h>
#include <IRremote.h>
//#include <ArduinoSTL.h> // Include the ArduinoSTL library
//#include <map>

// Define the IR receiver pin
const int IR_PIN = 2;

// Define the IR receiver object
IRrecv irrecv(IR_PIN);

// Function pointer type for IR functions
typedef void (*IRFunction)();


#define IR_RECEIVE_PIN 2
#define IR_POWER 64
#define IR_BRIGHTNESS_UP 92
#define IR_BRIGHTNESS_DOWN 93
#define IR_RED 88
#define IR_GREEN 89
#define IR_BLUE 69
#define IR_WHITE 68

#define IR_ORANGE 84
#define IR_LESS_ORANGE 80
#define IR_YELLOW_ORANGE 28
#define IR_YELLOW 24

#define IR_LIGHT_GREEN 85
#define IR_LIGHT_BLUE 81
#define IR_AQUA 29
#define IR_COBALT 25

#define IR_LIGHTER_BLUE 73
#define IR_PURPLE 77
#define IR_MAGENTA 30
#define IR_PINK 26

#define IR_WARM_1 72
#define IR_WARM_2 76
#define IR_COLD_1 31
#define IR_COLD_2 27

#define IR_RED_UP 20
#define IR_RED_DOWN 16
#define IR_GREEN_UP 21
#define IR_GREEN_DOWN 17
#define IR_BLUE_UP 22
#define IR_BLUE_DOWN 18

#define IR_QUICK 23
#define IR_SLOW 19
#define IR_DIY1 12
#define IR_DIY2 13
#define IR_DIY3 14
#define IR_DIY4 8
#define IR_DIY5 9
#define IR_DIY6 10

#define IR_AUTO 15
#define IR_FLASH 11
#define IR_JUMP3 4
#define IR_JUMP7 5
#define IR_FADE3 6
#define IR_FADE7 7


#define DATA_PIN    5
//#define CLK_PIN   4
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define NUM_LEDS    900
CRGB leds[NUM_LEDS];

#define BRIGHTNESS          50 //don't set over 150: 10,000 mA/300 LED = 33.3 mA -- > < 170 brightness (keep it below 150 for safety): note, setting above 50 will cause color to fade over length of strip

#define FRAMES_PER_SECOND  1

int CURR_BRIGHTNESS   =    15;
String receivedSignal = "";
decode_results results;

void setup() {
  
  delay(3000); // 3 second delay for recovery
  
  Serial.begin(9600);
  IrReceiver.begin(IR_RECEIVE_PIN);
  
  // tell FastLED about the LED strip configuration
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  //FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);
  rainbow();
  FastLED.show();
}

uint8_t gHue = 0; // rotating "base color" used by many of the patterns

void loop() {
  
  if (IrReceiver.decode()) {
    int command = IrReceiver.decodedIRData.command;
    Serial.println(command);
    delay(100);
    switch (command) {
      case IR_POWER: {
        Serial.println("power");
        if (CURR_BRIGHTNESS == 0) {
        CURR_BRIGHTNESS = BRIGHTNESS;
        }
        else{
          CURR_BRIGHTNESS = 0;
        }
        FastLED.setBrightness(CURR_BRIGHTNESS);
        FastLED.show();
        break;
      }

      case IR_BRIGHTNESS_UP: {
        if (CURR_BRIGHTNESS < BRIGHTNESS-9){
          CURR_BRIGHTNESS += 10;
          FastLED.setBrightness(CURR_BRIGHTNESS);
          FastLED.show();
        }
        break;
      }

      case IR_BRIGHTNESS_DOWN: {
        if (CURR_BRIGHTNESS > 9){
          CURR_BRIGHTNESS -= 10;
          FastLED.setBrightness(CURR_BRIGHTNESS);
          FastLED.show();
        }
        break;
      }

      case IR_RED: {
        red();
        FastLED.show();
        break;
      }

      case IR_GREEN: {
        green();
        FastLED.show();
        break;
      }

      case IR_BLUE: {
        blue();
        FastLED.show();
        break;
      }

      case IR_WHITE: {
        white();
        FastLED.show();
        break;
      }

      case IR_LIGHT_GREEN: {
        lightGreen();
        FastLED.show();
        break;
      }

      case IR_LIGHT_BLUE: {
        lightBlue();
        FastLED.show();
        break;
      }

      case IR_AQUA: {
        aqua();
        FastLED.show();
        break;
      }

      case IR_YELLOW: {
        yellow();
        FastLED.show();
        break;
      }

      case IR_PURPLE: {
        purple();
        FastLED.show();
        break;
      }

      case IR_COBALT: {
        cobalt();
        FastLED.show();
        break;
      }


      case IR_DIY1: {
        rainbow();
        FastLED.show();
        break;
      }

      case IR_DIY2: {
        crossword();
        FastLED.show();
        break;
      }

      case IR_WARM_1: {
        warm();
        FastLED.show();
        break;
      }

      default: {
        Serial.println("Button not recognized");
      }
    }
    IrReceiver.resume();
  }
  /*
  if (IrReceiver.decode()) {
    IrReceiver.resume();
    Serial.println(IrReceiver.decodedIRData.command);
  }
  */
  
  // send the 'leds' array out to the actual LED strip
  //FastLED.show();  
  // insert a delay to keep the framerate modest
  //FastLED.delay(1000/FRAMES_PER_SECOND);
  
  //handleState();
  // do some periodic updates
  //EVERY_N_MILLISECONDS( 20 ) { gHue++; } // slowly cycle the "base color" through the rainbow
  
}

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))


void rainbow() {
  // FastLED's built-in rainbow generator
  fill_rainbow( leds, NUM_LEDS, gHue, 7);
}

void warm() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = leds[i] = CRGB(255, 162, 57); // warm
  }
}



//////////////////////////////////
void red() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = leds[i] = CRGB(255, 0, 0); // cool teal
  }
}

void green() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = leds[i] = CRGB(0, 255, 0); // cool teal
  }
}

void blue() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = leds[i] = CRGB(0, 0, 255); // cool teal
  }
}

void white() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = leds[i] = CRGB(255, 255, 255); // cool teal
  }
}

void lightGreen() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(34,139,34); // Light Green
  }
}

void lightBlue() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(0, 255, 255); // Aqua
  }
}

void aqua() {
  for (int i = 0; i < NUM_LEDS; i++) {
    
    leds[i] = CRGB(0,191,255); // Light Blue
  }
}

void cobalt() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(0, 71, 171); // Teal
  }
}

void purple() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = leds[i] = CRGB(95, 0, 160); // cool teal
  }
}

void yellow() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(255,234,0); // Yellow
  }
}



void crossword() {
  int whitecounter = 0;
  int blackcounter = 0;
  int whitelength = 3;
  int blacklength = 15;
  for (int i = 0; i < NUM_LEDS; i++) { 
    
    if (whitecounter < whitelength){
      leds[i] = leds[i] = CRGB(255, 255, 255); // White
    } else {
      leds[i] = leds[i] = CRGB(0, 0, 0); // Black
    }
      
    if (whitecounter < whitelength){
      whitecounter += 1;
    }
    else if (blackcounter < blacklength) {
      blackcounter += 1;
    }
    else {
      blackcounter = 0;
      whitecounter = 0;
    }
  
    

  }
}

/*
void cool blue() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = leds[i] = CRGB(0, 162, 57); // cool teal
  }
}

Power: 64
Brightness up: 93
Brightness down: 92
Red: 88
Green: 89
Blue: 69
White: 68

Orange: 84
Less orange: 80
yellow orange: 28
yellow: 24

light green: 85
light blue: 81
aqua: 29
cobalt: 25

lighter blue: 73
purple: 77
magenta: 30
pink: 26

warm 1: 72
warm 2: 76
cold 1: 31
cold 2: 27

Red up: 20
red down: 16
green up: 21
green down: 17
blue up: 22
blue down: 18

quick: 23
slow: 19
DIY1: 12
DIY2: 13
DIY3: 14
DIY4: 8
DIY5: 9
DIY6: 10

Auto: 15
Flash: 11
Jump3: 4
Jump7: 5
Fade3: 6
Fade7: 7

*/