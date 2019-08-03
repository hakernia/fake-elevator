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
    
*************************************************************************/

#include "FastLED.h"
#include <Wtv020sd16p.h>

// wtv020 pin
#define WTV_RESET_PIN    9
#define WTV_CLOCK_PIN    5 
#define WTV_DATA_PIN     6
#define WTV_BUSY_PIN    19  // not used if async play only

// init wtv management object
Wtv020sd16p wtv020sd16p(WTV_RESET_PIN, WTV_CLOCK_PIN, WTV_DATA_PIN, WTV_BUSY_PIN);


// How many leds in your strip?
#define NUM_LEDS      13
#define NUM_PHYS_KEYS 13
#define NUM_KEYS      23  // 10 virtual keys (with shift)
#define NUM_FLOORS    13  // 0 ground (P), 1..10 floors, 11 hidden floor, 12 lift cabin

// virtual keys
#define KEY_P          0
#define KEY_DIGIT_MIN  1
#define KEY_DIGIT_MAX 20
#define KEY_STOP      21
#define KEY_BELL      22

#define LED_P      0
#define LED_STOP  11
#define LED_BELL  12

// Serial to parallel shift register 74hc595
#define SHIFT_CLOCK_PIN  A4  // rising edge active
#define SHIFT_LATCH_PIN  A6  // L - block, H - show
#define SHIFT_DATA_PIN   10
#define SHIFT_NUM_BITS   32

// relays polarity
#define POLARITY_ON_PIN    11  // H - active
#define POLARITY_OFF_PIN   13  // H - active

#define ENERGIZE_DURATION  35  // 30 - 72ms, 35 - 84ms  -> 5 - 12ms

#define NO_MATTER  0  // value that does not matter, zero is fine too

// For led chips like Neopixels, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806 define both DATA_PIN and CLOCK_PIN
#define DATA_PIN   3
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

#define NUM_KEY_MODES  5  // 0 - lift, 1,2,3 - switches, 4 - lift stop

// map keypad number to number in the LED chain
char map_key_to_led[NUM_KEYS] = 
        {7,                                // KEY_P
         6, 8, 5, 9, 4, 10, 3, 11, 2, 12,  // DIGITS
         6, 8, 5, 9, 4, 10, 3, 11, 2, 12,  // DIGITS
         1, 0};                            // KEY_STOP, KEY_BELL
char key_state[NUM_KEY_MODES][NUM_KEYS];
char key[NUM_KEYS];      // 0 - released, 1 - pressed
char last_key[NUM_KEYS]; // last state of key; used to detect a change
#define MAX_KEY_COUNTDOWN  3000
#define MAX_KEY_PRESS_COUNTDOWN  900
int key_countdown[NUM_KEYS];  // indicating time after key was pressed
int key_press_countdown[NUM_KEYS];  // indicating time the key stays pressed
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
#define PASSENGERS_MOVEMENT 10
#define LIFT_START_FALLING  11
#define LIFT_FALLING_DOWN   12
#define LIFT_CRASHING       13


char state = DOOR_OPEN;
char last_state = RESTART_THIS_STATE;
int state_countdown = 0;


// FLOOR related declarations
char curr_floor = 0;
char target_floor = 0;
char dir = 1;
char floors[NUM_FLOORS];  // !!! was [NUM_KEYS]; make sure it is not required


char ff;
char key_mode = 0;
char last_active_key_mode = 0;



int switch_sr_pin(char pin_num, char onoff);


/* BIT FLAG FUNCTIONS */

void set_bit_flag(unsigned long *flags, char flag_num) {
  *flags |= 1UL << flag_num;
}
void clear_bit_flag(unsigned long *flags, char flag_num) {
  *flags &= ~(1UL << flag_num);
}
char is_bit_flag(unsigned long flags, char flag_num) {
  return (flags << 31-flag_num) >> 31;  // (flags >> flag_num) & 1F;
}
char count_set_flags(unsigned long flags) {
  char flag_count = 0;
  for(char ff = 0; ff < 32; ff++) {
      flag_count += (flags & 1UL);
      flags >>= 1;
  }
  return flag_count;
}
// returns number of n-th flag being set (least significant bit first)
// n starting from 0
// returns 0 if first bit is set or if none is; works exactly as array
// so make sure to call only if you know any bits are set
char next_set_flag(unsigned long flags, char n) {
  char flag_count = 0;
  for(char ff = 0; ff < 32; ff++) {
    if(flags & 1UL) {
      if(flag_count == n)
        return ff;
      flag_count++;
    }
    flags >>= 1;
  }
  return 0;
}

/* END OF BIT FLAG FUNCTIONS */



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
      key[21] = 1;
    else
      key[21] = 0;
    if(digitalRead(COL_2) == LOW)
      key[22] = 1;
    else
      key[22] = 0;
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

// mode 0: lift
// mode 1-5: switches
// only switches, when key P pressed
void mapPhysKeyToKey(char mode, char key_p_state) {
  if(mode && key_p_state) {
    memcpy(&key[11], &key[1], sizeof(key[1]) * 10);
    memset(&key[1], 0, sizeof(key[1]) * 10);
  }
}

