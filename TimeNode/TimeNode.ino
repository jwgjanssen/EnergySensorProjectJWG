#include "DCF77Clock.h"
#include <JeeLib.h>
//#include <Metro.h>

DCF77Clock dcf(1, 4, false);  // (DCF77 module JeeNode DIO port, Blink-led JeeNode DIO Port, DCF77 signal inverted?)
struct Dcf77Time dt = { 0 };

//uint8_t curSec;
uint8_t curMin;

// structures for rf12 communication
typedef struct { char type;
                 long var1;
                 long var2;
} s_payload_t;  // Sensor data payload, size = 9 bytes
s_payload_t s_data;

//Metro sendMetro(15000);

void dumpTime(void)
{	
	Serial.println("DCF77 Time");
	// Print date
	Serial.print(" ");
	if(dt.day < 10)
		Serial.print("0");
	Serial.print(dt.day, DEC);
	Serial.print(".");
	if(dt.month < 10)
		Serial.print("0");
	Serial.print(dt.month, DEC);
	Serial.print(".");
	if(dt.year == 0) {
		Serial.print("000");
	} else	{
		Serial.print("20");
	}	
	Serial.print(dt.year, DEC);
	if(dcf.synced()) {
		Serial.println(" ");
		Serial.print(" ");
	} else {
		Serial.println(" ");
		Serial.print("~");
	}
	// Print Time 
	if (dt.hour < 10) 
		Serial.print("0");
	Serial.print(dt.hour, DEC);
	Serial.print(":");
	if (dt.min < 10) 
		Serial.print("0");
	Serial.print(dt.min, DEC);
	Serial.print(":");
	if (dt.sec < 10) 
		Serial.print("0");
	Serial.println(dt.sec, DEC);
	Serial.println(" ");
}

void sendTime() {
  s_data.type='t';
  s_data.var1=dt.hour;
  s_data.var2=dt.min;
  rf12_sendNow(0, &s_data, sizeof s_data);
}

void init_rf12 () {
  rf12_initialize(6, RF12_868MHZ, 5); // 868 Mhz, net group 5, node 6
}

void setup(void) 
{
  Serial.begin(57600);
  Serial.println("[DCF77 using interrupts]");
  init_rf12();
  dcf.init();
}


void loop(void) 
{
  dcf.getTime(dt);
          
  //if(dt.sec != curSec) {
  //	dumpTime();
  //}
  //curSec = dt.sec;

  if(dt.min != curMin) {
    //dumpTime();
    sendTime();
  }
  curMin = dt.min;

  /*if (sendMetro.check()) {
    dcf.getTime(dt);
    dumpTime();
    sendTime();
  }*/
}
