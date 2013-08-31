// CentralNode
// -----------
// Node connected to the computer via USB.
// Node address: 868 Mhz, net group 5, node 30.
// Local sensors: - outside temperature (DS18B20)
//                - barometric pressure (BMP085)
//                - DCF77 time module
// Receives:      - (e) electricity readings (from SensorNode)
//                - (g) gas readings (from SensorNode)
//                - (i) inside temperature (from GLCDNode)
//                - (s) solar readings (from SolarNode)
//                - (x) current eeprom sensor trigger values (from SensorNode)
//                - (y) adjusted gas sensor trigger values (from SensorNode)
//                - (z) adjusted electricity sensor trigger values (from SensorNode)
// Sends:         - (1) electricity actual usage (to GLCDNode)
//                - (2) solar actual production (to GLCDNode)
//                - (4) outside temperature (to GLCDNode)
//                - (5) outside pressure (to GLCDNode)
//                - local time with every packet send (to GLCDNode)
//                - all values to USB for webpage
//                - all values to xively.com via Ethercard
// Other:         - 2x16 LCD display (to display electricity usage & outside temperature)
//                - Uses an Ethercard to send readings to xively.com
//		  - commands to get/set eeprom sensor settings of the SensorNode:
//			(NOTE: get/set is possible via the serial interface of the Arduino IDE)
//			gtst,.		get/display all the sensor min and max settings
//			emnl,<value>.	set minimum trigger of left electricity sensor to <value>
//			emxl,<value>.	set maximum trigger of left electricity sensor to <value>
//			emnr,<value>.	set minimum trigger of right electricity sensor to <value>
//			emxr,<value>.	set maximum trigger of right electricity sensor to <value>
//			gmin,<value>.	set minimum trigger of gas sensor to <value>
//			gmax,<value>.	set maximum trigger of gas sensor to <value>
//			wmin,<value>.	set minimum trigger of water sensor to <value> (Not used yet)
//			wmax,<value>.	set maximum trigger of water sensor to <value> (Not used yet)
//				accepted values for <value> = 1 -> 1023
//
// Author: Jos Janssen
// Modifications:
// Date:        Who:   	Added:
// 20jan2012    Jos    	Send command for reading/writing eeprom of sensor node.
// 21jan2012    Jos    	Temporarily disabled local DS18B20 temperature sensor
//                        because of possible EMI interference on USB (disconnects).
// 15feb2012    Jos    	Enabled local DS18B20 temperature sensor again.
// 02jul2012    Jos    	Added sending outside temp & pressure via rf12
// 31aug2012	Jos	Added description of eeprom commands
// 08sep2012    Jos     Included the code for sending data to cosm.com via ethercard
// 09sep2012    Jos     Included code for 8 second watchdog reset
// 02oct2012    Jos     Added Inside temperature to local LCD (changed layout for this)
// 28oct2012	Jos	Added sending gas data to cosm.com
// 15feb2013    Jos     Added code to re-initialze the rf12 every one week to avoid hangup after a long time
// 04mar2013    Jos     Added code to receive adjusted electricity and gas sensor trigger values
// 04mar2013    Jos     Used showString(PSTR("...")) for several long strings to save RAM space
// 06mar2013    Jos     Changed the way data is send to GLCDNode
// 06mar2013    Jos     Using PSTR("...") in stash.print does not work
// 08mar2013    Jos     Added code for getting date & time from api.cosm.com
// 08mar2013    Jos     Added hours & mins to struct for sending time to GLCDNode
// 15mar2013    Jos     Added wait-loop in getting time from internet using ether.browseUrl()
// 19mar2013    Jos     Optimized communication structures for rf12
// 29mar2013    Jos     Removed code for getting date & time from api.cosm.com (did not work reliably)
// 30mar2013    Jos     Added code for getting date & time from DCF77 time module
// 15may2013    Jos     cosm.com was replaced by xively.com


