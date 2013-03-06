// GLCDNode
// --------
// Node connected to the 64x128 graphic LCD.
// Node address: 868 Mhz, net group 5, node 4.
// Local sensors: - inside temperature (DS18B20)
//                - microswitch (to light up GLCD)
// Receives:      - electricity actual usage (from CentralNode)
//                - outside temperature (from CentralNode)
//                - barometric pressure (from CentralNode)
// Sends:         - inside temperature (to CentralNode)
// Other:         - 64x128 graphic LCD (to display electricity usage,
//                  inside/outside temperature & barometric pressure)
//
// Author: Jos Janssen
// Modifications:
// Date:        Who:    Added:
// 01jul2012    Jos     Corrected typo in comments
// 22sep2012    Jos     Added LDR for controlling display brightness
// 24sep2012    Jos     Included code for 8 second watchdog reset
// 15feb2013    Jos     Added code to re-initialze the rf12 every one week to avoid hangup after a long time
// 16feb2013    Jos     Adjusted GLCD LDR to brightness mapping
// 05mar2013    Jos     Changed layout of GLCD

#define DEBUG 0

#include <JeeLib.h>
#include <Metro.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <GLCD_ST7565.h>
#include "utility/font_4x6.h"
#include "utility/font_clR5x8.h"
#include "utility/font_helvB10.h"
#include "utility/font_helvB18.h"

// Crash protection: Jeenode resets itself after x seconds of none activity (set in WDTO_xS)
const int UNO = 1;    // Set to 0 if your not using the UNO bootloader (i.e using Duemilanove)
#include <avr/wdt.h>  // All Jeenodes have the UNO bootloader
ISR(WDT_vect) { Sleepy::watchdogEvent(); }

// JeeNode Port 1+4: GLCD display 128x64
GLCD_ST7565 glcd;

// JeeNode Port 2: DS18B20 temperature inside
// Data wire is plugged into pin 5 on the Arduino (=Port 2 DIO on JeeNode)
#define ONE_WIRE_BUS 5
// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
int itemp;

// JeeNode Port 3: button to brighten display temporarily (digital), LDR for light measurement (analog)
Port button (3);
Port LDRport (3);
boolean buttonPressed=0;
int LDR, LDRbacklight;

Metro rf12ResetMetro = Metro(604800000); // re-init rf12 every 1 week
Metro sampleMetro = Metro(60500);
Metro LDRMetro = Metro(1000);
Metro wdtMetro = Metro(1500);


struct { char type; long actual; long rotations; long rotationMs;
                 int minA; int maxA; int minB; int maxB;
                 int minC; int maxC; int minD; int maxD;
} payload;

typedef struct {
  char box;
  char box_title[15];
  char box_data[10];
} glcd_recv;
glcd_recv glcd_data;

struct {
  char box_title[6][15];
  char box_data[6][10];
} glcd_display;
       
MilliTimer light;
int duration=60000;
int i;

void display_data () {
    byte i, x, y;
    
    strcpy(glcd_display.box_title[glcd_data.box], glcd_data.box_title);
    strcpy(glcd_display.box_data[glcd_data.box],  glcd_data.box_data);
    
    // Override text for box 2 because of locally read inside temperature data
    sprintf(glcd_display.box_title[2], "Temperatuur C");
    if (itemp > -1 || itemp < -9) {
      sprintf(glcd_display.box_data[4], "%3d,%d", itemp/10, abs(itemp%10));
    } else {
      sprintf(glcd_display.box_data[4], "-%2d,%d", itemp/10, abs(itemp%10));
    }
    //sprintf(glcd_display.box_data[4], "%2d,%d", itemp/10, itemp%10);
    
    glcd.clear();
    // Create box lines
    //glcd.drawLine(63, 0, 63, 63, WHITE);
    //glcd.drawLine(63, 16, 127, 16, WHITE);
    //glcd.drawLine(0, 32, 127, 32, WHITE);
    //glcd.drawLine(63, 48, 127, 48, WHITE);
    
    // Left pointing arrow (outside temp)
    glcd.drawLine(1, 45, 8, 45, WHITE);
    glcd.drawLine(2, 44, 2, 46, WHITE);
    glcd.drawLine(3, 43, 3, 47, WHITE);
    glcd.drawLine(4, 42, 4, 48, WHITE);
    
    // Right pointing arrow (inside temp)
    glcd.drawLine(1, 56, 8, 56, WHITE);
    glcd.drawLine(5, 53, 5, 59, WHITE);
    glcd.drawLine(6, 54, 6, 58, WHITE);
    glcd.drawLine(7, 55, 7, 57, WHITE);

    #if DEBUG
      Serial.print(glcd_display.box_title[glcd_data.box]);
      Serial.print(", ");
      Serial.println(glcd_display.box_data[glcd_data.box]);
    #endif
 
    for (i=0; i<6; i++){
        switch (i) {
        /*case 0: { x=1; y=1; break; }
          case 1: { x=65; y=1; break; }
          case 2: { x=65; y=17; break; }
          case 3: { x=1; y=33; break; }
          case 4: { x=65; y=33; break; }
          case 5: { x=65; y=49; break; }*/
          case 0: { x=1; y=1; break; }
          case 1: { x=65; y=1; break; }
          case 2: { x=1; y=33; break; }
          case 3: { x=65; y=33; break; }
          case 4: { x=1; y=49; break; }
          case 5: { x=65; y=49; break; }
        }
        glcd.setFont(font_4x6);
        glcd.drawString(x, y, glcd_display.box_title[i]);
        switch (i) {
          case 0: case 1: {
              glcd.setFont(font_helvB18);
              glcd.drawString(x, y+8, glcd_display.box_data[i]);
              break;
          }
          case 2: {
              glcd.setFont(font_helvB10);
              glcd.drawString(x+9, y+6, glcd_display.box_data[i]);
              break;
          }
          case 4: {
              glcd.setFont(font_helvB10);
              glcd.drawString(x+9, y+2, glcd_display.box_data[i]);
              break;
          }
          case 3: case 5: {
              glcd.setFont(font_helvB10);
              glcd.drawString(x+2, y+6, glcd_display.box_data[i]);
              break;
          }
        }
        
    }
    glcd.refresh();
}

