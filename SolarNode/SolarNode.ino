// SolarNode
// ----------
// Node connected to a Mastervolt Soladin 600 inverter for the solar panels.
// Node address: 868 Mhz, net group 5, node 5.
// Local sensors: - serial connection to communicationsport of the Mastervolt Soladin 600
// Receives:      - 
// Sends:         - (s) actual & total daily production, and inverter runtime (to CentralNode)
// Other:         - 64x128 graphic LCD (to display solar inverter data)
//
// Author: Jos Janssen
// Modifications:
// Date:        Who:	Added:
// 01apr2013    Jos	  first version
// 19may2013    Jos	  Changed sample and send intervals and delete unused code
// 02oct2013    Jos   Using rf12_sendNow in stead of rf12_easySend & rf12_easyPoll. Removed rf12ResetMetro code
// 30oct2013    Jos   Solved display backlight bug (stayed at level 75 all the time)
// 20oct2014    Jos   Added code to keep correct counters for Daily Operating Time and Gridoutput of the Soladin
//                      after being non-responsive during the day due to bad weather or solar eclipse.
// 24jun2016    Jos   Solved bug in sending solar data to Central Node (Actual data was sent, in stead of corrected data)

#include <JeeLib.h>
#include <StopWatch.h>
#include <Metro.h>
#include <Soladin_uart.h>
#include <GLCD_ST7565.h>
#include "utility/font_4x6.h"
#include "utility/font_clR5x8.h"

#define DEBUG 0
#define EXIT_SUCCESS 1
#define EXIT_FAILURE 0

// Crash protection: Jeenode resets itself after x seconds of none activity (set in WDTO_xS)
const int UNO = 1;    // Set to 0 if your not using the UNO bootloader (i.e using Duemilanove)
#include <avr/wdt.h>  // All Jeenodes have the UNO bootloader
ISR(WDT_vect) { Sleepy::watchdogEvent(); }

// **** START of var declarations ****

// what is connected to which port on JeeNode
//
// JeeNode Port 1+4: GLCD display 128x64
GLCD_ST7565 glcd;

// JeeNode Port 2: Uart Plug
PortI2C i2cBus (2);
UartPlug uart (i2cBus, 0x4D);

// JeeNode Port 3: LDR for light measurement (analog)
//Port LDRport (3);
//int LDR, LDRbacklight;

// structures for rf12 communication
typedef struct { char type;
        	 long var1;
		 long var2;
		 long var3;
} s_payload_t;  // Sensor data payload, size = 13 bytes
s_payload_t s_data;

// timers
Metro sampleMetro = Metro(10000);        // Sample every 10 sec
Metro sendMetro = Metro(60000);          // send solar data every 1 min
Metro wdtMetro = Metro(1000);            // reset watchdog timer every 1 sec
//Metro LDRMetro = Metro(1000);            // sample LDR every 1 sec
StopWatch susp_secs(StopWatch::SECONDS); // stopwatch to measure time since start suspended state

// vars for Soladin data
Soladin sol;                             // copy of soladin class
uint8_t  DailyOpTm,  DailyOpTm_bu;       // vars for handling Daily Operating Time before and after SUSPENDED state
uint16_t Gridoutput, Gridoutput_bu;      // vars for handling Gridoutput before and after SUSPENDED state

// vars for status of Soladin
#define SLEEPING 0
#define AWAKE 1
#define SUSPENDED 2
int inverter_state; // Possible values: 
                    // SLEEPING (0) : Soladin does not respond to commands
                    // AWAKE (1)    : Soladin responds to commands
                    // SUSPENDED (2): The first 3 hours of Soladin not responding to commands, is seen as SUSPENDED
                    //                state in stead of SLEEPING. This state is needed to handle the Soladin
                    //                being non-responsive during the day due to bad weather or solar eclipse.
char pr_value[12];

// counter variables
int i_nosleep = 0;  // Count good Soladin readings

// **** END of var declarations ****


