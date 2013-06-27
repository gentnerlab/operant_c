#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#include "/usr/local/src/operantio/operantio_6509.c"
#include <sunrise.h>

/* --- OPERANT IO CHANNELS ---*/

#define LEFTPECK   1
#define CENTERPECK 2
#define RIGHTPECK  3

#define HOPPEROP   4

#define LFTKEYLT  1  
#define CTRKEYLT  2  
#define RGTKEYLT  3  
#define HOUSELT	  4   
#define FEEDER	  5


/* --------- OPERANT VARIABLES ---------- */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */

int  starthour = EXP_START_TIME; 
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval = SLEEP_TIME;
const char exp_name[] = "lights";
int box_id = -1;

/**********************************************************************
 **********************************************************************/
void do_usage()
{
  fprintf(stderr, "lights usage:\n");
  fprintf(stderr, "    [-help] [-B int] [-on int:int] [-off int:int] \n\n");
  fprintf(stderr, "       -help         = show this help message\n");
  fprintf(stderr, "       -B int        = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "       -on int:int   = set hour for exp to start eg: '-on 7:30' (default is 7AM)\n");
  fprintf(stderr, "       -off int:int  = set hour for exp to stop eg: '-off 19:04' (default is 7PM)\n");
  fprintf(stderr, "              To use sunrise or sunset times, calculate daily, set on or off to '99'\n");
  exit(-1);
}

/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *starthour, int *stophour, int *startmin, int *stopmin)
{
  int i=0;
  
  for (i = 1; i < argc; i++){
    if (*argv[i] == '-'){
      if (strncmp(argv[i], "-B", 2) == 0) 
        sscanf(argv[++i], "%i", box_id);
      else if (strncmp(argv[i], "-on", 3) == 0)
        sscanf(argv[++i], "%i:%i", starthour, startmin);
      else if (strncmp(argv[i], "-off", 4) == 0)
        sscanf(argv[++i], "%i:%i", stophour, stopmin);
      else if (strncmp(argv[i], "-help", 5) == 0){
        do_usage();
      }
      else{
        fprintf(stderr, "Unknown option: %s\t", argv[i]);
        fprintf(stderr, "Try 'lights -help'\n");
      }
    }
  }
  return 1;
}


/**********************************************************************
 MAIN
**********************************************************************/	
int main(int argc, char *argv[])
{
	char hour[16], min[16], month[16],day[16], year[16];
	char buffer[30],temphour[16],tempmin[16];
	int dosunrise=0,dosunset=0,starttime,stoptime,currtime;
	float latitude = 32.82, longitude = 117.14;
	time_t curr_tt, rise_tt, set_tt;

	struct tm *loctime;

	srand (time (0) );
       
	/* Parse the command line */
        command_line_parse(argc, argv, &box_id, &starthour, &stophour, &startmin, &stopmin); 
	
	/* watch for terminal errors*/
	if( (stophour!=99) && (starthour !=99) ){
	  if ((stophour <= starthour) && (stopmin<=startmin)){
	    fprintf(stderr, "\tTERMINAL ERROR: exp start-time must be greater than stop-time\n");
	    exit(-1);
	  } 
	}
	if (box_id <= 0){
	  fprintf(stderr, "\tYou must enter a box ID!\n"); 
	  fprintf(stderr, "\tERROR: try 'lights -help' for available options\n");
	  exit(-1);
	}

	/*set some variables as needed*/
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (year, 16, "%Y", loctime);
	strftime(month, 16, "%m", loctime);
	strftime(day, 16,"%d", loctime);

	if(starthour==99){
	  dosunrise=1;
	  rise_tt = sunrise(atoi(year), atoi(month), atoi(day), latitude, longitude);
	  strftime(temphour, 16, "%H", localtime(&rise_tt));
	  strftime(tempmin, 16, "%M", localtime(&rise_tt));
	  starthour=atoi(temphour);
	  startmin=atoi(tempmin);
	  strftime(buffer,30,"%m-%d-%Y %H:%M:%S",localtime(&rise_tt));
	  printf("Sessions start at sunrise (Today: '%s')\n",buffer);
	}
	if(stophour==99){
	  dosunset=1;
	  set_tt = sunset(atoi(year), atoi(month), atoi(day), latitude, longitude);
	  strftime(temphour, 16, "%H", localtime(&set_tt));
	  strftime(tempmin, 16, "%M", localtime(&set_tt));
	  stophour=atoi(temphour);
	  stopmin=atoi(tempmin);
	  strftime(buffer,30,"%m-%d-%Y  %T",localtime(&set_tt));
	  printf("Sessions stop at sunset (Today: '%s')\n",buffer);
	}
	starttime=(starthour*60)+startmin;
	stoptime=(stophour*60)+stopmin;

	/* Initialize box */
	if (operant_open()!=0){
	  fprintf(stderr, "Problem opening IO interface\n");
	  exit (-1);
	}
	operant_clear(box_id);
	operant_write (box_id, HOUSELT, 1);        /* make sure houselight is on */

	do{                                                           /* start the main loop */
	  /* Loop with lights on all day*/
	  while ((currtime>=starttime) && (currtime<stoptime)){       /* start main trial loop */
	    operant_write (box_id, HOUSELT, 1);                       /* house light on */
	    sleep (sleep_interval);
	    curr_tt = time(NULL);
	    loctime = localtime (&curr_tt);
	    strftime (hour, 16, "%H", loctime);
	    strftime(min, 16, "%M", loctime);
	    currtime=(atoi(hour)*60)+atoi(min);
	  }
	  
	  /* Loop with lights out all night*/
	  while( (currtime<starttime) || (currtime>=stoptime) ){  
	    operant_write(box_id, HOUSELT, 0);
	    operant_write(box_id, FEEDER, 0);
	    operant_write(box_id, LFTKEYLT, 0);
	    operant_write(box_id, CTRKEYLT, 0);
	    operant_write(box_id, RGTKEYLT, 0);
	    sleep (sleep_interval);
	    curr_tt = time(NULL);
	    loctime = localtime (&curr_tt);
	    strftime (hour, 16, "%H", loctime);
	    strftime (min, 16, "%M", loctime);
	    currtime=(atoi(hour)*60)+atoi(min);
	  }
	  operant_write(box_id, HOUSELT, 1);
	  /*figure new sunrise/set times*/
	  curr_tt = time(NULL);
	  loctime = localtime (&curr_tt);
	  strftime (year, 16, "%Y", loctime);
	  strftime(month, 16, "%m", loctime);
	  strftime(day, 16,"%d", loctime);

	  if(dosunrise){
	    rise_tt = sunrise(atoi(year), atoi(month), atoi(day), latitude, longitude);
	    strftime(temphour, 16, "%H", localtime(&rise_tt));
	    strftime(tempmin, 16, "%M", localtime(&rise_tt));
	    starthour=atoi(temphour);
	    startmin=atoi(tempmin);
	    strftime(buffer,30,"%m-%d-%Y %H:%M:%S",localtime(&rise_tt));
	  }
	  if(dosunset){
	    set_tt = sunset(atoi(year), atoi(month), atoi(day), latitude, longitude);
	    strftime(temphour, 16, "%H", localtime(&set_tt));
	    strftime(tempmin, 16, "%M", localtime(&set_tt));
	    stophour=atoi(temphour);
	    stopmin=atoi(tempmin);
	    strftime(buffer,30,"%m-%d-%Y  %T",localtime(&set_tt));
	  }
	  	 	  
	}while (1);// main loop
	return 0;
}                         

