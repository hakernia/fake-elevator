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
	
	
	This revision:
		- add shift register support to introduce light switch functionality
		  The shift register will drive bistable relays. 
		  The principles of control:
		  - 16 shift register outputs select relays to be energized
		  - Two polarity lines identify if energizing turns relays ON or OFF
		    POLARITY_ON_PIN - LOW: turn selected relays ON
			POLARITY_OFF_PIN - LOW: turn selected relays OFF
		    only one polarity line can be LOW at any given time!
		  - Energize impulse lasts limited time (tenths of millis)
		  - Just one of the 16 relays to be energized at once, to minimize
		    current consumption
		    This requires introduction of relay queues: value[], bit_num[]
	
*************************************************************************/

#include "FastLED.h"

// How many leds in your strip?
#define NUM_LEDS 13
#define NUM_KEYS 23  // 10 virtual keys (with shift)
#define NUM_FLOORS 11


// Serial to parallel shift register 74hc595
#define SHIFT_CLOCK_PIN  A4  // rising edge active
#define SHIFT_LATCH_PIN  A6  // L - block, H - show
#define SHIFT_DATA_PIN   10

// relays polarity
#define POLARITY_ON_PIN    11  // L - active
#define POLARITY_OFF_PIN   13  // L - active

// For led chips like Neopixels, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806 define both DATA_PIN and CLOCK_PIN
#define DATA_PIN 3
#define CLOCK_PIN 13

// Define the array of leds
CRGB leds[NUM_LEDS];

// LED related declarations
#define BLINK_TIME  500
char blink_state = 0;
int blink_countdown = 0;


// KEYPAD related declarations
// keyboard matrix definition
// ROWS act as outputs
#define ROW_1   2
#define ROW_2   4
#define ROW_3   7
#define ROW_4   8

#define COL_1   12
#define COL_2   A0
#define COL_3   A1
#define COL_4   A2

#define NUM_MODESETS  5  // 0 - lift, 1,2,3 - switches, 4 - lift stop

// map keypad number to number in the LED chain
char keymap[NUM_KEYS] = {7, 6, 8, 5, 9, 4, 10, 3, 11, 2, 12, 1, 0};
char mode[NUM_MODESETS][NUM_KEYS];
char key[NUM_KEYS];      // 0 - released, 1 - pressed
char last_key[NUM_KEYS]; // last state of key; used to detect a change
#define MAX_KEY_COUNTDOWN  3000
int key_countdown[NUM_KEYS];  // indicating time after key was pressed
char key_num;
char key_pressed;
#define SLEEP_TIME  5000
long sleep_countdown = 0;


// LIFT related declarations
#define RESTART_THIS_STATE 0  // set last_state to this one to pretend the state has changed even if it has not
#define LIFT_STOPPING  1
#define LIFT_STOPPED   2
#define DOOR_OPENING   3
#define DOOR_OPEN      4
#define DOOR_CLOSING   5
#define DOOR_CLOSED    6
#define LIFT_STARTING  7
#define LIFT_RUNNING_UP 8
#define LIFT_RUNNING_DOWN 9

char state = DOOR_OPEN;
char last_state = RESTART_THIS_STATE;
int state_countdown = 0;


// FLOOR related declarations
char curr_floor = 0;
char target_floor = 0;
int dir = 1;
char floors[NUM_KEYS];


char ff;
char modeset_num;

void setup() {
      pinMode(SHIFT_CLOCK_PIN, OUTPUT);
      pinMode(SHIFT_LATCH_PIN, OUTPUT);
      pinMode(SHIFT_DATA_PIN, OUTPUT);
      pinMode(POLARITY_ON_PIN, OUTPUT);
      pinMode(POLARITY_OFF_PIN, OUTPUT);
  
      pinMode(ROW_1, INPUT); // only one row at a time will be switched to output
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

      // initialize each key's mode
      for(modeset_num=0; modeset_num < NUM_MODESETS; modeset_num++)
      for(key_num=0; key_num<NUM_KEYS; key_num++) {
        mode[modeset_num][key_num] = 0;
      }
      
      // blink all LEDs to indicate the program start
      for(ff=0; ff<NUM_LEDS; ff++) {
        leds[ff] = CRGB::White;
      }
      for(ff=0; ff<NUM_KEYS; ff++) {
        key_countdown[ff] = MAX_KEY_COUNTDOWN;
      }
      FastLED.show();
      delay(1000);
      for(ff=0; ff< NUM_LEDS; ff++) {
        leds[ff] = CRGB::Black;
      }
      FastLED.show();
}