void SDisplayTitles() {
  glcd.drawLine(21, 6, 42, 6, WHITE);
  glcd.drawLine(82, 6, 116, 6, WHITE);
  glcd.drawLine(3, 46, 25, 46, WHITE);
  glcd.drawLine(42, 46, 64, 46, WHITE);

  // Write all the titles for the display
  glcd.setFont(font_4x6);
  // Power part
  glcd.drawString_P(23, 0, PSTR("Power"));
  glcd.drawString_P(0, 10, PSTR("Actual"));
  glcd.drawString_P(0, 20, PSTR("Today"));
  glcd.drawString_P(0, 30, PSTR("Total"));
  // Inverter part
  glcd.drawString_P(84, 0, PSTR("Inverter"));
  glcd.drawString_P(76, 10, PSTR("Hours:"));
  glcd.drawString_P(76, 18, PSTR("Today"));
  glcd.drawString_P(76, 26, PSTR("Total"));
  glcd.drawString_P(76, 36, PSTR("Temp"));
  glcd.drawString_P(76, 44, PSTR("Eff"));
  glcd.drawString_P(76, 52, PSTR("Error"));
  switch (inverter_state) {
      case SLEEPING:  glcd.drawString_P(76, 59, PSTR("State: sleep")); break;
      case SUSPENDED: glcd.drawString_P(76, 59, PSTR("State: susp")); break;
      case AWAKE:     glcd.drawString_P(76, 59, PSTR("State: awake")); break;
  }
  // Solar part
  glcd.drawString_P(5, 40, PSTR("Solar"));
  // Mains part
  glcd.drawString_P(44, 40, PSTR("Mains"));
}

void SDisplayReadings() {
  int eff;
  glcd.clear();
  glcd.backLight(150);
  SDisplayTitles();
  // Write all the values for the display
  glcd.setFont(font_clR5x8);
  // Power part
  sprintf(pr_value, "%3d W", sol.Gridpower);
  glcd.drawString(25, 9, pr_value);
  sprintf(pr_value, "%2d.%02d kWh", Gridoutput/100, abs(Gridoutput%100));
  glcd.drawString(25, 19, pr_value);
  sprintf(pr_value, "%5d kWh", sol.Totalpower/100);
  glcd.drawString(25, 29, pr_value);
  // Inverter part
  sprintf(pr_value, "%02d:%02d", (DailyOpTm*5)/60, ((DailyOpTm*5)%60));
  glcd.drawString(97, 17, pr_value);
  sprintf(pr_value, "%06d", sol.TotalOperaTime/60);
  glcd.drawString(97, 25, pr_value);
  sprintf(pr_value, "%2d C", sol.DeviceTemp);
  glcd.drawString(97, 35, pr_value);
  glcd.drawCircle(109 ,35, 1, WHITE); // degree sign
  eff=(sol.Gridpower * 1000000) / sol.PVvolt / sol.PVamp; sprintf(pr_value, "%2d.%1d%%", eff/10, abs(eff%10));
  glcd.drawString(97, 43, pr_value);
  sprintf(pr_value, "0x%04x", sol.Flag);
  glcd.drawString(97, 51, pr_value);
  // Solar part
  sprintf(pr_value, "%2d.%02dV", sol.PVvolt/10, abs(sol.PVvolt%10));
  glcd.drawString(0, 49, pr_value);
  sprintf(pr_value, "%2d.%02dA", sol.PVamp/100, abs(sol.PVamp%100));
  glcd.drawString(0, 57, pr_value);
  // Mains part
  sprintf(pr_value, "%3dV", sol.Gridvolt);
  glcd.drawString(44, 49, pr_value);
  sprintf(pr_value, "%2d.%02dHz", sol.Gridfreq/100, abs(sol.Gridfreq%100));
  glcd.drawString(34, 57, pr_value);

  glcd.refresh();
}


void SDisplaySleep() {
  glcd.clear();
  glcd.backLight(75);
  SDisplayTitles();
  // Write all the values for the display
  glcd.setFont(font_clR5x8);
  // Power part
  glcd.drawString_P(25, 9, PSTR("--- W"));
  sprintf(pr_value, "%2d.%02d kWh", Gridoutput/100, abs(Gridoutput%100));
  glcd.drawString(25, 19, pr_value); //glcd.drawString_P(25, 19, PSTR("--.-- kWh"));
  sprintf(pr_value, "%5d kWh", sol.Totalpower/100);
  glcd.drawString(25, 29, pr_value); //glcd.drawString_P(25, 29, PSTR("----- kWh"));
  // Inverter part
  sprintf(pr_value, "%02d:%02d", (DailyOpTm*5)/60, ((DailyOpTm*5)%60));
  glcd.drawString(97, 17, pr_value); //glcd.drawString_P(97, 17, PSTR("--:--"));
  sprintf(pr_value, "%06d", sol.TotalOperaTime/60);
  glcd.drawString(97, 25, pr_value); //glcd.drawString_P(97, 25, PSTR("------"));
  glcd.drawString_P(97, 35, PSTR("-- C"));
  glcd.drawCircle(109 ,35, 1, WHITE); // degree sign
  glcd.drawString_P(97, 43, PSTR("--.-%"));
  glcd.drawString_P(97, 51, PSTR("------"));
  // Solar part
  glcd.drawString_P(0, 49, PSTR("--.--V"));
  glcd.drawString_P(0, 57, PSTR("--.--A"));
  // Mains part
  glcd.drawString_P(44, 49, PSTR("--- V"));
  glcd.drawString_P(34, 57, PSTR("--.-- Hz"));
  glcd.refresh();
}


