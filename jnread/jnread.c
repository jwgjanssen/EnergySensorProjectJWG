/*
#################################################################################
# Program reads from USB input and builds webpage				#
# Interpret lines that start with:						#
# 	e: for electricity data							#
# 	g: for gas data								#
# 	i: for inside temperature data						#
# 	o: for outside temperature data						#
# 	p: for outside pressure data						#
# 	w: for water data (not yet)                         			#
#										#
# Note: using Arduino IDE commands can be send to the SensorNode:		#
#	gtst,.		getstatus, list all the min/max values			#
#	emnl,<value>.	set electricity left sensor min value			#
#	emxl,<value>.	set electricity left sensor max value			#
#	emnr,<value>.	set electricity right sensor min value			#
#	emxr,<value>.	set electricity right sensor max value			#
#	gmin,<value>.	set gas sensor min value				#
#	gmax,<value>.	set gas sensor max value				#
#	wmin,<value>.	set water sensor min value				#
#	wmax,<value>.	set water sensor max value				# 
#										#
# Programmed by Jos Janssen							#
# Version 0.50, last modification: March 30, 2012				#
# Code written for Fedora 14 Linux and JeeNode with USB or BUB			#
#################################################################################
# This program is free software and is available under the terms of             #
# the GNU General Public License. You can redistribute it and/or                #
# modify it under the terms of the GNU General Public License as                #
# published by the Free Software Foundation.                                    #
#                                                                               #
# This program is distributed in the hope that it will be useful,               #
# but WITHOUT ANY WARRANTY; without even the implied warranty of                #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 #
# GNU General Public License for more details.                                  #
#################################################################################
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <time.h>
#include <string.h>


/*#### DEFINITIONS ##########################################################*/

/* Port where JeeNode is connected */
/*#define PORT "/dev/ttyUSB1" */
#define PORT "/dev/jeenode1"

/* Location of logfiles */
#define ALL_LOG "/var/jnread_jos/jnread_jos.log"
#define ACTUAL_LOG "/var/jnread_jos/jnread_actual.log"
#define MIDNIGHT_LOG "/var/jnread_jos/jnread_midnight.log"

/* Location to write the html output */
#define ACTUALHTML "/var/www/html/jnread_jos/index.html"
/* Temporary output file for html creation */
#define TMPHTML "/var/www/html/jnread_jos/tmphtml.new"

/* C factor for electricity meter (no. of rotations/kWh) */
#define CFACTOR 600


/*#### FUNCTIONS ############################################################*/

/* FUNCTION to fill the array from ACTUAL_LOG file */
/* Read ACTUAL_LOG to initialise variables
 * Internally the array "long actual[16]" above is used, values are:
 * 0=rotationcount electricity
 * 1=
 * 2=rotation start count electricity low rate
 * 3=rotation start count electricity high rate
 * 4=Electricity meter low rate reading (in Wh !! (=*1000))
 * 5=Electricity meter high rate reading (in Wh !! (=*1000))
 * 6=Electricity usage low rate today (in Wh !! (=*1000))
 * 7=Electricity usage high rate today (in Wh !! (=*1000))
 * 8=rotationcount gas
 * 9=rotation start count gas
 * 10=Gas meter reading (in L !! (=*1000))
 * 11=Gas usage today (in L !! (=*1000))
 * 12=rotationcount water
 * 13=rotation start count water
 * 14=Water meter reading (in L !! (=*1000))
 * 15=Water usage today (in L !! (=*1000))
 */
/* global vars used by this function */
long actual[16];

int read_actual(char filename[])
{
  FILE *rfp;
  int i;

  if ((rfp = fopen(filename, "r")) == NULL) {
    return(1);
  }
  for (i=0; i<16; i++) {
      fscanf(rfp, "%d", &actual[i]);
  }
  fclose(rfp);
}


