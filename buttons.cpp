#include <Arduino.h>
#include "buttons.h"

#define BTN_CALL_FILTERING      100
#define BTN_REPEATS_DELAY       200
#define BTN_DELAY_BEFORE_REPEAT 1000

enum button_state_en {
    BUTTON_STATE_RELEASED = 0,
    BUTTON_STATE_NOISE_FILTERING,
    BUTTON_STATE_DELAY_BEFORE_REPEAT,
    BUTTON_STATE_REPEAT_WAIT,
    BUTTON_STATE_UNEXPECTED
};

uint8_t button1_state, button2_state;

void buttons_init( void ) {
    pinMode(BUTTON1_PIN, INPUT_PULLUP); 
    pinMode(BUTTON2_PIN, INPUT_PULLUP); 
    button1_state = BUTTON_STATE_RELEASED;
    button2_state = BUTTON_STATE_RELEASED;
}

void buttons_process( uint8_t &but1_trig, uint8_t &but2_trig ) {
  
    uint8_t but1 = BUTTON_INACTIVE;
    uint8_t but2 = BUTTON_INACTIVE;
    uint16_t cnt1 = 0, cnt2 = 0;
    but1_trig = false;
    but2_trig = false;
    
    while(1) {
        but1 = digitalRead(BUTTON1_PIN);
        if( but1 == BUTTON_ACTIVE ) { 
            //Serial.print("1");
            cnt1++;
            switch( button1_state ) {
                case BUTTON_STATE_RELEASED:
                    //Serial.print("a");
                    button1_state = BUTTON_STATE_NOISE_FILTERING;
                    break;
                case BUTTON_STATE_NOISE_FILTERING:
                    //Serial.print("b");
                    if( cnt1 == BTN_CALL_FILTERING ) {
                        cnt1 = 0;
                        but1_trig = true; 
                        button1_state = BUTTON_STATE_DELAY_BEFORE_REPEAT;
                    }
                    break;
                case BUTTON_STATE_DELAY_BEFORE_REPEAT:
                    //Serial.print("c");
                    if( cnt1 == BTN_DELAY_BEFORE_REPEAT ) {
                        cnt1 = 0;
                        but1_trig = true; 
                        button1_state = BUTTON_STATE_REPEAT_WAIT;
                    }
                    break;
                case BUTTON_STATE_REPEAT_WAIT:
                    //Serial.print("d");
                    if( cnt1 == BTN_REPEATS_DELAY ) {
                        cnt1 = 0;
                        but1_trig = true; 
                    }
                    break;
                // case BUTTON_STATE_UNEXPECTED:
                //     break;
            }
        } else {
            //Serial.print("-");
            // if( button1_state != BUTTON_STATE_RELEASED && button2_state != BUTTON_STATE_RELEASED ) {
            //     button2_state = BUTTON_STATE_UNEXPECTED;
            // }
            button1_state = BUTTON_STATE_RELEASED;
            cnt1 = 0;
        }
        
        but2 = digitalRead(BUTTON2_PIN);
        if( but2 == BUTTON_ACTIVE ) { 
            //Serial.print("2");
            cnt2++;
            switch( button2_state ) {
                case BUTTON_STATE_RELEASED:
                    //Serial.print("a");
                    button2_state = BUTTON_STATE_NOISE_FILTERING;
                    break;
                case BUTTON_STATE_NOISE_FILTERING:
                    //Serial.print("b");
                    if( cnt2 == BTN_CALL_FILTERING ) {
                        cnt2 = 0;
                        but2_trig = true; 
                        button2_state = BUTTON_STATE_DELAY_BEFORE_REPEAT;
                    }
                    break;
                case BUTTON_STATE_DELAY_BEFORE_REPEAT:
                    //Serial.print("c");
                    if( cnt2 == BTN_DELAY_BEFORE_REPEAT ) {
                        cnt2 = 0;
                        but2_trig = true; 
                        button2_state = BUTTON_STATE_REPEAT_WAIT;
                    }
                    break;
                case BUTTON_STATE_REPEAT_WAIT:
                    //Serial.print("d");
                    if( cnt2 == BTN_REPEATS_DELAY ) {
                        cnt2 = 0;
                        but2_trig = true; 
                    }
                    break;
                // case BUTTON_STATE_UNEXPECTED:
                //     break;
            }
        } else {
            //Serial.print("+");
            // if( button1_state != BUTTON_STATE_RELEASED && button2_state != BUTTON_STATE_RELEASED ) {
            //     button1_state = BUTTON_STATE_UNEXPECTED;
            // }
            button2_state = BUTTON_STATE_RELEASED;
            cnt2 = 0;
        }
        
        if( (but1_trig || cnt1 == 0) && (but2_trig || cnt2 == 0) ) {
            break;
        }
        
        delay(1);
    } 
}