#define DEBUG 0        // Set to 1 to activate debug code
#define UNO 1          // Set to 0 if your not using the UNO bootloader (i.e using Duemilanove)

#include <JeeLib.h>
#include <EtherCard.h>
#include <Metro.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PortsBMP085.h>
#include <PortsLCD.h>
#include <Wire.h>
#include "DCF77Clock.h"
//#include <RTClib.h>
#include "cosm_settings.h"  // contains xively website, FEEDID & APIKEY :
			    //   char website[] PROGMEM = "api.xively.com";
			    //   #define FEEDID "your-own-id"
			    //   #define APIKEY "your-own-key"

// Crash protection: Jeenode resets itself after x seconds of none activity (set in WDTO_xS)
#define UNO 1          // Set to 0 if your not using the UNO bootloader (i.e using Duemilanove)
#if UNO
#include <avr/wdt.h>  // All Jeenodes have the UNO bootloader
ISR(WDT_vect) { 
  Sleepy::watchdogEvent(); 
}
#endif

// **** START of var declarations ****

// **** Ethercard (does not use a JeeNode Port)
// ethernet interface mac address, must be unique on the LAN
static byte mymac[] = { 0x58,0x17,0xAD,0x32,0x0A,0x00 };
byte Ethernet::buffer[375]; // 400 works! 650 & 700 give strange effects (serial does not seem to work....)
Stash stash;

// **** JeeNode Port 1: 2x16 LCD
PortI2C myI2C (1);
LiquidCrystalI2C lcd (myI2C);

// **** JeeNode Port 2: DCF77 module
DCF77Clock dcf(2, 0, false);  // (DCF77 module JeeNode DIO port, Blink-led JeeNode DIO Port (0=no blink-led), DCF77 signal inverted?)

// *****JeeNode Port 3: BMP085 temperature & pressure
PortI2C three (3);
BMP085 psensor (three, 3); // ultra high resolution
int16_t bmptemp;
int32_t bmppres;

// **** JeeNode Port 4: DS18B20 temperature outside
// Data wire is plugged into pin 7 on the Arduino (= Port 4 DIO on JeeNode)
#define ONE_WIRE_BUS 7
// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// timers
Metro rf12ResetMetro = Metro(604800000); // re-init rf12 every 1 week
Metro sendMetro = Metro(30500);          // send data to GLCDNode every 30.5 sec
Metro sampleMetro = Metro(300000);       // sample temperature & pressure every 5 min
Metro xivelyMetro = Metro(60000);        // send data to xively every 60 sec
Metro wdtMetro = Metro(1000);            // watchdog timer reset every 1 sec
Metro dcf77Metro = Metro(500);           // read time every 500ms

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
} x_payload_t;  // Status data payload, size = 17 bytes
x_payload_t x_data;

typedef struct {  byte type;
                  int value;
                  byte hours, mins;
} d_payload_t;  // Display data payload, size = 5 bytes
d_payload_t d_data;

typedef struct { char command[5];
                 int value;
} eeprom_command_t;  // EEprom command payload, size = 7 bytes
eeprom_command_t change_eeprom;
       
// vars for measurement
int watt = 0;
int gas = 0;
int itemp = 0;
int otemp = 0;
int opres = 0;
int swatt = 0;

// vars for displaying on local LCD
char lcd_temp[10];

// vars for reporting to xively
int watt_set = 0;
int gas_set = 1;	// Always report gas to show when no gas is used
int gas_send = 0;	// Is gas send to xively ?
int itemp_set = 0;
int otemp_set = 0;
int opres_set = 0;
int swatt_set = 0;
float itemp_float;
float otemp_float;
float opres_float;

// vars for handling eeprom commands
char cmdbuf[12] = "";
char valbuf[5] = "";
int cmdOK = 0;

// vars for DCF77 time measurement
struct Dcf77Time dt = { 0 };
uint8_t curMin;

