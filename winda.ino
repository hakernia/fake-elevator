/** winda.ino ***********************************************************

    Fake elevator - a resurrection of an old elevator button board.
	
    Copyright (C) 2019  Piotr Karpiewski

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

    Contact author at hakernia.pl@gmail.com
	
	
	Initial revision:
	    - use WS2812B LEDs as button light sources with FastLED library
		- reading buttons using pin mode changes to avoid logic state
		  clashes when multiple buttons are pressed at once
		- very basic elevator state contol
		- started from blink.ino in FastLED library
	
*************************************************************************/

#include "FastLED.h"
// How many leds in your strip?
#define NUM_LEDS 13
#define NUM_KEYS 13

// For led chips like Neopixels, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806 define both DATA_PIN and CLOCK_PIN
#define DATA_PIN 3
#define CLOCK_PIN 13

#define ROW_1   2
#define ROW_2   4
#define ROW_3   7
#define ROW_4   8

#define COL_1   12
#define COL_2   A0
#define COL_3   A1
#define COL_4   A2

// Define the array of leds
CRGB leds[NUM_LEDS];
char keymap[NUM_KEYS] = {7, 6, 8, 5, 9, 4, 10, 3, 11, 2, 12, 1, 0};
char ff;

void setup() { 
      pinMode(ROW_1, INPUT);
      pinMode(ROW_2, INPUT);
      pinMode(ROW_3, INPUT);
      pinMode(ROW_4, INPUT);
      digitalWrite(ROW_1, LOW);
      digitalWrite(ROW_2, LOW);
      digitalWrite(ROW_3, LOW);
      digitalWrite(ROW_4, LOW);
      pinMode(COL_1, INPUT_PULLUP);
      pinMode(COL_2, INPUT_PULLUP);
      pinMode(COL_3, INPUT_PULLUP);
      pinMode(COL_4, INPUT_PULLUP);
      
      // Uncomment/edit one of the following lines for your leds arrangement.
      // FastLED.addLeds<TM1803, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<TM1804, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<TM1809, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<WS2811, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<WS2812, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<WS2812B, DATA_PIN, RGB>(leds, NUM_LEDS);
  	  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
      // FastLED.addLeds<APA104, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<UCS1903, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<UCS1903B, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<GW6205, DATA_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<GW6205_400, DATA_PIN, RGB>(leds, NUM_LEDS);
      
      // FastLED.addLeds<WS2801, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<SM16716, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<LPD8806, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<P9813, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<APA102, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<DOTSTAR, RGB>(leds, NUM_LEDS);

      // FastLED.addLeds<WS2801, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<SM16716, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<LPD8806, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<P9813, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
      // FastLED.addLeds<DOTSTAR, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);

      for(ff=0; ff< NUM_LEDS; ff++) {
        leds[ff] = CRGB::White;
      }
  FastLED.show();
      delay(1000);
      for(ff=0; ff< NUM_LEDS; ff++) {
        leds[ff] = CRGB::Black;
      }
  FastLED.show();
}

