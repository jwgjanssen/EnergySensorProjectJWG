// SensorNode
// ----------
// Node connected to the sensors on the electricity- and gas-meter.
// Node address: 868 Mhz, net group 5, node 3.
// Local sensors: - reflective photosensor for electricitymeter(2 x RPR-220)
//                - reflective photosensor for gasmeter(1 x CNY70)
// Receives:      - eeprom data (eeprom contains sensor trigger values) (from CentralNode)
// Sends:         - current eeprom sensor trigger values (to CentralNode)
// Other:         - 
//
// Author: Jos Janssen
// Modifications:
// Date:        Who:	Added:
// 20jan2012    Jos	Receive command for reading/writing eeprom of sensor node
// 02jul2012    Jos	Changed vars for use with millis() from long to unsigned long
// 02jul2012	Jos	Added overflow protection for use with millis()
// 30aug2012	Jos	Because power sometimes showed erratic value when direction had changed,
//                        the time- and power-calculation has been adapted
// 31aug2012	Jos	Changed layout/readability/comments
// 09sep2012    Jos     Included code for 8 second watchdog reset
// 15feb2013    Jos     Added code to re-initialze the rf12 every one week to avoid hangup after a long time
// 15feb2013    Jos     Changed sampleMetro from 5 to 2 ms

#include <JeeLib.h>
#include <Metro.h>
#include <util/crc16.h>
#include <EEPROM.h>

#define DEBUG 0

// Crash protection: Jeenode resets itself after x seconds of none activity (set in WDTO_xS)
const int UNO = 1;    // Set to 0 if your not using the UNO bootloader (i.e using Duemilanove)
#include <avr/wdt.h>  // All Jeenodes have the UNO bootloader
ISR(WDT_vect) { Sleepy::watchdogEvent(); }

#define SENSOR_EEPROM_ADDR 0x60  //96 = 0x60

Port portLeft (1);
Port portRight (2);
Port electr_led (1);

Port portGas (3);
Port gas_led (3);

Metro rf12ResetMetro = Metro(604800000); // re-init rf12 every 1 week
Metro sampleMetro = Metro(2);
Metro wdtMetro = Metro(1000);

// Variables for the sensor readings
boolean stateLeft = 0;
boolean stateRight = 0;
boolean stateGas = 0;

int minLeft;
int maxLeft;

int minRight;
int maxRight;

int minGas;
int maxGas;

int mminLeft = 1024;
int mmaxLeft = 0;
int mminRight = 1024;
int mmaxRight = 0;
int mminGas = 1024;
int mmaxGas = 0;
int nextMinLeft = 1024;
int nextMaxLeft = 0;
int nextMinRight = 1024;
int nextMaxRight = 0;
int nextMinGas = 1024;
int nextMaxGas = 0;

int lightLeft = 0;  // latest reading left electricity sensor
int lightRight = 0; // latest reading right electricity sensor
int lightGas = 0;   // latest reading gas sensor

int e_margin = 20; // margin for determining peaks for electricity sensor readings
int g_margin = 20; // margin for determining peaks for gas sensor readings

// Variables for timing/counting
int e_direction = 1;        // 1=forward(normal), -1=backward
int e_prev_direction = 1;   // 1=forward(normal), -1=backward
int e_report_direction = 1; // 1=forward(normal), -1=backward
boolean direction_changed = 0;
long e_rotations = 0;
long watt = 0;

long g_rotations = 0;
long gas_ltr = 0;

unsigned long Ms = 0;         // timestamp of current peak
unsigned long prevMs = 0;     // timestamp of last peak
unsigned long prevprevMs = 0; // timestamp of 2nd last peak
long rotationMs = 0;

// Variables for sending/displaying readings
int e_onceDone = 0;
int e_onceDisplayed = 0;

int g_onceDone = 0;
int g_onceDisplayed = 0;