// **** END of var declarations ****

/*
int freeRam () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}
*/

void showString (PGM_P s) {
  char c;
  
  while ((c = pgm_read_byte(s++)) != 0)
    Serial.print(c);
}

 
void showStringln (PGM_P s) {
  char c;
  
  while ((c = pgm_read_byte(s++)) != 0)
    Serial.print(c);
  Serial.println();
}


void send_eeprom_update() {
    showString(PSTR("Sending: "));
    showString(PSTR("Command= "));
    Serial.print(change_eeprom.command);
    showString(PSTR(", Value= "));
    Serial.println(change_eeprom.value);
    rf12_easySend(&change_eeprom, sizeof change_eeprom);
    rf12_easyPoll(); // Actually send the data
}


void handleInput () {
    int c;
    int sensorvalue;
  
    if (Serial.available() > 0) {
        c = Serial.read();
        if (isAlpha(c)) {
            strncat(cmdbuf, (char *)&c, 1);
        }
        if (c==',') {
            if (strcmp(cmdbuf, "emnl") == 0 || strcmp(cmdbuf, "emxl") == 0 ||
                strcmp(cmdbuf, "emnr") == 0 || strcmp(cmdbuf, "emxr") == 0 ||
                strcmp(cmdbuf, "gmin") == 0 || strcmp(cmdbuf, "gmax") == 0 ||
                strcmp(cmdbuf, "wmin") == 0 || strcmp(cmdbuf, "wmax") == 0) {
                sprintf(change_eeprom.command, cmdbuf);
                cmdOK=1;
            } else if (strcmp(cmdbuf, "gtst") == 0) {
                       sprintf(change_eeprom.command, cmdbuf);
                       change_eeprom.value=0;
                       send_eeprom_update();
                       sprintf(cmdbuf, ""); sprintf(valbuf, ""); cmdOK=0;
                   } else {
                       showString(PSTR("\nIllegal command: "));
                       Serial.println(cmdbuf);
                       sprintf(cmdbuf, "");
                       cmdOK=0;
                   }
        }
        if (isDigit(c) && cmdOK) {
            strncat(valbuf, (char *)&c, 1);
        }
        if (c=='.' && cmdOK) {
            sensorvalue = atoi(valbuf);
            if (sensorvalue < 1 || sensorvalue > 1023) {
                showString(PSTR("\nIllegal sensor value: "));
                Serial.println(sensorvalue);
            } else {
                change_eeprom.value=sensorvalue;
                send_eeprom_update();
            }
            sprintf(cmdbuf, ""); sprintf(valbuf, ""); cmdOK=0;
        }
    }
}


void readTempPres() {
  sensors.requestTemperatures(); // Send the command to get temperatures
  otemp=(int) (10*sensors.getTempCByIndex(0));
  otemp_float=(float)otemp/10;
  otemp_set = 1;
  showString(PSTR("o "));
  Serial.print(otemp);
  showStringln(PSTR(" ")); // extra space at the end is needed
        
  psensor.measure(BMP085::TEMP);
  psensor.measure(BMP085::PRES);
  psensor.calculate(bmptemp, bmppres);
  opres=(int) (bmppres/10);
  opres_float=(float)opres/10;
  opres_set = 1;
  showString(PSTR("p "));
  Serial.print(opres);
  showStringln(PSTR(" ")); // extra space at the end is needed
}


void sendTime() {
  s_data.type='t';
  s_data.var1=dt.hour;
  s_data.var2=dt.min;
  Serial.print("t ");
  Serial.print(dt.hour); Serial.print(":"); if (dt.min < 10) Serial.print("0"); Serial.print(dt.min); Serial.print("  ");
  Serial.print(dt.day); Serial.print("-"); Serial.print(dt.month); Serial.print("-20"); Serial.print(dt.year);
  Serial.println();
  //rf12_easySend(&s_data, sizeof s_data);
  //rf12_easyPoll(); // Actually send the data
}


