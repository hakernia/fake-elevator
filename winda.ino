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
	  - introduce speech queue to communicate comfortably
	  - introduce a story framework: places(floors), persons, items
	  - introduce elevator falling and crashing
	  - relocate setup() function
	
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
#define NUM_FLOORS    11

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

// relays polarity
#define POLARITY_ON_PIN    11  // L - active
#define POLARITY_OFF_PIN   13  // L - active

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

#define NUM_MODESETS  5  // 0 - lift, 1,2,3 - switches, 4 - lift stop

// map keypad number to number in the LED chain
char keymap[NUM_KEYS] = {7,   // KEY_P
                         6, 8, 5, 9, 4, 10, 3, 11, 2, 12,  // DIGITS
                         6, 8, 5, 9, 4, 10, 3, 11, 2, 12,  // DIGITS
                         1, 0};                            // KEY_STOP, KEY_BELL
char mode[NUM_MODESETS][NUM_KEYS];
char physical_key[NUM_PHYS_KEYS];
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
int dir = 1;
char floors[NUM_KEYS];


char ff;
char modeset_num = 0;
char last_active_modeset_num = 0;




void readKbd() {
  // Rows are strobed by pinMode and not digitalWrite
  // so all rows except the selected one are high impedance.
  //digitalWrite(ROW_1, LOW);
  pinMode(ROW_1, OUTPUT);
    if(digitalRead(COL_1) == LOW)
      physical_key[10] = 1;
    else
      physical_key[10] = 0;
    if(digitalRead(COL_2) == LOW)
      physical_key[8] = 1;
    else
      physical_key[8] = 0;
    if(digitalRead(COL_3) == LOW)
      physical_key[6] = 1;
    else
      physical_key[6] = 0;
    if(digitalRead(COL_4) == LOW)
      physical_key[4] = 1;
    else
      physical_key[4] = 0;
  pinMode(ROW_1, INPUT);
  //digitalWrite(ROW_1, HIGH);
  
  
  //digitalWrite(ROW_2, LOW);
  pinMode(ROW_2, OUTPUT);
  if(digitalRead(COL_1) == LOW)
      physical_key[2] = 1;
    else
      physical_key[2] = 0;
    if(digitalRead(COL_2) == LOW)
      physical_key[0] = 1;
    else
      physical_key[0] = 0;
    if(digitalRead(COL_3) == LOW)
      physical_key[1] = 1;
    else
      physical_key[1] = 0;
    if(digitalRead(COL_4) == LOW)
      physical_key[3] = 1;
    else
      physical_key[3] = 0;
  pinMode(ROW_2, INPUT);
  //digitalWrite(ROW_2, HIGH);
  
  //digitalWrite(ROW_3, LOW);
  pinMode(ROW_3, OUTPUT);
    if(digitalRead(COL_1) == LOW)
      physical_key[5] = 1;
    else
      physical_key[5] = 0;
    if(digitalRead(COL_2) == LOW)
      physical_key[7] = 1;
    else
      physical_key[7] = 0;
    if(digitalRead(COL_3) == LOW)
      physical_key[9] = 1;
    else
      physical_key[9] = 0;
/*    if(digitalRead(COL_4) == LOW)
      physical_key[4] = 1;
    else
      physical_key[4] = 0;  */
  pinMode(ROW_3, INPUT);
  //digitalWrite(ROW_3, HIGH);
  
  //digitalWrite(ROW_4, LOW);
  pinMode(ROW_4, OUTPUT);  
    if(digitalRead(COL_1) == LOW)
      physical_key[11] = 1;
    else
      physical_key[11] = 0;
    if(digitalRead(COL_2) == LOW)
      physical_key[12] = 1;
    else
      physical_key[12] = 0;
/*    if(digitalRead(COL_3) == LOW)
      physical_key[6] = 1;
    else
      physical_key[6] = 0;
    if(digitalRead(COL_4) == LOW)
      physical_key[4] = 1;
    else
      physical_key[4] = 0;  */
  pinMode(ROW_4, INPUT);
  //digitalWrite(ROW_4, HIGH);
}

