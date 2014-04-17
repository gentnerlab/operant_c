/*  riskyBiz.c
 *  JAC
 *  
 *  changes the reliability with which the food hopper comes up based on response port
 ************************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <math.h>
#include <signal.h>
#include <sys/time.h>

#include "/usr/local/src/operantio/operantio.c"
#include "/usr/local/src/audioio/audout.c" 
#include <sunrise.h>

#include <alsa/asoundlib.h>

#define ALSA_PCM_NEW_HW_PARAMS_API

#define DEBUG 0

#define timeradd(a, b, result)                                                \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;                          \
    if ((result)->tv_usec >= 1000000)                                         \
      {                                                                       \
        ++(result)->tv_sec;                                                   \
        (result)->tv_usec -= 1000000;                                         \
      }                                                                       \
  } while (0)

#define timersub(a, b, result)                                               \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                          \
    if ((result)->tv_usec < 0) {                                              \
      --(result)->tv_sec;                                                     \
      (result)->tv_usec += 1000000;                                           \
    }                                                                         \
  } while (0)

/* --- I/O CHANNELS ---*/
#define LEFTPECK   1
#define CENTERPECK 2
#define RIGHTPECK  3

#define LFTKEYLT  1  
#define CTRKEYLT  2  
#define RGTKEYLT  3  
#define HOUSELT   4   
#define FEEDER    5

 

/* --------- OPERANT VARIABLES ---------- */
#define RESPONSE_SAMPLE_INTERVAL 5000000      /* input polling rate in nanoseconds */
#define EXP_START_TIME           6            /* hour(0-24) session will start (cannot be greater than the stop time)*/
#define EXP_END_TIME             20           /* hour(0-24) session will stop (cannot be less that the start time)*/
#define SLEEP_TIME               30           /* night loop polling interval (in sec) */
#define DACSAMPLERATE            20000         /* stimulus sampling rate */  
#define DACBITDEPTH              16            /* stimulus bit depth */
#define STIMPATH                "/usr/local/stimuli/"

int starthour = EXP_START_TIME; 
int stophour = EXP_END_TIME;
int stopmin = 0;
int startmin = 0;
int sleep_interval = SLEEP_TIME;
char date_out[256];

const char exp_name[] = "RISKY_BIZ";

int box_id = -1;
int dual_hopper = 0;

/* trial block functions */
int foragingRisk(int block_num, int leftTime, int leftProb, int rightTime, int rightProb, int lowTime, int lowProb, int starttime, int stoptime, int period, char fstim[256]);

struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};

FILE *datafp = NULL;
char datafname[128], hour [16];
char timebuff[64], choice[128];

int trial_num, block_num, hopx, shunt_seed, shunt, iti;
int left = 0, right= 0, center = 0;

long feed_duration, loop;
double duration;

struct timeval time0, time1, dur;
time_t curr_tt, prev_tt;
struct tm * loctime;
    

/******************************
 *    main                    *
 ******************************/