void sendSolar() {
  s_data.type='s';
  s_data.var1=sol.Gridpower;     // = Actual production in W
  s_data.var2=Gridoutput*10;     // = kWh today * 1000
  s_data.var3=DailyOpTm*5;       // = running time today in minutes
  rf12_sendNow(0, &s_data, sizeof s_data);
}


int GetDeviceReadings() {
  int rc;
  
  // Get Soladin values for: Flag, PVvolt, PVamp, Gridfreq, Gridvolt, Gridpower
  //                         Totalpower, DeviceTemp, TotalOperaTime
  for (int i=0 ; i < 3 ; i++) {
      if (sol.query(DVS)) {           // request Device status
          rc = EXIT_SUCCESS;
          break;
      } else {
          rc = EXIT_FAILURE;
      }
      delay(500);
  }
  
  // Get Soladin values for: DailyOpTm, Gridoutput
  for (int i=0 ; i < 3 ; i++) {
      if (sol.query(HSD,0)) {         // request today's power and running time
          DailyOpTm = sol.DailyOpTm + DailyOpTm_bu;
          Gridoutput = sol.Gridoutput + Gridoutput_bu;
          rc = EXIT_SUCCESS;
          break;
      } else {
          rc = EXIT_FAILURE;
      }
      delay(500);
  }
  return rc;
}


void init_rf12() {
    rf12_initialize(5, RF12_868MHZ, 5); // 868 Mhz, net group 5, node 5
}


void setup() {
    #if DEBUG
      Serial.begin(57600);
      Serial.println("\n[Monitoring a Mastervolt Soladin 600 inverter]");
    #endif
    init_rf12();
    uart.begin(9600);
    sol.begin(&uart);
    if (UNO) wdt_enable(WDTO_8S);  // set timeout to 8 seconds
    glcd.begin();  // set contast between 0x15 and 0x1a
    DailyOpTm_bu = 0;
    Gridoutput_bu = 0;
    if ( GetDeviceReadings() ) {
        inverter_state = AWAKE;
        SDisplayReadings();
        sendSolar();
    } else {
        inverter_state = SLEEPING;
        SDisplaySleep();
    }
}

  
void loop() {
    // take a reading from the Soladin and display corresponding data
    if ( sampleMetro.check() ) {
        if ( GetDeviceReadings() ) {
            if ( inverter_state == SUSPENDED ) {
                susp_secs.reset();  // reset stopwatch to 0
            }
            inverter_state = AWAKE;
            SDisplayReadings();
        } else {
            if ( inverter_state == AWAKE ) {
                // save last valid readings for DailyOpTm and Gridoutput
                DailyOpTm_bu = DailyOpTm;
                Gridoutput_bu = Gridoutput;

                susp_secs.reset();  // reset stopwatch to 0
                susp_secs.start();  // start stopwatch
                inverter_state = SUSPENDED;
            }
            
            SDisplaySleep();
            
            // set inverter state to SLEEPING after 3 hours (10800 secs) of SUSPENDED state
            if ( (susp_secs.state() == StopWatch::RUNNING) && (susp_secs.elapsed() > 10800) ) {
                susp_secs.reset();  // reset stopwatch to 0
                inverter_state = SLEEPING;
                // reset backup vars
                DailyOpTm_bu = 0;
                Gridoutput_bu = 0;
            }
        }
    }
        
    // send solar data to Central Node 
    if ( sendMetro.check() ) {
        if (inverter_state == AWAKE) {
            sendSolar();
        }
    }

    /*if ( LDRMetro.check() ) {
        LDR=LDRport.anaRead();				// Read LDR value for light level in the room
        LDRbacklight=map(LDR,0,400,25,255);     	// Map LDR data to GLCD brightness
        LDRbacklight=constrain(LDRbacklight,0,255);	// constrain value to 0-255
        #if DEBUG
          Serial.print("LDR = "); Serial.print(LDR);
          Serial.print("   LDRbacklight = "); Serial.println(LDRbacklight);
        #endif
        glcd.backLight(LDRbacklight);
    }*/
 
    if ( wdtMetro.check() ) {
      if (UNO) wdt_reset();
    }
}

