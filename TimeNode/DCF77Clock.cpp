/**
* DCF77Clock.cpp - Library for interacting with DCF77 radio clock modules.
* 
* Based on the Arduino DCF77 decoder v0.2 by Mathias Dalheimer (md@gonium.net).
* Adapted by Andreas Tacke (at@mail.fiendie.net).
* Adapted by Jos Janssen (jwg.janssen@gmail.com).
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "DCF77Clock.h"

#define DCF_split_millis 140           // Number of milliseconds before we assume a logic 1
#define DCF_sync_millis 1200           // No signal at second 59

static uint8_t m_dcf77JNport;
static uint8_t m_dcf77Pin;
static uint8_t m_blinkPin;
static boolean m_invertedSignal;

// DCF time format struct (uses struct bit field declarations)
struct DCF77Buffer  {
  unsigned long long prefix	:21;
  unsigned long long Min		:7;	// minutes
  unsigned long long P1		:1;	// parity minutes
  unsigned long long Hour		:6;	// hours
  unsigned long long P2		:1;	// parity hours
  unsigned long long Day		:6;	// day
  unsigned long long Weekday	:3;	// day of week
  unsigned long long Month	:5;	// month
  unsigned long long Year		:8;	// year (5 -> 2005)
  unsigned long long P3		:1;	// parity
};

// Parity struct  (uses struct bit field declarations)
struct {
  uint8_t parity_flag	:1;
  uint8_t parity_min	:1;
  uint8_t parity_hour	:1;
  uint8_t parity_date	:1;
} flags;

// Clock variables 
volatile uint8_t DCFSignalState = 0;  
uint8_t previousSignalState;
int previousFlankTime;
int bufferPosition;
unsigned long long dcf_rx_buffer;

// Time variables
volatile uint8_t ss;
volatile uint8_t mm;
volatile uint8_t hh;
volatile uint8_t day;
volatile uint8_t mon;
volatile uint8_t year;

uint8_t previousSecond;
unsigned long currentSync = 0;


/**
* Initialize the variables and configure the interrupt behaviour.
*/
void DCF77Clock::init() {
  previousSignalState = 0;
  previousFlankTime = 0;
  bufferPosition = 0;
  dcf_rx_buffer = 0;
  ss = mm = hh =day = mon = year = 0;
  
  pinMode(m_dcf77Pin, INPUT);
  if ( m_blinkPin != 0 ) {
    pinMode(m_blinkPin, OUTPUT);
  }  

  // Set interrupt on the DCF77 JeeNode Port
  PCICR |= (1<<PCIE2);              // turn interrupt on
  switch(m_dcf77JNport) {
  case 1 : {
      PCMSK2 |= (1<<PCINT20);       //   on JeeNode Port 1 DIO
      break;
    }
  case 2 : {
      PCMSK2 |= (1<<PCINT21);       //   on JeeNode Port 2 DIO
      break;
    }
  case 3 : {
      PCMSK2 |= (1<<PCINT22);       //   on JeeNode Port 3 DIO
      break;
    }
  case 4 : {
      PCMSK2 |= (1<<PCINT23);       //   on JeeNode Port 4 DIO
      break;
    }
  }
  MCUCR = (1<<ISC00) | (1<<ISC01);  //   on any change
}


/**
* Constructor
*/
DCF77Clock::DCF77Clock(uint8_t dcf77JNport, uint8_t blinkJNport, boolean invertedSignal) {
  m_dcf77JNport = dcf77JNport;
  m_dcf77Pin = dcf77JNport + 3;  // Arduino DIO pin = JeeNode portnumber + 3
  if ( blinkJNport == 0 ) {
    m_blinkPin = 0;
  } else {
    m_blinkPin = blinkJNport + 3;  // Arduino DIO pin = JeeNode portnumber + 3
  }
  m_invertedSignal = invertedSignal;
}


