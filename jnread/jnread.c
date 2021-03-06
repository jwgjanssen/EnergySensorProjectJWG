/*
#################################################################################
# Program reads from USB input and builds webpage			                   	#
# Interpret lines that start with:                                              #
#   a: for appliance data       					                           	#
# 	e: for electricity data							                            #
# 	g: for gas data								                                #
# 	w: for water data							                                #
# 	i: for inside temperature data						                        #
# 	o: for outside temperature data						                        #
# 	p: for outside pressure data						                        #
# 	s: for solar production data						                        #
#										                                        #
# Note: using Arduino IDE commands can be send to the SensorNode:		        #
#	gtst,.		getstatus, list all the min/max values			                #
#	emnl,<value>.	set electricity left sensor min value			            #
#	emxl,<value>.	set electricity left sensor max value			            #
#	emnr,<value>.	set electricity right sensor min value			            #
#	emxr,<value>.	set electricity right sensor max value			            #
#	gmin,<value>.	set gas sensor min value				                    #
#	gmax,<value>.	set gas sensor max value				                    #
#	wmin,<value>.	set water sensor min value				                    #
#	wmax,<value>.	set water sensor max value				                    #
#										                                        #
# Programmed by Jos Janssen							                            #
# Modifications:								                                #
# Date:        Who:   	Change:							                        #
# 11sep2012    Jos    	Added national holidays on variable dates for 2013	    #
# 28oct2012    Jos    	Deleted meter readings & changed webpage layout	        #
# 30mar2013    Jos    	Added handling solar production data			        #
# 01jul2013    Jos    	Changed date/time for midnight log entries		        #
# 31aug2013    Jos      Added logging to xively.com (excl. gas)			        #
# 01sep2013    Jos      Solution for reporting gas to xively.com                #
# 14feb2015    Jos      Changed file paths for use on Synology                  #
# 09oct2015    Jos      Added handling water data                               #
# 14dec2015    Jos      Added sending data to Domoticz server                   #
# 09mar2017    Jos      Removed xively.com stuff                                #
# 11mar2017    Jos      Changed format of daily stats file to CSV               #
# 13dec2019    Jos		Changes for use in Docker                               #
#										                                        #
# Code written for Linux and JeeNode with USB or BUB		                    #
#										                                        #
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
#include <errno.h>
#include <sys/fcntl.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <math.h>


/*#### DEFINITIONS ##########################################################*/

/* Turn debugging on or off */
#define DEBUG 0

/* Port where JeeNode is connected */
#define PORT "/dev/ttyUSB0"
/*#define PORT "/dev/ttyUSB1" */
/*#define PORT "/dev/jeenode1" */

/* Location of logfiles */
#define ALL_LOG "/opt/jnread/log/jnread_jos.log"
#define ACTUAL_LOG "/opt/jnread/log/jnread_actual.log"
#define MIDNIGHT_LOG "/opt/jnread/log/jnread_midnight.log"

/* Location to RRD solar database */
#define RRD_DB "/opt/jnread/rrd/solar_power.rrd"

/* Location to write the html output */
#define ACTUALHTML "/opt/jnread/www/index.html"
/* Temporary output file for html creation */
#define TMPHTML "/opt/jnread/www/tmphtml.new"

/* C factor for electricity meter (no. of rotations/kWh) */
#define CFACTOR 600

/* Settings for updating counters on the Domoticz server */
#define DOMOTICZ_SERVER "ha01:8080"
#define E_IDX_counter "99"
#define E_IDX_actual "96"
#define S_IDX "98"
#define G_IDX "100"
#define W_IDX "101"
#define I_IDX "94"
#define O_IDX "93"
#define P_IDX "95"
#define A_IDX "102"


/*#### FUNCTIONS ############################################################*/

/* FUNCTION to fill the array from ACTUAL_LOG file */
/* NEW Read ACTUAL_LOG to initialise variables
* Internally the array "long actual[9]" above is used, values are:
*  0=rotationcount electricity
*  1=rotation start count electricity
*  2=Electricity usage today (in Wh !! (=*1000))
*  3=rotationcount gas
*  4=rotation start count gas
*  5=Gas usage today (in L !! (=*1000))
*  6=rotationcount water
*  7=rotation start count water
*  8=Water usage today (in L)
*  9=Solar runtime today (in minutes)
* 10=Solar electricity production today (in Wh !! (=*1000))
*/
/* global vars used by this function */
long actual[11];