// actualize each key's key_countdown
void countdownKbd(char from_key, char to_key) {
  char key_num;
  for(key_num=from_key; key_num<=to_key; key_num++)
    if(key[key_num] != last_key[key_num]) {
      key_countdown[key_num] = MAX_KEY_COUNTDOWN;
      key_press_countdown[key_num] = MAX_KEY_PRESS_COUNTDOWN;
    }
    else {
      if(key_countdown[key_num] > 0)
        key_countdown[key_num]--;
      if(key[key_num])  // key is pressed
        if(key_press_countdown[key_num] > 0)
          key_press_countdown[key_num]--;
    }
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


/*
 * Connector -> Shift Register (SR) pin -> IC pin mapping:
 * Conn 1           Conn 5
 * 1. 7  IC1.A      1. 23 IC3.A
 * 2. 6  IC1.B      2. 16 IC3.H
 * 3. 5  IC1.C      3. 17 IC3.G
 *                  4. 18 IC3.F
 * Conn 2
 * 1. 4  IC1.D      Conn 6
 * 2. 3  IC1.E      1. 19 IC3.E
 * 3. 2  IC1.F      2. 20 IC3.D
 * 4. 1  IC1.G      3. 21 IC3.C
 * 5. 0  IC1.H      4. 22 IC3.B
 *                  5. 15 IC2.A
 * Conn 3
 * 1. 14 IC2.B                 IC 4
 * 2. 13 IC2.C                30 B
 * 3. 12 IC2.D                29 C    31 A
 * 4. 11 IC2.E                28 D
 *                            27 E
 * Conn 4                     26 F
 * 1. 10 IC2.F                25 G
 * 2. 9  IC2.G                24 H
 * 3. 8  IC2.H
 * 
 */
/* mapping table: key num to shift register pin */
unsigned char map_key_to_sr[1][KEY_DIGIT_MAX - KEY_DIGIT_MIN + 1] = 
       {{10, 9, 8, 3, 13, 11, 2, 14, 12, 7, 6, 5, 1, 4, 0,   // key digit 1-15: lights
         15, 22, 21, 20, 19}};                               // key digit 16-20: cold light, fan, ..., ..., ... 
/* 
 * Unmapped SR pins (can be driven by the story):
 * type 1 (bistable relay): 18, 17, 16, 23
 * type 0 (regular boolean): 24, 25, 26, 27, 28, 29, 30, 31
 */



// manage key modes
//unsigned long key_bell_states[4] = {CRGB::Red, CRGB::Yellow, CRGB::Green, CRGB::Blue};
char modeset_len[] = {4,2,2,2,1};
CRGB modeset[NUM_KEY_MODES][5] =
{{CRGB::Red, 0x888800, CRGB::Green, CRGB::Blue},   // func keys
 {CRGB::Black, 0x888800},              // switches
 {CRGB::Black, CRGB::Green},              // switches
 {CRGB::Black, CRGB::Blue},              // switches
 {CRGB::Black}};            // Floor STOP key
 
#define KEY_MODE_FUNCKEYS      0
#define KEY_MODE_SWITCHES_1    1
#define KEY_MODE_SWITCHES_2    2
#define KEY_MODE_SWITCHES_3    3
#define KEY_MODE_FLOOR_STOP    4

// set key to next color in given modeset
void next_key_state(char key_num, char key_mode) {
  if(key_state[key_mode][key_num] < modeset_len[key_mode]-1) 
    key_state[key_mode][key_num]++;
  else 
    key_state[key_mode][key_num] = 0;
}
void switch_key_state_while_visible(char key_num, char key_mode) {
  // used by bell key so assume key_num == KEY_BELL
  //                        and key_mode == KEY_MODE_FUNCKEYS
  // Switch mode only if pressed while lit.
  if(last_key[key_num] != key[key_num] &&   // just pressed or released
     key_press_countdown[key_num] > 0)      // and no long press
    if(key_countdown[key_num] > 0) {        // and did not sleep at the moment
      next_key_state(key_num, key_mode);      // KEY_BELL's mode determines key_mode
    }
}
void switch_key_state(char key_num, char key_mode) {  // used by all except bell key
  if(last_key[key_num] != key[key_num] &&    // just pressed or released
     key_press_countdown[key_num] > 0) {     // and no long press
      next_key_state(key_num, key_mode);
  }
}
void key_state_to_sr_pin(char key_num, char key_mode) {  // used by individual digit keys
  if(last_key[key_num] != key[key_num] &&    // just pressed or released
     key_press_countdown[key_num] > 0) {     // and no long press
      // Update queue of shift register pins events
      if(key_state[key_mode][KEY_STOP])                             // key stop is active
      if(key_num >= KEY_DIGIT_MIN && key_num <= KEY_DIGIT_MAX) {  // number keys only
        if(key_state[key_mode][key_num]) {
          switch_sr_pin(map_key_to_sr[0][key_num-1], 1); // sr_pin_num, polarity
          last_active_key_mode = key_mode;
        }
        else
          switch_sr_pin(map_key_to_sr[0][key_num-1], 0); // sr_pin_num, polarity
      }
  }
}
void all_digits_off() {
  for(unsigned char ff = KEY_DIGIT_MIN; ff<=KEY_DIGIT_MAX; ff++) {
    if(key_state[last_active_key_mode][ff])
      switch_sr_pin(map_key_to_sr[0][ff-KEY_DIGIT_MIN], 0);
  }
  key_state[last_active_key_mode][KEY_STOP] = 0;  // turn off STOP key in last modeset
}
void key_state_to_sr_pin_bulk(char key_num, char key_mode) {  // used by stop key
  if(last_key[key_num] != key[key_num] &&    // just pressed or released
     key_press_countdown[key_num] > 0) {     // and no long press
      if(last_active_key_mode != key_mode)  // modeset changed
        all_digits_off();                         // initially turn off previous modeset's keys
      // Update queue with all digits ON if key STOP is pressed
      for(unsigned char ff = KEY_DIGIT_MIN; ff <= KEY_DIGIT_MAX; ff++) {
        if(key_state[key_mode][ff])                 // the key ff is ON
          switch_sr_pin(map_key_to_sr[0][ff-KEY_DIGIT_MIN], key_state[key_mode][KEY_STOP]);
        if(key_state[key_mode][KEY_STOP])
          last_active_key_mode = key_mode;
      }
  }
}
void handle_long_press(char key_num, char key_mode) {  // used by stop key
  if(key_press_countdown[key_num] == 0) { // long pressed
    key_press_countdown[key_num] = -1;    // avoid reexecution at the next tick
    // Update all digits to ON or OFF if key STOP is pressed long
    for(unsigned char ff = KEY_DIGIT_MIN; ff<=KEY_DIGIT_MAX; ff++) {
      if(key_state[key_mode][KEY_STOP] != key_state[key_mode][ff]) {
          // update to mode of the stop key
          next_key_state(ff, key_mode);
          switch_sr_pin(map_key_to_sr[0][ff-KEY_DIGIT_MIN], key_state[key_mode][KEY_STOP]);
          if(key_state[key_mode][KEY_STOP])
            last_active_key_mode = key_mode;
      }
    }
  }
}
// Go through all keys and for pressed ones modify their state (mode)
void manage_key_state(char key_mode) {
  if(key[KEY_BELL]) {
    switch_key_state_while_visible(KEY_BELL, key_mode);
  }
  if(key[KEY_P]) {
    switch_key_state(KEY_P, key_mode);
  }
  if(key[KEY_STOP]) {
    //switch_key_state(KEY_STOP, key_mode);  // just pressed
    //key_state_to_sr_pin_bulk(KEY_STOP, key_mode); // just pressed
    handle_long_press(KEY_STOP, key_mode);
  } else {
    switch_key_state(KEY_STOP, key_mode);  // just released
    key_state_to_sr_pin_bulk(KEY_STOP, key_mode); // just released
  }
  for(key_num=KEY_DIGIT_MIN; key_num<=KEY_DIGIT_MAX; key_num++) {
    if(key[key_num]) {
      switch_key_state(key_num, key_mode);
      key_state_to_sr_pin(key_num, key_mode);
    }
  }
}

void display_based_on_floor(char from_key, char to_key) {
  for(key_num=from_key; key_num<=to_key; key_num++)
    if(key_num == curr_floor) {
      if(blink_state && sleep_countdown)
        leds[map_key_to_led[key_num]] = CRGB::Red;
      else
        leds[map_key_to_led[key_num]] = CRGB::Black;
      if(sleep_countdown)
        leds[map_key_to_led[key_num]] = (((long)blink_countdown * 256) / BLINK_TIME) << 16;
    }
    else {
      if(floors[key_num] > 0)
        leds[map_key_to_led[key_num]] = 0x888888;  // CRGB::AntiqueWhite;  //CRGB::White;
      else
        leds[map_key_to_led[key_num]] = CRGB::Black;
    }
}

void display_based_on_mode(char from_led, char to_led, char key_mode, char key_p_state) {
    // lit up the right mode if recently pressed; turn off if slept long 
    char led_num;
    for(led_num=from_led; led_num <= to_led; led_num++) {
      
      if(led_num == 0)
        key_num = KEY_P;
      else
      if(led_num == 11)
        key_num = KEY_STOP;
      else
      if(led_num == 12)
        key_num = KEY_BELL;
      else
        // digit keys
        key_num = led_num + key_p_state * 10;

      if(key_num == KEY_BELL) {
        if(key_countdown[key_num] > 0)  // recently pressed
          leds[map_key_to_led[key_num]] = modeset[key_mode][key_state[key_mode][key_num]];
        else                            // idle for long time
          leds[map_key_to_led[key_num]] = CRGB::Black;
      }
      else
        leds[map_key_to_led[key_num]] = modeset[key_mode][key_state[key_mode][key_num]];
    }
}
/*
void dim_turned_off_by_group(char driver_key_mode, char from_key, char to_key) {
    for(key_num=from_key; key_num <= to_key; key_num++) {
      if(driver_key_mode == CRGB::Black) {
          leds[map_key_to_led[key_num]] = modeset[key_mode][key_state[key_mode][key_num]];
      }
      else
          leds[map_key_to_led[key_num]].setRGB(((CRGB)modeset[key_mode][key_state[key_mode][key_num]]).r / 2,
                                       ((CRGB)modeset[key_mode][key_state[key_mode][key_num]]).g / 2,
                                       ((CRGB)modeset[key_mode][key_state[key_mode][key_num]]).b / 2);
    }  
}
*/





/* SPEAK QUEUE */
#define MAX_SPK  400  // 512
unsigned char spk_len[MAX_SPK] = {
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5,   5, 5, 5, 5, 5, 5, 5, 5, 5, 5,   5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5,   5, 5, 5, 5, 6, 7, 
  5, // 46 wynosza
  5, // 47 ma przy sobie
  5, 15,  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5,   5, 5, 5, 5, 5, 
  3, // 75 i
  3, // 76 oraz 
  5, 5, 5,   7, 7, 7, 5, 5, 5, 5, 5, 5, 
  21, // 89 donos do GRU - pietro zawiera
  21, // 90 mdleje na widok 11 pietra
  21, // 91 mowi, ze istnieje 11 pietro
  19, // 92 musi poczekac
  19, // 93 musza poczekac
  26, // 94 smutni raportuja utrudnienia
  35, // 95 dobra zmiana ignor limitow
  6, // 96 niestety
  7, // 97 wyprowadzaja
  5, // 98 dorecza
  5, // 99 smutni usiluja namierzyc
  5, 5, 5, 5, // 100-103 Ania
  6, 6, 5, 5, // 104-107 Gosia
  4, 4, 5, 4, // 108-111 Rysiu
  5, 5, 5, 5, // 112-115 Agnieszka
  3, 3, 5, 3, // 116-119 Jozef
  4, 4, 4, 4, // Julia
  5, 5, 5, 5, // Kasia
  6, 6, 7, 6, // 128-131 Krzysztof
  5, 5, 5, 5, // 132-135 Lena
  4, 4, 6, 5, // 136-139 Lukasz
  4, 4, 5, 4, // 140-143 Marcin
  5, 5, 5, 5, // 144-147 Mariola
  4, 4, 5, 4, // 148-151 Michal                                         
  3, 3, 5, 3, // 152-155 Piotr
  4, 4, 5, 4, // 156-159 Rafal
  5, 5, 5, 5, // 160-163 Jola
  5, 5, 5, 5, // 164-167 Sowa
  5, 5, 5, 5, // 168-171 Wladek
  5, 5, 5, 5, // 172-175 Zosia
  5, 5, 5, 5, // 176-179 Nastka
  5, 5, 5, 5, // 180-183 Asia
  5, 5, 5, 5, // 184-187 Majka
  5, 5, 5, 5, // 188-191 Ela
  7, 7, 7, 7, // 192-195 Grupa smutnych panow
  9, 9, 10, 10, // 196-199 Srebrny deweloper
  4, 4, 4, 4, // 200-203 Anuszka
  6, 6, 6, 6, // 204-207 Malgorzata
  8, 8,10,10, // 208-211 Kot Behemot
  6, 6, 8, 7, // 212-215 Woland                 
                                                  13, 13, 5, 5,   5, 5, 5, 5, 5, 5, 5, 5,
  35, // 228 musi wyjsc z windy
  35, // 229 musza wyjsc z windy
  4,  // 230 na
  70,  // 231 Woland intro
  6,  // 232 zostawia
  5, 5, 5, 5, 5, 5, 5,   5, 5, 5, 5, 5, 5, 5, 5, 5, 5,   5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5,   5, 5, 5, 5, 5, 5, 5, 5, 5, 5,   5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  7, 7, 8, 8, // ITEM  0  300-303 Olej slonecznikowy
  5, 5, 5, 5, // ITEM  1  304-307 Dlugopis
  5, 5, 5, 5, // ITEM  2  308-311 Rekopis
  4, 4, 4, 4, // ITEM  3  312-315 Frytki 
  4, 4, 4, 4, // ITEM  4  316-319 Gwozdz
  4, 4, 4, 4, // ITEM  5  320-323 Kartka
  5, 5, 5, 5, // ITEM  6
  5, 5, 5, 5, // ITEM  7  328-331 Klucz
  5, 5, 5, 5, // ITEM  8  332-335 Kwiat
  3, 3, 3, 3, // ITEM  9  336-339 List
  4, 4, 4, 4, // ITEM 10  340-343 Mlotek
  5, 5, 5, 5, // ITEM 11  344-347 Nasionko
  5, 5, 5, 5, // ITEM 12  
  5, 5, 5, 5, // ITEM 13  
  5, 5, 5, 5, // ITEM 14  
  8, 8, 8, 8, // ITEM 15  360-363 przepis na piwo
  5, 5, 5, 5, // ITEM 16  364-367 
  5, 5, 5, 5, // ITEM 17  368-371 
  6, 6, 6, 6, // ITEM 18  372-375 Worek ziemniakow
  5, 5, 5, 5, // ITEM 19  376-379 
  5, 5, 5, 5, // ITEM 20  380-383 ITEM_WYROK wyrok (zegarek)
  5, 5, 5, 5, 5, 5, 
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5
  };  // 5 = 500 ms
#define MAX_SPK_QUEUE 30
int spk_que[MAX_SPK_QUEUE];
unsigned char spk_que_len = 0;
long spk_countdown = -1;

void add_spk(int spk_num) {
  if(spk_num < 0)
    return;
  if(spk_que_len < MAX_SPK_QUEUE - 1) {
    spk_que[spk_que_len] = spk_num;
    spk_que_len++;
  }
}
// change latest added spk, if any
void chg_spk(int spk_num) {
  if(spk_que_len > 0) {
    spk_que[spk_que_len-1] = spk_num;
  }
}
void say_num(long num) {
  char buf[15];
  sprintf(buf, "%ld", num);
  for(char ff=0; ff<strlen(buf); ff++) {
    add_spk(buf[ff] - '0');
  }
}
void spk_que_tick() {
  int spk;
  if(spk_que_len > 0 && spk_countdown == -1)
    spk_countdown = 0;
  if(spk_countdown == 0) {
    if(0 < spk_que_len) {
      spk = spk_que[0];
      spk_countdown = spk_len[ spk ] * 100;
      wtv020sd16p.asyncPlayVoice(spk_que[0]);
      memmove(&spk_que[0], &spk_que[1], sizeof(spk_que[1])*(spk_que_len-1));
      spk_que_len--;
    } else {
      spk_countdown = -1;  // don't get in here again until spk_que_len gets > 0
    }
  } else {
    if(spk_countdown > 0)
      spk_countdown--;
  }
}
char is_speaking() {
  return (spk_que_len > 0 || spk_countdown > 0);
}

/* END of SPEAK QUEUE */





/* LIFT WORLD SIMULATION */

#define NUM_ITEMS    21
#define NUM_PERSONS  29
//#define NUM_FLOORS   13  // 0 ground (P), 1..10 floors, 11 mystery floor, 12 lift cabin
#define PLACE_GROUND_FLOOR    0
#define PLACE_MYSTERY_FLOOR  11
#define PLACE_CABIN          12
// item levels, lower level item can hold higher level item
#define LVL_1    0x40  // 0100 0000
#define LVL_2    0x80  // 1000 0000
#define LVL_3    0xC0  // 1100 0000

// plot specific parameters
#define PERSON_ANIA         0
#define PERSON_ANUSZKA     25
#define PERSON_GOSIA        1
#define PERSON_MALGORZATA  26
#define PERSON_RYSIU        2
#define PERSON_KOT_BEHEMOT 27
#define PERSON_WOLAND      28
#define PERSON_WLADEK      17
#define PERSON_SPY  PERSON_WLADEK
#define PERSON_SMUTNI      23
#define PERSON_SREBRNY     24
#define ITEM_OLEJ_SLONECZNIKOWY  0
#define ITEM_WYROK     20  // !!! 20 to zegarek, chwilowo uzyty jako wezwanie
#define ITEM_NAKAZ     22
#define ITEM_WEZWANIE  23

char person_spy = PERSON_SPY;

#define ALT_FLOOR_ANNOUNCEMENTS   0   // anuszka starts announcements
#define ANUSZKA_BROKE_OIL         1
#define ANUSZKA_BROKE_OIL_SILENT  2
#define LIMITS_APPLY_FLAG         3   // people dont go overboard
#define WOLAND_INTRO_DONE         4   // was woland introduced?
//char alt_floor_announcements = 0;
//char anuszka_broke_oil = 0;

unsigned long plot_flags = 0;
unsigned long nuts_person_flags = 0;
unsigned long unhappy_person_flags = 0;
unsigned long rejected_person_flags = 0;
unsigned long targeted_person_flags = 22;  // DEBUG!!! 0;


/*
 * Floors are not stored in lift_obj. They are values in persons and items.
 * lift_obj[0..NUM_PERSONS-1] - value is location of person
 * lift_obj[NUM_PERSONS..NUM_PERSONS+NUM_ITEMS-1] - value is description and location of item:
 *   0x11000000 - description
 *   0x00111111 - location
 *     location value ranges:
 *                          0..NUM_FLOORS-1                       - a floor: 0-ground, 1..10-floors, 11-mystery floor, 12-cabin
 *                 NUM_FLOORS..NUM_FLOORS+NUM_PERSONS-1           - a person
 *     NUM_FLOORS+NUM_PERSONS..NUM_FLOORS_NUM_PERSONS+NUM_ITEMS-1 - an item
 * 
 */
// who/what is where
char lift_obj[NUM_PERSONS + NUM_ITEMS] = 
{
  // persons
  0,                         // person 0 Ania is on floor 0
        3,                         // person 1 Gosia is on floor 3
      2,                         // person 2 Rysiu is on floor 3
  9,                         // person 3 Agnieszka
          4,                         // person 4 Jozef
  5,                         // person 5 Julia
  6,                         // person 6 Kasia
  7,                         // person 7 Krzysztof
  8,                         // person 8 Lena
  9,                         // person 9 Lukasz
  7,                         // person 10 Marcin
  8,                         // person 11 Mariola
  6,                         // person 12 Michał
  7,                         // person 13 Piotr
  8,                         // person 14 Rafal
        3,                         // person 15 Jola
      2,                         // person 16 Sowa
        3,                         // person 17 Wladek
          4,                         // person 18 Zosia
  5,                         // person 19 Nastka
  6,                         // person 20 Asia
  10,                        // person 21 Majka
     1,                         // person 22 Ela
   9,                        // person 23 Grupa smutnych panow
  10,                        // person 24 Srebrny deweloper
  11,                        // person 25 Anuszka
  11,                        // person 26 Malgorzata
  11,                        // person 27 Kot Behemot
  11,                        // person 28 Woland
  // items
  0,               // item 0 olej slonecznikowy is on 0th floor
  NUM_FLOORS + 1,  // item 1 dlugopis is on person 1 Gosia
  NUM_FLOORS + NUM_PERSONS + 5,  // item 2 rekopis is on item 5 kartka
  NUM_FLOORS + NUM_PERSONS + 18, // item 3 frytki is on item 18 worek ziemniakow
  0,               // item 4 gwozdz is on ground floor
  NUM_FLOORS + 1,  // item 5 kartka is on person 1 Gosia
  NUM_FLOORS + NUM_PERSONS + 17,  // item 6 konstytucja is on item 17 szkatulka
  NUM_FLOORS + 5,  // item 7 klucz is on 5th person Julia
  NUM_FLOORS + NUM_PERSONS + 11, // item 8 kwiat is in item 11 nasionko
  NUM_FLOORS + 2,  // item 9 list is on 2nd person
  NUM_FLOORS + 5,  // item 10 mlotek is on person 5 Julia
  0,               // item 11 nasionko is on ground_floor
  1,               // item 12 noz is on 1st floor
  2,               // item 13 okulary is on 2nd floor
  3,               // item 14 pierscionek is on 3rd floor
  NUM_FLOORS + 4,  // item 15 przepis na piwo is on person 4 Jozef
  5,               // item 16 ramka is on 5th floor
  6,               // item 17 szkatulka is on 6th floor
  0,               // item 18 worek ziemniakow is on 0th floor
  8,               // item 19 zdjecie is on 8th floor
  0                // item 20 wyrok(zegarek) is on ground floor
  // nakaz aresztowania
  // wezwanie
  // 
};
char people_on_board;
char max_people_on_board;
char hospitalized_person;
char removed_person;
char rozsadek_rzadu = 3;   // !!! make it bigger?; declines with rejections of smutni
char smutni_target = -1;   // noone on target

//char exiting[NUM_PERSONS];
char ex_count;
unsigned long exiting_flags = 0;
// uses curr_floor
void exit_lift(char person) {
  lift_obj[person] = curr_floor;
  //exiting[ex_count] = person;
  set_bit_flag(&exiting_flags, person);
  ex_count++;
  people_on_board--;
}
char forced_ex_count = 0;
unsigned long forced_exiting_flags = 0;
void force_exit_lift(char person) {
  lift_obj[person] = curr_floor;
  //exiting[ex_count] = person;
  set_bit_flag(&forced_exiting_flags, person);
  forced_ex_count++;
  people_on_board--;
}
void move_person_by_stairs(char person, char to_floor) {
  // ignore if person is not on real floor (dont move her from cabin)
  if(lift_obj[person] >= PLACE_CABIN)
    return;
  lift_obj[person] = to_floor;
}
void move_item_floor_to_floor(char item, char to_floor) {
  // ignore if item is in cabin or on person
  if(lift_obj[NUM_PERSONS + item] >= PLACE_CABIN)
    return;
  lift_obj[NUM_PERSONS + item] = to_floor;
}

// uses curr_floor
void hospitalize(char person) {
  lift_obj[person] = -1;
  hospitalized_person = person;
  people_on_board--;
}

char is_guilty(char person) {
  if(is_item_on_person(NUM_PERSONS + ITEM_WYROK) == person)
    return ITEM_WYROK;
  if(is_item_on_person(NUM_PERSONS + ITEM_NAKAZ) == person)
    return ITEM_NAKAZ;
  if(is_item_on_person(NUM_PERSONS + ITEM_WEZWANIE) == person)
    return ITEM_WEZWANIE;
  return 0;
}
void remove_person(char person) {
  //drop_items(person);  // let she drop ITEM_WYROK only
  //drop_item(NUM_PERSONS + ITEM_WYROK);
  lift_obj[NUM_PERSONS + ITEM_WYROK] = 0;
  lift_obj[person] = -2;
  removed_person = person;
  people_on_board--;
  smutni_target = -1;
}


// sentence modifiers
#define MIANOWNIK_UP        0
#define MIANOWNIK_DOWN      1
#define CELOWNIK_PERSONS    2
#define BIERNIK_PERSONS     3
#define BIERNIK_UP_ITEMS    2
#define BIERNIK_DOWN_ITEMS  3
#define MSG_OFFS_PLACES  50
#define MSG_OFFS_PERSONS 100  // 0 M/, 1 M\, 2 C
#define MSG_OFFS_ITEMS   300
#define MSG_ALT_FLOOR_ANUSZKA 12  // alternative floor announcements
// general sentences
#define MSG_I           75
#define MSG_ORAZ        76
#define MSG_LIFT_FALLING_SCREAM_3 77  // there are three consecutive, replaceable msgs
#define MSG_DOOR_OPEN_2       80          // there are two consecutive msgs
#define MSG_DOOR_CLOSE        82
#define MSG_LIFT_RUNNING      83
#define MSG_LIFT_RUNNING_OVERWEIGHT 84
#define MSG_LIFT_DECELERATING 85
#define MSG_LIFT_DECELERATING_OVERWEIGHT 86
#define MSG_CRASH_2           87

// specific sentences
#define MSG_WSIADA              42
#define MSG_WSIADAJA            43
#define MSG_Z_WINDY_WYSIADA     44
#define MSG_Z_WINDY_WYSIADAJA   45
#define MSG_WYNOSZA             46
#define MSG_OWNS                47
#define MSG_JESTES_U_CELU       48
#define MSG_ANUSZKA_ROZLALA_OLEJ 49
#define MSG_FLOOR_CONTAINS      89   
#define MSG_SHOCKED_DICOVERY_MYSTERY_FLOOR  90 // !!! dodac "mdleje na widok nieznanego piętra"
#define MSG_SAYS_MYSTERY_FLOOR_EXISTS  91  // !!! dodac "mówi, ze istnieje tajemne pietro"
#define MSG_NIE_WPUSZCZONY      92
#define MSG_NIE_WPUSZCZENI      93
#define MSG_SMUTNI_RAPORTUJA_UTRUDNIENIA  94
#define MSG_DOBRA_ZMIANA_IGNOR_LIMITOW  95
#define MSG_NIESTETY            96
#define MSG_WYPROWADZAJA        97
#define MSG_HANDING             98
#define MSG_SMUTNI_CHCA_ZNALEZC 99

#define MSG_MUSI_WYJSC         228
#define MSG_MUSZA_WYJSC        229
#define MSG_NA                 230  // for reporting "na kogo"
#define MSG_WOLAND_INTRO       231
#define MSG_DROPPING            98 // DEBUG!!!, docelowo ma byc 232

// communicate_list(rejected_person_flags, MSG_NIESTETY, MSG_OFFS_PERSONS, MSG_NIE_WPUSZCZONY, MSG_NIE_WPUSZCZENI);

void communicate_list(unsigned long list_flags,      // rejected_person_flags
                      int msg_head,        // MSG_NIESTETY, Z_WINDY_WYSIADA
                      int msg_head_plural, // MSG_NIESTETY, Z_WINDY_WYSIADAJA
                      int msg_offs,        // MSG_OFFS_PERSONS
                      int msg_tail,        // MSG_NIE_WPUSZCZONY
                      int msg_tail_plural) // MSG_NIE_WPUSZCZENI
{
  char item_count = count_set_flags(list_flags);
  if(item_count == 0)
    return;
  if(item_count == 1) {
    add_spk(msg_head);
    //add_spk(MSG_OFFS_PERSONS + exiting[0] * 4 + MIANOWNIK_DOWN);
    add_spk(MSG_OFFS_PERSONS + next_set_flag(list_flags, 0) * 4 + (msg_tail < 0 ? MIANOWNIK_DOWN : MIANOWNIK_UP));
    add_spk(msg_tail);
  }
  else {
    add_spk(msg_head_plural);  // ++ -> plural
    item_count--;         // count 1 less to treat the last item differently
    for(ff=0; ff<item_count; ff++) {
      add_spk(MSG_OFFS_PERSONS + next_set_flag(list_flags, ff) * 4 + MIANOWNIK_UP);
    }
    add_spk(MSG_I + random(2));
    add_spk(MSG_OFFS_PERSONS + next_set_flag(list_flags, ff) * 4 + (msg_tail_plural < 0 ? MIANOWNIK_DOWN : MIANOWNIK_UP));
    add_spk(msg_tail_plural);
  }
/*
  
  char last_item = -1;
  for(char item = 0; item < 32; item++) {
    if(list_flags & 1UL) {
      item_count++;
      if(item_count == 1) {
        add_spk(msg_head);
      }
      add_spk(msg_offs + item * 4 + MIANOWNIK_UP);
      last_item = item;
    }
    list_flags >>= 1;
  }
  if(item_count == 0)
    return;
  if(item_count == 1)
    add_spk(msg_tail);
  else {
    chg_spk(MSG_I + random(2));
    add_spk(msg_offs + last_item * 4 + MIANOWNIK_UP);
    add_spk(++msg_tail);  // ++ -> plural
  }

  if(is_bit_flag(rejected_person_flags, PERSON_SMUTNI)) {
    add_spk(MSG_SMUTNI_RAPORTUJA_UTRUDNIENIA);
  }
  if(rozsadek_rzadu == 1) {
    add_spk(MSG_DOBRA_ZMIANA_IGNOR_LIMITOW);
    rozsadek_rzadu = 0;  // do not inform again
  }    
*/
}



void communicate_exits() {
  char ff;
  if(hospitalized_person > -1) {
    add_spk(MSG_WYNOSZA);
    add_spk(MSG_OFFS_PERSONS + hospitalized_person * 4 + BIERNIK_PERSONS);
  }
  if(removed_person > -1) {
    add_spk(MSG_WYPROWADZAJA);
    add_spk(MSG_OFFS_PERSONS + removed_person * 4 + BIERNIK_PERSONS);
//    communicate_dropped(removed_person);  // the person drops whatever it had
  }

  communicate_list(exiting_flags, MSG_Z_WINDY_WYSIADA, MSG_Z_WINDY_WYSIADAJA, MSG_OFFS_PERSONS, -1, -1); // persons, head, head plural, pers offset, tail, tail plural
/*
  if(ex_count == 0)
    return;
  if(ex_count == 1) {
    add_spk(MSG_Z_WINDY_WYSIADA);
    //add_spk(MSG_OFFS_PERSONS + exiting[0] * 4 + MIANOWNIK_DOWN);
    add_spk(MSG_OFFS_PERSONS + next_set_flag(exiting_flags, 0) * 4 + MIANOWNIK_DOWN);
  }
  else {
    add_spk(MSG_Z_WINDY_WYSIADAJA);
    for(ff=0; ff<ex_count-1; ff++) {
      //add_spk(MSG_OFFS_PERSONS + exiting[ff] * 4 + MIANOWNIK_UP);
      add_spk(MSG_OFFS_PERSONS + next_set_flag(exiting_flags, ff) * 4 + MIANOWNIK_UP);
    }
    add_spk(MSG_I + random(2));
    //add_spk(MSG_OFFS_PERSONS + exiting[ff] * 4 + MIANOWNIK_DOWN);
    add_spk(MSG_OFFS_PERSONS + next_set_flag(exiting_flags, ff) * 4 + MIANOWNIK_DOWN);
  }
*/
}
//char entering[NUM_PERSONS];
char ent_count;
unsigned long entering_flags = 0;

void enter_lift(char person) {
  lift_obj[person] = PLACE_CABIN;
  // register action for further communication
  //entering[ent_count] = person;
  set_bit_flag(&entering_flags, person);
  ent_count++;
  people_on_board++;
}
void communicate_entries() {
  communicate_list(entering_flags, MSG_WSIADA, MSG_WSIADAJA, MSG_OFFS_PERSONS, -1, -1); // persons, head, head plural, pers offset, tail, tail plural
  if(is_bit_flag(entering_flags, PERSON_WOLAND) && !is_bit_flag(plot_flags, WOLAND_INTRO_DONE)) {
    add_spk(MSG_WOLAND_INTRO);
    set_bit_flag(&plot_flags, WOLAND_INTRO_DONE);
  }
/*
  char ff;
  if(ent_count == 0)
    return;
  if(ent_count == 1) {
    add_spk(MSG_WSIADA);
    //add_spk(MSG_OFFS_PERSONS + entering[0] * 4 + MIANOWNIK_DOWN);
    add_spk(MSG_OFFS_PERSONS + next_set_flag(entering_flags, 0) * 4 + MIANOWNIK_DOWN);
  }
  else {
    add_spk(MSG_WSIADAJA);
    for(ff=0; ff<ent_count-1; ff++) {
      //add_spk(MSG_OFFS_PERSONS + entering[ff] * 4 + MIANOWNIK_UP);
      add_spk(MSG_OFFS_PERSONS + next_set_flag(entering_flags, ff) * 4 + MIANOWNIK_UP);
    }
    add_spk(MSG_I + random(2));
    //add_spk(MSG_OFFS_PERSONS + entering[ff] * 4 + MIANOWNIK_DOWN);
    add_spk(MSG_OFFS_PERSONS + next_set_flag(entering_flags, ff) * 4 + MIANOWNIK_DOWN);
  }
*/
}


char picking[NUM_ITEMS];
char picked[NUM_ITEMS];
char pick_count;
// have person take item from place where she stands
void pick_item(char person, char item_idx) {
  lift_obj[item_idx] = NUM_FLOORS + person;   // move the item to person (person 0 idx in lift_obj[] == NUM_FLOORS)
  // register action for further communication
  picking[pick_count] = person;
  picked[pick_count] = item_idx;
  pick_count++;
}
char dropping[NUM_ITEMS];
char dropped[NUM_ITEMS];
char drop_count;
// have person drop item to the place the person is in and register the action
void drop_item(char item_idx) {
  // assume it's never called if item is not on person
  char person_holding_item = lift_obj[item_idx] - NUM_FLOORS;          // 0x 0111 1111
  char person_location = lift_obj[ person_holding_item ];              // 0x 0111 1111 - person locations are -1..NUM_PLACES-1; do not && ox3F!
  lift_obj[item_idx] = person_location;  // move the item to floor idx
  // register action for further communication
  dropping[drop_count] = person_holding_item;
  dropped[drop_count] = item_idx;
  drop_count++;
}
void drop_items(char person) {
  for(char ff = NUM_PERSONS; ff < NUM_PERSONS + NUM_ITEMS; ff++) {
    if(is_item_on_person(ff) == person)
      drop_item(ff);
  }
}
void communicate_dropped(char person) {
  char person_drop_count = 0;
  char biernik = BIERNIK_UP_ITEMS;
  // count drops of this person
  for(char ff=0; ff < drop_count; ff++) {
    if(dropping[ff] == person) {
      person_drop_count++;
    }
  }
  // say header if drops exist
  if(person_drop_count) {
    add_spk(MSG_OFFS_PERSONS + dropping[ff] * 4 + MIANOWNIK_UP);
    add_spk(MSG_DROPPING);
  }
  // say list of drops
  for(char ff=0; ff < drop_count; ff++) {
    if(dropping[ff] == person) {
      if(--person_drop_count == 0) {
        biernik = BIERNIK_DOWN_ITEMS;
      }
      add_spk(MSG_OFFS_ITEMS + dropped[ff] * 4 + biernik);
      if(person_drop_count == 1) {
        add_spk(MSG_I + random(2));
      }
    }
  }
}


char handing[NUM_ITEMS];  // osoba która daje
char handed[NUM_ITEMS];   // dawany przedmiot
char receiver[NUM_ITEMS]; // osoba która dostaje
char hand_count;
char hand_item_to_other_person(char item_idx, char other_person) {
  // assume it's never called if item is not on person
  char person_holding_item = lift_obj[item_idx] - NUM_FLOORS;
  lift_obj[item_idx] = NUM_FLOORS + other_person;   // move the item to person
  // register action for further communication
  handing[hand_count] = person_holding_item;
  handed[hand_count] = item_idx - NUM_PERSONS;
  receiver[hand_count] = other_person;
  hand_count++;
}

void communicate_handed() {
  for(char ff=0; ff < hand_count; ff++) {
    add_spk(MSG_OFFS_PERSONS + handing[ff] * 4 + MIANOWNIK_UP);
    add_spk(MSG_HANDING);
    add_spk(MSG_OFFS_PERSONS + receiver[ff] * 4 + CELOWNIK_PERSONS);
    add_spk(MSG_OFFS_ITEMS + handed[ff] * 4 + BIERNIK_DOWN_ITEMS);
  }
}
void clear_lift_world_queues() {
  //memset(exiting, 0, sizeof(exiting));
  exiting_flags = 0;
  ex_count = 0;
  forced_exiting_flags = 0;
  forced_ex_count = 0;
  //memset(entering, 0, sizeof(entering));
  entering_flags = 0;
  ent_count = 0;
  memset(picking, 0, sizeof(picking));
  memset(picked, 0, sizeof(picked));
  pick_count = 0;
  memset(dropping, 0, sizeof(dropping));
  memset(dropped, 0, sizeof(dropped));
  drop_count = 0;
  memset(handing, 0, sizeof(handing));
  memset(handed, 0, sizeof(handed));
  memset(receiver, 0, sizeof(receiver));
  hand_count = 0;
  hospitalized_person = -1;  // -1 - noone to hospitalize, -2 - hospitalize someone in lift
  removed_person = -1;
  rejected_person_flags = 0;
}
// checks if object idx is a person (and not an item)
// return -1 or person_id
char is_person(char obj_idx) {  // obj_idx is an idx of lift_obj[] (0=first person)
  if(obj_idx < NUM_PERSONS)     // it is a person
    return obj_idx;  // obj_idx is incidentally a person id
  else
    return -1;
  //return obj_id - NUM_FLOORS;
  /*
  if((lift_obj[obj_id] >> 6) == 0)
    return obj_id;
  else
    return 0;
  */
}
// checks if object id is a floor or a cabin (and not person or item)
// return -1 or place id (there is no place idx!)
char is_place(char location) {
  if(location < NUM_FLOORS)
    return location;
  else
    return -1;
}
// checks if object(item) idx is located on person (owned by it) or not
// return -1 or person_id
char is_item_on_person(char obj_idx) {
  char location_id = lift_obj[obj_idx];
  if(location_id < NUM_FLOORS)                  // it is on floor
    return -1;
  if(location_id >= NUM_FLOORS + NUM_PERSONS)   // it is on other item
    return -1;
  return location_id - NUM_FLOORS;
}
char is_at_place(char obj_idx) {
  if(obj_idx < 0)
    return -1;
  char location = lift_obj[obj_idx];
  return is_place(location);  // is_place() returns place_id, or -1
}


char want_to_exit(char person) {  // equal to idx in lift_obj[]!
  switch(person) {
    // ania/anuszka z olejem zawsze wysiada na 6, bez oleju na parterze
    case PERSON_ANIA:
    case PERSON_ANUSZKA:
      if((is_item_on_person(NUM_PERSONS + ITEM_OLEJ_SLONECZNIKOWY)) == person) {
      // ma olej
        if(curr_floor == 6)
          return true;
      } else {
      // nie ma oleju
        if(curr_floor == 0)
          return true;
      }
      break;
    case PERSON_WOLAND:
      if(curr_floor == 11)
        return true;
      else
        return false;
      break;

    case PERSON_SMUTNI:
      // if have nakaz, do not leave on parter
      // else if target is here, do not leave
      // else leave 30%
      if(is_item_on_person(NUM_PERSONS + ITEM_WYROK) == PERSON_SMUTNI ||
         is_item_on_person(NUM_PERSONS + ITEM_NAKAZ) == PERSON_SMUTNI ||
         is_item_on_person(NUM_PERSONS + ITEM_WEZWANIE) == PERSON_SMUTNI) {
        // smutni maja przy sobie jakis wyrok lub nakaz
        if(curr_floor == PLACE_GROUND_FLOOR)
          return false;    // no exit on ground floor if have doc to deliver
        else {
          if(smutni_target > -1 &&
             is_at_place(smutni_target) == curr_floor) {
            // target jest na tym pietrze, wysiadamy do niego
            return true;
          }
        }
      } else {
        // no paper to deliver; exit at ground floor
        if(curr_floor == PLACE_GROUND_FLOOR)
          return true;    // no exit on ground floor if have doc to deliver        
      }
      break;
  }

  // wszyscy z nakazem, poza smutnymi, wysiadają tylko na parterze
  if(person != PERSON_SMUTNI) {
    if(is_item_on_person(NUM_PERSONS + ITEM_WYROK) == person ||
       is_item_on_person(NUM_PERSONS + ITEM_NAKAZ) == person ||
       is_item_on_person(NUM_PERSONS + ITEM_WEZWANIE) == person) {
        // ma doręczony nakaz, wyrok, wezwanie; wiec wysiada tylko na parterze
      if(curr_floor == PLACE_GROUND_FLOOR)
        return true;
      else
        return false;
    }
  }

  return ((person % NUM_FLOORS) == curr_floor && random(10) < 8) ||
         (curr_floor == 0 && random(10) < 5) ||
         random(10) < 3;
}

char person_loc(char person) {
  return lift_obj[person];
}

void communicate_rejections() {
  communicate_list(rejected_person_flags, 
                   MSG_NIESTETY, MSG_NIESTETY,
                   MSG_OFFS_PERSONS, 
                   MSG_NIE_WPUSZCZONY, MSG_NIE_WPUSZCZENI);
  
  if(is_bit_flag(rejected_person_flags, PERSON_SMUTNI)) {
    add_spk(MSG_SMUTNI_RAPORTUJA_UTRUDNIENIA);
  }
  if(rozsadek_rzadu == 1) {
    add_spk(MSG_DOBRA_ZMIANA_IGNOR_LIMITOW);
    rozsadek_rzadu = 0;  // do not inform again
  }    
}
  
void communicate_forced_exits() {
  communicate_list(forced_exiting_flags, 
                   MSG_NIESTETY, MSG_NIESTETY,
                   MSG_OFFS_PERSONS, 
                   MSG_MUSI_WYJSC, MSG_MUSZA_WYJSC);
}

char want_to_enter(char person) {
  // assumption: person is on the curr_floor
  if(people_on_board >= max_people_on_board && is_bit_flag(plot_flags, LIMITS_APPLY_FLAG) &&
     person < PERSON_SREBRNY) {  // hack: excludes also PERSON_WOLAND, ANUSZKA, MALGORZATA, BEHEMOT
    set_bit_flag(&unhappy_person_flags, person);
    set_bit_flag(&rejected_person_flags, person);
    return false;
  }
  clear_bit_flag(&unhappy_person_flags, person);  // who's not rejected - gets happy
  
  // assumption: person is on curr_floor
  switch(person) {
    // ania/anuszka z olejem zawsze wsiada na parterze
    case PERSON_ANIA:
    case PERSON_ANUSZKA:
      if((is_item_on_person(NUM_PERSONS + ITEM_OLEJ_SLONECZNIKOWY)) == person) {
      // ma olej
        if(curr_floor == 0)
          return true;
      // jak nie ma oleju to wsiada na zasadach ogólnych
      }
      break;
  }
  switch(person) {
    // Woland wsiada do windy jesli alter ego ktorejs mary nie zyje, a mara jest na 11 pietrze
    case PERSON_WOLAND:
      if(person_loc(PERSON_ANIA) == -1 && person_loc(PERSON_ANUSZKA) == 11 ||
         person_loc(PERSON_GOSIA) == -1 && person_loc(PERSON_MALGORZATA) == 11 ||
         person_loc(PERSON_RYSIU) == -1 && person_loc(PERSON_KOT_BEHEMOT) == 11)
        return true;
      else
        return false;
      break;
    // Mary z piekiel wsiadaja do windy jesli jest tu Woland
    case PERSON_ANUSZKA:
      if(person_loc(PERSON_WOLAND) == curr_floor || person_loc(PERSON_WOLAND) == PLACE_CABIN) {
        if(person_loc(PERSON_ANIA) == -1)
          return true;
        else
          return false;
      }
      break;
    case PERSON_MALGORZATA:
      if(person_loc(PERSON_WOLAND) == curr_floor || person_loc(PERSON_WOLAND) == PLACE_CABIN) {
        if(person_loc(PERSON_GOSIA) == -1)
          return true;
        else
          return false;
      }
      break;
    case PERSON_KOT_BEHEMOT:
      if(person_loc(PERSON_WOLAND) == curr_floor || person_loc(PERSON_WOLAND) == PLACE_CABIN) {
        if(person_loc(PERSON_RYSIU) == -1)
          return true;
        else
          return false;
      }
      break;
    case PERSON_SMUTNI:
      //if(smutni_target > -1) // && 
         //person_loc(smutni_target) == PLACE_CABIN)
        return true;  // smutni wchodza jesli ich target jest w windzie
      break;
  }
  return random(2); //person == curr_floor;
}


char are_other_persons_here(char obj_idx) {  // obj_id - we expect index in lift_obj[] here
  char object_location = lift_obj[obj_idx];
  char other_person = 0;
  // loop all people and get the last being at the same place
  for(char possible_neighbor = 0; possible_neighbor < NUM_PERSONS; possible_neighbor++) {
    if(possible_neighbor != obj_idx) { // skip given object
      if(lift_obj[possible_neighbor] == object_location)
        other_person = possible_neighbor;
    }
  }
  return other_person + 1;
}
char want_to_be_dropped(char item) {
  return random(10) == 0;
}
char want_to_be_picked(char picking_person, char item) {
  // ignore person at the moment, but it may be usable later
  return random(10) < 5;
}
char communicate_person_owns(char person) {
  // loop through items and find those on the person
  char own_count = 0;
  char biernik = BIERNIK_UP_ITEMS;
  char item;
  for(char ff = NUM_PERSONS; ff < NUM_PERSONS + NUM_ITEMS; ff++) {
    if(is_item_on_person(ff) == person)
      own_count++;
  }
  if(own_count > 0) {
    add_spk(MSG_OFFS_PERSONS + person * 4 + MIANOWNIK_UP);
    add_spk(MSG_OWNS);
  } else {
    return 0;
  }
  for(char ff = NUM_PERSONS; ff < NUM_PERSONS + NUM_ITEMS; ff++) {
    if(is_item_on_person(ff) == person) {
      item = ff - NUM_PERSONS;
      if(--own_count == 0 && item != ITEM_WYROK) {
        biernik = BIERNIK_DOWN_ITEMS;  // if last item and not a sentence, use closing pronounciation
      }
      add_spk(MSG_OFFS_ITEMS + item * 4 + biernik);  // item name
      // who the sentence refers to
      if(item == ITEM_WYROK) {
        add_spk(MSG_NA);
//          say_num(MSG_OFFS_PERSONS + smutni_target * 4 + BIERNIK_PERSONS);
//          add_spk(76);  // 76 = oraz
          add_spk(MSG_OFFS_PERSONS + smutni_target * 4 + BIERNIK_PERSONS);
//          add_spk(9);
      }
      if(own_count == 1)
        add_spk(MSG_I + random(2));
    }
  }
/*
  if(own_count == 1) {
    chg_spk(MSG_OFFS_ITEMS + item * 4 + BIERNIK_DOWN_ITEMS);  // item name
  } else if(own_count > 1) {
    chg_spk(MSG_I + random(2));
    add_spk(MSG_OFFS_ITEMS + item * 4 + BIERNIK_DOWN_ITEMS);  // item name
  }
*/
}
void communicate_possessions_in_cabin() {
  char person;
  for(char ff = 0; ff < NUM_PERSONS + NUM_ITEMS; ff++) {    // zawezic do samych persons !!!
    if((person = is_person(ff)) > -1) {             // zlikwidowac !!!
      //person--; // is_person() returns id, to use -1 as false
      if(is_at_place(person) == PLACE_CABIN) {
        // person in cabin
        communicate_person_owns(person);
      }
    }
  }
}
void communicate_possessions_entered_to_cabin() {
  char person;
  for(char ff = 0; ff < ent_count; ff++) {
    // person in cabin
    //communicate_person_owns(entering[ff]);
    communicate_person_owns(next_set_flag(entering_flags, ff));
  }
}

// comunicate target of smutni if they have just entered the lift
void communicate_target_of_entering_smutni() {
  if(is_bit_flag(entering_flags, PERSON_SMUTNI)) {
    if(smutni_target > -1) {
      add_spk(MSG_SMUTNI_CHCA_ZNALEZC);
      add_spk(MSG_OFFS_PERSONS + smutni_target * 4 + BIERNIK_PERSONS);
    }
  }
}

char num_items_on_floor(char floor_num) {
  // loop through items and find those on the floor
  char own_count = 0;
  for(char ff = NUM_PERSONS; ff < NUM_PERSONS + NUM_ITEMS; ff++) {
    if(is_at_place(ff) == floor_num) {
      own_count++;
    }
  }
  return own_count;
}
char communicate_floor_contains(char floor_num) {
  // loop through items and find those on the floor
  char own_count = 0;
  char item;
  for(char ff = NUM_PERSONS; ff < NUM_PERSONS + NUM_ITEMS; ff++) {
    if(is_at_place(ff) == floor_num) {
      if(own_count == 0) {
        add_spk(MSG_FLOOR_CONTAINS);
      }
      own_count++;
      item = ff - NUM_PERSONS;
      add_spk(MSG_OFFS_ITEMS + item * 4 + MIANOWNIK_UP);  // item name
    }
  }
  if(own_count == 1) {
    chg_spk(MSG_OFFS_ITEMS + item * 4 + MIANOWNIK_DOWN);  // change item name
  } else if(own_count > 1) {
    chg_spk(MSG_I + random(2));
    add_spk(MSG_OFFS_ITEMS + item * 4 + MIANOWNIK_DOWN);  // item name
  }
}

// Pick random nuts person in cabin to speak about mystery floor
void communicate_mystery_floor_gossip() {
  char nuts_count = 0;
  do {
    for(char person = 0; person < NUM_PERSONS; person++) {
      if(is_bit_flag(nuts_person_flags, person))
      if(is_at_place(person) == PLACE_CABIN) {
        nuts_count++;
        if(random(10) < nuts_count) {
          add_spk(MSG_OFFS_PERSONS + person * 4 + MIANOWNIK_UP);
          if(curr_floor == PLACE_MYSTERY_FLOOR)
            add_spk(MSG_SHOCKED_DICOVERY_MYSTERY_FLOOR);
          else
            add_spk(MSG_SAYS_MYSTERY_FLOOR_EXISTS);
          return;
        }
      }
    }
  } while (nuts_count > 0);
}


void communicate_premigration_stuff() {
  //if(anuszka_broke_oil == 1) {
  if(is_bit_flag(plot_flags, ANUSZKA_BROKE_OIL) && !is_bit_flag(plot_flags, ANUSZKA_BROKE_OIL_SILENT)) {
    set_bit_flag(&plot_flags, ANUSZKA_BROKE_OIL_SILENT);  // do not communicate next time
    add_spk(MSG_ANUSZKA_ROZLALA_OLEJ);
  }
}

void communicate_nuts() {
  
}

void migrate_objs() {
  char person;
  char place;
  char other_person;
  for(char ff = 0; ff < NUM_PERSONS; ff++) {
    if((person = is_person(ff)) > -1) {        // zlikwidowac!!!
      //person--; // is_person() returns id, to use -1 as false
      if((place = is_at_place(person)) == PLACE_CABIN) {
        //place--;
        // person in cabin
        if(hospitalized_person == -2) {
          hospitalize(person);  // it also changes hospitalized_person to person id
        }
        else
        if(curr_floor == PLACE_GROUND_FLOOR && is_guilty(person) && smutni_target == person) {
          remove_person(person);
        }
        else
        if(want_to_exit(person))
          exit_lift(person);
      } 
      /*
        else {
        place--;
        // person not in cabin
        if((place == curr_floor) && want_to_enter(person))
          enter_lift(person);
      }
      */
    }
  }
  for(char ff = 0; ff < NUM_PERSONS; ff++) {
    if((person = is_person(ff)) > -1) {      // zlikwidowac!!!
      //person--; // is_person() returns id, to use -1 as false
      if((place = is_at_place(person)) == curr_floor) {
        //place--;
        // person not in cabin
        if(!is_bit_flag(exiting_flags, person) && want_to_enter(person))
          enter_lift(person);
      }
    }
  }
  // now force exits if srebrny dev entered the lift
  if(is_at_place(PERSON_SREBRNY) == PLACE_CABIN) {
    for(char ff = 0; ff < NUM_PERSONS; ff++) {
      if(ff == PERSON_SREBRNY)  // ignore srebrny
        continue;
      if((is_at_place(ff)) == PLACE_CABIN) {
        // person in cabin
        force_exit_lift(ff);
      }
    }
  }
    
  {
    /*
    else
    // it is an item, not a person; check if it sits on a person
    if((person = is_item_on_person(ff)) > 0) {
      person--;
      if((other_person = are_other_persons_here(person)) > 0) {
        other_person--;
        // want to be handed to someone else?
        hand_item_to_other_person(ff, other_person);
      } else {
        if(want_to_be_dropped(ff))
          drop_item(ff);
      }
    } else
    // it does not sit on a person; check if it sits on place (floor or cabin)
    if((place = is_at_place(ff)) > -1) {
      //place--;
      if((other_person = are_other_persons_here(ff)) > 0) {
        other_person--;
        if(want_to_be_picked(other_person, ff)) {
          pick_item(other_person, ff);
        }
      }
    } else
    // it is not at place, it is at other item; ignore it
    {}
    */
  }
}
// call before clear_lift_world_queues()
char just_entered(char person) {
  for(char ff=0; ff<ent_count; ff++)
    //if(entering[ff] == person)
    if(next_set_flag(entering_flags, ff) == person)
      return true;
  return false;
}
// call before clear_lift_world_queues()
char just_exited(char person) {
  for(char ff=0; ff<ex_count; ff++)
    //if(exiting[ff] == person)
    if(next_set_flag(exiting_flags, ff) == person)
      return true;
  return false;
}
char get_next_target() {
  for(char person=0; person < NUM_PERSONS; person++) {
    if(person_loc(person) > -1 && 
       is_bit_flag(targeted_person_flags, person)) {
      return person;
    }
  }
  return -1;
}
void proceed_after_migration() {
  char person;
  char place;
  char other_person;
  if(is_at_place(PERSON_WOLAND) == PLACE_MYSTERY_FLOOR) {     // woland exited on floor 11
    move_person_by_stairs(PERSON_WOLAND, 0);  // move woland to ground floor
  }
  if(just_entered(PERSON_WOLAND))
    floors[PLACE_MYSTERY_FLOOR] = 1;  // Woland presses invisible key to go to floor 11

  if(just_entered(PERSON_ANUSZKA)) {
    set_bit_flag(&plot_flags, ALT_FLOOR_ANNOUNCEMENTS); // make anuszka announce floors from now on
    //alt_floor_announcements = MSG_ALT_FLOOR_ANUSZKA;
  }

  // there was a crash and anuszka was in it and she had an oil
  if((hospitalized_person == -2 || hospitalized_person >= 0) &&     // there was a crash
     (just_exited(PERSON_ANUSZKA) || person_loc(PERSON_ANUSZKA) == PLACE_CABIN ||
      hospitalized_person == PERSON_ANUSZKA) && !just_entered(PERSON_ANUSZKA) && // anuszka was there
     (is_item_on_person(NUM_PERSONS + ITEM_OLEJ_SLONECZNIKOWY) == PERSON_ANUSZKA) // anuszka had oil
    ) {
    //anuszka_broke_oil = 1; // communicate broken oil
    set_bit_flag(&plot_flags, ANUSZKA_BROKE_OIL);  // communicate broken oil
  }
     
  // ania na 6 pietrze zostawia olej, a na innych pietrach go bierze
  if(just_exited(PERSON_ANIA)) {
    // anuszka takes the oil if it is not on 6th floor
     if(curr_floor != 6) {
       if(is_at_place(NUM_PERSONS + ITEM_OLEJ_SLONECZNIKOWY) == curr_floor)
         pick_item(PERSON_ANIA, NUM_PERSONS + ITEM_OLEJ_SLONECZNIKOWY);
     } else {
       if(is_item_on_person(NUM_PERSONS + ITEM_OLEJ_SLONECZNIKOWY) == PERSON_ANIA)
         drop_item(NUM_PERSONS + ITEM_OLEJ_SLONECZNIKOWY);
     }
  }
  // anuszka na 6 pietrze zostawia olej, a na innych pietrach go bierze
  if(just_exited(PERSON_ANUSZKA)) {
    // anuszka takes the oil if it is not on 6th floor
     if(curr_floor != 6) {
       if(is_at_place(NUM_PERSONS + ITEM_OLEJ_SLONECZNIKOWY) == curr_floor)
         pick_item(PERSON_ANUSZKA, NUM_PERSONS + ITEM_OLEJ_SLONECZNIKOWY);
     } else {
       if(is_item_on_person(NUM_PERSONS + ITEM_OLEJ_SLONECZNIKOWY) == PERSON_ANUSZKA)
         drop_item(NUM_PERSONS + ITEM_OLEJ_SLONECZNIKOWY);
     }
  }

  if(curr_floor == PLACE_MYSTERY_FLOOR)
  for(char ff = 0; ff < NUM_PERSONS; ff++) {
    if((person = is_person(ff)) > -1) {     // zlikwidowac!!!
      //person--; // is_person() returns id, to use -1 as false
      if((is_at_place(person)) == PLACE_CABIN) {
        //place--;
        switch(person) {
          case PERSON_ANUSZKA:
          case PERSON_WOLAND:
          case PERSON_KOT_BEHEMOT:
          case PERSON_MALGORZATA:
            break;  // ignore them
          default:
            // other persons on floor 11 get nuts
          set_bit_flag(&nuts_person_flags, person);
        }
      }
    }
  }

  // smutni sie skarza centrali jesli maja na kogos wyrok a nie moga wejsc do windy
  if(is_bit_flag(rejected_person_flags, PERSON_SMUTNI)) { // && is_item_on_person(NUM_PERSONS + ITEM_WYROK) == PERSON_SMUTNI) {
    if(rozsadek_rzadu > 0)
      rozsadek_rzadu--;
    if(rozsadek_rzadu == 1)
      clear_bit_flag(&plot_flags, LIMITS_APPLY_FLAG);  // gvmt orders ignoring weight limit of the lift
  }

  if(smutni_target >= 0 &&  // smutni maja kogos na celowniku; szukaja kogos
     (is_at_place(PERSON_SMUTNI) == is_at_place(smutni_target)) &&    // smutni i ich target sa na tym samym pietrze
     is_item_on_person(NUM_PERSONS + ITEM_WYROK) == PERSON_SMUTNI) {  // smutni mają przy sobie wyrok
    hand_item_to_other_person(NUM_PERSONS + ITEM_WYROK, smutni_target);  // smutni dają nakaz targetowi
  }

  // wpisz na sile usunietych z windy na liste targetow
  targeted_person_flags |= forced_exiting_flags;

  if((just_exited(PERSON_SMUTNI) || person_loc(PERSON_SMUTNI) <= PLACE_GROUND_FLOOR) && // smutni wlasnie wyszli z windy lub sa poza budynkiem
     curr_floor == PLACE_GROUND_FLOOR &&        // jestesmy na parterze
     smutni_target == -1 &&                     // smutni nie maja w tej chwili targetu
     targeted_person_flags &&                   // jacys ludzie podpadli wladzy
     is_at_place(NUM_PERSONS + ITEM_WYROK) == PLACE_GROUND_FLOOR) {  // wyrok mozna podniesc na parterze
    smutni_target = get_next_target();          // smutni dostają target do schwytania
    pick_item(PERSON_SMUTNI, NUM_PERSONS + ITEM_WYROK);       // smutni podnoszą wyrok
    lift_obj[PERSON_SMUTNI] = PLACE_GROUND_FLOOR;             // smutni wracają na parter, nawet jeśli byli poza budynkiem
  }

/*
  if(just_entered(PERSON_SPY))
    person_spy = PERSON_WLADEK;
  else
    person_disclosing_floor_contents = -1;
*/
  
  /*
  if(hospitalized_person == PERSON_ANIA) {
    move_person_by_stairs(PERSON_ANIA, 11);  // have ania appear on 11th floor waiting for woland
  }
  
  for(char ff = 0; ff < NUM_PERSONS + NUM_ITEMS; ff++) {
    if((person = is_person(ff)) > -1) {
      //person--; // is_person() returns id, to use -1 as false
      if((place = is_at_place(person)) == (PLACE_CABIN)) {
        place--;
        // person in cabin
        if(hospitalized_person == -2) {
          hospitalize(person);  // it also changes hospitalized_person to person id
        }
        else
        if(want_to_exit(person))
          exit_lift(person);
      } else {
        place--;
        // person not in cabin
        if((place == curr_floor) && want_to_enter(person))
          enter_lift(person);
      }
    }
  }
  */
}
void activate_objs() {
  
}

/* END of LIFT WORLD SIMULATION */





/* SHIFT REGISTER (SR) AND EVENT RING ROUTINES */

void send_to_sr(unsigned long data) {
  digitalWrite(SHIFT_LATCH_PIN, LOW);
  shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, LSBFIRST, data);
  data >>= 8;
  shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, LSBFIRST, data);
  data >>= 8;
  shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, LSBFIRST, data);
  data >>= 8;
  shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, LSBFIRST, data);
  digitalWrite(SHIFT_LATCH_PIN, HIGH);
}