void readKbd() {
  // Rows are strobed by pinMode and not digitalWrite
  // so all rows except the selected one are high impedance.
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

// actualize each key's key_countdown
void countdownKbd(char from_key, char to_key) {
  char key_num;
  for(key_num=from_key; key_num<=to_key; key_num++)
    if(key[key_num] != last_key[key_num])
      key_countdown[key_num] = MAX_KEY_COUNTDOWN;
    else
      if(key_countdown[key_num] > 0)
        key_countdown[key_num]--;
}





// Has any floor been called below current one
char is_below(char curr_floor) {
  for(ff=curr_floor-1; ff>=0; ff--)
    if(floors[ff] > 0)
       return ff;
  return -1;
}
// Has any floor been called above current one
char is_above(char curr_floor) {
  for(ff=curr_floor+1; ff<NUM_FLOORS; ff++)
    if(floors[ff] > 0)
       return ff;
  return -1;
}


// manage key modes
unsigned long modeset[NUM_MODESETS][5] =
{{4, CRGB::Red, CRGB::Yellow, CRGB::Green, CRGB::Blue},   // func keys
 {2, CRGB::Black, CRGB::Yellow},              // switches
 {2, CRGB::Black, CRGB::Green},              // switches
 {2, CRGB::Black, CRGB::Blue},              // switches
 {1, CRGB::Black}};            // Floor STOP key
 
#define MODESET_FUNCKEYS      0
#define MODESET_SWITCHES_1    1
#define MODESET_SWITCHES_2    2
#define MODESET_SWITCHES_3    3
#define MODESET_FLOOR_STOP    4

// set key to next color in given modeset
void switch_mode(char key_num, char modeset_num) {
  if(mode[modeset_num][key_num] < modeset[modeset_num][0]-1) 
    mode[modeset_num][key_num]++;
  else 
    mode[modeset_num][key_num] = 0;
}
void switch_mode_while_visible(char key_num, char modeset_num) {
  // Switch mode only if pressed while lit.
  if(last_key[key_num] != key[key_num]) // just pressed
    if(key_countdown[key_num] > 0) { // and did not sleep at the moment
      switch_mode(key_num, modeset_num);

      // Update queue of shift register pins events
      if(key_num > 0 && key_num <= 10) {  // number keys only
        if(mode[modeset_num][key_num])
          add_to_queue(mode[modeset_num][0] * 10 + key_num-1, 300, 1, 1, 1); // bit_num, duration, exclusive, polarity, energize
        else
          add_to_queue(mode[modeset_num][0] * 10 + key_num-1, 300, 1, 0, 1);
      }
      
    }
}
// set range of keys to next color in given modeset
void manage_key_mode(char from_key, char to_key, char modeset_num) {
  for(key_num=from_key; key_num<=to_key; key_num++) {
    if(key[key_num])
      switch_mode_while_visible(key_num, modeset_num);
  }
}

void display_based_on_floor(char from_key, char to_key) {
  for(key_num=from_key; key_num<=to_key; key_num++)
    if(key_num == curr_floor) {
      if(blink_state && sleep_countdown)
        leds[keymap[key_num]] = CRGB::Red;
      else
        leds[keymap[key_num]] = CRGB::Black;
      if(sleep_countdown)
        leds[keymap[key_num]] = (((long)blink_countdown * 256) / BLINK_TIME) << 16;
    }
    else {
      if(floors[key_num] > 0)
        leds[keymap[key_num]] = CRGB::White;
      else
        leds[keymap[key_num]] = CRGB::Black;
    }
}

void display_based_on_mode(char from_key, char to_key, char modeset_num) {
    // lit up the right mode if recently pressed; turn off if slept long 
    for(key_num=from_key; key_num <= to_key; key_num++) {
      if(key_countdown[key_num] > 0) {
          leds[keymap[key_num]] = modeset[modeset_num][mode[modeset_num][key_num]+1];
      }
      else
        leds[keymap[key_num]] = CRGB::Black;
    }
}

void dim_turned_off_by_group(char driver_key_mode, char from_key, char to_key) {
    for(key_num=from_key; key_num <= to_key; key_num++) {
      if(driver_key_mode == CRGB::Black) {
          leds[keymap[key_num]] = modeset[modeset_num][mode[modeset_num][key_num]];
      }
      else
          leds[keymap[key_num]].setRGB(((CRGB)modeset[modeset_num][mode[modeset_num][key_num]]).r / 2,
                                       ((CRGB)modeset[modeset_num][mode[modeset_num][key_num]]).g / 2,
                                       ((CRGB)modeset[modeset_num][mode[modeset_num][key_num]]).b / 2);
    }  
}




void send_to_sr(int data) {
  digitalWrite(SHIFT_LATCH_PIN, LOW);
  /*
  for(unsigned char ff = 0; ff<16; ff++) {
    digitalWrite(SHIFT_CLOCK_PIN, LOW);
    digitalWrite(SHIFT_DATA_PIN, (data & 1));
    digitalWrite(SHIFT_CLOCK_PIN, HIGH);
    data >>= 1;
  }
  */
  shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, LSBFIRST, data);
  shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, LSBFIRST, (data >> 8));
  digitalWrite(SHIFT_LATCH_PIN, HIGH);
}

