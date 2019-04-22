#ifndef BUTTONS_H_
#define BUTTONS_H_

#define BUTTON1_PIN             6     
#define BUTTON2_PIN             5

#define BUTTON_ACTIVE           0
#define BUTTON_INACTIVE         1

void buttons_init( void );
void buttons_process( uint8_t &but1_trig, uint8_t &but2_trig );

#endif // BUTTONS_H_