// Variables for reading, changing or reporting eeprom values
typedef struct {
  int e_minL, e_maxL;
  int e_minR, e_maxR;
  int g_min, g_max;
  int w_min, w_max;
  int crc;
} SensorConfig;
SensorConfig rconfig, wconfig;

typedef struct {
  char command[5];
  int value;
} eeprom_command;
eeprom_command change_eeprom;
int crc;
char i;
char b;
int do_write = 0;
int do_report = 0;

// Variables for wireless communication (sending sensor readings)
struct { char type; long actual; long rotations; long rotationMs;
         int minA; int maxA; int minB; int maxB;
         int minC; int maxC; int minD; int maxD;
} payload;


void read_eeprom() {
    #if DEBUG
      Serial.print("Read EEPROM sensor values ... ");
    #endif
    // read crc
    crc=~0;
    for (i=0; i < sizeof rconfig; ++i) {
        crc=_crc16_update(crc, EEPROM.read(SENSOR_EEPROM_ADDR+i));
    }
    if (crc != 0) {
        #if DEBUG
          Serial.println("CRC error");
        #endif
        while (1);
    } else {
        #if DEBUG
          Serial.println("OK");
        #endif
        // read EEPROM
        for (i=0; i < sizeof rconfig - 2; ++i) {
            ((char*) &rconfig)[i]=EEPROM.read(SENSOR_EEPROM_ADDR+i);
        }
    }
    // fill program variables with the eeprom values
    minLeft=rconfig.e_minL;
    maxLeft=rconfig.e_maxL;
    minRight=rconfig.e_minR;
    maxRight=rconfig.e_maxR;
    minGas=rconfig.g_min;
    maxGas=rconfig.g_max;
}


void write_eeprom() {
    #if DEBUG
      Serial.println("Write EEPROM sensor values ... ");
    #endif
    // compute crc
    wconfig.crc=~0;
    for (i=0; i<sizeof wconfig - 2; i++) {
        wconfig.crc=_crc16_update(wconfig.crc, ((char*) &wconfig)[i]);
    }
    #if DEBUG
      Serial.print("computed CRC: ");
      Serial.print(wconfig.crc);
      Serial.print(" ... ");
    #endif
    
    // save to EEPROM
    for (i=0; i < sizeof wconfig; ++i) {
        b=((char*) &wconfig)[i];
        EEPROM.write(SENSOR_EEPROM_ADDR+i, b);
    }
    #if DEBUG
      Serial.println("OK");
    #endif
}


void report_settings() {
    payload.type='s';
    payload.minA=rconfig.e_minL;
    payload.maxA=rconfig.e_maxL;
    payload.minB=rconfig.e_minR;
    payload.maxB=rconfig.e_maxR;
    payload.minC=rconfig.g_min;
    payload.maxC=rconfig.g_max;
    payload.minD=rconfig.w_min;
    payload.maxD=rconfig.w_max;
    
    // send report
    #if DEBUG
      Serial.print("Sending report ... ");
    #endif
    rf12_easySend(&payload, sizeof payload);
    rf12_easyPoll(); // Actually send the data
    #if DEBUG
      Serial.println("OK");
    #endif
}


