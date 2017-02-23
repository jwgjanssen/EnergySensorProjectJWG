// SensorNode
// ----------
// Node connected to the sensors on the electricity- and gas-meter.
// Node address: 868 Mhz, net group 5, node 3.
// Local sensors: - reflective photosensor for electricitymeter(2 x RPR-220)
//                - reflective photosensor for gasmeter(1 x CNY70)
// Receives:      - eeprom data (eeprom contains sensor trigger values) (from CentralNode)
// Sends:         - (e) electricity readings (to CentralNode)
//                - (g) gas readings (to CentralNode)
//                - (w) water readings (to CentralNode)
//                - (l) list current eeprom sensor trigger values (to CentralNode)
//                - (x) adjusted water sensor trigger values (to CentralNode)
//                - (y) adjusted gas sensor trigger values (to CentralNode)
//                - (z) adjusted electricity sensor trigger values (to CentralNode)
// Other:         - 
//
// Author: Jos Janssen
// Modifications:
// Date:        Who:    Added:
// 20jan2012    Jos     Receive command for reading/writing eeprom of sensor node
// 02jul2012    Jos     Changed vars for use with millis() from long to unsigned long
// 02jul2012    Jos     Added overflow protection for use with millis()
// 30aug2012    Jos     Because power sometimes showed erratic value when direction had changed,
//                        the time- and power-calculation has been adapted
// 31aug2012    Jos     Changed layout/readability/comments
// 09sep2012    Jos     Included code for 8 second watchdog reset
// 15feb2013    Jos     Added code to re-initialze the rf12 every one week to avoid hangup after a long time
// 15feb2013    Jos     Changed sampleMetro from 5 to 2 ms
// 04mar2013    Jos     Added code to update eeprom sensor trigger values once a week
// 19mar2013    Jos     Optimized communication structures for rf12
// 02oct2013    Jos     Using rf12_sendNow in stead of rf12_easySend & rf12_easyPoll. Removed rf12ResetMetro code
// 09oct2015    Jos     Changed data type letter coding due to future addition of water sensor
// 09oct2015    Jos     Added code for handling water sensor
// 04Nov2015    Jos     Solved bug in reporting adjusted water sensor trigger values
// 19Nov2015    Jos     Solved bug in calculating mean water sensor trigger values

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

// **** START of var declarations ****

// what is connected to which port on JeeNode
Port portLeft (1);
Port portRight (2);
Port electr_led (1);
Port portGas (3);
Port gas_led (3);
Port portWater (4);
Port water_led (4);

// structures for rf12 communication
typedef struct { char type;
  long var1;
  long var2;
  long var3;
} s_payload_t;  // Sensor data payload, size = 13 bytes
s_payload_t s_data;

typedef struct { char type;
  int minA; int maxA; int minB; int maxB;
  int minC; int maxC; int minD; int maxD;
} l_payload_t;  // Status data payload, size = 17 bytes
l_payload_t l_data;

typedef struct { char command[5];
  int value;
} eeprom_command_t;  // EEprom command payload, size = 7 bytes
eeprom_command_t change_eeprom;

// timers
Metro sampleMetro = Metro(2);            // Sample the sensors every 2 ms
Metro wdtMetro = Metro(1000);            // reset watchdog timer every 1 sec
Metro EeepromMetro = Metro(604800000);   // write mean measured electricity sensor trigger values to eeprom every 1 week
Metro GeepromMetro = Metro(604800000);   // write mean measured gas sensor trigger values to eeprom every 1 week
Metro WeepromMetro = Metro(604800000);   // write mean measured water sensor trigger values to eeprom every 1 week
//Metro EeepromMetro = Metro(86400000);   // write mean measured electricity sensor trigger values to eeprom every day
//Metro GeepromMetro = Metro(86400000);   // write mean measured gas sensor trigger values to eeprom every day

// vars read from eeprom
int minLeft;
int maxLeft;
int minRight;
int maxRight;
int minGas;
int maxGas;
int minWater;
int maxWater;

// vars for the sensor readings
boolean stateLeft = 0;
boolean stateRight = 0;
boolean stateGas = 0;
boolean stateWater = 0;

int lightLeft = 0;  // latest reading left electricity sensor
int lightRight = 0; // latest reading right electricity sensor
int lightGas = 0;   // latest reading gas sensor
int lightWater = 0; // latest reading water sensor

