#ifndef _KEYPAD_H_
#define _KEYPAD_H_

#define KEY_1       1
#define KEY_2       2
#define KEY_3       4
#define SWIPE_LEFT  8
#define SWIPE_RIGHT 16

void keypad_setup();
void keypad_loop();

extern void notifyKeyEvent(uint8_t keyEvent ) __attribute__ ((weak));

#endif // _KEYPAD_H_