// event ring buffers
#define MAX_ERING 64
unsigned char ering_pin_data[MAX_ERING];  // bits: 0,1,1,11111 -> unused, init, polarity, pin number
unsigned char ering_head = 0;
unsigned char ering_tail = ering_head;
unsigned char duration;
#define DURATION_INIT_BIT_NEG 0xBF  // 1011 1111
#define DURATION_INIT_BIT     0x40  // 0100 0000
#define POLARITY_BIT          0x20  // 0010 0000
#define PIN_NUM_BITS          0x1F  // 0001 1111
#define POLARITY_CLEAR   -1
#define POLARITY_IGNORE  -2

// output buffer for shift register
unsigned long sr_data = 0x00000000;  // 4 bytes

/*
 * Shift Register pin types:
 *   a. set pin + polarity for specified time; clear both after that; just one of the type energized at a time
 *   b. set or clear pin, ignore polarity; ignore time; allow other pins at the same time
 *   
 *   Note! Pin types are determined by attached hardware. Handle mindfully!
 */
#define SR_PIN_TYPES  0x00FFFFFF    // 0 - regular bool, 1 - bistable relay

/* 
 * Set polarity and single pin state in shift register (sr)
 * polarity:
 *   > 0 - set OFF
 *     0 - set ON
 *    -1 - set NONE
 *   <-1 - ignore polarity
 */
