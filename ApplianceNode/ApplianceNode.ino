// ApplianceNode
// -------------
// Node connected to a CT current sensor.
// Node address: 868 Mhz, net group 5, node 6.
// Local sensors: - CT current sensor (SCT013)
// Receives:      - 
// Sends:         - (a) appliance power readings (from ApplianceNode)
// Other:         - 
//
// Author: Jos Janssen
// Modifications:
// Date:        Who:    Added:
// 28nov2013    Jos     Initial version.
// 08oct2015    Jos     Changed Node address.
//
// EmonLibrary examples openenergymonitor.org, Licence GNU GPL V3

#include <JeeLib.h>
#include <Metro.h>
#include "EmonLib.h"                   // Include Emon Library

#define DEBUG 0

// Crash protection: Jeenode resets itself after x seconds of none activity (set in WDTO_xS)
const int UNO = 1;    // Set to 0 if your not using the UNO bootloader (i.e using Duemilanove)
#include <avr/wdt.h>  // All Jeenodes have the UNO bootloader
ISR(WDT_vect) { Sleepy::watchdogEvent(); }

// structures for rf12 communication
typedef struct { char type;
     long var1;
		 long var2;
		 long var3;
} s_payload_t;  // Sensor data payload, size = 13 bytes
s_payload_t s_data;

// timers
Metro sendMetro = Metro(30000);          // send solar data every 1 min

EnergyMonitor emon1;                   // Create an instance
double Irms;
long AppliancePower;

void sendAppliancePower() {
    s_data.type='a';
    s_data.var1=AppliancePower;     // = Actual production in W
    rf12_sendNow(0, &s_data, sizeof s_data);
}

void init_rf12 () {
    rf12_initialize(6, RF12_868MHZ, 5); // 868 Mhz, net group 5, node 6
}

void setup() {
    #if DEBUG
      Serial.begin(57600);
      Serial.println("\n[Monitoring a current sensor]");
    #endif
    init_rf12();
  
    emon1.current(14, 30);             // Current: input pin, calibration.
}

void loop()
{
  Irms = emon1.calcIrms(1480);  // Calculate Irms only
  AppliancePower = Irms * 230;
  #if DEBUG
    Serial.print(AppliancePower);      // Apparent power
    Serial.print(" ");
    Serial.println(Irms);	       // Irms
  #endif
  
  // send solar data to Central Node 
  if ( sendMetro.check() ) {
    sendAppliancePower();
  }
}