int read_actual(char filename[])
{
  FILE *rfp;
  int i;

  if ((rfp = fopen(filename, "r")) == NULL) {
    return(1);
  }
  for (i=0; i<11; i++) {
    fscanf(rfp, "%ld", &actual[i]);
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
  for (i=0; i<11; i++) {
    fprintf(wfp, "%ld ", actual[i]);
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


/* FUNCTIONs to set all the needed measurement vars from the array */
/* global vars used by these functions */
unsigned int e_today;		      // electricity usage today in Wh 
unsigned int e_start_rotations;       // no. rotations at midnight 
unsigned int e_rotations;	      // no. rotations since start of JeeNode 
unsigned int g_today;		      // gas usage today in L 
unsigned int g_start_rotations;	      // no. LS digit rotations at midnight 
unsigned int g_rotations;	      // no. LS digit rotations since start of JeeNode
unsigned int w_today;		      // water usage today in L 
unsigned int w_start_rotations;	      // no. rotations at midnight 
unsigned int w_rotations;	      // no. rotations since start of JeeNode
unsigned int s_today;		      // solar electricity production today in Wh 
unsigned int s_runtime;               // solar production runtime today

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
  s_runtime          = actual[9];
  s_today            = actual[10];
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
  actual[9] = s_runtime;
  actual[10] = s_today;
}


/* FUNCTION set_time_vars - set time variables */
/* global vars used by this function */
int hours, minutes;
//char wday[3];
//char today[10];
char logdatetime[17],prevlogdatetime[17];
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
  //strftime(wday, sizeof(date_time_str), "%a", l_date_time);
  //strftime(today, sizeof(date_time_str), "%d-%m-%Y", l_date_time);
  /*  Time/date var for logging purposes */
  strcpy(prevlogdatetime,logdatetime);
  strftime(logdatetime, sizeof(date_time_str), "%d-%m-%y,%H:%M:%S", l_date_time);
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

/* FUNCTION to create the html pages with relevant data */
/* global vars used by this functions */
char ahtml[]=ACTUALHTML;	// File with the actual html page
char thtml[]=TMPHTML; 		// File with the temporary html page
char htmlstring[255];		// The string to be written to the htmlfile
int watt=0;
int swatt=0;
int itemperature=0;
int otemperature=0;
int opressure=0;

void create_html_page() {
  #if DEBUG
  printf("watt %d, e_today %d, g_today %d,  w_today %d, itemp %d, otemp %d opres %d, swatt %d, s_today %d, s_runtime %d\n",
  watt,    e_today,    g_today,i    w_today, itemperature,otemperature,opressure,swatt,s_today,    s_runtime);
  #endif
  /* Create HTML page */
  sprintf(htmlstring, "<HTML><HEAD><TITLE>JJ Home data</TITLE><META HTTP-EQUIV=\"refresh\" CONTENT=\"30\"><LINK REL=\"shortcut icon\" HREF=\"favicon.ico\"></HEAD>");
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
  sprintf(htmlstring, "<TR><TD ROWSPAN=3><CENTER><IMG BORDER=0 SRC=\"pictures/solar-button.png\" WIDTH=90 HEIGHT=50></CENTER></TD><TD>Actual solar power (W)</TD><TD><FONT SIZE=4>%d W</FONT></TD>", swatt);
  append_to_file(thtml, htmlstring);
  sprintf(htmlstring, "<TR><TD>Solar power today (kWh)</TD><TD><FONT SIZE=4>%3.3f kWh</FONT></TD></TR>", (float)s_today/1000);
  append_to_file(thtml, htmlstring);
  sprintf(htmlstring, "<TR><TD>Running time today (hh:mm)</TD><TD><FONT SIZE=4>%02u:%02u</FONT></TD></TR>", s_runtime/60, s_runtime%60);
  append_to_file(thtml, htmlstring);
  sprintf(htmlstring, "<TR><TD><CENTER><IMG BORDER=0 SRC=\"pictures/gas-button.png\" WIDTH=90 HEIGHT=50></CENTER></TD><TD>Gas usage today (m&sup3;)</TD><TD><FONT SIZE=4>%6.3f m&sup3;</FONT></TD></TR>", (float)g_today/1000);
  append_to_file(thtml, htmlstring);
  sprintf(htmlstring, "<TR><TD><CENTER><IMG BORDER=0 SRC=\"pictures/water-button.png\" WIDTH=90 HEIGHT=50></CENTER></TD><TD>Water usage today (L)</TD><TD><FONT SIZE=4>%d L</FONT></TD></TR>", w_today);
  append_to_file(thtml, htmlstring);
  sprintf(htmlstring, "<TR><TD><CENTER><IMG BORDER=0 SRC=\"pictures/temp_inside-button.png\" WIDTH=90 HEIGHT=50></CENTER></TD><TD>Inside temperature</TD><TD><FONT SIZE=4>%2.1f &deg;C</FONT></TD></TR>", (float)itemperature/10);
  append_to_file(thtml, htmlstring);
  sprintf(htmlstring, "<TR><TD><CENTER><IMG BORDER=0 SRC=\"pictures/temp_outside-button.png\" WIDTH=90 HEIGHT=50></CENTER></TD><TD>Outside temperature</TD><TD><FONT SIZE=4>%2.1f &deg;C</FONT></TD></TR>", (float)otemperature/10);
  append_to_file(thtml, htmlstring);
  sprintf(htmlstring, "<TR><TD><CENTER><IMG BORDER=0 SRC=\"pictures/pressure-button.png\" WIDTH=90 HEIGHT=50></CENTER></TD><TD>Barometric pressure</TD><TD><FONT SIZE=4>%4.1f hPa</FONT></TD></TR>", (float)opressure/10);
  append_to_file(thtml, htmlstring);
  sprintf(htmlstring, "<TR><TD COLSPAN=3><IMG SRC=\"graph/solar_power_last_day.png\"></TD>");
  append_to_file(thtml, htmlstring);
  sprintf(htmlstring, "<TR><TD COLSPAN=3><IMG SRC=\"graph/solar_power_last_week.png\"></TD>");
  append_to_file(thtml, htmlstring);
  sprintf(htmlstring, "<TR><TD COLSPAN=3><IMG SRC=\"graph/solar_power_last_month.png\"></TD>");
  append_to_file(thtml, htmlstring);
  sprintf(htmlstring, "<TR><TD COLSPAN=3><IMG SRC=\"graph/solar_power_last_year.png\"></TD>");
  append_to_file(thtml, htmlstring);
  sprintf(htmlstring, "</FONT></TABLE></BODY></HTML>");
  append_to_file(thtml, htmlstring);
  rename(thtml, ahtml);
}


/*#### MAIN #################################################################*/

/* Initialise some variables */
int prev_hours=0;

int main(int argc, char *argv[])
{
  char *prog = argv[0]; 	// program name for errors
  char log[]=ALL_LOG;		// The logfile
  char mlog[]=MIDNIGHT_LOG;	// The midnight logfile
  char logstring[255];		// The string to be written to the logfile
  char alog[]=ACTUAL_LOG;	// File with the last actual values
  char usb_line[128];		// line read from usb port
  char rrd_db[]=RRD_DB; 	// RRD database file
  char systemstr[255];          // line to be executed by OS
  int gbytes;			// bytes read from usb port
  char type; int item2; long item3; long item4; // items in USB message
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

  /*  read line from port and process only lines that start with:
  *     a: for appliance data
  * 	e: for electricity data
  * 	g: for gas data
  * 	i: for inside temperature data
  * 	o: for outside temperature data
  * 	p: for outside pressure data
  * 	s: for solar production data
  * 	w: for water data
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
      sscanf(usb_line, "%c %d %ld %ld", &type, &item2, &item3, &item4);
      switch (type) {
      case 'a':
        #if DEBUG
        printf("type %c, watt %d\n", type, item2);
        #endif
        sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%d\"", DOMOTICZ_SERVER, A_IDX, item2);
        system(systemstr);
        //sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%d\"", N_DOMOTICZ_SERVER, N_A_IDX, item2);
        //system(systemstr);
        break;
      case 'e':
        watt=item2;
        e_rotations=item3;
        #if DEBUG
        printf("type %c, watt %d, e_rotations %d\n", type, watt, e_rotations);
        #endif
        e_today = ((e_rotations-e_start_rotations)*1000)/CFACTOR;
        sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%d\"", DOMOTICZ_SERVER, E_IDX_actual, watt);
        system(systemstr);
        //sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%d;%d\"", N_DOMOTICZ_SERVER, N_E_IDX_actual, watt);
        //system(systemstr);
        // The "(e_rotations*1000)/600" in the line below is needed to be able to set the "Energy counter divider" in Domoticz on 1000 (and not 600)
        sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%d\"", DOMOTICZ_SERVER, E_IDX_counter, (e_rotations*1000)/600);
        system(systemstr);
        //sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%d;%d\"", N_DOMOTICZ_SERVER, N_E_IDX_counter, (e_rotations*1000)/600);
        //system(systemstr);
        break;
      case 'g':
        g_rotations=item3;
        #if DEBUG
        printf("type %c, g_rotations %d\n", type, g_rotations);
        #endif
        g_today = (g_rotations-g_start_rotations)*10;
        sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%d\"", DOMOTICZ_SERVER, G_IDX, g_rotations);
        system(systemstr);
        //sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%d\"", N_DOMOTICZ_SERVER, N_G_IDX, g_rotations);
        //system(systemstr);
        //sleep(1);
        break;
      case 'w':
        w_rotations=item3;
        #if DEBUG
        printf("type %c, w_rotations %d\n", type, w_rotations);
        #endif
        w_today = (w_rotations-w_start_rotations)*1;
        sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%d\"", DOMOTICZ_SERVER, W_IDX, w_rotations);
        system(systemstr);
        //sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%d\"", N_DOMOTICZ_SERVER, N_W_IDX, w_rotations);
        //system(systemstr);
        //sleep(1);
        break;
      case 'i':
        itemperature=item2;
        #if DEBUG
        printf("type %c, itemperature %d\n", type, itemperature);
        #endif
        sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%2.1f\"", DOMOTICZ_SERVER, I_IDX, (float)itemperature/10.0f);
        system(systemstr);
        //sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%2.1f\"", N_DOMOTICZ_SERVER, N_I_IDX, (float)itemperature/10.0f);
        //system(systemstr);
        break;
      case 'o':
        otemperature=item2;
        #if DEBUG
        printf("type %c, otemperature %d\n", type, otemperature);
        #endif
        sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%2.1f\"", DOMOTICZ_SERVER, O_IDX, (float)otemperature/10.0f);
        system(systemstr);
        //sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%2.1f\"", N_DOMOTICZ_SERVER, N_O_IDX, (float)otemperature/10.0f);
        //system(systemstr);
        break;
      case 'p':
        opressure=item2;
        #if DEBUG
        printf("type %c, opressure %d\n", type, opressure);
        #endif
        sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%4.1f;5\"", DOMOTICZ_SERVER, P_IDX, (float)opressure/10.0f);
        system(systemstr);
        //sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%4.1f;5\"", N_DOMOTICZ_SERVER, N_P_IDX, (float)opressure/10.0f);
        //system(systemstr);
        break;
      case 's':
        swatt=item2;
        s_today=item3;
        s_runtime=item4;
        #if DEBUG
        printf("type %c, swatt %d, s_today %d, s_runtime %d\n", type, swatt, s_today, s_runtime);
        #endif
        sprintf(systemstr, "rrdtool update %s N:%d", rrd_db, swatt);
        system(systemstr);
        sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%d;%d\"", DOMOTICZ_SERVER, S_IDX, swatt, s_today);
        system(systemstr);
        //sprintf(systemstr, "curl -s -i -H \"Accept: application/json\" \"http://%s/json.htm?type=command&param=udevice&idx=%s&nvalue=0&svalue=%d;%d\"", N_DOMOTICZ_SERVER, N_S_IDX, swatt, s_today);
        //system(systemstr);
        break;
      }
      set_actual_array();
      write_actual(alog);
      
      /*  Reset the daily counter e_today, g_today and w_today, because of a new day,
      *  set e_start_rotations, g_start_rotations and w_start_rotations to the number of
      *  rotations now, because of a new day
      */
      if ( (prev_hours == 23) && (hours == 00) ) {
        // Data for daily log: Date, Time, Imported energy (Wh), Gas usage (L), Water usage (L), Solar production (Wh), Solar runtime (mins), Used energy (Wh)(=Imported energy+Solar production)
        sprintf(logstring, "%s,%d,%d,%d,%d,%d,%d\n", prevlogdatetime, e_today, g_today, w_today, s_today, s_runtime, e_today+s_today);
        append_to_file(mlog, logstring);
        sprintf(logstring, "Midnight reset of the counters\n");
        append_to_file(log, logstring);
        e_today = 0;
        e_start_rotations = e_rotations;
        g_today = 0;
        g_start_rotations = g_rotations;
        s_today = 0;
        s_runtime = 0;
        w_today = 0;
        w_start_rotations = w_rotations;

      }
      prev_hours = hours;

      create_html_page();
    }
  }
}
