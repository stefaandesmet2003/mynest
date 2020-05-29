#include "Arduino.h"
#include "keypad.h"

#undef MYDEBUG 

#define pinKey1   16     // D0
#define pinKey2   14     // D5
#define pinKey3   12     // D6
// inKeys : komt overeen met de definitie van KEY_1, KEY_2, KEY_3 in keypad.h
// dat kan allicht beter ..

#define inKeys (digitalRead(pinKey1) << 0) | (digitalRead(pinKey2) << 1) | (digitalRead(pinKey3) << 2)

#define VALIDKEY_DELAY      50  
#define SWIPETIMEOUT_DELAY  500 // max tijd in TOUCH1VALID met toets ingedrukt, daarna verwachten we geen bijkomende toets meer, en dus ook geen swipe
#define LATESWIPEWAIT_DELAY 20  // na het lossen van een toets wachten we nog eventjes op een mogelijke swipe
#define IDLEWAIT_DELAY      50 // minimale tijd tussen 2 key events

typedef enum {IDLE, TOUCH1WAIT, MULTITOUCHWAIT,TOUCH1VALID,LATESWIPEWAIT,IDLEWAIT} keyState_t;

keyState_t keyState = IDLE;
uint16_t curKeys;
uint32_t millisInState;

void keypad_setup()
{
    pinMode(pinKey1,INPUT_PULLUP);
    pinMode(pinKey2,INPUT_PULLUP);
    pinMode(pinKey3,INPUT_PULLUP);
    curKeys = 0;   
    keyState = IDLE;
    Serial.println("Keypad started");
  
} // keypad_setup

// loopfunction for scanning the keys
void keypad_loop()
{
  int keys = inKeys; // lees de key inputs bij elke loop
    //touch2 = digitalRead(5);
#ifdef MYDEBUG    
    if (keys != prevKeys)
    {
      Serial.print(millis());Serial.print(" : keys : " );Serial.println(keys);
      prevKeys = keys;
    }
#endif
  switch (keyState)
  {
    case IDLE :
      // alles keys zijn off
      if (curKeys != keys)
      {
        keyState = TOUCH1WAIT;
#ifdef MYDEBUG    
        Serial.print("TOUCH1WAIT ");Serial.println(curKeys);
#endif        
        curKeys = keys;
        millisInState = millis();
      }
      break;
    case TOUCH1WAIT : 
      if (curKeys != keys) { // iets veranderd
        if (keys == 0) {
          keyState = IDLE;
#ifdef MYDEBUG
          Serial.print("IDLE ");Serial.println(curKeys);
#endif          
          curKeys = 0;
        }
        else {
          keyState = MULTITOUCHWAIT;
#ifdef MYDEBUG          
          Serial.print("MULTITOUCHWAIT ");Serial.println(curKeys);
#endif          
          curKeys = keys;
          millisInState = millis();
        }
      }
      else if ((millis() - millisInState) > VALIDKEY_DELAY) {
        keyState = TOUCH1VALID;
#ifdef MYDEBUG        
        Serial.print("TOUCH1VALID "); Serial.println(curKeys);
#endif        
        millisInState = millis();
      }
      break;
    case TOUCH1VALID : 
      if (curKeys != keys) { // iets veranderd
        if (keys == 0) {
          keyState = LATESWIPEWAIT;
#ifdef MYDEBUG
          Serial.print("LATESWIPEWAIT ");Serial.println(curKeys); // misschien komt er nog een 'on' van een swipe key?
#endif          
          millisInState = millis();
        }
        else { 
          // swipe !
          if (notifyKeyEvent) {
            if ((keys ^ curKeys)> curKeys) // keys XOR curKeys geeft de nieuwe key, nieuws key > eerste key -> rechts (1->2->3)
              notifyKeyEvent(SWIPE_RIGHT);
            else
              notifyKeyEvent(SWIPE_LEFT);
          }
          keyState = IDLEWAIT;
#ifdef MYDEBUG          
          Serial.print("IDLEWAIT ");Serial.println(curKeys);
#endif          
        }
      }
      else if ((millis() - millisInState) > SWIPETIMEOUT_DELAY) {
        // we verwachten geen swipe meer, en gaan de key event melden
        keyState = IDLEWAIT;
#ifdef MYDEBUG        
        Serial.print("IDLEWAIT ");Serial.println(curKeys);
#endif
        if (notifyKeyEvent)
          notifyKeyEvent(curKeys);
        millisInState = millis();
      }      
      break;
    case MULTITOUCHWAIT : 
     if (curKeys != keys) { // iets veranderd
        if (keys == 0) {
          keyState = IDLE;
#ifdef MYDEBUG          
          Serial.print("IDLE ");Serial.println(curKeys);
#endif          
          curKeys = 0;
        }
        else {
          millisInState = millis(); // er is misschien nog een key bijgekomen; reset de wachttijd
          curKeys = keys;
        }
      }
      else if ((millis() - millisInState) > VALIDKEY_DELAY) {
        // valid multitouch -> send event
        if (notifyKeyEvent)
          notifyKeyEvent(curKeys);
        keyState = IDLEWAIT;
#ifdef MYDEBUG        
        Serial.print("IDLEWAIT ");Serial.println(curKeys);
#endif
        millisInState = millis();
      }
      break;
    case LATESWIPEWAIT :
      if (keys != 0) {
        if (keys != curKeys) {
          // swipe!
          // meld swipe event
          if (notifyKeyEvent)
          {
            if ((keys ^ curKeys)> curKeys) // keys XOR curKeys geeft de nieuwe key, nieuws key > eerste key -> rechts (1->2->3)
              notifyKeyEvent(SWIPE_RIGHT);
            else
              Serial.println(SWIPE_LEFT);
          }
          keyState = IDLEWAIT;
#ifdef MYDEBUG          
          Serial.print("IDLEWAIT ");Serial.println(curKeys);
#endif          
        }
      }
      else if ((millis() - millisInState) > LATESWIPEWAIT_DELAY) {
        if (notifyKeyEvent)
          notifyKeyEvent(curKeys);
        keyState = IDLEWAIT;
#ifdef MYDEBUG        
        Serial.print("IDLEWAIT ");Serial.println(curKeys);
#endif        
        millisInState = millis();
      }
      break;
    case IDLEWAIT :
      // we willen nu 200ms geen enkele key vooraleer terug naar idle te gaan
      if (keys != 0) {
        millisInState = millis(); // reset de wachttijd
      }
      else if ((millis() - millisInState) > IDLEWAIT_DELAY) { // voldoende lang geen keys gekregen
        keyState = IDLE;
#ifdef MYDEBUG        
        Serial.print("IDLE "); Serial.println(curKeys);
#endif        
      }
      curKeys = 0;
      break;
  }
  
} // keypad_loop
