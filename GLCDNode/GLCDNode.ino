// GLCDNode
// --------
// Node connected to the 64x128 graphic LCD.
// Node address: 868 Mhz, net group 5, node 4.
// Local sensors: - inside temperature (DS18B20)
//                - microswitch (to light up GLCD)
// Receives:      - (1) electricity actual usage (from CentralNode)
//                - (2) solar actual production (from CentralNode)
//                - (4) outside temperature (from CentralNode)
//                - (5) outside pressure (from CentralNode)
//                - local time with every packet received (from CentralNode)
// Sends:         - (i) inside temperature (to CentralNode)
// Other:         - 64x128 graphic LCD (to display electricity usage),
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
// 06mar2013    Jos     Changed the way data is received from CentralNode
// 08mar2013    Jos     Added hours & mins to struct for receiving time from CentralNode
// 12mar2013    Jos     Changed contrast for new display using glcd.begin(0x1a)
// 19mar2013    Jos     Optimized communication structures for rf12
// 30mar2013    Jos     Added code to display solar data
// 02oct2013    Jos     Using rf12_sendNow in stead of rf12_easySend & rf12_easyPoll. Removed rf12ResetMetro code


#define DEBUG 0

#include <JeeLib.h>
#include <Metro.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <GLCD_ST7565.h>
#include "utility/font_4x6.h"
#include "utility/font_helvB10.h"
#include "utility/font_helvB12.h"
#include "utility/font_helvB18.h"

// Crash protection: Jeenode resets itself after x seconds of none activity (set in WDTO_xS)
const int UNO = 1;    // Set to 0 if your not using the UNO bootloader (i.e using Duemilanove)
#include <avr/wdt.h>  // All Jeenodes have the UNO bootloader
ISR(WDT_vect) { Sleepy::watchdogEvent(); }

// **** START of var declarations ****

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

// timers
Metro temperatureMetro = Metro(60500);   // sample temperature every 60.5 sec
Metro LDRMetro = Metro(1000);            // sample LDR every 1 sec
Metro wdtMetro = Metro(1500);            // watchdog timer reset every 1.5 sec

// structures for rf12 communication
typedef struct { char type;
       	         long var1;
		 long var2;
		 long var3;
} s_payload_t;  // Sensor data payload, size = 13 bytes
s_payload_t s_data;

typedef struct { byte type;
                 int value;
                 byte hours, mins;
} d_payload_t;  // Display data payload, size = 5 bytes
d_payload_t d_data;

// vars for keeping display data
char d_value[6][10];
       
// vars for lighting up display for 60 sec
MilliTimer light;
int duration=60000;
int i;

// **** END of var declarations ****