// queue buffers
#define MAX_QUEUE 8
int duration[MAX_QUEUE];
unsigned char value[MAX_QUEUE] = {0};  // bit 2 - exclusive, bit 1 - polarity, bit 0 - energize
unsigned char bit_num[MAX_QUEUE];
unsigned char queue_start = 0;
unsigned char queue_end = queue_start;

// output buffer for shift register
int sr_data = 0x0000;

// set polarity and single pin in shift register (sr)
void set_sr_output_pin(int bit_num, char polarity, char energize) {
  int mask = 1;
  // set polarity pin only if changed
  static char last_polarity;
  polarity = polarity > 0;
  if(polarity != last_polarity) {
    // turn off both polarity pins to avoid both ON
    digitalWrite(POLARITY_ON_PIN, HIGH);
    digitalWrite(POLARITY_OFF_PIN, HIGH);
    // turn on single one
    if(polarity)
      digitalWrite(POLARITY_ON_PIN, LOW);
    else
      digitalWrite(POLARITY_OFF_PIN, LOW);
    last_polarity = (polarity > 0);
  }

  // set shift register data
  if(energize > 0) {
    sr_data = sr_data | (mask << bit_num);  // move single 1 num bits
  }
  else {
    sr_data = sr_data & (0xffff ^ (mask << bit_num)); // move single 1 num bits and invert all
    sr_data = 0;
  }
  send_to_sr(sr_data);
}

// >0 - energize or deenergize it and countdown
// 0 - deenergize it and remove
// <0 - energize or deenergize and remove
void process_queue_tick() {
  //for(unsigned char ff = queue_start; ff < MAX_QUEUE; ff++) 
  unsigned char ff = queue_start;
  unsigned char excl = 0;
  while ((ff != queue_end) && !excl) {
    if(duration[ff]) {
      // nonzero duration - process it
      excl = ((4 & value[ff]) > 0);
      if(excl && (ff == queue_start) ||  // do exclusive bits only if first in queue
        !excl) {             // nonexclusive are always fine

        // avoid repeating sending sr data
        if(8 & value[ff]) {
          set_sr_output_pin(bit_num[ff], 2 & value[ff], 1 & value[ff]);
          value[ff] &= 0xF7; // 11110111 - clear init bit
        }
        
        if(duration[ff] > 0)
        {
          duration[ff]--; // countdown
        }
      }
    }
    if(duration[ff] == 0) {
      set_sr_output_pin(bit_num[ff], 0, 0);  // deenergize
    }
    
    ff++;
    if(ff == MAX_QUEUE)
      ff = 0;
  }
}
void clean_queue() {
  unsigned char excl = 0;
  while (queue_start != queue_end && !excl) {
    excl = ((4 & value[queue_start]) > 0);
    if(duration[queue_start] <= 0) {
      bit_num[queue_start] = 0;
      duration[queue_start] = 0;
      value[queue_start] = 0;
      excl = 0;
    }
    if(excl == 0) {
      queue_start++;
      if(queue_start == MAX_QUEUE)
        queue_start = 0;
    }
  }
}

