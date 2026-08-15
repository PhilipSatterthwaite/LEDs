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



#define DATA_PIN    5
//#define CLK_PIN   4
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define NUM_LEDS    300
CRGB leds[NUM_LEDS];

#define BRIGHTNESS          50 //don't set over 150: 10,000 mA/300 LED = 33.3 mA -- > < 170 brightness (keep it below 150 for safety): note, setting above 50 will cause color to fade over length of strip

#define FRAMES_PER_SECOND  30

int CURR_BRIGHTNESS   =    15;
String receivedSignal = "";
decode_results results;

void setup() {
  
  delay(3000); // 3 second delay for recovery

  Serial.begin(9600);
  //Serial.begin(9600);
  //irrecv.enableIRIn();
  
  // tell FastLED about the LED strip configuration
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  //FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);
  rainbow();
  FastLED.show();
  //IRloop();
}

void IRloop() {
  delay(1000);
  Serial.println("starting IR loop");
  //boolean didFind = false;
  
  irrecv.enableIRIn();
  
  while (!irrecv.decode(&results)) { 
      
  }
  Serial.println("IR received");
  receivedSignal = String(irrecv.decodedIRData.decodedRawData, HEX);
  //String signal = String(irrecv.decodedIRData.decodedRawData)
  char* hexString = irrecv.decodedIRData.decodedRawData;

  int numSig = 0;
  Serial.println(irrecv.decodedIRData.decodedRawData);
  Serial.println(hexString);
  receivedSignal.toUpperCase(); // Convert to uppercase
  Serial.println(receivedSignal);
  if (receivedSignal.length() == 100) {
    Serial.println(receivedSignal);
    Serial.println("found Signal");
    //doSomething(receivedSignal);
    return;
  }
  doSomething(receivedSignal, numSig);
  Serial.println("no valid signal");
  

}

uint8_t gHue = 0; // rotating "base color" used by many of the patterns

void doSomething(String receivedSIgnal, int numSig) {
  Serial.println("doing something");
  Serial.println(numSig);
  if (numSig == 3208707840){
    Serial.println("off button");
    delay(3000);
  }

  if (receivedSignal.equals("BF40FF00")) {    //Power button
      if (CURR_BRIGHTNESS == 0) {
        CURR_BRIGHTNESS = BRIGHTNESS;
      }
      else{
        CURR_BRIGHTNESS = 0;
      }
      FastLED.setBrightness(CURR_BRIGHTNESS);
      FastLED.show();
    }
  
    else if (receivedSignal.equals("A35CFF00")){  //Brightness up
      if (CURR_BRIGHTNESS < BRIGHTNESS-9){
        CURR_BRIGHTNESS += 10;
        FastLED.setBrightness(CURR_BRIGHTNESS);
        FastLED.show();
      }
    }

    else if (receivedSignal.equals("A25DFF00")){  //Brightness down
      if (CURR_BRIGHTNESS > 9){
        CURR_BRIGHTNESS -= 10;
        FastLED.setBrightness(CURR_BRIGHTNESS);
        FastLED.show();
      }
    }

    else if (receivedSignal.equals("E31CFF00")){  //yellow orange
       //warm();
       //FastLED.show();
       Serial.println("hello");
    }
}

void loop() {
  // Check if the IR receiver has received a signal
  //delay(1000);
  //IRloop();
  if (irrecv.decode(&results)){
        Serial.println(results.value);
        switch(results.value){
          case 0xFF38C7: //Keypad button "5"
          Serial.println("press 1");
          }

        switch(results.value){
          case 0xFF18E7: //Keypad button "2"
          Serial.println("press 2");
          }

        irrecv.resume(); 
    }
  /*
  


  Serial.println("loop function");
  if (irrecv.decode()) {
    receivedSignal = String(irrecv.decodedIRData.decodedRawData, HEX);
    receivedSignal.toUpperCase(); // Convert to uppercase
    if (receivedSignal.length() == 8) {
      Serial.println(receivedSignal);
    }
    
    if (receivedSignal.equals("BF40FF00")) {    //Power button
      if (CURR_BRIGHTNESS == 0) {
        CURR_BRIGHTNESS = BRIGHTNESS;
      }
      else{
        CURR_BRIGHTNESS = 0;
      }
      FastLED.setBrightness(CURR_BRIGHTNESS);
      FastLED.show();
    }
  
    else if (receivedSignal.equals("A35CFF00")){  //Brightness up
      if (CURR_BRIGHTNESS < BRIGHTNESS-9){
        CURR_BRIGHTNESS += 10;
        FastLED.setBrightness(CURR_BRIGHTNESS);
        FastLED.show();
      }
    }

    else if (receivedSignal.equals("A25DFF00")){  //Brightness down
      if (CURR_BRIGHTNESS > 9){
        CURR_BRIGHTNESS -= 10;
        FastLED.setBrightness(CURR_BRIGHTNESS);
        FastLED.show();
      }
    }

    else if (receivedSignal.equals("E31CFF00")){  //yellow orange
       //warm();
       //FastLED.show();
       Serial.println("hello");
    }
    
    irrecv.resume();
    
  }

  //rainbow();
  // send the 'leds' array out to the actual LED strip
  //FastLED.show();  
  // insert a delay to keep the framerate modest
  //FastLED.delay(1000/FRAMES_PER_SECOND);
  
  //handleState();
  // do some periodic updates
  EVERY_N_MILLISECONDS( 20 ) { gHue++; } // slowly cycle the "base color" through the rainbow
  */
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

void teal() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = leds[i] = CRGB(0, 162, 57); // cool teal
  }
}


/*
Power: BF40FF00
Brightness up: A35CFF00
Brightness down: A25DFF00
Red: A758FF00
Green: A659FF00
Blue: BA45FF00
White: BB44FF00
Orange: AB54FF00
Less orange: AF50FF00
yellow orange: E31CFF00
yellow: E718FF00

light green: AA55FF00
light blue: AA55FF00
aqua: E21DFF00
teal: E619FF00

lighter blue: B649FF00
purple: B24DFF00
magenta: E11EFF00
pink: E51AFF00

warm 1: B748FF00
warm 2: B34CFF00
cold 1: E01FFF00
cold 2: E41BFF00

Red up: EB14FF00
red down: EF10FF00
green up: EA15FF00
green down: EE11FF00
blue up: E916FF00
blue down: ED12FF00

quick: E817FF00
slow: EC13FF00
DIY1: F30CFF00
DIY2: F20DFF00
DIY3: F10EFF00
DIY4: F708FF00
DIY5: F609FF00
DIY6: F50AFF00

Auto: F00FFF00
Flash: F40BFF00
Jump3: FB04FF00
Jump7: FA05FF00
Fade3: F906FF00
Fade7: F807FF00

*/