void display_data () {
    byte i;
    char hours[6];
    char mins[6];

        switch (d_data.type)
        {
          case 1: {  // Electricity data
            sprintf(d_value[1], " %4d", d_data.value);
            break;
          }
          case 2: {  // Solar data
            sprintf(d_value[2], " %4d", d_data.value);
            break;
          }
          case 3: {  // Inside temperature
            if (d_data.value > -1 || d_data.value < -9) {
              sprintf(d_value[3], "%3d.%d", d_data.value/10, abs(d_data.value%10));
            } else {
              sprintf(d_value[3], "-%2d.%d", d_data.value/10, abs(d_data.value%10));
            }
            break;
          }
          case 4: {  // Outside temperature
            if (d_data.value > -1 || d_data.value < -9) {
              sprintf(d_value[4], "%3d.%d", d_data.value/10, abs(d_data.value%10));
            } else {
              sprintf(d_value[4], "-%2d.%d", d_data.value/10, abs(d_data.value%10));
            }
            break;
          }
          case 5: {  // Outside pressure
            sprintf(d_value[5], "%4d.%d", d_data.value/10, d_data.value%10);
            break;
          }
          case 6: {  // Spare
            break;
          }
        }
    
    glcd.clear();
    
    // Write all the data for the display
    glcd.setFont(font_4x6);
    glcd.drawString_P(1, 1, PSTR("   Verbruik W"));
    glcd.drawString_P(65, 1, PSTR("Zonnepanelen W"));
    glcd.drawString_P(1, 32, PSTR(" Temperatuur C"));
    glcd.drawString_P(65, 45, PSTR("Luchtdruk hPa"));
    // display time
    glcd.setFont(font_helvB12);
    sprintf(hours, "%02d:", d_data.hours);
    glcd.drawString(81, 30, hours);
    sprintf(mins, "%02d", d_data.mins);
    glcd.drawString(106, 30, mins);

    for (i=0; i<6; i++){
        switch (i) {
          case 1:{  // Electricity
            glcd.setFont(font_helvB18);
            glcd.drawString(0, 9, d_value[1]);
            break;
          }
          case 2:{  // Solar
            glcd.setFont(font_helvB18);
            glcd.drawString(65, 9, d_value[2]);
            break;
          }
          case 3:{  // Inside temperature
            // Right pointing arrow (inside temp)
            glcd.drawLine(1, 45, 8, 45, WHITE);
            glcd.drawLine(5, 42, 5, 48, WHITE);
            glcd.drawLine(6, 43, 6, 47, WHITE);
            glcd.drawLine(7, 44, 7, 46, WHITE);

            glcd.setFont(font_helvB10);
            glcd.drawString(12, 38, d_value[3]);
            break;
          }
          case 4:{  // Outside temperature
            // Left pointing arrow (outside temp)
            glcd.drawLine(1, 56, 8, 56, WHITE);
            glcd.drawLine(2, 55, 2, 57, WHITE);
            glcd.drawLine(3, 54, 3, 58, WHITE);
            glcd.drawLine(4, 53, 4, 59, WHITE);

            glcd.setFont(font_helvB10);
            glcd.drawString(12, 51, d_value[4]);
            break;
          }
          case 5:{  // Outside pressure
            glcd.setFont(font_helvB10);
            glcd.drawString(67, 51, d_value[5]);
            break;
          }
          case 6:{  // Spare
            break;
          }
        }
    }
    
    glcd.refresh();
}

void get_temperature () {
    sensors.requestTemperatures(); // Send the command to get temperatures
    itemp=10*sensors.getTempCByIndex(0);
    s_data.type = 'i'; // type is inside temperature data
    s_data.var1 = (long) itemp;
    s_data.var2 = 0;
    #if DEBUG
      Serial.print("i ");
      Serial.print(itemp);
      Serial.println(" "); // extra space at the end is needed
    #endif
    rf12_sendNow(0, &s_data, sizeof s_data);
     
    d_data.type=3;
    d_data.value=itemp;
}

void init_rf12 () {
    rf12_initialize(4, RF12_868MHZ, 5); // 868 Mhz, net group 5, node 4
}

void setup () {
    button.mode(INPUT);
    #if DEBUG
      Serial.begin(57600);
      Serial.println("\n[Start debug output]");
    #endif
    init_rf12();
    sensors.begin(); // DS18B20 default precision 12 bit.
    glcd.begin(0x1a);  // set contast between 0x15 and 0x1a
    d_data.type=0;
    get_temperature();
    display_data();
    
    if (UNO) wdt_enable(WDTO_8S);  // set timeout to 8 seconds
}

void loop () {
    if ( temperatureMetro.check() ) {
        get_temperature();
        display_data();
    }
    
    if (rf12_recvDone() && rf12_crc == 0 && rf12_len == sizeof(d_payload_t)) {
        d_data = *(d_payload_t*) rf12_data;
        #if DEBUG
          Serial.print(d_data.type);
          Serial.print(" ");
          Serial.println(d_data.value);
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
        LDRbacklight=map(LDR,0,400,25,250);     	// Map LDR data to GLCD brightness
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