int main(int ac, char *av[])
{
  if(DEBUG){printf("into main\n");}
  int initstate, leftTime, rightTime, leftProb, rightProb;
  char *pcm_name;
  int i, subjectid, starthour, startmin, stophour, stopmin;
  int dosunrise=0, dosunset=0,starttime,stoptime;
  int lowProb=100, midProb=100, highProb=100; /*default risk per response port (completely equal, always yields reward)*/
  int lowTime, midTime,highTime;
  int period;
  time_t curr_tt, rise_tt, set_tt;
  float latitude = 32.82, longitude = 117.14;
  char  timebuff[64],month[16],day[16], year[16], buffer[30],temphour[16],tempmin[16];
  char tonesf[1024];
  char fstim[256];
  
  sprintf(tonesf,"%s1000Hz.wav", STIMPATH);
  strcpy(fstim, tonesf); 
  srand ( time (0) );	
  
  /* Parse the command line */
  
  for (i = 1; i < ac; i++)
    {
      if (*av[i] == '-')
	{
	  if (strncmp(av[i], "-B", 2) == 0){ 
	    sscanf(av[++i], "%i", &box_id);
	    if(DEBUG){printf("box number = %d\n", box_id);}
	  }
	  else if (strncmp(av[i], "-S", 2) == 0){
	    sscanf(av[++i], "%i", &subjectid);
	    if(DEBUG){printf("subject ID number = %d\n", subjectid);}
	  }
      else if (strncmp(av[i], "-state", 4) == 0){
        sscanf(av[++i], "%i", &initstate);
	  }
      else if (strncmp(av[i], "-P", 2) == 0){
	    sscanf(av[++i], "%i:%i:%i", &lowProb, &midProb, &highProb);
	  }
      else if(strncmp(av[i], "-T", 2) == 0){
        sscanf(av[++i], "%i:%i:%i", &lowTime, &midTime, &highTime);
      }
	  else if (strncmp(av[i], "-on", 3) == 0)
	    sscanf(av[++i], "%i:%i", &starthour, &startmin);
	  else if (strncmp(av[i], "-off", 4) == 0)
	    sscanf(av[++i], "%i:%i", &stophour, &stopmin);
	  else if (strncmp(av[i], "-h", 2) == 0){
	    fprintf(stderr, "shape usage:\n");
	    fprintf(stderr, "    shape [-h] [-B x] [-T x] [-S x] \n\n");
	    fprintf(stderr, "     -h              = show this help message\n");
	    fprintf(stderr, "     -B int          = use '-B 1' '-B 2' ... '-B 12' for box 1-12\n");
	    fprintf(stderr, "     -S int          = specifies the subject ID number\n");
	    fprintf(stderr, "     -state int      = specifies whether the left perch is mid or high risk (1 or 2)\n");
        fprintf(stderr, "     -P int:int:int  = specifies probability of low, mid, high risk feeding\n");
	    fprintf(stderr, "     -T int:int:int  = CONSERVATIVE TIME IS HARD CODED TO 1.5 SECONDS REGARDLESS OF USER INPUT specifies time hopper is up for low, mid and high risk\n");
	    fprintf(stderr, "     -on int:int     = set hour:min for exp to start eg: '-on 7:35' (default is 7AM, use 99 for sunrise)\n");
	    fprintf(stderr, "     -off int:int    = set hour:min for exp to stop eg: '-off 19:01' (default is 7PM, use 99 for sunset)\n");
	    exit(-1);
	  }
	  else
	    {
	      fprintf(stderr, "Unknown option: %s\n", av[i]);  
	      fprintf(stderr, "Try 'shape -h' for help\n");
	    }
	}
    }
  
  /* check for errors and Initialize box */
  if (box_id < 0)
    {
      fprintf(stderr, "\tYou must enter a box ID!: %s \n", av[i]); 
      fprintf(stderr, "\tERROR: try '2choice -help' for help\n");
      exit(-1);
  } else{
    printf("Initializing box #%d...", box_id);
    
    sprintf(pcm_name, "dac%i", box_id);
    if((period=setup_pcmdev(pcm_name))<0){
    fprintf(stderr,"FAILED to set up the pcm device %s\n", pcm_name);
    exit (-1);}
                        
    
    if (operant_open()!=0){
      fprintf(stderr, "Problem opening IO interface\n");
      exit (-1);
    }
    operant_clear(box_id);
    printf("done\n");
  }
  

  fprintf(stderr, "Session started:\n"); 
  fprintf(stderr, "   Subject ID: %d\n", subjectid);
  fprintf(stderr, "          Box: %d\n", box_id);


  /*Open & setup data logging files */
  
  curr_tt = time(NULL);
  loctime = localtime (&curr_tt); 
  strftime (timebuff, 64, "%d%b%y", loctime);
  sprintf(datafname, "%i_%s.riskDAT", subjectid, timebuff);
  datafp = fopen(datafname, "a");

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
  if(DEBUG){fprintf(stderr, "starthour: %d\tstartmin: %d\tstophour: %d\tstopmin: %d\n", starthour,startmin,stophour,stopmin);}

        
  if (datafp==NULL) {
    fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", datafname);
    fclose(datafp);
    operant_close();
    exit(-1); }

  /* Write data file header info */
  fprintf (datafp, "File name: %s\n", datafname);
  fprintf (datafp, "Start time: %s", asctime(loctime));
  fprintf (datafp, "Subject ID: %d\n\n", subjectid);

    switch(initstate){
    case 1:
      leftTime = midTime;
      rightTime = highTime;
      leftProb = midProb;
      rightProb = highProb;
      break;
    case 2:
      leftTime = highTime;
      rightTime = midTime;
      leftProb = highProb;
      rightProb = midProb;
      break;
    default:
      printf("FATAL ERROR: unknown swap state %d!!\n\n", initstate);
      exit(-1);
  }
                                                          

  /*Run the appropriate shaping sequence */
  if(DEBUG){printf("start sequence list\n");}	
  
  block_num = 1;
  foragingRisk(block_num, leftTime, leftProb, rightTime, rightProb, lowTime, lowProb, starttime, stoptime, period, fstim);
   

  /*Cleanup */

  curr_tt = time(NULL);
  fclose(datafp);
  return 0;
}