void set_sr_output_pin(int sr_pin_num, char polarity, char energize) {
  unsigned long mask = 1;

    // set proper polarity pin
    if(polarity > 0) {
      digitalWrite(POLARITY_ON_PIN, LOW);
      digitalWrite(POLARITY_OFF_PIN, HIGH); // set the OFF polarity
    } else if(polarity == 0) {
      digitalWrite(POLARITY_OFF_PIN, LOW);
      digitalWrite(POLARITY_ON_PIN, HIGH);  // set the ON polarity
    } else if(polarity == POLARITY_CLEAR) {
      digitalWrite(POLARITY_ON_PIN, LOW);   // clear both polarity pins
      digitalWrite(POLARITY_OFF_PIN, LOW);
    } // else POLARITY_IGNORE

  // set shift register data
  if(energize > 0) {
    // set output pin
    sr_data = sr_data | (mask << sr_pin_num);  // move single 1 num bits
  }
  else {
    // clear output pin
    sr_data = sr_data & (0xffffffff ^ (mask << sr_pin_num)); // move single 1 num bits and invert all
  }
  send_to_sr(sr_data);
}

void process_event_ring_tick() {
  if(ering_head == ering_tail)
    return;
  if(DURATION_INIT_BIT & ering_pin_data[ering_head]) {
    // ering not empty and current entry just begins processing
    set_sr_output_pin(PIN_NUM_BITS & ering_pin_data[ering_head], // get pin number
                      POLARITY_BIT & ering_pin_data[ering_head], // get requested polarity
                      1);                                        // energize
    ering_pin_data[ering_head] &= DURATION_INIT_BIT_NEG; // clear duration init bit
    duration = ENERGIZE_DURATION; // start counting duration
  }
  if(duration) {
    duration--;
  } else {
    // duration ended, remove entry
    set_sr_output_pin(PIN_NUM_BITS & ering_pin_data[ering_head], // get pin number
                      POLARITY_CLEAR,                            // clear polarity
                      0);                                        // deenergize
    // regain ering capacity
    ering_head++;
    if(ering_head == MAX_ERING)
      ering_head = 0;
  }    
}