char key[NUM_KEYS];
void readKbd() {
  //digitalWrite(ROW_1, LOW);
  pinMode(ROW_1, OUTPUT);
    if(digitalRead(COL_1) == LOW)
      key[10] = 1;
    else
      key[10] = 0;
    if(digitalRead(COL_2) == LOW)
      key[8] = 1;
    else
      key[8] = 0;
    if(digitalRead(COL_3) == LOW)
      key[6] = 1;
    else
      key[6] = 0;
    if(digitalRead(COL_4) == LOW)
      key[4] = 1;
    else
      key[4] = 0;
  pinMode(ROW_1, INPUT);
  //digitalWrite(ROW_1, HIGH);
  
  
  //digitalWrite(ROW_2, LOW);
  pinMode(ROW_2, OUTPUT);
  if(digitalRead(COL_1) == LOW)
      key[2] = 1;
    else
      key[2] = 0;
    if(digitalRead(COL_2) == LOW)
      key[0] = 1;
    else
      key[0] = 0;
    if(digitalRead(COL_3) == LOW)
      key[1] = 1;
    else
      key[1] = 0;
    if(digitalRead(COL_4) == LOW)
      key[3] = 1;
    else
      key[3] = 0;
  pinMode(ROW_2, INPUT);
  //digitalWrite(ROW_2, HIGH);
  
  //digitalWrite(ROW_3, LOW);
  pinMode(ROW_3, OUTPUT);
    if(digitalRead(COL_1) == LOW)
      key[5] = 1;
    else
      key[5] = 0;
    if(digitalRead(COL_2) == LOW)
      key[7] = 1;
    else
      key[7] = 0;
    if(digitalRead(COL_3) == LOW)
      key[9] = 1;
    else
      key[9] = 0;
/*    if(digitalRead(COL_4) == LOW)
      key[4] = 1;
    else
      key[4] = 0;  */
  pinMode(ROW_3, INPUT);
  //digitalWrite(ROW_3, HIGH);
  
  //digitalWrite(ROW_4, LOW);
  pinMode(ROW_4, OUTPUT);  
    if(digitalRead(COL_1) == LOW)
      key[11] = 1;
    else
      key[11] = 0;
    if(digitalRead(COL_2) == LOW)
      key[12] = 1;
    else
      key[12] = 0;
/*    if(digitalRead(COL_3) == LOW)
      key[6] = 1;
    else
      key[6] = 0;
    if(digitalRead(COL_4) == LOW)
      key[4] = 1;
    else
      key[4] = 0;  */
  pinMode(ROW_4, INPUT);
  //digitalWrite(ROW_4, HIGH);
  
}


#define RUN_FLOOR_DELAY  1000
#define STOP_FLOOR_DELAY 10000
#define WINDA_STOI  0
#define WINDA_JEDZIE  1
#define BLINK_TIME  500
char blink_state = 0;
int blink_countdown = 0;
int countdown = 0;
char state = WINDA_STOI;
char dir = 0;
char cur_pietro;
char pietro[NUM_KEYS];
char is_below(char cur_pietro) {
  for(ff=cur_pietro-1; ff>=0; ff--)
    if(pietro[ff] > 0)
       return true;
  return false;
}
char is_above(char cur_pietro) {
  for(ff=cur_pietro+1; ff<NUM_KEYS; ff++)
    if(pietro[ff] > 0)
       return true;
  return false;
}

void loop() { 

  // MOVE
  if(countdown)
    countdown--;
  else {
    if(state==WINDA_STOI) {
      // w koncu rusza
      if(dir==-1 && !is_below(cur_pietro)) {
         if(is_above(cur_pietro))
           dir = 1;
         else
           dir = 0;
      } else 
      if(dir==1 && !is_above(cur_pietro)) {
         if(is_below(cur_pietro))
           dir = -1;
         else
           dir = 0;
      }
    }
    cur_pietro += dir;
    if(pietro[cur_pietro] > 0) {
      countdown = 3000 + random(STOP_FLOOR_DELAY);
      state = WINDA_STOI;
      pietro[cur_pietro] = 0;
    }
    else {
      countdown = RUN_FLOOR_DELAY;
      state = WINDA_JEDZIE;
    }
  }
  delay(1);

  
  readKbd();
  for(ff=0; ff<NUM_KEYS; ff++) {
    if(key[ff] == 1) 
        pietro[keymap[ff]] = 1;
        /*
      leds[keymap[ff]] = CRGB::Yellow;
    else
      leds[keymap[ff]] = CRGB::Black;
      */
  }


  // DISPLAY
  for(ff=0; ff<NUM_KEYS; ff++)
    if(ff == cur_pietro) {
      if(blink_state)
        leds[keymap[ff]] = CRGB::Red;
      else
        leds[keymap[ff]] = CRGB::Black;
    }
    else
    if(pietro[ff] > 0)
      leds[keymap[ff]] = CRGB::White;
    else
      leds[keymap[ff]] = CRGB::Black;


  if(blink_countdown > 0)
    blink_countdown--;
  else {
    blink_countdown = BLINK_TIME;
    //blink_state = (blink_state * -1) + 1;
    blink_state = blink_state == 0 ? 1 : 0;
  }
    
  
  // Turn the LED on, then pause
//  leds[0] = CRGB::Red;
  FastLED.show();
//  delay(500);
  // Now turn the LED off, then pause
//  leds[0] = CRGB::Black;
//  FastLED.show();
 // delay(100);
      leds[0] = CRGB::Red;
//  delay(100);
      leds[0] = CRGB::Black;
}