void init_rf12 () {
    rf12_initialize(30, RF12_868MHZ, 5); // 868 Mhz, net group 5, node 30
    rf12_easyInit(0); // Send interval = 0 sec (=immediate).
}


void setup () {
    Serial.begin(57600);
    showStringln(PSTR("\n[START recv]"));
    init_rf12();
    if (ether.begin(sizeof Ethernet::buffer, mymac) == 0) {
        showStringln(PSTR("Failed to access Ethernet controller"));
        while (1);
    }
    #if DEBUG
      showStringln(PSTR("begin DONE"));
    #endif
    if (!ether.dhcpSetup()) {
        showStringln(PSTR("DHCP failed"));
        while (1);
    }
    #if DEBUG
      showStringln(PSTR("DHCP DONE"));
    #endif
    ether.printIp("IP:  ", ether.myip);
    ether.printIp("GW:  ", ether.gwip);
    ether.printIp("DNS: ", ether.dnsip); 
    if (!ether.dnsLookup(website))
        showStringln(PSTR("DNS failed"));
    ether.printIp("SRV: ", ether.hisip);
    // INIT port 1
    lcd.begin(16, 2);       // LCD is 2x16 chars
    lcd.backlight();        // Turn on LCD backlight
    // INIT port 3
    psensor.getCalibData(); // Get BMP085 calibration data
    // INIT port 4
    sensors.begin();        // DS18B20 default precision 12 bit.
    dcf.init();

    #if UNO
      wdt_enable(WDTO_8S);  // set timeout to 8 seconds
    #endif
    readTempPres();
}