int add_to_queue(char new_bit_num, int new_duration, char exclusive, char polarity, char energize) {
  if(queue_end < MAX_QUEUE - 1 && queue_end != queue_start -1 ||
     queue_end == MAX_QUEUE - 1 && queue_start != 0) {
    bit_num[queue_end] = new_bit_num;
    duration[queue_end] = new_duration;
    // bit 3 (val 8) indicates initialization
    value[queue_end] = 8 + ((exclusive > 0) << 2) + ((polarity > 0) << 1) + (energize > 0);

    queue_end++;
    if(queue_end == MAX_QUEUE)
      queue_end = 0;
  }
}





void display_debug_2() {
    // debug display - mark state and dir on the keypad
    /*
    if(state == DOOR_OPEN)
      leds[keymap[0]] = CRGB::Green;
      
    if(dir == -1)
      leds[keymap[1]] = CRGB::Green;
    else if(dir == 0)
      leds[keymap[2]] = CRGB::Green;
    else
      leds[keymap[3]] = CRGB::Green;
    */

    
    unsigned char dd = 0;
    for (unsigned char ff=0; ff < MAX_QUEUE; ff++) {
      if(duration[ff] > 0)
        dd++;
    }
    leds[keymap[dd]] = CRGB::Blue;
    
    leds[keymap[queue_start]] = CRGB::Green;
    leds[keymap[queue_end]] = CRGB::Red;
}