void mapPhysKeyToKey(char mode, char key_p_state) {
  switch(mode) {
    case 0:  // lift
            memcpy(&key[0], &physical_key[0], sizeof(physical_key[0]) * 11);
            memcpy(&key[21], &physical_key[11], sizeof(physical_key[11]) * 2);
            break;
            
            // switches
    case 1:
    case 2:
    case 3:
    case 4: if(key_p_state == 0) {  // P key is OFF
              // copy keys normal way, digits to lower bank
              memcpy(&key[0], &physical_key[0], sizeof(physical_key[0]) * 11);
              memcpy(&key[21], &physical_key[11], sizeof(physical_key[11]) * 2);
            }
            else {
              // P key is ON -> copy digit keys to upper bank
              memcpy(&key[0], &physical_key[0], sizeof(physical_key[0]));
              memcpy(&key[11], &physical_key[1], sizeof(physical_key[1]) * 10);
              memcpy(&key[21], &physical_key[11], sizeof(physical_key[11]) * 2);
            }
            break;
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
void next_mode(char key_num, char modeset_num) {
  if(mode[modeset_num][key_num] < modeset[modeset_num][0]-1) 
    mode[modeset_num][key_num]++;
  else 
    mode[modeset_num][key_num] = 0;
}
void switch_mode_while_visible(char key_num, char modeset_num) {  // used by bell key
  // Switch mode only if pressed while lit.
  if(last_key[key_num] != key[key_num] &&   // just pressed or released
     key_press_countdown[key_num] > 0)      // and no long press
    if(key_countdown[key_num] > 0) { // and did not sleep at the moment
      next_mode(key_num, modeset_num);      
    }
}
void switch_mode(char key_num, char modeset_num) {  // used by all except bell key
  if(last_key[key_num] != key[key_num] &&    // just pressed or released
     key_press_countdown[key_num] > 0) {     // and no long press
      next_mode(key_num, modeset_num);
  }
}
void handle_queue(char key_num, char modeset_num) {  // used by individual digit keys
  if(last_key[key_num] != key[key_num] &&    // just pressed or released
     key_press_countdown[key_num] > 0) {     // and no long press
      // Update queue of shift register pins events
      if(mode[modeset_num][KEY_STOP])                             // key stop is active
      if(key_num >= KEY_DIGIT_MIN && key_num <= KEY_DIGIT_MAX) {  // number keys only
        if(mode[modeset_num][key_num]) {
          add_to_queue(key_num-1, ENERGIZE_DURATION, 1, 1, 1); // bit_num, duration, exclusive, polarity, energize
          last_active_modeset_num = modeset_num;
        }
        else
          add_to_queue(key_num-1, ENERGIZE_DURATION, 1, 0, 1);
      }
  }
}
void all_digits_off() {
  for(unsigned char ff = KEY_DIGIT_MIN; ff<=KEY_DIGIT_MAX; ff++) {
    if(mode[last_active_modeset_num][ff])
      add_to_queue(ff-KEY_DIGIT_MIN, ENERGIZE_DURATION, 1, 0, 1);
  }
  mode[last_active_modeset_num][KEY_STOP] = 0;  // turn off STOP key in last modeset
}
void handle_queue_bulk(char key_num, char modeset_num) {  // used by stop key
  if(last_key[key_num] != key[key_num] &&    // just pressed or released
     key_press_countdown[key_num] > 0) {     // and no long press
      if(last_active_modeset_num != modeset_num)
        all_digits_off();
      // Update queue with all digits ON if key STOP is pressed
      for(unsigned char ff = KEY_DIGIT_MIN; ff<=KEY_DIGIT_MAX; ff++) {
        if(mode[modeset_num][ff])
          add_to_queue(ff-KEY_DIGIT_MIN, ENERGIZE_DURATION, 1, mode[modeset_num][KEY_STOP], 1);
          if(mode[modeset_num][KEY_STOP])
            last_active_modeset_num = modeset_num;
      }
  }
}
void handle_long_press(char key_num, char modeset_num) {  // used by stop key
  if(key_press_countdown[key_num] == 0) { // long pressed
    key_press_countdown[key_num] = -1;    // avoid reexecution at the next tick
    // Update all digits to ON or OFF if key STOP is pressed long
    for(unsigned char ff = KEY_DIGIT_MIN; ff<=KEY_DIGIT_MAX; ff++) {
      if(mode[modeset_num][KEY_STOP] != mode[modeset_num][ff]) {
          // update to mode of the stop key
          next_mode(ff, modeset_num);
          add_to_queue(ff-KEY_DIGIT_MIN, ENERGIZE_DURATION, 1, mode[modeset_num][KEY_STOP], 1);
          if(mode[modeset_num][KEY_STOP])
            last_active_modeset_num = modeset_num;
      }
    }
  }
}
// handle pressed keys
void manage_key_mode(char modeset_num) {
  if(key[KEY_BELL])
    switch_mode_while_visible(KEY_BELL, modeset_num);
  if(key[KEY_P]) {
    switch_mode(KEY_P, modeset_num);
  }
  if(key[KEY_STOP]) {
    //switch_mode(KEY_STOP, modeset_num);  // just pressed
    //handle_queue_bulk(KEY_STOP, modeset_num); // just pressed
    handle_long_press(KEY_STOP, modeset_num);
  } else {
    switch_mode(KEY_STOP, modeset_num);  // just released
    handle_queue_bulk(KEY_STOP, modeset_num); // just released
  }
  for(key_num=KEY_DIGIT_MIN; key_num<=KEY_DIGIT_MAX; key_num++) {
    if(key[key_num]) {
      switch_mode(key_num, modeset_num);
      handle_queue(key_num, modeset_num);
    }
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

void display_based_on_mode(char from_led, char to_led, char modeset_num, char key_p_state) {
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
        if(key_countdown[key_num] > 0)
          leds[keymap[key_num]] = modeset[modeset_num][mode[modeset_num][key_num]+1];
        else
          leds[keymap[key_num]] = CRGB::Black;
      }
      else
        leds[keymap[key_num]] = modeset[modeset_num][mode[modeset_num][key_num]+1];
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






/* SPEAK QUEUE */
#define MAX_SPK  400  // 512
unsigned char spk_len[MAX_SPK] = {5};  // 5 = 500 ms
#define MAX_SPK_QUEUE 30
int spk_que[MAX_SPK_QUEUE];
unsigned char spk_que_len = 0;
//long spk_que_pos = 0;
long spk_countdown = -1;

void add_spk(int spk_num) {
  if(spk_que_len < MAX_SPK_QUEUE - 1) {
    spk_que[spk_que_len] = spk_num;
    spk_que_len++;
  }
}
void spk_que_tick() {
  if(spk_que_len > 0 && spk_countdown == -1)
    spk_countdown = 0;
  if(spk_countdown == 0) {
//wtv020sd16p.asyncPlayVoice(2);
    if(0 < spk_que_len) {
      spk_countdown = ((unsigned char)spk_len[ spk_que[0] ]) * 100;
      wtv020sd16p.asyncPlayVoice(spk_que[0]);
      memmove(&spk_que[0], &spk_que[1], sizeof(spk_que[1])*(spk_que_len-1));  // thanks to this line spk_qie_pos is always 0
      spk_que_len--;
      //spk_que_pos++;
    } else {
      //wtv020sd16p.stopVoice();
      spk_countdown = -1;  // don't get in here again until spk_que_len gets > 0
    }
  } else {
    if(spk_countdown > 0)
      spk_countdown--;
  }
}

/* END of SPEAK QUEUE */





/* LIFT WORLD SIMULATION */

#define NUM_ITEMS    21
#define NUM_PERSONS  22
#define NUM_FLOORS   12  // 0 ground (P), 1..10 floors, 11 lift cabin
#define PLACE_CABIN  11
// item levels, lower level item can hold higher level item
#define LVL_1    0x40  // 0100 0000
#define LVL_2    0x80  // 1000 0000
#define LVL_3    0xC0  // 1100 0000
// floor
char lift_obj[NUM_PERSONS + NUM_ITEMS] = 
{
  // persons
  0,                         // 0. person 0 is on floor 1
  1,                         // 1. person 1 is on floor 2
  2,                         // 2. person 2 is on floor 3
  3,                         // 3. person 3 is on floor 5
  4,
  5,
  6,
  7,
  8,
  9,
  7,
  8,
  6,
  7,
  8,
  9,
  2,
  3,
  4,
  5,
  6,
  10,
  // items
  LVL_1 + 1,               // 4. item 0 is on 1st floor
  LVL_1 + NUM_FLOORS + 0,  // 5. item 1 is on 0th person
  LVL_1 + NUM_FLOORS + 0,  // 6. item 2 is on 0th person
  LVL_2 + NUM_FLOORS + NUM_PERSONS + 2,   // 7. item 3 is on item 2
  LVL_1 + 0,               // 8. item 4 is on ground floor
  LVL_1 + PLACE_CABIN,     // 9. item 5 is in lift cabin
  LVL_1 + NUM_FLOORS + 2,  // 10. item 6 is on 2nd person
  LVL_1 + NUM_FLOORS + 3,  // 11. item 7 is on 3rd person
  LVL_1 + PLACE_CABIN,     // 12. item 8 is in lift cabin
  LVL_1 + NUM_FLOORS + 2,  // 13. item 9 is on 2nd person
  LVL_1 + NUM_FLOORS + 3,  // 14. item 10 is on 3rd person
  LVL_1 + 0,               // 15. item 11 is on ground_floor
  LVL_1 + 1,               // 16. item 12 is on 1st floor
  LVL_1 + 2,               // 17. item 13 is on 2nd floor
  LVL_1 + 3,               // 18. item 14 is on 3rd floor
  LVL_1 + 4,               // 19. item 15 is on 4th floor
  LVL_1 + 5,               // 20. item 16 is on 5th floor
  LVL_1 + 6,               // 21. item 17 is on 6th floor
  LVL_1 + 7,               // 22. item 18 is on 7th floor
  LVL_1 + 8,               // 23. item 19 is on 8th floor
  LVL_1 + 9                // 24. item 20 is on 9th floor
};
char people_on_board;
char max_people_on_board;
char hospitalized_person;

char exiting[NUM_PERSONS];
char ex_count;
// uses curr_floor
void exit_lift(char person) {
  lift_obj[person] = curr_floor;
  exiting[ex_count] = person;
  ex_count++;
  people_on_board--;
}

// uses curr_floor
void hospitalize(char person) {
  lift_obj[person] = -1;
  hospitalized_person = person;
  people_on_board--;
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
// general sentences
#define MSG_I           65
#define MSG_ORAZ        66
#define MSG_LIFT_FALLING_SCREAM_3 67  // there are three consecutive, replaceable msgs
#define MSG_DOOR_OPEN_2       70          // there are two consecutive msgs
#define MSG_DOOR_CLOSE        72
#define MSG_LIFT_RUNNING      73
#define MSG_LIFT_RUNNING_OVERWEIGHT 74
#define MSG_LIFT_DECELERATING 75
#define MSG_LIFT_DECELERATING_OVERWEIGHT 76
#define MSG_CRASH             77

// specific sentences
#define MSG_WSIADA              42
#define MSG_WSIADAJA            43
#define MSG_Z_WINDY_WYSIADA     44
#define MSG_Z_WINDY_WYSIADAJA   45
#define MSG_WYNOSZA             46

void communicate_exits() {
  char ff;
  if(hospitalized_person > -1) {
    add_spk(MSG_WYNOSZA);
    add_spk(MSG_OFFS_PERSONS + hospitalized_person * 4 + BIERNIK_PERSONS);
  }
  if(ex_count == 0)
    return;
  if(ex_count == 1) {
    add_spk(MSG_Z_WINDY_WYSIADA);
    add_spk(MSG_OFFS_PERSONS + exiting[0] * 4 + MIANOWNIK_DOWN);
  }
  else {
    add_spk(MSG_Z_WINDY_WYSIADAJA);
    for(ff=0; ff<ex_count-1; ff++) {
      add_spk(MSG_OFFS_PERSONS + exiting[ff] * 4 + MIANOWNIK_UP);
    }
    add_spk(MSG_ORAZ);
    add_spk(MSG_OFFS_PERSONS + exiting[ff] * 4 + MIANOWNIK_DOWN);
  }
}
char entering[NUM_PERSONS];
char ent_count;
void enter_lift(char person) {
  lift_obj[person] = PLACE_CABIN;
  // register action for further communication
  entering[ent_count] = person;
  ent_count++;
  people_on_board++;
}
void communicate_entries() {
  char ff;
  if(ent_count == 0)
    return;
  if(ent_count == 1) {
    add_spk(MSG_WSIADA);
    add_spk(MSG_OFFS_PERSONS + entering[0] * 4 + MIANOWNIK_DOWN);
  }
  else {
    add_spk(MSG_WSIADAJA);
    for(ff=0; ff<ent_count-1; ff++) {
      add_spk(MSG_OFFS_PERSONS + entering[ff] * 4 + MIANOWNIK_UP);
    }
    add_spk(MSG_I);
    add_spk(MSG_OFFS_PERSONS + entering[ff] * 4 + MIANOWNIK_DOWN);
  }
}
char picking[NUM_ITEMS];
char picked[NUM_ITEMS];
char pick_count;
// have person take item from place where she stands
void pick_item(char person, char item_idx) {
  char item_desc = (lift_obj[item_idx] & 0xC0);
  lift_obj[item_idx] = NUM_FLOORS + item_desc + person;   // move the item to person (person 0 idx in lift_obj[] == NUM_FLOORS)
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
  char person_holding_item = (lift_obj[item_idx] & 0x3F) - NUM_FLOORS;
  char person_location = (lift_obj[ person_holding_item ] & 0x3F);
  char item_desc = (lift_obj[item_idx] & 0xC0);
  lift_obj[item_idx] = item_desc + person_location;  // move the item to floor idx
  // register action for further communication
  dropping[drop_count] = person_holding_item;
  dropped[drop_count] = item_idx;
  drop_count++;
}
char handing[NUM_ITEMS];
char handed[NUM_ITEMS];
char receiver[NUM_ITEMS];
char hand_count;
char hand_item_to_other_person(char item_idx, char other_person) {
  // assume it's never called if item is not on person
  char person_holding_item = (lift_obj[item_idx] & 0x3F) - NUM_FLOORS;
  char item_desc = (lift_obj[ item_idx ] & 0xC0);
  lift_obj[item_idx] = NUM_FLOORS + item_desc + other_person;   // move the item to person
  // register action for further communication
  handing[hand_count] = person_holding_item;
  handed[hand_count] = item_idx;
  receiver[hand_count] = other_person;
  hand_count++;
}

void clear_lift_world_queues() {
  memset(exiting, 0, sizeof(exiting));
  ex_count = 0;
  memset(entering, 0, sizeof(entering));
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
}
// checks if object idx is a person (and not an item)
// return 0 or person_id + 1  (remember the 1!)
char is_person(char obj_idx) {  // obj_idx is an idx of lift_obj[]
  if(obj_idx < NUM_PERSONS)     // it is a person
    return obj_idx + 1;  // obj_idx is incidentally a person id
  else
    return 0;
  //return obj_id - NUM_FLOORS;
  /*
  if((lift_obj[obj_id] >> 6) == 0)
    return obj_id;
  else
    return 0;
  */
}
// checks if object id is a floor or a cabin (and not person or item)
// return 0 or place id + 1 (there is no place idx!)
char is_place(char location) {
  if(location < NUM_FLOORS)
    return location + 1;
  else
    return 0;
}
// checks if object(item) idx is located on person (owned by it) or not
// return 0 or person id
char is_item_on_person(char obj_idx) {
  char location_id = (lift_obj[obj_idx] & 0x3F);
  if(location_id < NUM_FLOORS)                  // it is on floor
    return 0;
  if(location_id >= NUM_FLOORS + NUM_PERSONS)   // it is on other item
    return 0;
  return location_id - NUM_FLOORS;
}
char is_at_place(char obj_idx) {
  char location = (lift_obj[obj_idx] & 0x3F);
  return is_place(location);  // is_place() returns place_id + 1, or 0
}
char want_to_exit(char person) {  // equal to idx in lift_obj[]!
  return ((person % NUM_FLOORS) == curr_floor && random(10) < 8) ||
         (curr_floor == 0 && random(10) < 5) ||
         random(10) < 3;
         
}
char want_to_enter(char person) {
  return random(2); //person == curr_floor;
}
char are_other_persons_here(char obj_idx) {  // obj_id - we expect index in lift_obj[] here
  char object_location = (lift_obj[obj_idx] & 0x3F);
  char other_person = 0;
  // loop all people and get the last being at the same place
  for(char possible_neighbor = 0; possible_neighbor < NUM_PERSONS; possible_neighbor++) {
    if(possible_neighbor != obj_idx) { // skip given object
      if((lift_obj[possible_neighbor] & 0x3F) == object_location)
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
void migrate_objs() {
  char person;
  char place;
  char other_person;
  for(char ff = 0; ff < NUM_PERSONS + NUM_ITEMS; ff++) {
    if((person = is_person(ff)) > 0) {
      person--; // is_person() returns id+1, to use 0 as false
      if((place = is_at_place(person)) == (PLACE_CABIN+1)) {
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
    /*
    else
    // it is an item, not a person; check if it sits on a person
    if((person = is_item_on_person(ff)) > 0) {
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
    if((place = is_at_place(ff)) > 0) {
      place--;
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

void activate_objs() {
  
}

/* END of LIFT WORLD SIMULATION */






// Shift Register (SR) and event queue routines

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
#define MAX_QUEUE 64
int duration[MAX_QUEUE];
unsigned char value[MAX_QUEUE] = {0};  // bit 2 - exclusive, bit 1 - polarity, bit 0 - energize
unsigned char bit_num[MAX_QUEUE];
unsigned char queue_start = 0;
unsigned char queue_end = queue_start;

unsigned char map_key_to_sr[1][NUM_KEYS] = {{0, 
                                            8, 7, 6, 4, 14, 12, 3, 15, 13, 11, 10, 9, 2, 5, 1,
                                            16, 17, 18, 19, 20, 21, 22}};

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
          set_sr_output_pin(map_key_to_sr[0][bit_num[ff]], 2 & value[ff], 1 & value[ff]);
          value[ff] &= 0xF7; // 11110111 - clear init bit
        }
        
        if(duration[ff] > 0)
        {
          duration[ff]--; // countdown
        }
      }
    }
    if(duration[ff] == 0) {
      set_sr_output_pin(map_key_to_sr[0][bit_num[ff]], 0, 0);  // deenergize
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
    value[queue_end] = (unsigned char) (8 + ((exclusive > 0) << 2) + ((polarity > 0) << 1) + (energize > 0));

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
    if(dd < NUM_KEYS)
      leds[keymap[dd]] = CRGB::Blue;

    if(queue_start < NUM_KEYS)
      leds[keymap[queue_start]] = CRGB::Green;
    if(queue_end < NUM_KEYS)
      leds[keymap[queue_end]] = CRGB::Red;

    leds[0] = CRGB::Blue;
}







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
      memset(spk_len, 5, sizeof(spk_len)); // set default for all
      clear_lift_world_queues();
      people_on_board = 0;
      max_people_on_board = (NUM_PERSONS / 2) > 2 ? 2 : NUM_PERSONS / 2;
}




void loop() {

  // process switch event queue
  process_queue_tick();
  clean_queue();
  //send_to_sr(0xff00);

  // process speaking queue
  spk_que_tick();

  
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
             state_countdown = 1200;  // how long it will be stopping
           }
           if(state_countdown == 0) {
             state = LIFT_STOPPED;
             wtv020sd16p.asyncPlayVoice(MSG_OFFS_PLACES + curr_floor);  // Announcing "floor X"
           }
           break;
    case LIFT_STOPPED:
           if(last_state != state) {
             last_state = state;
             state_countdown = 800;  // how long it will stand still after stopping
           }
           if(state_countdown == 0) {
             state = DOOR_OPENING;
             wtv020sd16p.asyncPlayVoice(MSG_DOOR_OPEN_2 + random(2)); // the sound of opening door
           }
           break;
    case DOOR_OPENING:
           if(last_state != state) {
             last_state = state;
             state_countdown = 1500;  // how long takes opening the door
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
             state_countdown = 200;  // for how long minimum the door stays open
           }
           if(state_countdown == 0) {
             state = PASSENGERS_MOVEMENT;
             //wtv020sd16p.asyncPlayVoice(11);  // the sound of passenger movement
             migrate_objs();
             communicate_exits();
             communicate_entries();
             clear_lift_world_queues();  // queues already loaded to communication
           }
           break;
    case PASSENGERS_MOVEMENT:
           //enter_lift_state(500);
           if(last_state != state) {
             last_state = state;
             state_countdown = 1500;  // for how long the passenger enters or exits the lift
           }
           if(spk_que_len > 0)
             state_countdown = 500;  // keep waiting until communicating object moves
           if(state_countdown == 0) {
            /*
            if(passengers_to_go > 0) {
               last_state = RESTART_THIS_STATE;
               state = PASSENGERS_MOVEMENT;
               passengers_to_go--;
               wtv020sd16p.asyncPlayVoice(7);  // sound of moving passengers
             } else
             if(target_floor != curr_floor) {
               state = DOOR_CLOSING;
               wtv020sd16p.asyncPlayVoice(4);  // the sound of closing door
             }
             */
             if(target_floor != curr_floor) {
               state = DOOR_CLOSING;
               wtv020sd16p.asyncPlayVoice(MSG_DOOR_CLOSE);  // the sound of closing door
             }
           }
           break;
    
    case DOOR_CLOSING:
           if(last_state != state) {
             last_state = state;
             state_countdown = 700;  // how long takes closing the door
           }
           if(state_countdown == 0) {
             state = DOOR_CLOSED;
             //wtv020sd16p.asyncPlayVoice(5);   // bump of door being closed 
           }
           break;
    case DOOR_CLOSED:
           if(last_state != state) {
             last_state = state;
             state_countdown = 100;  // for how long the lift holds after closing the door before it starts running
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
             state_countdown = 500;  // for how long the lift decides where to go
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
             state_countdown = 500;  // for how long the lift goes up one floor
           }
           // probability of falling down while going upwards
           if(people_on_board > max_people_on_board &&
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
             state_countdown = 500;  // for how long the lift goes down one floor
           }
           // probability of falling down while going downwards
           if(people_on_board > max_people_on_board && 
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
               wtv020sd16p.asyncPlayVoice(MSG_CRASH);  // sound of crashing lift
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
             wtv020sd16p.asyncPlayVoice(MSG_OFFS_PLACES + curr_floor);  // Announcing "floor X"
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
  mapPhysKeyToKey(mode[0][KEY_BELL],            // key 22 - bell, its state determines mode
                  mode[ mode[0][KEY_BELL] ][0]);  // key 0 - P, its state determines digits bank

/*
  if(key[11]) {
    switch_mode_while_visible(11, mode[0][KEY_BELL]);
    // DEBUG DISPLAY for key 11
    // debug display - mark target floor with the LED on the keypad
    leds[keymap[target_floor]] = CRGB::Green;
  } 
  else
*/
  if(key[KEY_BELL]) {
    // Switch mode only if pressed while lit.
    switch_mode_while_visible(KEY_BELL, MODESET_FUNCKEYS);
    // DEBUG DISPLAY for key 12
    display_debug_2();
  }
  else {
    if(key[KEY_STOP] && (state == LIFT_RUNNING_UP || state == LIFT_RUNNING_DOWN)) {
      state = LIFT_START_FALLING;
    }
    // REGULAR DISPLAY
    if(mode[0][KEY_BELL] == 0) {
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

      // handle STOP key (KEY_STOP)
      manage_key_mode(MODESET_FLOOR_STOP);  // from key, to key, mode set
      display_based_on_mode(LED_STOP, LED_STOP, MODESET_FLOOR_STOP, mode[MODESET_FLOOR_STOP][KEY_P]);
    }
    else
    if(mode[0][KEY_BELL] == 1) {
      // Switch/Toggle mode
      manage_key_mode(MODESET_SWITCHES_1);  // from key, to key, mode set
      display_based_on_mode(LED_P, LED_STOP, MODESET_SWITCHES_1, mode[MODESET_SWITCHES_1][KEY_P]);
    }
    else
    if(mode[0][KEY_BELL] == 2) {
      manage_key_mode(MODESET_SWITCHES_2);
      display_based_on_mode(LED_P, LED_STOP, MODESET_SWITCHES_2, mode[MODESET_SWITCHES_2][KEY_P]);
    }
    else
    if(mode[0][KEY_BELL] == 3) {
      manage_key_mode(MODESET_SWITCHES_3);
      display_based_on_mode(LED_P, LED_STOP, MODESET_SWITCHES_3, mode[MODESET_SWITCHES_3][KEY_P]);
      //dim_turned_off_by_group(mode[mode[0][KEY_BELL]][11], 0, 10);
    }
  } //else debug



  // DISPLAY key KEY_BELL
  //display_based_on_mode(11, 11, mode[0][KEY_BELL]);  // key BELL determines modeset for other keys
  display_based_on_mode(LED_BELL, LED_BELL, MODESET_FUNCKEYS, NO_MATTER);
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