/* foraging risk */
int foragingRisk(int block_num, int leftTime, int leftProb, int rightTime, int rightProb, int lowTime, int lowProb, int starttime, int stoptime, int period, char fstim[256]) { 
  int currtime;
  char hour[16], min[16];
  /* update current time */
  curr_tt = time(NULL);
  loctime = localtime (&curr_tt);
  strftime (hour, 16, "%H", loctime);
  strftime(min, 16, "%M", loctime);
  if (DEBUG){printf("hour:min at loop end: %d:%d \n", atoi(hour),atoi(min));}
  currtime=(atoi(hour)*60)+atoi(min);
  loctime = localtime (&curr_tt);
  if(DEBUG){printf("time: %s\n", ctime(&curr_tt));}
  strftime (hour, 16, "%H", loctime);
  strftime (date_out, 256, "%m%d", loctime);


  trial_num = 1;
  feed_duration = 1000000;
  fprintf(datafp,"Blk #\tTrl #\tCOND\tLEFT\tCENTER\tRIGHT\tFED?\tBLKOUT\tTIME\tDATE\n");
  
  operant_write(box_id, HOUSELT, 1);
  int condition = 0;
  //int chance = 0;
  int fed;
  int order[4] = {0,0,0,0};
  int toss = 1+(int)(1000.0*rand()/(RAND_MAX+1.0));
  if (toss < 500) {
    order[0] = 1;
    order[1] = 2;
    order[1] = 3;
    order[2] = 4;
  } else {
    order[0] = 1;
    order[1] = 2;
    order[1] = 4;
    order[2] = 3;
  }
  int dayTracker = -1; 
  do{
    dayTracker++;
    int currtime;
    char hour[16], min[16];
    /* update current time */
    curr_tt = time(NULL);
    loctime = localtime (&curr_tt);
    strftime (hour, 16, "%H", loctime);
    strftime(min, 16, "%M", loctime);
    if (DEBUG){printf("hour:min at loop end: %d:%d \n", atoi(hour),atoi(min));}
    currtime=(atoi(hour)*60)+atoi(min);
    //int hourMark = atoi(hour);
    //int hourMark = 7;
    loctime = localtime (&curr_tt);
    if(DEBUG){printf("time: %s\n", ctime(&curr_tt));}
    strftime (hour, 16, "%H", loctime);
    strftime (date_out, 256, "%m%d", loctime);
                               
    condition = 0;
    //chance = 1+(int)(3.0*rand()/(RAND_MAX+1.0));
    if (order[dayTracker] == 1) {
      condition = 1;
    } else if (order[dayTracker] == 2) {
      condition = 2;                                 
      do {
        curr_tt = time(NULL);
        loctime = localtime (&curr_tt);
        strftime(min, 16, "%M", loctime);
        strftime (hour, 16, "%H", loctime);
        if (atoi(min) % 5 == 0) {
         operant_write(box_id, FEEDER, 0);
        } else { operant_write(box_id, FEEDER, 1);}
         nanosleep(&rsi, NULL);
      //} while (atoi(hour) != hourMark);
      } while (atoi(hour) != 11);

    } else if (order[dayTracker] == 3) {
      condition = 3;
      do {
          curr_tt = time(NULL);
          loctime = localtime (&curr_tt);
          strftime(min, 16, "%M", loctime);
          strftime (hour, 16, "%H", loctime);   
      }  while (atoi(hour) != 11);
      
    }
    int lock = 0;
    do{
   
    if (atoi(min)%15 == 0 && lock == 0) {
      lock = 1;
      playwav(fstim, period);
    }
  
    loop = 0;
      operant_write (box_id, CTRKEYLT, 1);
      operant_write (box_id, LFTKEYLT, 1);
      operant_write (box_id, RGTKEYLT, 1);
      center = left = right = 0;
      do{
	nanosleep(&rsi, NULL);	               	       
	center = operant_read(box_id, CENTERPECK);
    left = operant_read(box_id, LEFTPECK);
    right = operant_read(box_id, RIGHTPECK);
	++loop;
	if ( loop % 25 == 0 ) {
	    if ( loop % 50 == 0 ){
          operant_write(box_id, CTRKEYLT, 1); 
          operant_write (box_id, LFTKEYLT, 1);
          operant_write (box_id, RGTKEYLT, 1);            
        } else {
        operant_write(box_id, CTRKEYLT, 0); 
        operant_write (box_id, LFTKEYLT, 0);
        operant_write (box_id, RGTKEYLT, 0);
        }
	    }
      } while (center == 0 && left == 0 && right == 0);   
      
      operant_write(box_id, CTRKEYLT, 0);
      operant_write (box_id, LFTKEYLT, 0);
      operant_write (box_id, RGTKEYLT, 0);
     
      /*raise a hopper */	
      if (center == 1) {
        int coin =  1+(int)(100.0*rand()/(RAND_MAX+1.0));
        if (coin < lowProb) {
          operant_write(box_id, FEEDER, 1);
          usleep(lowTime*feed_duration);
          operant_write(box_id, FEEDER, 0);
          usleep(40*feed_duration);
          fed = 1;
          //fprintf(stderr, "%d\t%d\t\n", coin, lowProb);
        } else {
          fed = 0;
          usleep(40*feed_duration);
        }
      }
     
     if (left == 1) {
     int coin = 1 + (int)( 100.0 * rand() / ( RAND_MAX + 1.0 ) );
     if (coin < leftProb) {
       operant_write(box_id, FEEDER, 1);
       usleep(leftTime*feed_duration);
       operant_write(box_id, FEEDER, 0);
       usleep(40*feed_duration);
       fed = 1;
     } else {
       usleep(40*feed_duration);
       fed = 0;
       }
     }
     else if (right == 1) {
      int coin = 1 + (int)( 100.0 * rand() / ( RAND_MAX + 1.0 ) );
      if (coin < rightProb) {
        operant_write(box_id, FEEDER, 1);
        usleep(rightTime*feed_duration);
        operant_write(box_id, FEEDER, 0);
        usleep(40*feed_duration);
        fed = 1;
      } else {
        usleep(40*feed_duration);
        fed = 0;
        }
      }
      
      /*output trial data */
      fprintf(datafp, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d:%d\t%s\n", block_num, trial_num, condition, left, center, right, fed, (lock-1),atoi(hour),atoi(min), date_out );
      fflush (datafp);
      sleep(iti);
      ++trial_num;

      /* update current time */
      curr_tt = time(NULL);
      loctime = localtime (&curr_tt);
      strftime (hour, 16, "%H", loctime);
      strftime(min, 16, "%M", loctime);
      if (DEBUG){printf("hour:min at loop end: %d:%d \n", atoi(hour),atoi(min));}
      currtime=(atoi(hour)*60)+atoi(min);
      loctime = localtime (&curr_tt);
      if(DEBUG){printf("time: %s\n", ctime(&curr_tt));} 
      strftime (hour, 16, "%H", loctime);	
      strftime (date_out, 256, "%m%d", loctime);
       
      if (lock  != 0) {
        lock++;
      }
        
      if (lock == 6) {
      lock = 0;
      operant_write(box_id, HOUSELT, 0);
      usleep(5*60*feed_duration);
      operant_write(box_id, HOUSELT, 1);
      }
    } while ((currtime>=starttime) && (currtime<stoptime));
      
    while ( (currtime<starttime) || (currtime>=stoptime) )
      {
	block_num++;
    operant_clear(box_id);
	sleep (sleep_interval);
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
	strftime (date_out, 256, "%m%d", loctime);    
    if (DEBUG){printf("hour:min at loop end: %d:%d \n", atoi(hour),atoi(min));}
	currtime=(atoi(hour)*60)+atoi(min);
      }  	     
    operant_write(box_id, HOUSELT, 1);
  } while (1);

  return(1);
}