void interpret_command() {
    #if DEBUG
      Serial.print(change_eeprom.command);
      Serial.print(" -> ");
      Serial.println(change_eeprom.value);
    #endif
    
    // make the config to write equal to the current config
    wconfig.e_minL=rconfig.e_minL;
    wconfig.e_maxL=rconfig.e_maxL;
    wconfig.e_minR=rconfig.e_minR;
    wconfig.e_maxR=rconfig.e_maxR;
    wconfig.g_min=rconfig.g_min;
    wconfig.g_max=rconfig.g_max;
    wconfig.w_min=rconfig.w_min;
    wconfig.w_max=rconfig.w_max;
    
    // change the wanted config item, the rest remains unchanged
    if (strcmp(change_eeprom.command, "emnl") == 0) {
      wconfig.e_minL=change_eeprom.value;
      do_write=1;
    }
    if (strcmp(change_eeprom.command, "emxl") == 0) {
      wconfig.e_maxL=change_eeprom.value;
      do_write=1;
    }
    if (strcmp(change_eeprom.command, "emnr") == 0) {
      wconfig.e_minR=change_eeprom.value;
      do_write=1;
    }
    if (strcmp(change_eeprom.command, "emxr") == 0) {
      wconfig.e_maxR=change_eeprom.value;
      do_write=1;
    }
    if (strcmp(change_eeprom.command, "gmin") == 0) {
      wconfig.g_min=change_eeprom.value;
      do_write=1;
    }
    if (strcmp(change_eeprom.command, "gmax") == 0) {
      wconfig.g_max=change_eeprom.value;
      do_write=1;
    }
    if (strcmp(change_eeprom.command, "wmin") == 0) {
      wconfig.w_min=change_eeprom.value;
      do_write=1;
    }
    if (strcmp(change_eeprom.command, "wmax") == 0) {
      wconfig.w_max=change_eeprom.value;
      do_write=1;
    }
    
    // this is reporting only, no settings are changed
    if (strcmp(change_eeprom.command, "gtst") == 0) {
      do_report=1;
    }
}


void flashd(Port light) {
    light.digiWrite(1);
    delay(20);
    light.digiWrite(0);
}


void init_rf12 () {
    rf12_initialize(3, RF12_868MHZ, 5); // 868 Mhz, net group 5, node 3
    rf12_easyInit(0); // Send interval = 0 sec (=immediate).
}