/* FUNCTION to write string to ACTUAL_LOG file */
int write_actual(char filename[])
{
  FILE *wfp;
  int i;

  if ((wfp = fopen(filename, "w")) == NULL) {
    return(1);
  }
  for (i=0; i<16; i++) {
      fprintf(wfp, "%d ", actual[i]);
  }
  fprintf(wfp, "\n");
  fclose(wfp);
}


/* FUNCTION to append string to file */
int append_to_file(char filename[], char str2log[])
{
  FILE *afp;

  if ((afp = fopen(filename, "a")) == NULL) {
    return(1);
  }
  fputs(str2log, afp);
  fclose(afp);
}


/* FUNCTION to set all the needed measurement vars from the array */
/* global vars used by this function */
long e_low_today;	      // low rate electricity usage today in Wh 
long e_high_today;	      // high rate electricity usage today in Wh 
long e_today;		      // total  electricity usage today in Wh 
long e_start_low_rotations;   // no. rotations at start low rate or midnight 
long e_start_high_rotations;  // no. rotations at start high rate or midnight 
long e_rotations;	      // no. rotations since start of JeeNode 
long e_midnight_low_wmeter;   // meter reading at midnight in Wh 
long e_midnight_high_wmeter;  // meter reading at midnight in Wh 
long e_low_wmeter;     	      // meter reading in Wh 
long e_high_wmeter;           // meter reading in Wh 

long g_today;		      // gas usage today in L 
long g_start_rotations;	      // no. LS digit rotations at midnight 
long g_rotations;	      // no. LS digit rotations since start of JeeNode
long g_midnight_lmeter;	      // meter reading at midnight in L 
long g_lmeter;		      // meter reading in L 

void set_measurement_vars()
{
  e_rotations            = actual[0];
  // actual[1] is not used
  e_start_low_rotations  = actual[2];
  e_start_high_rotations = actual[3];
  e_midnight_low_wmeter  = actual[4];
  e_midnight_high_wmeter = actual[5];
  e_low_today            = actual[6];
  e_high_today           = actual[7];
  e_today                = e_low_today + e_high_today;
  e_low_wmeter           = e_midnight_low_wmeter + e_low_today;
  e_high_wmeter          = e_midnight_high_wmeter + e_high_today;

  g_rotations            = actual[8];
  g_start_rotations      = actual[9];
  g_midnight_lmeter      = actual[10];
  g_today                = actual[11];
  g_lmeter               = g_midnight_lmeter + g_today;
}


void set_actual_array()
{
  actual[0] = e_rotations;
  actual[1] = 0;	// actual[1] is not used
  actual[2] = e_start_low_rotations;
  actual[3] = e_start_high_rotations;
  actual[4] = e_midnight_low_wmeter;
  actual[5] = e_midnight_high_wmeter;
  actual[6] = e_low_today;
  actual[7] = e_high_today;
  actual[8] = g_rotations;
  actual[9] = g_start_rotations;
  actual[10] = g_midnight_lmeter;
  actual[11] = g_today;
  actual[12] = 0;	// actual[12] is not used
  actual[13] = 0;	// actual[13] is not used
  actual[14] = 0;	// actual[14] is not used
  actual[15] = 0;	// actual[15] is not used
}


/* FUNCTION set_time_vars - set time variables */
/* global vars used by this function */
int hours, minutes;
char wday[3];
char today[10];
char logdatetime[17];
char htmldatetime[32];

void set_time_vars() {
  char date_time_str[200];
  time_t date_time;
  struct tm *l_date_time;

  date_time = time(NULL);
  l_date_time = localtime(&date_time);
  if (l_date_time == NULL) {
        perror("Can't get localtime");
        exit(EXIT_FAILURE);
  }
  /*  Time vars for time/date dependent functions */
  strftime(date_time_str, sizeof(date_time_str), "%H", l_date_time);
  hours=atoi(date_time_str);
  strftime(date_time_str, sizeof(date_time_str), "%M", l_date_time);
  minutes=atoi(date_time_str);
  strftime(wday, sizeof(date_time_str), "%a", l_date_time);
  strftime(today, sizeof(date_time_str), "%d-%m-%Y", l_date_time);
  /*  Time/date var for logging purposes */
  strftime(logdatetime, sizeof(date_time_str), "%d-%m-%y %H:%M:%S", l_date_time);
  /*  Time/date var for html page */
  strftime(htmldatetime, sizeof(date_time_str), "%a %b %d %H:%M:%S %Z %Y", l_date_time);
}