/**
* Evaluates the information stored in the buffer. This is where the DCF77
* signal is decoded and the internal clock is updated.
*/
void finalizeBuffer(void) {
  if (bufferPosition == 59) {
    struct DCF77Buffer *rx_buffer;
    rx_buffer = (struct DCF77Buffer *)(unsigned long long)&dcf_rx_buffer;
    if (flags.parity_min == rx_buffer->P1 && flags.parity_hour == rx_buffer->P2 && flags.parity_date == rx_buffer->P3) { 
      // Convert the received bits from BCD to decimal
      mm   = rx_buffer->Min-((rx_buffer->Min/16)*6);
      hh   = rx_buffer->Hour-((rx_buffer->Hour/16)*6);
      day  = rx_buffer->Day-((rx_buffer->Day/16)*6);
      mon  = rx_buffer->Month-((rx_buffer->Month/16)*6);
      year = rx_buffer->Year-((rx_buffer->Year/16)*6);
      //Serial.print("F");
    }
  } 
  ss = 0;
  bufferPosition = 0;
  dcf_rx_buffer=0;
  currentSync = millis();
}


/* *
* Append a signal to the dcf_rx_buffer. Argument can be 1 or 0. An internal
* counter shifts the writing position within the buffer. If position > 59,
* a new minute begins -> time to call finalizeBuffer(). 
*/
void appendSignal(uint8_t signal) {
  dcf_rx_buffer = dcf_rx_buffer | ((unsigned long long) signal << bufferPosition);  
  // Update the parity bits. First: Reset when minute, hour or date starts.
  if (bufferPosition ==  21 || bufferPosition ==  29 || bufferPosition ==  36) { flags.parity_flag = 0; }
  // Save the parity when the corresponding segment ends
  if (bufferPosition ==  28) { flags.parity_min  = flags.parity_flag; };
  if (bufferPosition ==  35) { flags.parity_hour = flags.parity_flag; };
  if (bufferPosition ==  58) { flags.parity_date = flags.parity_flag; };
  // When we received a 1, toggle the parity flag
  if (signal == 1) { flags.parity_flag = flags.parity_flag ^ 1; }
  bufferPosition++;
  if (bufferPosition > 59) { finalizeBuffer(); }
}


/**
* Evaluates the signal as it is received. 
*/
void scanSignal(void) {
  if (DCFSignalState == 1) {
    int thisFlankTime = millis();
    if (thisFlankTime - previousFlankTime > DCF_sync_millis) {
      finalizeBuffer();
    } else if (thisFlankTime - previousFlankTime < 300) {
      bufferPosition--;
      if (bufferPosition < 0) bufferPosition = 0;
    }
    if (thisFlankTime - previousFlankTime > 300) {
      previousFlankTime = thisFlankTime;
    }
  } else {
    int difference = millis() - previousFlankTime;
    if (difference < DCF_split_millis) {
      appendSignal(0);
    } else {
      appendSignal(1);
    }
  }
}


/**
* Interrupt handler. Called when the signal on interrupt pin changes. 
*/
ISR(PCINT2_vect) {
  Serial.print("r.");
  uint8_t signalState = digitalRead(m_dcf77Pin);
  DCFSignalState = (!m_invertedSignal) ? signalState : !signalState;
  if (DCFSignalState != previousSignalState) {
    scanSignal();
    if (DCFSignalState) {
      digitalWrite(m_blinkPin, HIGH);
      Serial.print("H");
    } else {
      digitalWrite(m_blinkPin, LOW);
      Serial.println("L");
    }
    previousSignalState = DCFSignalState;
  }
}


/**
* Fills the given struct with the current time signature.
*/
void DCF77Clock::getTime(Dcf77Time& dt) 
{
  //if (ss != previousSecond) {	
  dt.sec = ss;
  if(dt.min != mm) dt.min = mm;
  if(dt.hour != hh) dt.hour = hh;
  if(dt.day != day) dt.day = day;
  if(dt.month != mon) dt.month = mon;
  if(dt.year != year) dt.year = year;
  //  previousSecond = ss;
  //}
}


/**
* Does a plausibility check of the current time signature.
* Returns 0 if the last sync is older than two minutes or 
* if there hasn't been a successful sync yet.
*/
uint8_t DCF77Clock::synced() {
  if(day == 0 || mon == 0) return 0;
  else if(millis() - currentSync > (120 * 1000)) return 0;
  else return 1;
}