void add_to_event_ring(char sr_pin_num, char polarity) {
  // if ering current capacity allows
  if(ering_tail < MAX_ERING - 1 && ering_tail != ering_head -1 ||
     ering_tail == MAX_ERING - 1 && ering_head != 0) {
    // encode init flag, polarity and pin number on bits
    ering_pin_data[ering_tail] = DURATION_INIT_BIT | ((polarity > 0) * POLARITY_BIT) | sr_pin_num;
    // decrease current ering capacity
    ering_tail++;
    if(ering_tail == MAX_ERING)
      ering_tail = 0;
  }
}

/*
 * Turn on or off required pin.
 * The proc takes care to identify pin type.
 */
int switch_sr_pin(char pin_num, char onoff) {
  if(is_bit_flag(SR_PIN_TYPES, pin_num)) {
    // bistable relay; use ring to time the output value
    add_to_event_ring(pin_num, onoff);
  } else {
    // regular bool; just set or clear
    set_sr_output_pin(pin_num, POLARITY_IGNORE, onoff);
  }
}

/* END OF SHIFT REGISTER (SR) AND EVENT RING ROUTINES */






void display_debug_2() {
    // debug display - mark state and dir on the keypad
    /*
    if(state == DOOR_OPEN)
      leds[map_key_to_led[0]] = CRGB::Green;
      
    if(dir == -1)
      leds[map_key_to_led[1]] = CRGB::Green;
    else if(dir == 0)
      leds[map_key_to_led[2]] = CRGB::Green;
    else
      leds[map_key_to_led[3]] = CRGB::Green;
    */

    /*
    unsigned char dd = 0;
    for (unsigned char ff=0; ff < MAX_ERING; ff++) {
      if(duration > 0)
        dd++;
    }
    if(dd < NUM_KEYS)
      leds[map_key_to_led[dd]] = CRGB::Blue;

    if(ering_head < NUM_KEYS)
      leds[map_key_to_led[ering_head]] = CRGB::Green;
    if(ering_tail < NUM_KEYS)
      leds[map_key_to_led[ering_tail]] = CRGB::Red;

    leds[0] = CRGB::Blue;
    */
}







