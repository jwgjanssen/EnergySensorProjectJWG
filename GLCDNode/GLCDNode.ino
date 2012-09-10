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
// Date:        Who:   Added:
// 01jul2012    Jos    Corrected typo in comments

#include <JeeLib.h>
#include <Metro.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <GLCD_ST7565.h>
#include "utility/font_4x6.h"
#include "utility/font_clR5x8.h"
#include "utility/font_10x20.h"

#define DEBUG 0

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

// JeeNode Port 3: button to brighten display temporarily
Port button (3);

Metro sampleMetro = Metro(60500);

struct { char type; long actual; long rotations; long rotationMs;
                 int minA; int maxA; int minB; int maxB;
                 int minC; int maxC; int minD; int maxD;
} payload;

typedef struct {
  char box;
  char box_title[12];
  char box_data[12];
} glcd_recv;
glcd_recv glcd_data;

struct {
  char box_title[8][12];
  char box_data[8][12];
} glcd_display;
       
MilliTimer light;
int duration=60000;
int i;

void display_data () {
    byte i, x, y;
    
    strcpy(glcd_display.box_title[glcd_data.box], glcd_data.box_title);
    strcpy(glcd_display.box_data[glcd_data.box],  glcd_data.box_data);
    
    // Override text for box 1 because of locally read inside temperature data
    sprintf(glcd_display.box_title[1], "Binnen");
    sprintf(glcd_display.box_data[1], "%2d,%d C", itemp/10, itemp%10);
    
    glcd.clear();
    glcd.drawLine(63, 0, 63, 63, WHITE);
    glcd.drawLine(63, 16, 127, 16, WHITE);
    glcd.drawLine(0, 32, 127, 32, WHITE);
    glcd.drawLine(63, 48, 127, 48, WHITE);
    
    #if DEBUG
      Serial.print(glcd_display.box_title[glcd_data.box]);
      Serial.print(", ");
      Serial.println(glcd_display.box_data[glcd_data.box]);
    #endif
 
    for (i=0; i<6; i++){
        switch (i) {
          case 0: { x=1; y=1; break; }
          case 1: { x=65; y=1; break; }
          case 2: { x=65; y=17; break; }
          case 3: { x=1; y=33; break; }
          case 4: { x=65; y=33; break; }
          case 5: { x=65; y=49; break; }
        }
        glcd.setFont(font_4x6);
        glcd.drawString(x, y, glcd_display.box_title[i]);
        switch (i) {
          case 0: case 3: {
              glcd.setFont(font_10x20);
              glcd.drawString(x, y+10, glcd_display.box_data[i]);
              break;
          }
          case 1: case 2: case 4: case 5: {
              glcd.setFont(font_clR5x8);
              glcd.drawString(x+2, y+7, glcd_display.box_data[i]);
              break;
          }
        }
        
    }
    glcd.refresh();
}

void setup () {
    button.mode(INPUT);
    #if DEBUG
      Serial.begin(57600);
      Serial.println("\n[Start debug output]");
    #endif
    rf12_initialize(4, RF12_868MHZ, 5); // 868 Mhz, net group 5, node 4
    rf12_easyInit(0); // Send interval = 0 sec (=immediate)
    sensors.begin(); // DS18B20 default precision 12 bit.
    glcd.begin();
    glcd.backLight(255);
    light.set(duration); // for dimming purposes
    display_data();
}

void loop () {
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
        #if DEBUG
          Serial.println("button pressed");
        #endif
        for(i=50; i<255; i++) {
          glcd.backLight(i);
          delay(3);
        }
        light.set(duration);
    }
    if (light.poll()) { 
        for(i=255; i>50; i--) {
          glcd.backLight(i);
          delay(3);
        }
    }
}