int mminLeft = 1024;
int mmaxLeft = 0;
int mminRight = 1024;
int mmaxRight = 0;
int mminGas = 1024;
int mmaxGas = 0;
int mminWater = 1024;
int mmaxWater = 0;
int currMinLeft = 1024;
int currMaxLeft = 0;
int currMinRight = 1024;
int currMaxRight = 0;
int currMinGas = 1024;
int currMaxGas = 0;
int currMinWater = 1024;
int currMaxWater = 0;

// vars for timing/counting
int e_direction = 1;        // 1=forward(normal), -1=backward
int e_prev_direction = 1;   // 1=forward(normal), -1=backward
int e_report_direction = 1; // 1=forward(normal), -1=backward
boolean direction_changed = 0;
long e_rotations = 0;
long watt = 0;

long g_rotations = 0;
long gas_ltr = 0;

long w_rotations = 0;
long water_ltr = 0;

unsigned long Ms = 0;         // timestamp of current peak
unsigned long prevMs = 0;     // timestamp of last peak
unsigned long prevprevMs = 0; // timestamp of 2nd last peak
long rotationMs = 0;

// vars for sending/displaying readings
int e_onceDone = 0;
int e_onceDisplayed = 0;

int g_onceDone = 0;
int g_onceDisplayed = 0;

int w_onceDone = 0;
int w_onceDisplayed = 0;

// vars for reading, changing or reporting eeprom values
typedef struct { int e_minL, e_maxL;
  int e_minR, e_maxR;
  int g_min, g_max;
  int w_min, w_max;
  int crc;
} SensorConfig_t;
SensorConfig_t rconfig, wconfig;
int crc;
char i;
char b;
int do_write = 0;
int do_report = 0;

// vars for updating sensor trigger values in eeprom
typedef struct { int e_minL, e_maxL;
  int e_minR, e_maxR;
} Eset;

typedef struct { int g_min, g_max;
} Gset;

typedef struct { int w_min, w_max;
} Wset;

Eset Evalues[10];
Gset Gvalues[10];
Wset Wvalues[10];
boolean do_save_Evalues = 0;
boolean do_save_Gvalues = 0;
boolean do_save_Wvalues = 0;
int Ecounter;
char Ei;
int Gcounter;
char Gi;
int Wcounter;
char Wi;
int meanMinL, meanMaxL, meanMinR, meanMaxR;
int meanMinG, meanMaxG;
int meanMinW, meanMaxW;

// **** END of var declarations ****


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
  minWater=rconfig.w_min;
  maxWater=rconfig.w_max;
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
  l_data.type='l';
  l_data.minA=rconfig.e_minL;
  l_data.maxA=rconfig.e_maxL;
  l_data.minB=rconfig.e_minR;
  l_data.maxB=rconfig.e_maxR;
  l_data.minC=rconfig.g_min;
  l_data.maxC=rconfig.g_max;
  l_data.minD=rconfig.w_min;
  l_data.maxD=rconfig.w_max;
  
  // send report
  #if DEBUG
  Serial.print("Sending report ... ");
  #endif
  rf12_sendNow(0, &l_data, sizeof l_data);
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
}


void setup() {
  #if DEBUG
  Serial.begin(57600);
  Serial.println("\n[Monitoring a Electricity & Gas & Water meter]");
  #endif
  init_rf12();
  portLeft.mode(INPUT);
  portRight.mode(INPUT);
  electr_led.mode(OUTPUT);
  portGas.mode(INPUT);
  gas_led.mode(OUTPUT);
  portWater.mode(INPUT);
  water_led.mode(OUTPUT);

  // initialise min-max values for sensors
  read_eeprom();
  
  if (UNO) wdt_enable(WDTO_8S);  // set timeout to 8 seconds
}