void setup() {
      // set up the two polarity pins
      pinMode(POLARITY_ON_PIN, OUTPUT);
      pinMode(POLARITY_OFF_PIN, OUTPUT);
      // turn off both pins ASAP
      digitalWrite(POLARITY_ON_PIN, LOW);
      digitalWrite(POLARITY_OFF_PIN, LOW);

      // set up the output shift register pins
      pinMode(SHIFT_CLOCK_PIN, OUTPUT);
      pinMode(SHIFT_LATCH_PIN, OUTPUT);
      pinMode(SHIFT_DATA_PIN, OUTPUT);
      send_to_sr(0x00000000);  // initialize all shift outputs to 0 ASAP to avoid initial undefined state

      // set up the keyboard pins
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
      
      // Initialize LED library
      FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);

      // initialize each key's mode
      for(key_mode=0; key_mode < NUM_KEY_MODES; key_mode++)
      for(key_num=0; key_num<NUM_KEYS; key_num++) {
        key_state[key_mode][key_num] = 0;
      }
      // preset some keys initially so base lights may be turned ON with one click
      for(key_num=1; key_num<=6; key_num++) {
        key_state[KEY_MODE_SWITCHES_1][key_num] = 1;
      }
      key_state[KEY_MODE_FUNCKEYS][KEY_BELL] = 1;  // set mode to first light bank
      
      for(ff=0; ff<NUM_KEYS; ff++) {
        key_countdown[ff] = MAX_KEY_COUNTDOWN;
      }
      for(ff=0; ff<NUM_KEYS; ff++) {
        key_press_countdown[ff] = MAX_KEY_PRESS_COUNTDOWN;
      }

      // blink all LEDs to indicate the program start
      for(ff=0; ff<NUM_LEDS; ff++) {
        leds[ff] = CRGB::White;
      }
      FastLED.show();
      delay(1000);
      for(ff=0; ff< NUM_LEDS; ff++) {
        leds[ff] = CRGB::Black;
      }
      FastLED.show();

      // Initializes the wtv sound module
      wtv020sd16p.reset();
      clear_lift_world_queues();
      people_on_board = 0;
      max_people_on_board = (NUM_PERSONS / 2) > 2 ? 2 : NUM_PERSONS / 2;

      // Initialize the plot related data
 // DEBUG!!!     set_bit_flag(&plot_flags, LIMITS_APPLY_FLAG); // start with people obeying lift limits

      // Initially send OFF events to all shift outputs
      for(ff=0; ff<SHIFT_NUM_BITS; ff++) {
        switch_sr_pin(ff, 0);
      }
}


