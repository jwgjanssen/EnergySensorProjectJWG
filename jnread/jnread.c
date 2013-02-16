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
# Modifications:								#
# Date:        Who:   	Change:							#
# 11sep2012    Jos    	Added national holidays on variable dates for 2013	#
# 28oct2012    Jos    	Deleted meter readings & changed webpage layout		#
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
/* NEW Read ACTUAL_LOG to initialise variables
 * Internally the array "long actual[9]" above is used, values are:
 * 0=rotationcount electricity
 * 1=rotation start count electricity
 * 2=Electricity usage today (in Wh !! (=*1000))
 * 3=rotationcount gas
 * 4=rotation start count gas
 * 5=Gas usage today (in L !! (=*1000))
 * 6=rotationcount water
 * 7=rotation start count water
 * 8=Water usage today (in L !! (=*1000))
 */
/* OLD Read ACTUAL_LOG to initialise variables
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
long actual[9];

int read_actual(char filename[])
{
  FILE *rfp;
  int i;

  if ((rfp = fopen(filename, "r")) == NULL) {
    return(1);
  }
  for (i=0; i<9; i++) {
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
  for (i=0; i<9; i++) {
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
long e_today;		      // total  electricity usage today in Wh 
long e_start_rotations;       // no. rotations at midnight 
long e_rotations;	      // no. rotations since start of JeeNode 
long g_today;		      // gas usage today in L 
long g_start_rotations;	      // no. LS digit rotations at midnight 
long g_rotations;	      // no. LS digit rotations since start of JeeNode
long w_today;		      // water usage today in L 
long w_start_rotations;	      // no. rotations at midnight 
long w_rotations;	      // no. rotations since start of JeeNode

void set_measurement_vars()
{
  e_rotations        = actual[0];
  e_start_rotations  = actual[1];
  e_today            = actual[2];
  g_rotations        = actual[3];
  g_start_rotations  = actual[4];
  g_today            = actual[5];
  w_rotations        = actual[6];
  w_start_rotations  = actual[7];
  w_today            = actual[8];

}


void set_actual_array()
{
  actual[0] = e_rotations;
  actual[1] = e_start_rotations;
  actual[2] = e_today;
  actual[3] = g_rotations;
  actual[4] = g_start_rotations;
  actual[5] = g_today;
  actual[6] = w_rotations;
  actual[7] = w_start_rotations;
  actual[8] = w_today;
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


/*#### MAIN #################################################################*/

/* Initialise some variables */
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
  //for (i=0; i<9; i++) {
  //    printf("%2d= %d\n",i, actual[i]);
  //}
  set_measurement_vars();

  set_time_vars();
  //printf("%d %d %s %s  %s\n", hours, minutes, wday, today, logdatetime);

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
      /* process the line */
      sscanf(usb_line, "%c %d %d", &type, &item2, &item3);
      switch (type) {
        case 'e':
          watt=item2;
          e_rotations=item3;
          //printf("type %c, watt %d, e_rotations %d\n", type, watt, e_rotations);
          e_today= ((e_rotations-e_start_rotations)*1000)/CFACTOR;
          break;
        case 'g':
          g_rotations=item3;
          //printf("type %c, g_rotations %d\n", type, g_rotations);
          g_today = (g_rotations-g_start_rotations)*10;
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
  
      /*  Reset the daily counter e_today and g_today, because of a new day set
       *  e_start_rotations, g_start_rotations and w_start_rotations(not yet) to the number of
       *  rotations now, because of a new day
       */
      if ( (prev_hours == 23) && (hours == 00) ) {
        sprintf(logstring, "%s %d %d 0\n", logdatetime, e_today, g_today);
        append_to_file(mlog, logstring);
        sprintf(logstring, "Midnight reset of the counters\n");
        append_to_file(log, logstring);
        e_today = 0;
        e_start_rotations = e_rotations;
        g_today = 0;
        g_start_rotations = g_rotations;
        /*w_today = 0;
        w_start_rotations = g_rotations;*/

      }
      prev_hours = hours;
  
      /* Create HTML page */
      sprintf(htmlstring, "<HTML><HEAD><TITLE>JJ Electricity/Gas-meter</TITLE><META HTTP-EQUIV=\"refresh\" CONTENT=\"30\"><LINK REL=\"shortcut icon\" HREF=\"favicon.ico\"></HEAD>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<BODY BGCOLOR=#000066 TEXT=#E8EEFD LINK=#FFFFFF VLINK=#C6FDF4 ALINK=#0BBFFF BACKGROUND=$BGIMG>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<FONT FACE=\"Arial\" SIZE=3>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TABLE WIDTH=500 BORDER=1 CELLPADDING=2 CELLSPACING=0 BGCOLOR=#1A689D BORDERCOLOR=#0DD3EA>");
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD COLSPAN=3><FONT SIZE=4 COLOR=#00FF00><CENTER>%s</CENTER></FONT></TD></TR>", htmldatetime);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD ROWSPAN=2><CENTER><IMG BORDER=0 SRC=\"pictures/electricity-button.png\" WIDTH=90 HEIGHT=50></CENTER></TD><TD>Actual power usage (W)</TD><TD><FONT SIZE=4>%d W</FONT></TD>", watt);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD>Electricity usage today (kWh)</TD><TD><FONT SIZE=4>%3.3f kWh</FONT></TD></TR>", (float)e_today/1000);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD><CENTER><IMG BORDER=0 SRC=\"pictures/gas-button.png\" WIDTH=90 HEIGHT=50></CENTER></TD><TD>Gas usage today (m&sup3;)</TD><TD><FONT SIZE=4>%6.3f m&sup3;</FONT></TD></TR>", (float)g_today/1000);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD><CENTER><IMG BORDER=0 SRC=\"pictures/temp_inside-button.png\" WIDTH=90 HEIGHT=50></CENTER></TD><TD>Inside temperature</TD><TD><FONT SIZE=4>%2.1f &deg;C</FONT></TD></TR>", (float)itemperature/10);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD><CENTER><IMG BORDER=0 SRC=\"pictures/temp_outside-button.png\" WIDTH=90 HEIGHT=50></CENTER></TD><TD>Outside temperature</TD><TD><FONT SIZE=4>%2.1f &deg;C</FONT></TD></TR>", (float)otemperature/10);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "<TR><TD><CENTER><IMG BORDER=0 SRC=\"pictures/pressure-button.png\" WIDTH=90 HEIGHT=50></CENTER></TD><TD>Barometric pressure</TD><TD><FONT SIZE=4>%4.1f hPa</FONT></TD></TR>", (float)opressure/10);
      append_to_file(thtml, htmlstring);
      sprintf(htmlstring, "</FONT></TABLE></BODY></HTML>");
      append_to_file(thtml, htmlstring);
      rename(thtml, ahtml);
    }
  }
}