/* FUNCTIONs to open USB port and read line from USB port */
/* global vars used by this functions */
FILE *usb_fp;

int open_usb(char usbdevice[])
{
  char setting_string[255];

  sprintf(setting_string, "stty -F %s -hupcl -clocal ignbrk -icrnl -ixon"
          " -opost -onlcr -isig -icanon time 50 -iexten -echo -echoe -echok"
          " -echoctl -echoke 57600 -crtscts", usbdevice);
  system(setting_string);
  if ((usb_fp = fopen(usbdevice, "r+")) == NULL) {
    return(1);
  }
  //fclose(fp);
}

int get_usb_line(char *line, int max)
{
  if (fgets(line, max, usb_fp) == NULL)
    return 0;
  else
    return strlen(line);
}


/* FUNCTION set_e_rate - set electricity rate to low (low=1) or HIGH (low=0) */
/* global vars used by this function */
short low;
char h_indicator, l_indicator;

void set_e_rate() {
  //strcpy(wday,"Sat");
  low=0;  /*  assume HIGH rate for electricity as a default */
  h_indicator='*';
  l_indicator=' ';
  /* Set electricity to low rate on the following times/dates
   * (for the Netherlands): */
  /* Low tarif from 21:00 to 07:00 */
  switch (hours) {
    case 21: case 22: case 23: case 00: case 01:
    case 02: case 03: case 04: case 05: case 06:
      low=1; h_indicator=' '; l_indicator='*';
      break;
  }
  /* Low tarif on Sat and Sun */
  if ( (strcmp(wday, "Sat") == 0) || (strcmp(wday, "Sun") == 0) ) {
      low=1; h_indicator=' '; l_indicator='*';
  }
  /* Low tarif on dates below (national holidays on fixed and variable dates) */
  if (
      (strncmp(today, "01-01-", 6) == 0) || (strncmp(today, "30-04-", 6) == 0)
   || (strncmp(today, "25-12-", 6) == 0) || (strncmp(today, "26-12-", 6) == 0)
     ) {
      low=1; h_indicator=' '; l_indicator='*';
  }
  if (
      (strcmp(today, "08-04-2012") == 0) || (strcmp(today, "09-04-2012") == 0)
   || (strcmp(today, "17-05-2012") == 0) || (strcmp(today, "27-05-2012") == 0)
   || (strcmp(today, "28-05-2012") == 0)
     ) {
      low=1; h_indicator=' '; l_indicator='*';
  }
  /*switch (minutes) {
    case 32:
      low=1; h_indicator=' '; l_indicator='*';
      break;
  }*/
}


/*#### MAIN #################################################################*/

/* Initialise some variables */
int prev_low=0;
int prev_hours=0;
int watt=0;
int itemperature=0;
int otemperature=0;
int opressure=0;