void setup() {
    #if DEBUG
      Serial.begin(57600);
      Serial.println("\n[Monitoring a Electricity & Gas Meter]");
    #endif
    init_rf12();
    //rf12_initialize(3, RF12_868MHZ, 5); // 868 Mhz, net group 5, node 3
    //rf12_easyInit(0); // Send interval = 0 sec (=immediate).
    portLeft.mode(INPUT);
    portRight.mode(INPUT);
    electr_led.mode(OUTPUT);
    portGas.mode(INPUT);
    gas_led.mode(OUTPUT);

    // initialise min-max values for sensors
    read_eeprom();
    
    if (UNO) wdt_enable(WDTO_8S);  // set timeout to 8 seconds
}

  
void loop() {
    // re-initialize rf12 every week to avoid rf12 hangup after a long time
    if ( rf12ResetMetro.check() ) {
         init_rf12();
    }
    // take a reading from the sensors 
    if ( sampleMetro.check() ) {
         // read sensors
         lightLeft=portLeft.anaRead();
         lightRight=portRight.anaRead();
         lightGas=portGas.anaRead();

         // to monitor changing peak sizes, we follow the peak size continuously
         // the found peak size serves as the target (minus a small margin) for the next peak detection
         // update minimum and maximum for the left sensor
         if ( lightLeft > nextMaxLeft ) {
             nextMaxLeft=lightLeft;
         }  
         // we also do this for the valley
         if ( lightLeft < nextMinLeft ) {
             nextMinLeft=lightLeft;
         }
          
         // and the same for the right sensor
         // update minimum and maximum for the right sensor
         if ( lightRight > nextMaxRight ) {
             nextMaxRight=lightRight;
         }  
         if ( lightRight < nextMinRight ) {
             nextMinRight=lightRight;
         }

         // and the same for the gas sensor
         // update minimum and maximum for the gas sensor
         if ( lightGas > nextMaxGas ) {
             nextMaxGas=lightGas;
         }  
         if ( lightGas < nextMinGas ) {
             nextMinGas=lightGas;
         }

         //
         // Electricity:
         //   Normally the state = 0. When the painted mark on the disc is at the sensor, the state = 1.
         //   The mark is the least reflective part of the disc, giving the highest sensor value readout.
         //
         if ( (stateLeft == 0) && (lightLeft > maxLeft - e_margin) ) {  // the mark is at the left sensor, light above threshold
             stateLeft=1;
             #if DEBUG                    
                 Serial.println("L1");
             #endif
             if ( (stateRight==0) && (e_direction==-1) ) {  // calculate rotation direction
                 e_prev_direction=-1;  // save previous direction
                 e_direction=1;
		 direction_changed=1;
                 #if DEBUG                    
                     Serial.println("-1 > 1");
                 #endif
             } else if ( (stateRight==1) && (e_direction==1) ) {
                 e_prev_direction=1;  // save previous direction
                 e_direction=-1;
		 direction_changed=1;
                 #if DEBUG                    
                     Serial.println("1 > -1");
                 #endif
             }
	     e_report_direction=e_direction;  // direction power calculations is the current direction
             if ( e_onceDone == 0) {
		 if (direction_changed) {
                     // calculate time between the last two peaks, now and prevprevMs (because of direction change)
                     //   timed only once during stateLeft = 1
                     Ms=millis(); // 4 bytes, 32 bits, = 49.7 days
                     if(Ms < prevprevMs) {	// Overflow protection (use preprevMs because of direction change)
                         rotationMs=(4294967295-prevprevMs)+Ms;
                     } else {
                         rotationMs=Ms-prevprevMs;             
                     }
                     prevprevMs=prevMs;
                     prevMs=Ms;
                     e_onceDone=1;
                     e_onceDisplayed=0;
		     direction_changed=0;
		     e_report_direction=e_prev_direction;  // direction for power calculations is the previous direction,
                                                           //   because no full rotation has been made the timing is of.
                                                           //   To compensate, prevprevMs is used. To display a sensible
                                                           //   powervalue, the previous direction is used here.
		 } else {
                     // calculate time between the last two peaks, now and prevMs timed only once during stateLeft = 1
                     Ms=millis(); // 4 bytes, 32 bits, = 49.7 days
                     if(Ms < prevMs) {	// Overflow protection
                         rotationMs=(4294967295-prevMs)+Ms;
                     } else {
                         rotationMs=Ms-prevMs;             
                     }
		     prevprevMs=prevMs;
                     prevMs=Ms;
                     e_onceDone=1;
                     e_onceDisplayed=0;
                 }
             }
             mminLeft=nextMinLeft;  // going to minimum, reset minimum value of left sensor
             nextMinLeft=1024;
         } else if ( (stateLeft == 1) && (lightLeft < minLeft + e_margin) ) {  // the mark is not at the left sensor, light below threshold
             stateLeft=0;
             #if DEBUG                    
                 Serial.println("L0");
             #endif
             mmaxLeft=nextMaxLeft;  // going to a maximum, reset maximum value of left sensor
             nextMaxLeft=0;
             e_onceDone=0; // reset e_onceDone in a minimum
         }
          
         if ( (stateRight == 0) && (lightRight > maxRight - e_margin) ) {  // the mark is at the right sensor, light below threshold
             stateRight=1;
             #if DEBUG                    
                 Serial.println("R1");
             #endif
             mminRight=nextMinRight;  // going to a minimum, reset minimum value of right sensor
             nextMinRight=1024;
         } else if ( (stateRight == 1) && (lightRight < minRight + e_margin) ) {  // the mark is not at the right sensor, light above threshold
             stateRight=0;
             #if DEBUG                    
                 Serial.println("R0");
             #endif
             mmaxRight=nextMaxRight;  // going to a maximum, reset maximum value of right sensor
             nextMaxRight=0;
         }
         
         //
         // Gas:
         //   Normally the state = 0. When the 0 with the little "mirror" is at the sensor, the state = 1.
         //   The mirror is the most reflective part of the counter, giving the lowest sensor value readout.
         //
         if ( (stateGas == 0) && (lightGas < minGas + g_margin) ) {  //  the mirror is at the sensor, light below threshold
             stateGas=1;
             mmaxGas=nextMaxGas;  // going to a maximum, reset maximum value of Gas sensor
             nextMaxGas=0;
             // Set appropriate values only once in during stateGas = 1
             if (g_onceDone == 0) {
                 g_onceDone=1;
                 g_onceDisplayed=0;
             }
         } else if ( (stateGas == 1) && (lightGas > maxGas - g_margin) ) {  // the mirror is not at the sensor, light above threshold
             stateGas=0;
             mminGas=nextMinGas;  // going to a minimum, reset minimum value of Gas sensor
             nextMinGas=1024;
             g_onceDone=0; // reset g_onceDone in a minimum
         }
         
    }
        
        
    // print current status when in maximum and not displayed before in this maximum
    if ((e_onceDone == 1)&&(e_onceDisplayed == 0)&&(maxLeft-minLeft > 50) ) {

        watt=e_report_direction*6000000/rotationMs; // my kwh meter says 600 rotations per kwh
                                     //watt = e_direction * 3600000/(600*(tempDelay/1000));
        
        // count rotations only if the number of watts is sensible
        if ((watt > -600)&&(watt < 7500)) {
            e_rotations=e_rotations+e_direction; // = +1 when e_direction=1, = -1 when e_direction=-1
            flashd(electr_led);
        }
        
        payload.type='e'; // type is electricity data
        payload.actual=watt;
        payload.rotations=e_rotations;
        payload.rotationMs=rotationMs;
        payload.minA=mminLeft; payload.maxA=mmaxLeft;
        payload.minB=mminRight; payload.maxB=mmaxRight;
        rf12_easySend(&payload, sizeof payload);
        #if DEBUG                    
          Serial.print("e ");
          Serial.print(watt);
          Serial.print(" Watt, count=");
          Serial.print(e_rotations);
          Serial.print(", time=");
          Serial.print(rotationMs);
          Serial.print(" ms");
          Serial.print(",   measured min-max: L:");
          Serial.print(mminLeft);
          Serial.print("->");
          Serial.print(mmaxLeft);
          Serial.print(" R:");
          Serial.print(mminRight);
          Serial.print("->");
          Serial.print(mmaxRight);
          Serial.println("");
        #endif
        e_onceDisplayed=1;
        rf12_easyPoll(); // Actually send the data
    }
    
    
    // print current status when in maximum and not displayed before in this maximum
    if ((g_onceDone == 1)&&(g_onceDisplayed == 0)&&(maxGas-minGas > 50) ) {
        gas_ltr=gas_ltr+10;
       
        g_rotations++;
        flashd(gas_led);

        payload.type='g'; // type is gas data
        payload.actual=gas_ltr;
        payload.rotations=g_rotations;
        payload.rotationMs=rotationMs;
        payload.minC=mminGas; payload.maxC=mmaxGas;
        rf12_easySend(&payload, sizeof payload);
        #if DEBUG                    
          Serial.print("g ");
          Serial.print(gas_ltr);
          Serial.print(" Liter, count=");
          Serial.print(g_rotations);
          Serial.print(",   measured min-max: ");
          Serial.print(mminGas);
          Serial.print("->");
          Serial.print(mmaxGas);
          Serial.println("");
        #endif
        g_onceDisplayed=1;
        rf12_easyPoll(); // Actually send the data
    }
    
    // receive command for changing eeprom values or report settings
    if (rf12_recvDone() && rf12_crc == 0 && rf12_len == sizeof (change_eeprom)) {
        change_eeprom=*(eeprom_command*) rf12_data;
        interpret_command();
        if (RF12_WANTS_ACK) {
            rf12_sendStart(RF12_ACK_REPLY, 0, 0);
        }
        delay(10);
        if (do_write) {
            write_eeprom();
            read_eeprom();
            do_write=0;
        }
        if (do_report) {
            report_settings();
            do_report=0;
        }
    }
        
    if ( wdtMetro.check() ) {
      if (UNO) wdt_reset();
    }
}