#define DEBUG_DELAY  * 0 + 1

void loop() {

  // process switch register event ring
  process_event_ring_tick();

  // process speaking queue
  spk_que_tick();

  
  // find new target floor after reaching the previous target
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
             state_countdown = 1200 DEBUG_DELAY;  // how long it will be stopping
           }
           if(state_countdown == 0) {
             state = LIFT_STOPPED;
             wtv020sd16p.asyncPlayVoice(MSG_OFFS_PLACES + curr_floor + 
                     is_bit_flag(plot_flags, ALT_FLOOR_ANNOUNCEMENTS) * MSG_ALT_FLOOR_ANUSZKA);  // Announcing "floor X"
           }
           break;
    case LIFT_STOPPED:
           if(last_state != state) {
             last_state = state;
             state_countdown = 950 DEBUG_DELAY;  // how long it will stand still after stopping
           }
           if(state_countdown == 0) {
             state = DOOR_OPENING;
             wtv020sd16p.asyncPlayVoice(MSG_DOOR_OPEN_2 + random(2)); // the sound of opening door
           }
           break;
    case DOOR_OPENING:
           if(last_state != state) {
             last_state = state;
             state_countdown = 1600 DEBUG_DELAY;  // how long takes opening the door
           }
           if(state_countdown == 0) {
             state = DOOR_OPEN;
             //wtv020sd16p.asyncPlayVoice(3);   // the door has been opened
           }
           break;
    case DOOR_OPEN:
           //enter_lift_state(500);
           if(last_state != state) {
             last_state = state;
             state_countdown = 200 DEBUG_DELAY;  // for how long minimum the door stays open
           }
           if(state_countdown == 0) {
             state = PASSENGERS_MOVEMENT;
             //wtv020sd16p.asyncPlayVoice(11);  // the sound of passenger movement
             migrate_objs();  // floor <-> lift movements
             proceed_after_migration();  // keep moving or play after migrations
             communicate_premigration_stuff();
             communicate_exits();
             communicate_entries();
             communicate_rejections();
             communicate_forced_exits();
             communicate_handed();
             communicate_possessions_entered_to_cabin();
             //communicate_target_of_entering_smutni();
             communicate_mystery_floor_gossip();
             if(is_at_place(person_spy) == (PLACE_CABIN))
               communicate_floor_contains(curr_floor);
             clear_lift_world_queues();  // queues already loaded to communication
             //communicate_possessions_in_cabin();
           }
           break;
    case PASSENGERS_MOVEMENT:
           //enter_lift_state(500);
           if(last_state != state) {
             last_state = state;
             state_countdown = 1500 DEBUG_DELAY;  // for how long the passenger enters or exits the lift
           }
           if(is_speaking())
             state_countdown = 500;  // keep waiting until communicating object moves
           if(state_countdown == 0) {
             if(target_floor != curr_floor) {
               state = DOOR_CLOSING;
               wtv020sd16p.asyncPlayVoice(MSG_DOOR_CLOSE);  // the sound of closing door
             }
           }
           break;
    
    case DOOR_CLOSING:
           if(last_state != state) {
             last_state = state;
             state_countdown = 700 DEBUG_DELAY;  // how long takes closing the door
           }
           if(state_countdown == 0) {
             state = DOOR_CLOSED;
             //wtv020sd16p.asyncPlayVoice(5);   // bump of door being closed 
           }
           break;
    case DOOR_CLOSED:
           if(last_state != state) {
             last_state = state;
             state_countdown = 100 DEBUG_DELAY;  // for how long the lift holds after closing the door before it starts running
           }
           if(state_countdown == 0) {
             if(target_floor != curr_floor) {
               state = LIFT_STARTING;
               //wtv020sd16p.asyncPlayVoice(6);   // sound of accelerating engines
             }
           }
           break;
    case LIFT_STARTING:
           if(last_state != state) {
             last_state = state;
             state_countdown = 500 DEBUG_DELAY;  // for how long the lift decides where to go
           }
           if(state_countdown == 0) {
             if(dir > 0) {
               state = LIFT_RUNNING_UP;
               if(people_on_board > max_people_on_board)
                 wtv020sd16p.asyncPlayVoice(MSG_LIFT_RUNNING_OVERWEIGHT);   // sound of engines running up full speed
               else
                 wtv020sd16p.asyncPlayVoice(MSG_LIFT_RUNNING);
             }
             else {
               state = LIFT_RUNNING_DOWN;
               if(people_on_board > max_people_on_board)
                 wtv020sd16p.asyncPlayVoice(MSG_LIFT_RUNNING_OVERWEIGHT);
               else
                 wtv020sd16p.asyncPlayVoice(MSG_LIFT_RUNNING);   // sound of engines running down full speed
             }
           }
           break;
    case LIFT_RUNNING_UP:
           if(last_state != state) {
             last_state = state;
             //dir = 1;
             state_countdown = 500 DEBUG_DELAY;  // for how long the lift goes up one floor
           }
           // probability of falling down while going upwards
           if(people_on_board > max_people_on_board + 1 &&
              curr_floor > 5 &&
              random(10000) < curr_floor + people_on_board)
             state = LIFT_START_FALLING;  // test
             
           if(state_countdown == 0) {
             if(curr_floor < NUM_FLOORS)
               curr_floor++;
             // arrived to called floor
             //if(floors[curr_floor] > 0) {
             if(curr_floor == target_floor) {
               state = LIFT_STOPPING;
               floors[curr_floor] = 0;
               //wtv020sd16p.asyncPlayVoice(MSG_LIFT_DECELERATING);  
               if(people_on_board > max_people_on_board)
                 wtv020sd16p.asyncPlayVoice(MSG_LIFT_DECELERATING_OVERWEIGHT);  // sound of deccelerating engines going upward
               else
                 wtv020sd16p.asyncPlayVoice(MSG_LIFT_DECELERATING);
             }
             else {
               last_state = RESTART_THIS_STATE;
               state = LIFT_RUNNING_UP;
               //wtv020sd16p.asyncPlayVoice(7);  // sound of engines running up full speed (again)
             }
           }
           break;
    case LIFT_RUNNING_DOWN:
           if(last_state != state) {
             last_state = state;
             // dir = -1;
             state_countdown = 500 DEBUG_DELAY;  // for how long the lift goes down one floor
           }
           // probability of falling down while going downwards
           if(people_on_board > max_people_on_board + 1 && 
              curr_floor > 5 &&
              random(10000) < curr_floor + people_on_board)
             state = LIFT_START_FALLING;  // test
             
           if(state_countdown == 0) {
             if(curr_floor > 0)
               curr_floor--;
             // arrived to called floor  
             //if(floors[curr_floor] > 0) {
             if(curr_floor == target_floor) {
               state = LIFT_STOPPING;
               floors[curr_floor] = 0;
               //wtv020sd16p.asyncPlayVoice(MSG_LIFT_DECELERATING);  // sound of deccelerating engines going downward
               if(people_on_board > max_people_on_board)
                 wtv020sd16p.asyncPlayVoice(MSG_LIFT_DECELERATING_OVERWEIGHT);  // sound of deccelerating engines going upward
               else
                 wtv020sd16p.asyncPlayVoice(MSG_LIFT_DECELERATING);
             }
             else {
               state = LIFT_RUNNING_DOWN;
               last_state = RESTART_THIS_STATE;
               //wtv020sd16p.asyncPlayVoice(8);  // sound of engines running down full speed (again)
             }
           }
           break;
    case LIFT_START_FALLING:
           if(last_state != state) {
             if(last_state != RESTART_THIS_STATE)
               wtv020sd16p.asyncPlayVoice(MSG_LIFT_FALLING_SCREAM_3 + random(3));  // sound of screaming while falling down
             last_state = state;
             state_countdown = 1000;  // how long take screams before lift starts falling
           }
           if(state_countdown == 0) {
             state = LIFT_FALLING_DOWN;
             //wtv020sd16p.asyncPlayVoice(MSG_OFFS_PLACES + curr_floor);
           }
           break;
    case LIFT_FALLING_DOWN:
           if(last_state != state) {
             last_state = state;
             // dir = -1;
             state_countdown = 150;  // for how long the lift falls down one floor
           }
           target_floor = 0;  // when falling keep setting target floor to 0
           if(state_countdown == 0) {
             if(curr_floor > 0)
               curr_floor--;
             // arrived to called floor  
             //if(floors[curr_floor] > 0) {
             if(curr_floor == target_floor) {
               state = LIFT_CRASHING;
               floors[curr_floor] = 0;
               wtv020sd16p.asyncPlayVoice(MSG_CRASH_2 + random(2));  // sound of crashing lift
             }
             else {
               state = LIFT_FALLING_DOWN;
               last_state = RESTART_THIS_STATE;
               //wtv020sd16p.asyncPlayVoice(8);  // don't stop screaming
             }
           }
           break;
    case LIFT_CRASHING:
           if(last_state != state) {
             last_state = state;
             state_countdown = 2500;  // how long the crash sounds out
             hospitalized_person = -2; // hospitalize single person in lift, if any
           }
           if(state_countdown == 0) {
             state = LIFT_STOPPED;
             //wtv020sd16p.asyncPlayVoice(MSG_OFFS_PLACES + curr_floor + alt_floor_announcements);  // Announcing "floor X"
             wtv020sd16p.asyncPlayVoice(MSG_OFFS_PLACES + curr_floor + 
                is_bit_flag(plot_flags, ALT_FLOOR_ANNOUNCEMENTS) * MSG_ALT_FLOOR_ANUSZKA);  // Announcing "floor X"

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

  
  countdownKbd(0, 22);  // calculate long press and fade for key range
  memcpy(last_key, key, sizeof(last_key));
  readKbd();
  mapPhysKeyToKey(key_state[0][KEY_BELL],            // key 22 - bell, its state determines mode
                  key_state[ key_state[0][KEY_BELL] ][0]);  // key 0 - P, its state determines digits bank

  if(key[KEY_BELL]) {
    // Switch mode only if pressed while lit.
    switch_key_state_while_visible(KEY_BELL, KEY_MODE_FUNCKEYS);
    // DEBUG DISPLAY for key 12
    display_debug_2();
  }
  else {
    if(key[KEY_STOP] && (state == LIFT_RUNNING_UP || state == LIFT_RUNNING_DOWN)) {
      state = LIFT_START_FALLING;
    }
    // REGULAR DISPLAY
    if(key_state[0][KEY_BELL] == 0) {
      // Lift mode
      // detect if key_pressed and update floors[]
      key_pressed = false;
      for(ff=0; ff<NUM_FLOORS; ff++) {
        if(key[ff] == 1) {
            floors[ff] = 1;
            key_pressed = true;
        }
      }
      display_based_on_floor(0, 10);  // from key, to_key
      // uses: curr_floor, blink_state, sleep_countdown, blink_countdown, leds[], map_key_to_led[], floors[]

      // handle STOP key (KEY_STOP)
      manage_key_state(KEY_MODE_FLOOR_STOP);  // from key, to key, mode set
      // display STOP key. In lift mode it uses KEY_MODE_FLOOR_STOP modeset (only black color in this color set)
      display_based_on_mode(LED_STOP, LED_STOP, KEY_MODE_FLOOR_STOP, key_state[KEY_MODE_FLOOR_STOP][KEY_P]);
    }
    else
    if(key_state[0][KEY_BELL] == 1) {
      // Switch/Toggle mode
      manage_key_state(KEY_MODE_SWITCHES_1);
      // display keys from grround floor (LED_P) through numbered floors, up to LED_STOP
      // All these keys use KEY_MODE_SWITCHES_x modeset (color set) in non lift modes.
      display_based_on_mode(LED_P, LED_STOP, KEY_MODE_SWITCHES_1, key_state[KEY_MODE_SWITCHES_1][KEY_P]);
    }
    else
    if(key_state[0][KEY_BELL] == 2) {
      manage_key_state(KEY_MODE_SWITCHES_2);
      display_based_on_mode(LED_P, LED_STOP, KEY_MODE_SWITCHES_2, key_state[KEY_MODE_SWITCHES_2][KEY_P]);
    }
    else
    if(key_state[0][KEY_BELL] == 3) {
      manage_key_state(KEY_MODE_SWITCHES_3);
      display_based_on_mode(LED_P, LED_STOP, KEY_MODE_SWITCHES_3, key_state[KEY_MODE_SWITCHES_3][KEY_P]);
    }
  } //else debug



  // DISPLAY key KEY_BELL
  // key BELL determines modeset for other keys
  display_based_on_mode(LED_BELL, LED_BELL, KEY_MODE_FUNCKEYS, NO_MATTER);
  // uses: key_num, key_countdown[], key_state[][], leds[], map_key_to_led[]


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