main(int argc, char *argv[])
{
  char *prog = argv[0]; 	// program name for errors
  char log[]=ALL_LOG;		// The logfile
  char mlog[]=MIDNIGHT_LOG;	// The midnight logfile
  char logstring[255];		// The string to be written to the logfile
  char alog[]=ACTUAL_LOG;	// File with the last actual values
  char ahtml[]=ACTUALHTML;	// File with the actual html page
  char thtml[]=TMPHTML; 	// File with the temporary html page
  char htmlstring[255];		// The string to be written to the htmlfile
  char usb_line[128];		// line read from usb port
  int gbytes;			// bytes read from usb port
  char type; int item2; long item3; // items in USB message
  int i;			// counter

  /* Read values from the ACTUAL_LOG file and fill the vars */
  if ((read_actual(alog)) == 1) {
    fprintf(stderr, "Can't open %s\n", alog);
    exit(EXIT_FAILURE);
  }
  //for (i=0; i<16; i++) {
  //    printf("%2d= %d\n",i, actual[i]);
  //}
  set_measurement_vars();

  /* set correct electricity rate on startup */
  set_time_vars();
  //printf("%d %d %s %s  %s\n", hours, minutes, wday, today, logdatetime);
  set_e_rate();
  prev_low=low;
  //printf("%d %c %c\n", low, h_indicator, l_indicator);

  /*  read line from port and process only lines that start with:
   * 	e: for electricity data
   * 	g: for gas data
   * 	i: for inside temperature data
   * 	o: for outside temperature data
   * 	p: for outside pressure data
   * 	w: for water data (not yet)
   */
  open_usb(PORT);
  while (1) {
    gbytes=get_usb_line(usb_line, 128);  
    //printf("%d bytes: ", gbytes);
    //for (i=0; i<gbytes; i++) {
    //  printf("%c", usb_line[i]);
    //}
    if (gbytes!=0) {
      set_time_vars();
      sprintf(logstring, "%s %s", logdatetime, usb_line);
      append_to_file(log, logstring);
      set_e_rate();
      if ( (prev_low == 0) && (low == 1) ) {
        // special case: high->low: do not loose low rotationcount
        // set start low rotationcount in the evening to e_rotations minus
        // the low rotationcount of the morning
        e_start_low_rotations=e_rotations-(e_start_high_rotations-e_start_low_rotations);
        sprintf(logstring, "Changed electricity rate from high -> low\n");
        append_to_file(log, logstring);
      }
      if ( (prev_low == 1) && (low == 0) ) {
        e_start_high_rotations=e_rotations;
        sprintf(logstring, "Changed electricity rate from low -> high\n");
        append_to_file(log, logstring);
      }
      prev_low=low;
      /* process the line */
      sscanf(usb_line, "%c %d %d", &type, &item2, &item3);
      switch (type) {
        case 'e':
          watt=item2;
          e_rotations=item3;
          //printf("type %c, watt %d, e_rotations %d\n", type, watt, e_rotations);
          /*  Make calculations for the meter readings */
          if ( low == 1 ) {
            e_low_today = ((e_rotations-e_start_low_rotations)*1000)/CFACTOR;
            e_low_wmeter = e_midnight_low_wmeter+e_low_today;
          }
          if ( low == 0 ) {
            e_high_today = ((e_rotations-e_start_high_rotations)*1000)/CFACTOR;
            e_high_wmeter = e_midnight_high_wmeter+e_high_today;
          }
          e_today= e_low_today+e_high_today;
          break;
        case 'g':
          g_rotations=item3;
          //printf("type %c, g_rotations %d\n", type, g_rotations);
          /*  Make calculations for the meter readings */
          g_today = (g_rotations-g_start_rotations)*10;
          g_lmeter = g_midnight_lmeter+g_today;
          break;
        case 'i':
          itemperature=item2;
          //printf("type %c, itemperature %d\n", type, itemperature);
          break;
        case 'o':
          otemperature=item2;
          //printf("type %c, otemperature %d\n", type, otemperature);
          break;
        case 'p':
          opressure=item2;
          //printf("type %c, opressure %d\n", type, opressure);
          break;
//      case 'w':
//        break;
      }
      set_actual_array();
      write_actual(alog);
  
      /*  Reset the daily counter e_today and g_today, because of a new day 
       *  Set e_start_rotations and g_start_rotations to the number of
       *  rotations now, because of a new day
       */
      if ( (prev_hours == 23) && (hours == 00) ) {
        sprintf(logstring, "%s %d %d 0\n", logdatetime, e_today, g_today);
        append_to_file(mlog, logstring);
        sprintf(logstring, "Midnight reset of the counters\n");
        append_to_file(log, logstring);
        e_today = 0;
        e_low_today = 0;
        e_high_today = 0;
        e_midnight_low_wmeter = e_low_wmeter;
        e_midnight_high_wmeter = e_high_wmeter;
        e_start_low_rotations = e_rotations;
        e_start_high_rotations = e_rotations;
        g_today = 0;
        g_midnight_lmeter = g_lmeter;
        g_start_rotations = g_rotations;
      }
      prev_hours = hours;
  
      /* Create HTML page */
      sprintf(htmlstring, "<HTML><HEAD><TITLE>JJ Electricity/Gas/Water-meter</TITLE><META HTTP-EQUIV=\"refresh\" CONTENT=\"30\"><LINK REL=\"shortcut icon\" HREF=\"favicon.ico\"></HEAD>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<BODY BGCOLOR=#000066 TEXT=#E8EEFD LINK=#FFFFFF VLINK=#C6FDF4 ALINK=#0BBFFF BACKGROUND=$BGIMG>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TABLE WIDTH=1210 BORDER=1 CELLPADDING=2 CELLSPACING=2 BGCOLOR=#1A689D BORDERCOLOR=#0DD3EA>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD WIDTH=293><FONT COLOR=#00FF00>Actual meter measurement on</FONT></TD>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "    <TD WIDTH=293><FONT COLOR=#00FF00>%s</FONT></TD>", htmldatetime);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TD><CENTER>Electricitymeter type: AEG T2J16H<BR>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "Gasmeter type: UGI G4Mk2S</CENTER></TD></TR>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD>Actual power usage (W)</TD><TD>%d W</TD>", watt);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TD rowspan=12><CENTER><IMG BORDER=0 SRC=\"meters_s.jpg\" WIDTH=623 HEIGHT=334></CENTER></TD></TR>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD>Electricity usage today (kWh)</TD><TD>%3.3f kWh</TD></TR>", (float)e_today/1000);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD>Electricitymeter low rate counter (kWh)</TD><TD>%6.3f  kWh   %c</TD></TR>", (float)e_low_wmeter/1000, l_indicator);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD>Electricitymeter high rate counter (kWh)</TD><TD>%6.3f  kWh   %c</TD></TR>", (float)e_high_wmeter/1000, h_indicator);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD>Gas usage today (m&sup3;)</TD><TD>%6.3f m&sup3;</TD></TR>", (float)g_today/1000);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD>Gas meter counter (m&sup3;)</TD><TD>%6.3f m&sup3;</TD></TR>", (float)g_lmeter/1000);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD>Inside temperature</TD><TD>%2.1f &deg;C</TD></TR>", (float)itemperature/10);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD>Outside temperature</TD><TD>%2.1f &deg;C</TD></TR>", (float)otemperature/10);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD>Barometric pressure</TD><TD>%4.1f hPa (mbar)</TD></TR>", (float)opressure/10);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD>Water usage today (l)</TD><TD>Not implemented (yet)</TD></TR>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD>Watermeter counter (m3)</TD><TD>Not implemented (yet)</TD></TR>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "</TABLE>");
      append_to_file(thtml, htmlstring);
//    sprintf(htmlstring, "<TABLE WIDTH=1210 BORDER=1 CELLPADDING=1 CELLSPACING=2 BGCOLOR=#1A689D BORDERCOLOR=#0DD3EA>"
//    append_to_file(thtml, htmlstring);
//    sprintf(htmlstring, "<TR><TD><IMG SRC=\"graph/solar_power_last_day.png\"></TD>"
//    append_to_file(thtml, htmlstring);
//    sprintf(htmlstring, "    <TD><IMG SRC=\"graph/solar_power_last_week.png\"></TD></TR>"
//    append_to_file(thtml, htmlstring);
//    sprintf(htmlstring, "<TR><TD><IMG SRC=\"graph/solar_power_last_month.png\"></TD>"
//    append_to_file(thtml, htmlstring);
//    sprintf(htmlstring, "    <TD><IMG SRC=\"graph/solar_power_last_year.png\"></TD></TR>"
//    append_to_file(thtml, htmlstring);
//    sprintf(htmlstring, "</TABLE>" 
      rename(thtml, ahtml);
    }
  }
}