void loop() {

  // process switch event queue
  process_queue_tick();
  clean_queue();
  //send_to_sr(0xff00);
  
  
  // aktualizuj target podczas ruchu w dana strone
  /*
  if(target_floor != curr_floor) {
    if(dir == 1)
      target_floor = is_above(curr_floor);
    else
      target_floor = is_below(curr_floor);
  }
  else
  */
  // znajdz nowy target po osiagnieciu poprzedniego
  if(true) {
    if(dir == 1) {
      if((target_floor = is_above(curr_floor)) == -1) {
        dir = -1;
        if((target_floor = is_below(curr_floor)) == -1) {
          target_floor = curr_floor;  //0;  // go bottom if there is no calls
        }
      }
    }
    else {
      if((target_floor = is_below(curr_floor)) == -1) {
        if((target_floor = is_above(curr_floor)) > -1)
          dir = 1;
        else {
          target_floor = curr_floor;  //0; // go down if there is no calls
        }
      }
    }
  }

  switch(state) {
    case LIFT_STOPPING:
           if(last_state != state) {
             last_state = state;
             state_countdown = 500;  // how long it will be stopping
           }
           if(state_countdown == 0) {
             state = LIFT_STOPPED;
           }
           break;
    case LIFT_STOPPED:
           if(last_state != state) {
             last_state = state;
             state_countdown = 500;  // how long it will stand still after stopping
           }
           if(state_countdown == 0) {
             state = DOOR_OPENING;
           }
           break;
    case DOOR_OPENING:
           if(last_state != state) {
             last_state = state;
             state_countdown = 500;  // how long takes opening the door
           }
           if(state_countdown == 0) {
             state = DOOR_OPEN;
           }
           break;
    case DOOR_OPEN:
           if(last_state != state) {
             last_state = state;
             state_countdown = 500;  // for how long the door is open
           }
           if(state_countdown == 0) {
             if(target_floor != curr_floor)
               state = DOOR_CLOSING;
           }
           break;
    case DOOR_CLOSING:
           if(last_state != state) {
             last_state = state;
             state_countdown = 500;  // how long takes closing the door
           }
           if(state_countdown == 0) {
             if(target_floor != curr_floor)
               state = DOOR_CLOSED;
           }
           break;
    case DOOR_CLOSED:
           if(last_state != state) {
             last_state = state;
             state_countdown = 500;  // for how long the lift holds after closing the door before it starts running
           }
           if(state_countdown == 0) {
             if(target_floor != curr_floor)
               state = LIFT_STARTING;
           }
           break;
    case LIFT_STARTING:
           if(last_state != state) {
             last_state = state;
             state_countdown = 500;  // for how long the lift decides where to go
           }
           if(state_countdown == 0) {
             if(dir > 0)
               state = LIFT_RUNNING_UP;
             else
               state = LIFT_RUNNING_DOWN;
           }
           break;
    case LIFT_RUNNING_UP:
           if(last_state != state) {
             last_state = state;
             //dir = 1;
             state_countdown = 500;  // for how long the lift goes up one floor
           }
           if(state_countdown == 0) {
             if(curr_floor < NUM_FLOORS)
               curr_floor++;
             // arrived to called floor
             //if(floors[curr_floor] > 0) {
             if(curr_floor == target_floor) {
               state = LIFT_STOPPING;
               floors[curr_floor] = 0;
             }
             else {
               last_state = RESTART_THIS_STATE;
               state = LIFT_RUNNING_UP;
             }
           }
           break;
    case LIFT_RUNNING_DOWN:
           if(last_state != state) {
             last_state = state;
             // dir = -1;
             state_countdown = 500;  // for how long the lift goes down one floor
           }
           if(state_countdown == 0) {
             if(curr_floor > 0)
               curr_floor--;
             // arrived to called floor  
             //if(floors[curr_floor] > 0) {
             if(curr_floor == target_floor) {
               state = LIFT_STOPPING;
               floors[curr_floor] = 0;
             }
             else {
               state = LIFT_RUNNING_DOWN;
               last_state = RESTART_THIS_STATE;
             }
           }
           break;
  }

  // MOVE
  
  if(state_countdown)
    state_countdown--;

  delay(1);


  // only countdown if target floor is reached (possible idle state)
  if(curr_floor == target_floor) {
    if(sleep_countdown > 0)
      sleep_countdown--;
  }
  else
    sleep_countdown = SLEEP_TIME;

  
  countdownKbd(12, 12);  // fade out just the two "function" keys 
  memcpy(last_key, key, sizeof(last_key));
  readKbd();

/*
  if(key[11]) {
    switch_mode_while_visible(11, mode[0][12]);
    // DEBUG DISPLAY for key 11
    // debug display - mark target floor with the LED on the keypad
    leds[keymap[target_floor]] = CRGB::Green;
  } 
  else
*/
  if(key[12]) {
    // Switch mode only if pressed while lit.
    switch_mode_while_visible(12, MODESET_FUNCKEYS);
    // DEBUG DISPLAY for key 12
    display_debug_2();
  }
  else {
    // REGULAR DISPLAY
    if(mode[0][12] == 0) {
      // Lift mode
      // detect if key_pressed and update floors[]
      key_pressed = false;
      for(ff=0; ff<NUM_FLOORS; ff++) {
        if(key[ff] == 1) {
            floors[ff] = 1;
            key_pressed = true;
        }
      }
      display_based_on_floor(0, 10);
      // uses: curr_floor, blink_state, sleep_countdown, blink_countdown, leds[], keymap[], floors[]

      // handle STOP key (11)
      manage_key_mode(11, 11, MODESET_FLOOR_STOP);  // from key, to key, mode set
      display_based_on_mode(11, 11, MODESET_FLOOR_STOP);
    }
    else
    if(mode[0][12] == 1) {
      // Switch/Toggle mode
      manage_key_mode(0, 11, MODESET_SWITCHES_1);  // from key, to key, mode set
      display_based_on_mode(0, 11, MODESET_SWITCHES_1);
    }
    else
    if(mode[0][12] == 2) {
      manage_key_mode(0, 11, MODESET_SWITCHES_2);
      display_based_on_mode(0, 11, MODESET_SWITCHES_2);
    }
    else
    if(mode[0][12] == 3) {
      manage_key_mode(0, 11, MODESET_SWITCHES_3);
      display_based_on_mode(0, 11, MODESET_SWITCHES_3);
      //dim_turned_off_by_group(mode[mode[0][12]][11], 0, 10);
    }
  } //else debug

  // DISPLAY key 12
  //display_based_on_mode(11, 11, mode[0][12]);  // key 12 determines modeset for other keys
  display_based_on_mode(12, 12, MODESET_FUNCKEYS);
  // uses: key_num, key_countdown[], mode[][], leds[], keymap[]


  if(blink_countdown > 0)
    blink_countdown--;
  else {
    blink_countdown = BLINK_TIME;
    blink_state = blink_state == 0 ? 1 : 0;
  }

  if(key_pressed)
    sleep_countdown = SLEEP_TIME;

  FastLED.show();
}