void loop () {
    // re-initialize rf12 every week to avoid rf12 hangup after a long time
    if ( rf12ResetMetro.check() ) {
         init_rf12();
    }

    ether.packetLoop(ether.packetReceive());

    if (rf12_recvDone() && rf12_crc == 0) {
      if (rf12_len == sizeof (s_payload_t)) {
        s_data = *(s_payload_t*) rf12_data;
        switch (s_data.type)
	{
	    case 'e':   // Electricity data
                {
                  showString(PSTR("e "));
                  Serial.print(s_data.var1);
                  watt = (int)s_data.var1;
		  watt_set = 1;
                  showString(PSTR(" "));
                  Serial.print(s_data.var2);
                  break;
                }
	    case 'g':  // Gas data
                {
                  showString(PSTR("g "));
                  Serial.print(s_data.var1);
		  if ( gas_send == 1 ) {
                    gas_send = 0;
		    gas = 10;       // Start with 10L on first received g-line after sending
                  } else {
		    gas = gas + 10; // Every g-line received from sensornode = 10L gas used
                                    //   Is reset when send to xively
                  }
                  showString(PSTR(" "));
                  Serial.print(s_data.var2);
                 break;
                }
            case 'i':  // Inside temperature
                {
                  showString(PSTR("i "));
                  Serial.print(s_data.var1);
                  itemp=(int)s_data.var1;
                  itemp_float=(float)s_data.var1/10;
                  itemp_set = 1;
                  showString(PSTR(" "));
                  break;
                }
            case 's':  // Solar data
                {
                  showString(PSTR("s "));
                  Serial.print(s_data.var1);
                  swatt = (int)s_data.var1;
		  swatt_set = 1;
                  showString(PSTR(" "));
                  Serial.print(s_data.var2);
                  showString(PSTR(" "));
                  Serial.print(s_data.var3);
                  break;
                }
            /*case 't':  // Time data (disabled, sensor is local, so no data to receive from rf12)
                {
                  showString(PSTR("t "));
                  Serial.print(s_data.var1);showString(PSTR(":"));Serial.print(s_data.var2);
                  d_data.hours=(byte)s_data.var1; d_data.mins=(byte)s_data.var2;
                  showString(PSTR(" "));
                  break;
                }*/
            /* case 'o':  // Outside temperature (disabled, sensor is local, so no data to receive from rf12)
                {
                  showString(PSTR("o "));
                  Serial.print(s_data.var1);
                  otemp=(int)s_data.var1;
                  showString(PSTR(" "));
                  break;
                } */
            /* case 'p':  // Outside pressure (disabled, sensor is local, so no data to receive from rf12)
                {
                  showString(PSTR("p "));
                  Serial.print(s_data.var1);
                  opres=(int)s_data.var1;
                  showString(PSTR(" ")); // extra space at the end is needed
                  break;
                } */
	    default:
		// You can use the default case.
		showString(PSTR("Wrong measurement payload type!"));
                break;
	}
        if (RF12_WANTS_ACK) {
            rf12_sendStart(RF12_ACK_REPLY, 0, 0);
        }
        Serial.println("");
      } else if (rf12_len == sizeof (x_payload_t)) {
        x_data = *(x_payload_t*) rf12_data;
        switch (x_data.type)
	{
       	    case 'x':  // display sensor settings
                {
                  showString(PSTR("x "));
                  showString(PSTR("min-max: L:"));
                  Serial.print(x_data.minA);
                  showString(PSTR("->"));
                  Serial.print(x_data.maxA);
                  showString(PSTR(", R:"));
                  Serial.print(x_data.minB);
                  showString(PSTR("->"));
                  Serial.print(x_data.maxB);
                  showString(PSTR(", G:"));
                  Serial.print(x_data.minC);
                  showString(PSTR("->"));
                  Serial.print(x_data.maxC);
                  showString(PSTR(", W:"));
                  Serial.print(x_data.minD);
                  showString(PSTR("->"));
                  Serial.print(x_data.maxD);
                  break;
                }
       	    case 'y':  // display adjusted gas sensor trigger values
                {
                  showString(PSTR("y "));
                  showString(PSTR("Adjusted gas sensor trigger values: "));
                  Serial.print(x_data.minA);
                  showString(PSTR("->"));
                  Serial.print(x_data.maxA);
                  break;
                }
       	    case 'z':  // display adjusted gas sensor trigger values
                {
                  showString(PSTR("z "));
                  showString(PSTR("Adjusted electricity sensor trigger values: L:"));
                  Serial.print(x_data.minA);
                  showString(PSTR("->"));
                  Serial.print(x_data.maxA);
                  showString(PSTR(" R:"));
                  Serial.print(x_data.minB);
                  showString(PSTR("->"));
                  Serial.print(x_data.maxB);
                  break;
                }
	    default:
		// You can use the default case.
		showString(PSTR("Wrong status payload type!"));
                break;
	}
        if (RF12_WANTS_ACK) {
            rf12_sendStart(RF12_ACK_REPLY, 0, 0);
        }
        Serial.println("");
      }
    }


    // Send data for displaying on GLCD JeeNode & display selected data on local LCD
    if ( sendMetro.check() ) {
        // Send data to display on GLCD Jeenode
        d_data.type=1;  // display electricity data
        d_data.value=watt;
        rf12_easySend(&d_data, sizeof d_data);
        rf12_easyPoll(); // Actually send the data
        
        delay(100);
        d_data.type=2;  // display solar data
        d_data.value=swatt;
        rf12_easySend(&d_data, sizeof d_data);
        rf12_easyPoll(); // Actually send the data
        
	/* Do not send inside temp, is GLCDNode local
        delay(100);
        d_data.type=3;  // display inside temperature data
        d_data.value=itemp;
        rf12_easySend(&d_data, sizeof d_data);
        rf12_easyPoll(); // Actually send the data
	*/
        
        delay(100);
        d_data.type=4;  // display outside temperature data
        d_data.value=otemp;
        rf12_easySend(&d_data, sizeof d_data);
        rf12_easyPoll(); // Actually send the data
        
        delay(100);
        d_data.type=5;  // display outside pressure data
        d_data.value=opres;
        rf12_easySend(&d_data, sizeof d_data);
        rf12_easyPoll(); // Actually send the data
        
        // Display data on local LCD
        lcd.setCursor(8,0); lcd.print("Verbruik"); 
        lcd.setCursor(9,1); lcd.print(watt); lcd.print(" W ");

        if (otemp > -1 || otemp < -9) {
          sprintf(lcd_temp, "%3d,%d", otemp/10, abs(otemp%10));
        } else {
          sprintf(lcd_temp, "-%2d,%d", otemp/10, abs(otemp%10));
        }
        lcd.setCursor(0,0);
        lcd.print("<"); lcd.print(lcd_temp); lcd.print("C");
 
        if (itemp > -1 || itemp < -9) {
          sprintf(lcd_temp, "%3d,%d", itemp/10, abs(itemp%10));
        } else {
          sprintf(lcd_temp, "-%2d,%d", itemp/10, abs(itemp%10));
        }
        lcd.setCursor(0,1);
        lcd.print(">"); lcd.print(lcd_temp); lcd.print("C"); 
  }
    
    // Read outside temperature & pressure
    if ( sampleMetro.check() ) {
      readTempPres();
    }

    // Send data to xively
    if ( xivelyMetro.check() ) {
        // generate payload stash,
        // we can determine the size of the generated message ahead of time
        byte sd = stash.create();
        if(watt_set) {
            stash.print("Electricity,");stash.println(watt);
            #if DEBUG
            showString(PSTR("Send e  "));Serial.println(watt);
            #endif
            watt_set = 0;
        }
        if(gas_set) {
            stash.print("Gas,");stash.println(gas);
            #if DEBUG
            showString(PSTR("Send g  "));Serial.println(gas);
            #endif
            gas_send = 1;
	    gas = 0;
        }
        if(itemp_set) {
            stash.print("Inside,");stash.println(itemp_float);
            #if DEBUG
            showString(PSTR("Send i  "));Serial.println(itemp_float);
            #endif
            itemp_set = 0;
        }
        if(otemp_set) {
            stash.print("Outside,");stash.println(otemp_float);
            #if DEBUG
            showString(PSTR("Send o  "));Serial.println(otemp_float);
            #endif
            otemp_set = 0;
        }
        if(opres_set) {
            stash.print("Pressure,");stash.println(opres_float);
            #if DEBUG
            showString(PSTR("Send p  "));Serial.println(opres_float);
            #endif
            opres_set = 0;
        }
        if(swatt_set) {
            stash.print("SolarOutput,");stash.println(swatt);
            #if DEBUG
            showString(PSTR("Send s  "));Serial.println(swatt);
            #endif
            swatt_set = 0;
        }
        stash.save();
    
        // generate the header with payload - note that the stash size is used,
        // and that a "stash descriptor" is passed in as argument using "$H"
        Stash::prepare(PSTR("PUT http://$F/v2/feeds/$F.csv HTTP/1.0" "\r\n"
                            "Host: $F" "\r\n"
                            "X-PachubeApiKey: $F" "\r\n"
                            "Content-Length: $D" "\r\n"
                            "\r\n"
                            "$H"),
                website, PSTR(FEEDID), website, PSTR(APIKEY), stash.size(), sd);

        ether.tcpSend();  // send the packet - this also releases all stash buffers once done
        //Serial.println(freeRam());
    }
    
    if ( dcf77Metro.check() ) {
      dcf.getTime(dt);
      if(dt.min != curMin) {
        d_data.hours=dt.hour; d_data.mins=dt.min;
        sendTime();
      }
      curMin = dt.min;
    }
    
    if ( wdtMetro.check() ) {
        #if UNO
          wdt_reset();
        #endif
    }
    
    // read commands from serial input
    handleInput();
}