void loop() {
  // take a reading from the sensors 
  if ( sampleMetro.check() ) {
    // read sensors
    lightLeft=portLeft.anaRead();
    lightRight=portRight.anaRead();
    lightGas=portGas.anaRead();
    lightWater=portWater.anaRead();

    // to monitor changing peak & valley sizes, we follow the sizes continuously
    // the found sizes serve as the target for the next detection
    
    // update minimum and maximum for the left sensor
    if ( lightLeft > currMaxLeft ) {
      currMaxLeft=lightLeft;
    }  
    if ( lightLeft < currMinLeft ) {
      currMinLeft=lightLeft;
    }
    
    // update minimum and maximum for the right sensor
    if ( lightRight > currMaxRight ) {
      currMaxRight=lightRight;
    }  
    if ( lightRight < currMinRight ) {
      currMinRight=lightRight;
    }

    // update minimum and maximum for the gas sensor
    if ( lightGas > currMaxGas ) {
      currMaxGas=lightGas;
    }  
    if ( lightGas < currMinGas ) {
      currMinGas=lightGas;
    }

    // update minimum and maximum for the water sensor
    if ( lightWater > currMaxWater ) {
      currMaxWater=lightWater;
    }  
    if ( lightWater < currMinWater ) {
      currMinWater=lightWater;
    }
    //
    // Electricity:
    //   Normally the state = 0. When the painted mark on the disc is at the sensor, the state = 1.
    //   The mark is the least reflective part of the disc, giving the highest sensor value readout.
    //
    if ( (stateLeft == 0) && (lightLeft > maxLeft) ) {  // the mark is at the left sensor, light above threshold
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
          if (Ms < prevprevMs) {	// Overflow protection (use preprevMs because of direction change)
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
          if (Ms < prevMs) {	// Overflow protection
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
      mminLeft=currMinLeft;  // going to minimum, reset minimum value of left sensor
      currMinLeft=1024;
    } else if ( (stateLeft == 1) && (lightLeft < minLeft) ) {  // the mark is not at the left sensor, light below threshold
      stateLeft=0;
      #if DEBUG                    
      Serial.println("L0");
      #endif
      mmaxLeft=currMaxLeft;  // going to a maximum, reset maximum value of left sensor
      currMaxLeft=0;
      e_onceDone=0; // reset e_onceDone in a minimum
    }
    
    if ( (stateRight == 0) && (lightRight > maxRight) ) {  // the mark is at the right sensor, light below threshold
      stateRight=1;
      #if DEBUG                    
      Serial.println("R1");
      #endif
      mminRight=currMinRight;  // going to a minimum, reset minimum value of right sensor
      currMinRight=1024;
    } else if ( (stateRight == 1) && (lightRight < minRight) ) {  // the mark is not at the right sensor, light above threshold
      stateRight=0;
      #if DEBUG                    
      Serial.println("R0");
      #endif
      mmaxRight=currMaxRight;  // going to a maximum, reset maximum value of right sensor
      currMaxRight=0;
    }
    
    //
    // Gas:
    //   Normally the state = 0. When the 0 with the little "mirror" is at the sensor, the state = 1.
    //   The mirror is the most reflective part of the counter, giving the lowest sensor value readout.
    //
    if ( (stateGas == 0) && (lightGas < minGas) ) {  //  the mirror is at the sensor, light below threshold
      stateGas=1;
      #if DEBUG                    
      Serial.println("G1");
      #endif
      mmaxGas=currMaxGas;  // going to a maximum, reset maximum value of Gas sensor
      currMaxGas=0;
      // Set appropriate values only once in during stateGas = 1
      if (g_onceDone == 0) {
        g_onceDone=1;
        g_onceDisplayed=0;
      }
    } else if ( (stateGas == 1) && (lightGas > maxGas) ) {  // the mirror is not at the sensor, light above threshold
      stateGas=0;
      #if DEBUG                    
      Serial.println("G0");
      #endif
      mminGas=currMinGas;  // going to a minimum, reset minimum value of Gas sensor
      currMinGas=1024;
      g_onceDone=0; // reset g_onceDone in a minimum
    }

    //
    // Water:
    //   Normally the state = 0. When the 0 with the little "mirror" is at the sensor, the state = 1.
    //   The mirror is the most reflective part of the counter, giving the lowest sensor value readout.
    //
    if ( (stateWater == 0) && (lightWater < minWater) ) {  //  the mirror is at the sensor, light below threshold
      stateWater=1;
      #if DEBUG                    
      Serial.println("W1");
      #endif
      mmaxWater=currMaxWater;  // going to a maximum, reset maximum value of Water sensor
      currMaxWater=0;
      // Set appropriate values only once in during stateWater = 1
      if (w_onceDone == 0) {
        w_onceDone=1;
        w_onceDisplayed=0;
      }
    } else if ( (stateWater == 1) && (lightWater > maxWater) ) {  // the mirror is not at the sensor, light above threshold
      stateWater=0;
      #if DEBUG                    
      Serial.println("W0");
      #endif
      mminWater=currMinWater;  // going to a minimum, reset minimum value of Water sensor
      currMinWater=1024;
      w_onceDone=0; // reset w_onceDone in a minimum
    }
  } /* if ( sampleMetro.check() ) */

  
  
  // Electricity:
  // print current status when in maximum and not displayed before in this maximum
  if ((e_onceDone == 1)&&(e_onceDisplayed == 0)&&(maxLeft-minLeft > 50) ) {
    watt=e_report_direction*6000000/rotationMs; // my kwh meter says 600 rotations per kwh
    // count rotations only if the number of watts is sensible
    if ((watt > -600)&&(watt < 7500)) {
      e_rotations=e_rotations+e_direction; // = +1 when e_direction=1, = -1 when e_direction=-1
      flashd(electr_led);
    }
    s_data.type='e'; // type is electricity data
    s_data.var1=watt;
    s_data.var2=e_rotations;
    rf12_sendNow(0, &s_data, sizeof s_data);
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
    
    if (do_save_Evalues) {
      #if DEBUG
      Serial.print("Enter do_save_Evalues "); Serial.println(Ecounter);
      #endif
      Evalues[Ecounter].e_minL=mminLeft;  Evalues[Ecounter].e_maxL=mmaxLeft;
      Evalues[Ecounter].e_minR=mminRight; Evalues[Ecounter].e_maxR=mmaxRight;
      Ecounter++;
      if (Ecounter > 9) {
        do_save_Evalues=0;
        meanMinL=0; meanMaxL=0; meanMinR=0; meanMaxR=0;
        for (Ei=0; Ei < 10; Ei++) {
          meanMinL+=Evalues[Ei].e_minL; meanMaxL+=Evalues[Ei].e_maxL;
          meanMinR+=Evalues[Ei].e_minR; meanMaxR+=Evalues[Ei].e_maxR;
        }
        meanMinL=meanMinL/10; meanMaxL=meanMaxL/10; meanMinR=meanMinR/10; meanMaxR=meanMaxR/10;
        // fill all eeprom trigger values
        wconfig.e_minL=meanMinL+150;
        wconfig.e_maxL=meanMaxL-75;
        wconfig.e_minR=meanMinR+150;
        wconfig.e_maxR=meanMaxR-75;
        wconfig.g_min=rconfig.g_min;
        wconfig.g_max=rconfig.g_max;
        wconfig.w_min=rconfig.w_min;
        wconfig.w_max=rconfig.w_max;
        write_eeprom();
        read_eeprom();
        // Send the adjusted electricity sensor trigger values
        l_data.type='z'; // type is electricity sensor trigger values
        l_data.minA=wconfig.e_minL; l_data.maxA=wconfig.e_maxL;
        l_data.minB=wconfig.e_minR; l_data.maxB=wconfig.e_maxR;
        rf12_sendNow(0, &l_data, sizeof l_data);
        #if DEBUG                    
        Serial.print("z ");
        Serial.print("Adjusted electricity sensor trigger values: L:");
        Serial.print(wconfig.e_minL);
        Serial.print("->");
        Serial.print(wconfig.e_maxL);
        Serial.print(" R:");
        Serial.print(wconfig.e_minR);
        Serial.print("->");
        Serial.print(wconfig.e_maxR);
        Serial.println("");
        #endif
      }
    }
  }
  
  // Gas:
  // print current status when in maximum and not displayed before in this maximum
  if ((g_onceDone == 1)&&(g_onceDisplayed == 0)&&(maxGas-minGas > 50) ) {
    gas_ltr=gas_ltr+10;
    g_rotations++;
    flashd(gas_led);
    s_data.type='g'; // type is gas data
    s_data.var1=gas_ltr;
    s_data.var2=g_rotations;
    rf12_sendNow(0, &s_data, sizeof s_data);
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
    
    if (do_save_Gvalues) {
      #if DEBUG
      Serial.print("Enter do_save_Gvalues "); Serial.println(Gcounter);
      #endif
      Gvalues[Gcounter].g_min=mminGas;  Gvalues[Gcounter].g_max=mmaxGas;
      Gcounter++;
      if (Gcounter > 9) {
        do_save_Gvalues=0;
        meanMinG=0; meanMaxG=0;
        for (Gi=0; Gi < 10; Gi++) {
          meanMinG+=Gvalues[Gi].g_min; meanMaxG+=Gvalues[Gi].g_max;
        }
        meanMinG=meanMinG/10; meanMaxG=meanMaxG/10;
        // fill all eeprom trigger values
        wconfig.e_minL=rconfig.e_minL;
        wconfig.e_maxL=rconfig.e_maxL;
        wconfig.e_minR=rconfig.e_minR;
        wconfig.e_maxR=rconfig.e_maxR;
        wconfig.g_min=meanMinG+75;
        wconfig.g_max=meanMaxG-150;
        wconfig.w_min=rconfig.w_min;
        wconfig.w_max=rconfig.w_max;
        write_eeprom();
        read_eeprom();
        // Send the adjusted gas sensor trigger values
        l_data.type='y'; // type is gas sensor trigger values
        l_data.minA=wconfig.g_min; l_data.maxA=wconfig.g_max;
        rf12_sendNow(0, &l_data, sizeof l_data);
        #if DEBUG                    
        Serial.print("y ");
        Serial.print("Adjusted gas sensor trigger values: ");
        Serial.print(wconfig.g_min);
        Serial.print("->");
        Serial.print(wconfig.g_max);
        Serial.println("");
        #endif
      }
    }
  }

  // Water:
  // print current status when in maximum and not displayed before in this maximum
  if ((w_onceDone == 1)&&(w_onceDisplayed == 0)&&(maxWater-minWater > 50) ) {
    water_ltr=water_ltr+1;
    w_rotations++;
    flashd(water_led);
    s_data.type='w'; // type is water data
    s_data.var1=water_ltr;
    s_data.var2=w_rotations;
    rf12_sendNow(0, &s_data, sizeof s_data);
    #if DEBUG                    
    Serial.print("w ");
    Serial.print(water_ltr);
    Serial.print(" Liter, count=");
    Serial.print(w_rotations);
    Serial.print(",   measured min-max: ");
    Serial.print(mminWater);
    Serial.print("->");
    Serial.print(mmaxWater);
    Serial.println("");
    #endif
    w_onceDisplayed=1;
    
    if (do_save_Wvalues) {
      #if DEBUG
      Serial.print("Enter do_save_Wvalues "); Serial.println(Wcounter);
      #endif
      Wvalues[Wcounter].w_min=mminWater;  Wvalues[Wcounter].w_max=mmaxWater;
      Wcounter++;
      if (Wcounter > 9) {
        do_save_Wvalues=0;
        meanMinW=0; meanMaxW=0;
        for (Wi=0; Wi < 10; Wi++) {
          meanMinW+=Wvalues[Wi].w_min; meanMaxW+=Wvalues[Wi].w_max;
        }
        meanMinW=meanMinW/10; meanMaxW=meanMaxW/10;
        // fill all eeprom trigger values
        wconfig.e_minL=rconfig.e_minL;
        wconfig.e_maxL=rconfig.e_maxL;
        wconfig.e_minR=rconfig.e_minR;
        wconfig.e_maxR=rconfig.e_maxR;
        wconfig.g_min=rconfig.g_min;
        wconfig.g_max=rconfig.g_max;
        wconfig.w_min=meanMinW+75;
        wconfig.w_max=meanMaxW-150;
        write_eeprom();
        read_eeprom();
        // Send the adjusted water sensor trigger values
        l_data.type='x'; // type is water sensor trigger values
        l_data.minA=wconfig.w_min; l_data.maxA=wconfig.w_max;
        rf12_sendNow(0, &l_data, sizeof l_data);
        #if DEBUG                    
        Serial.print("x ");
        Serial.print("Adjusted water sensor trigger values: ");
        Serial.print(wconfig.w_min);
        Serial.print("->");
        Serial.print(wconfig.w_max);
        Serial.println("");
        #endif
      }
    }
  }
  
  // receive command for changing eeprom values or report settings
  if (rf12_recvDone() && rf12_crc == 0 && rf12_len == sizeof (eeprom_command_t)) {
    change_eeprom=*(eeprom_command_t*) rf12_data;
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
  
  if ( EeepromMetro.check() ) {
    do_save_Evalues=1;
    Ecounter=0;
  }
  
  if ( GeepromMetro.check() ) {
    do_save_Gvalues=1;
    Gcounter=0;
  }

  if ( WeepromMetro.check() ) {
    do_save_Wvalues=1;
    Wcounter=0;
  }
}

