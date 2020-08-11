#ifndef BUTTONS_H_
#define BUTTONS_H_

#define UI_SWITCH_PIN           0   // A9
#define S1_CHANGE_BUTTON_PIN    9   // D9
#define S2_SET_BUTTON_PIN       5   // D5
#define START_BUTTON_PIN        6   // D6


//#define BUTTON1_PIN             6     
//#define BUTTON2_PIN             5

#define BUTTON_ACTIVE           0
#define BUTTON_INACTIVE         1

void buttons_init( void );
void buttons_process( uint8_t &but1_trig, uint8_t &but2_trig, uint8_t &but3_trig );

#endif // BUTTONS_H_