void init_rf12 () {
    rf12_initialize(4, RF12_868MHZ, 5); // 868 Mhz, net group 5, node 4
    rf12_easyInit(0); // Send interval = 0 sec (=immediate).
}

void setup () {
    button.mode(INPUT);
    #if DEBUG
      Serial.begin(57600);
      Serial.println("\n[Start debug output]");
    #endif
    init_rf12();
    sensors.begin(); // DS18B20 default precision 12 bit.
    glcd.begin();
    
    if (UNO) wdt_enable(WDTO_8S);  // set timeout to 8 seconds
}

void loop () {
    // re-initialize rf12 every week to avoid rf12 hangup after a long time
    if ( rf12ResetMetro.check() ) {
         init_rf12();
    }
    
    if ( sampleMetro.check() ) {
        sensors.requestTemperatures(); // Send the command to get temperatures
        itemp=10*sensors.getTempCByIndex(0);
        payload.type = 'i'; // type is inside temperature data
        payload.actual = (long) itemp;
        payload.rotations = 0;
        #if DEBUG
          Serial.print("i ");
          Serial.print(itemp);
          Serial.println(" "); // extra space at the end is needed
        #endif
        rf12_easySend(&payload, sizeof payload);
        rf12_easyPoll(); // Actually send the data every interval (see easyInit above)
        
        display_data();
    }
    
    if (rf12_recvDone() && rf12_crc == 0 && rf12_len == sizeof (glcd_data)) {
        #if DEBUG
              Serial.println("R ");
        #endif
        glcd_data = *(glcd_recv*) rf12_data;
        #if DEBUG
          Serial.print(glcd_data.box);
          Serial.print(" ");
          Serial.print(glcd_data.box_title);
          Serial.print(" ");
          Serial.println(glcd_data.box_data);
        #endif
        if (RF12_WANTS_ACK) {
            rf12_sendStart(RF12_ACK_REPLY, 0, 0);
        }
        display_data();
    }
    
    if (button.digiRead()) {
        buttonPressed=1;
        #if DEBUG
          Serial.println("button pressed");
        #endif
        for(i=LDRbacklight; i<255; i++) {
          glcd.backLight(i);
          delay(3);
        }
        light.set(duration);
    }
    
    if (light.poll()) {
        for(i=255; i>LDRbacklight; i--) {
          glcd.backLight(i);
          delay(3);
        }
        buttonPressed=0;
    }

    if ( LDRMetro.check() && !buttonPressed) {
        LDR=LDRport.anaRead();				// Read LDR value for light level in the room
        LDRbacklight=map(LDR,0,400,25,255);     	// Map LDR data to GLCD brightness
        LDRbacklight=constrain(LDRbacklight,0,255);	// constrain value to 0-255
        #if DEBUG
          Serial.print("LDR = "); Serial.print(LDR);
          Serial.print("   LDRbacklight = "); Serial.println(LDRbacklight);
        #endif
        glcd.backLight(LDRbacklight);
    }
    
    if ( wdtMetro.check() ) {
      if (UNO) wdt_reset();
    }
}
