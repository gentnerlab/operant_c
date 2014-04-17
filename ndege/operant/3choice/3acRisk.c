#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <math.h>
#include <signal.h>
#include <sys/time.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include "/usr/local/src/operantio/operantio.c"
#include "/usr/local/src/audioio/audout.c"
#include <sunrise.h>

#define DEBUG 1
 
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
#define timersub(a, b, result)                                                \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                          \
    if ((result)->tv_usec < 0) {                                              \
      --(result)->tv_sec;                                                     \
      (result)->tv_usec += 1000000;                                           \
    }                                                                         \
  } while (0)


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
#define RESP_INT_SEC             2             /* seconds from simulus end until NORESP is registered  (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  1024          /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 256           /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define CUE_STIM_INTERVAL        100000000     /* input polling rate in nanoseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define TIMEOUT_DURATION         2000000       /* default duration of timeout in microseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define HOPPER_DROP_MS           300           /*time for hopper to fall before checking that it did */


long feed_duration = FEED_DURATION;
long timeout_duration = TIMEOUT_DURATION;
int  trial_max = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME; 
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval = SLEEP_TIME;
const char exp_name[] = "3AC";
int box_id = -1;
int flash = 0;
int xresp = 0;

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timespec isi = { 0, CUE_STIM_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};

typedef struct {
  int hopper_failures;
  int hopper_wont_go_down_failures;
  int hopper_already_up_failures;
  int response_failures;
} Failures; 


/* -------- Signal handling --------- */
int client_fd = -1;
static void sig_pipe(int signum)
{ fprintf(stdout,"SIGPIPE caught\n"); client_fd = -1;}
static void termination_handler (int signum){
  snd_pcm_close(handle);
  fprintf(stdout,"closed pcm device: term signal caught: exiting\n");
  exit(-1);
}

/*********************************************************************************
 * FEED FUNCTIONS 
 ********************************************************************************/
void doCheckedFeed(Failures *f)
{
  if(operant_read(box_id,HOPPEROP)!=0){
    printf("error -- hopper found in up position when it shouldn't be -- box %d\n",box_id);
    f->hopper_already_up_failures++;
  }
  operant_write(box_id, FEEDER, 1);
  usleep(feed_duration/2);
  if(operant_read(box_id, CENTERPECK)!=0){
    printf("WARNING -- suspicious center key peck detected during feeding -- box %d\n",box_id);
    f->response_failures++;
  }
  if(operant_read(box_id, HOPPEROP)!=1){
    printf("error -- hopper not raised when it should be -- box %d\n",box_id);
    f->hopper_failures++;
  }
  usleep(feed_duration/2);
  operant_write(box_id, FEEDER, 0);
  usleep(HOPPER_DROP_MS*1000);
  if(operant_read(box_id, HOPPEROP)!=0){
    printf("error -- hopper didn't come down when it should have -- box %d\n",box_id);
    f->hopper_wont_go_down_failures++;
  }
}

int feed(int rval, Failures *f)
{
  int feed_me=0;
  
  feed_me = 1+(int) (100.0*rand()/(RAND_MAX+1.0) ); 
  if(DEBUG){fprintf(stderr,"feed_me = %d\t rval = %d\n", feed_me, rval);}
  
  if (feed_me <= rval){
    doCheckedFeed(f);
    if(DEBUG){fprintf(stderr,"feed left\n");}
    return(1);
  }
  else{return (0);}
}

int probeRx(int rval, int pval, Failures *f)
{
 int outcome=0;

 outcome = 1+(int) (100.0*rand()/(RAND_MAX+1.0)); 
 if (outcome <= rval){
   /*do feed */
   doCheckedFeed(f);
   return(1);
 }
 else if (outcome <= (rval+pval)){
   /*do timeout*/
   operant_write(box_id, HOUSELT, 0);
   usleep(timeout_duration);
   operant_write(box_id, HOUSELT, 1);
   return(2);
 }
 else {
   /*do nothing*/
   return(0);
 }
}
 
/**********************************************************************
 **********************************************************************/
int timeout(int rval)
{
  int do_timeout=0;

  do_timeout = 1+(int) (100.0*rand()/(RAND_MAX+1.0) ); 

  if(DEBUG){fprintf(stderr,"do_timeout = %d\t rval=%d\n", do_timeout, rval);}
 
  if(do_timeout <= rval){
    operant_write(box_id, HOUSELT, 0);
    usleep(timeout_duration);
    operant_write(box_id, HOUSELT, 1);
    return (1);
  }
  else{return(0);}
}

/**********************************************************************
 **********************************************************************/
void do_usage()
{
  fprintf(stderr, "3ac usage:\n");
  fprintf(stderr, "     [-help] [-B int] [-R int] [-fx] [-t int] [-w int] [-on int] [-off int] [-S int] <filename>\n\n");
  fprintf(stderr, "        -help        = show this help message\n");
  fprintf(stderr, "        -B int       = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "        -f           = flash left & right pokelights during response window\n");
  fprintf(stderr, "        -t real      = set the timeout duration to 'x' secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -w int       = set the response window duration to 'x' secs (use an integer)\n");
  fprintf(stderr, "        -x           = use this flag to enable correction trials for 'no-response' trials\n");
  fprintf(stderr, "   -Q int:int:int    = set the probabilities for trials with no cues, valid cues, and invalid cues\n");
  fprintf(stderr, "                      e.g: 40:40:20 to present invalid cues on 20%% of trials and cues on the remaning half\n");   
  fprintf(stderr, "        -on int:int      = set hour:min for exp to start eg: '-on 7:35' (default is 7AM, use 99 for sunrise)\n");
  fprintf(stderr, "        -off int:int     = set hour:min for exp to stop eg: '-off 19:01' (default is 7PM, use 99 for sunset)\n");
  fprintf(stderr, "        -state int     = start state is either 1 or 2 (high risk begins on Left or Right)\n");
  fprintf(stderr, "        -S int       = specify the subject ID number (required)\n");
  fprintf(stderr, "        filename     = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "        -lag         = specify lag range (e.g. 800:1800 for minlag 800ms, maxlag 1800ms)\n");
  fprintf(stderr, "        -splT        = specify sound level range (e.g. 45.0:75.0 for min 45.0 dB, max 75.0 dB) \n");
  fprintf(stderr, "                       where each line is: 'Class' 'Wavfile' 'Cue' 'Present_freq' 'Rnf_rate 'Pun_rate''\n");
  fprintf(stderr, "                       'Class = -1 for nogo; 0 for CENTER-key, 1 for LEFT-key, 2 for RIGHT-key assignment \n");
  fprintf(stderr, "                       'Wavfile' is the name of the stimulus soundfile (must be 44100 Hz sample rate\n");
  fprintf(stderr, "                       'Presfreq' is the overall rate (compared to the other stimuli) at which the stimulus is presented. \n"); 
  fprintf(stderr, "                         The actual integer rate for each stimulus is that value divided by the sum for all stimuli.\n");
  fprintf(stderr, "                         Use '1' for equal probablility \n"); 
  fprintf(stderr, "                       'Cue' stimulus associated with this stimulus exemplar\n");         
  fprintf(stderr, "                       'Rnf_rate' percentage of time that food is available following correct responses to this stimulus.\n");
  fprintf(stderr, "                       'Pun_rate' percentage of time that a timeout follows incorrect responses to this stimulus.\n"); 
  exit(-1);
}

/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *lowTime, int *midTime, int *highTime, int *lowProb, int *midProb, int *highProb, int *initstate)
{
  int i=0;
  
  for (i = 1; i < argc; i++){
    if (*argv[i] == '-'){
      if (strncmp(argv[i], "-B", 2) == 0) 
        sscanf(argv[++i], "%i", box_id);
      else if (strncmp(argv[i], "-S", 2) == 0)
        sscanf(argv[++i], "%i", subjectid);
      else if (strncmp(argv[i], "-on", 3) == 0)
        sscanf(argv[++i], "%i:%i", starthour, startmin);
      else if (strncmp(argv[i], "-off", 4) == 0)
        sscanf(argv[++i], "%i:%i", stophour, stopmin);
      else if (strncmp(argv[i], "-P", 2) == 0)
        sscanf(argv[++i], "%i:%i:%i", lowProb, midProb, highProb);
      else if (strncmp(argv[i], "-T", 2) == 0)
        sscanf(argv[++i], "%i:%i:%i", lowTime, midTime, highTime);
      else if (strncmp(argv[i], "-state", 6) == 0)
        sscanf(argv[++i], "%i", initstate);
      else if (strncmp(argv[i], "-help", 5) == 0){
        do_usage();
      }
      else{
        fprintf(stderr, "Unknown option: %s\t", argv[i]);
        fprintf(stderr, "Try '3ac -help'\n");
      }
    }
  }
  return 1;
}


/**********************************************************************
 MAIN
**********************************************************************/	
int main(int argc, char *argv[]){

  //FILE *stimfp = NULL, 
  FILE *datafp = NULL, *dsumfp = NULL;
  //char *stimfname = NULL;
  //char *stimfroot;
  //const char noq[] = "NOCUE             ";
  const char delimiters[] = " .,;:!-";
  int lowTime, midTime, highTime, lowProb, midProb, highProb;
  int leftTime = 0;
  int rightTime = 0;
  int leftProb, rightProb;
  char datafname[128], hour[16], min[16], month[16],day[16], year[16], dsumfname[128], stimftemp[128], pcm_name[128];
  char  buf[128],cuexm[128],stimexm[128],fstim[256],timebuff[64],tod[256],date_out[256],buffer[30],temphour[16],tempmin[16], cstim[128];
  int nclasses, nstims, stim_class, stim_reinf, stim_punish, resp_sel, resp_acc, subjectid, period, tot_trial_num, 
    resp_wind=0,trial_num, session_num, i,j,k, correction,playval,stim_number,minlag,maxlag,qcond,cueval,
    *playlist=NULL,totnstims=0,dosunrise=0,dosunset=0,starttime,stoptime,currtime,classtest,doprobes,class_idx,doq,
    wn_spl,wn_dur,UBlag,onsetlag,initstate = -1;
  float timeout_val=0.0, resp_rxt=0.0, UBsplT=0.0, spl_target=0.0, minsplT=0.0, maxsplT=0.0;
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct tm *loctime;
  Failures f = {0,0,0,0};
  int left = 0, right= 0, center = 0, fed = 0;
  int reinfor_sum = 0, reinfor = 0;
  int ncue_cond = 3;
  int max_spl_bins = 40;
  int nspl_bins = 13;
  int cue_cond=0;
  float out_minDB=45.0;
  float out_maxDB=75.0;
  float probeCO=68.0;
  int spl_bin = ceil((out_maxDB-out_minDB)/nspl_bins);
  int low_bin, high_bin;
  int cue_code, stim_code;
  char cue_lbl[8], stim_lbl[8], bin_lbl[16];
  struct stim {
    char exemplar[128];
    int class;
    int reinf;
    int punish;
    int freq;
    char cue[128];
    unsigned long int dur; 
    int num;
  }stimulus[MAXSTIM];
  struct resp{
    int C;
    int X;
    int no;
    int left;
    int center;
    int right;
    int count;
  }Rstim[MAXSTIM], Tstim[MAXSTIM], Rclass[MAXCLASS], Tclass[MAXCLASS], RclassQ[MAXSTIM*ncue_cond], TclassQ[MAXSTIM*ncue_cond], RsplT[max_spl_bins+1], TsplT[max_spl_bins+1] ;

  sigset_t trial_mask;
  
  srand (time (0) );
  
  /* set up termination handler*/
  if (signal(SIGPIPE, sig_pipe) == SIG_ERR){
    perror("error installing signal handler for SIG_PIPE");
    exit (-1);
  }
  
  sigemptyset (&trial_mask);
  sigaddset (&trial_mask, SIGINT);
  sigaddset (&trial_mask, SIGTERM);
  signal(SIGTERM, termination_handler);
  signal(SIGINT, termination_handler);
  

  /* Parse the command line */
  command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &lowProb, &midProb, &highProb, &lowTime, &midTime, &highTime, &initstate); 
  sprintf(pcm_name, "dac%i", box_id);
  if(DEBUG){fprintf(stderr,"dac: %s\n",pcm_name);}
  
  if(DEBUG){fprintf(stderr,"commandline done, now checking for errors\n");}
  
  /* watch for terminal errors*/
  if( (stophour!=99) && (starthour !=99) ){
    if ((stophour <= starthour) && (stopmin<=startmin)){
      fprintf(stderr, "\tTERMINAL ERROR: exp start-time must be greater than stop-time\n");
      exit(-1);
    } 
  }
  if (box_id <= 0){
    fprintf(stderr, "\tYou must enter a box ID!\n"); 
    fprintf(stderr, "\tERROR: try '3ac -help' for available options\n");
    exit(-1);
  }
  
  /* set some variables as needed*/
  if (resp_wind>0)
    respoff.tv_sec = resp_wind;
  fprintf(stderr, "response window duration set to %d secs\n", (int) respoff.tv_sec);	
  if(timeout_val>0.0)
    timeout_duration = (int) (timeout_val*1000000);
  fprintf(stderr, "timeout duration set to %d microsecs\n", (int) timeout_duration);
  
  if(DEBUG){fprintf(stderr, "starthour: %d\tstartmin: %d\tstophour: %d\tstopmin: %d\n", starthour,startmin,stophour,stopmin);}

  operant_write (box_id, HOUSELT, 1); 

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
  if(DEBUG){fprintf(stderr, "starthour: %d\tstartmin: %d\tstophour: %d\tstopmin: %d\n", starthour,startmin,stophour,stopmin);}
  
  /* Initialize box */
  if(DEBUG){
    printf("Initializing box %d ...\n", box_id);
    printf("trying to execute setup(%s)\n", pcm_name);
  }
  if((period= setup_pcmdev(pcm_name))<0){
    fprintf(stderr,"FAILED to set up the pcm device %s\n", pcm_name);
    exit (-1);}
  if (operant_open()!=0){
    fprintf(stderr, "Problem opening IO interface\n");
    snd_pcm_close(handle);
    exit (-1);
  }
  operant_clear(box_id);
  if(DEBUG){printf("done\n");}
  
  /* give user some feedback*/
  fprintf(stderr, "Subject ID number: %i\n", subjectid);
  if(flash){fprintf(stderr, "!!WARNING: Flashing keylights during response window!!\n");}
  if(xresp){fprintf(stderr, "!!WARNING: Enabling correction trials for 'No' responses !!\n");}
  /*  Open & setup data logging files */
  curr_tt = time (NULL);
  loctime = localtime (&curr_tt);
  strftime (timebuff, 64, "%d%b%y", loctime);
  //sprintf (stimftemp, "%s", stimfname);
  //stimfroot = strtok (stftemp, delimiters); 
  sprintf(datafname, "%i_%s.3ac_rDAT", subjectid, "risk");
  sprintf(dsumfname, "%i.summaryDAT", subjectid);
  datafp = fopen(datafname, "a");
  dsumfp = fopen(dsumfname, "w");
  
  if ( (datafp==NULL) || (dsumfp==NULL) ){
    fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", datafname);
    snd_pcm_close(handle);
    fclose(datafp);
    fclose(dsumfp);
    exit(-1);
  }
  
  /* Write data file header info */
  fprintf (stderr,"Data output to '%s'\n", datafname);
  fprintf (datafp, "File name: %s\n", datafname);
  fprintf (datafp, "Procedure source: %s\n", exp_name);
  fprintf (datafp, "Start time: %s", asctime(loctime));
  fprintf (datafp, "Subject ID: %d\n", subjectid);
  //fprintf (datafp, "Stimulus source: %s\n", stimfname);  
  fprintf (datafp, "Sess#\tTrl#\tType\tStimulus\t\tCue\t\t\tClass\tsplT\tR_sel\tR_acc\tRT\tReinf\tTOD\tDate\n");
  
  /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
  ********************************************/
  session_num = 1;
  trial_num = 0;
  tot_trial_num = 0;
  correction = 1;
	
  /*zero out the response tallies */
  curr_tt = time(NULL);
  loctime = localtime (&curr_tt);
  strftime (hour, 16, "%H", loctime);
  strftime(min, 16, "%M", loctime);
  if (DEBUG){printf("hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
  currtime=(atoi(hour)*60)+atoi(min);
	
  operant_write (box_id, HOUSELT, 1);        /* make sure houselight is on */

  do{                                                                               /* start the main loop */
    while ((currtime>=starttime) && (currtime<stoptime)){                          /* start main trial loop */

      
      
      do{                                             /* start correction trial loop */
	left = right = center = 0;                    /* zero trial peckcounts       */
	resp_sel = resp_acc = resp_rxt = 0;           /* zero trial variables        */
	++trial_num;++tot_trial_num;
	
	/* Wait for center key press */
	if (DEBUG){printf("\n\nWaiting for left or right key press\n");}
	operant_write (box_id, HOUSELT, 1);        /* house light on */
	long flasher = 0;
    center = left = right = 0;
	  do{                                         
	    nanosleep(&rsi, NULL);	               	       
	    center = operant_read(box_id, CENTERPECK);   /*get value at center response port*/	
	    left = operant_read(box_id, LEFTPECK);       /*get value at left response port*/
        right = operant_read(box_id, RIGHTPECK);     /*get value at right response port*/
        flasher++; 
        if (flasher % 320 == 0) { 
          operant_write (box_id, LFTKEYLT, 1);
          operant_write (box_id, RGTKEYLT, 1);
          operant_write (box_id, CTRKEYLT, 1);
        } else {
          operant_write (box_id, LFTKEYLT, 0);
          operant_write (box_id, RGTKEYLT, 0);
          operant_write (box_id, CTRKEYLT,0);
        }
      } while (center == 0 && left == 0 && right == 0);  

	sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	
	operant_write (box_id, LFTKEYLT, 0);    /*make sure the key lights are off after resp interval*/
	operant_write (box_id, RGTKEYLT, 0);
	operant_write (box_id, CTRKEYLT, 0);

	strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
	strftime (min, 16, "%M", loctime);
	strftime (month, 16, "%m", loctime);
	strftime (day, 16, "%d", loctime);

    /* determine consequences */

    if (center == 1) {
      int coin = 1+(int) (100*rand()/(RAND_MAX+1.0) );
        if (coin < lowProb) {
          operant_write(box_id, FEEDER, 1);
          usleep(lowTime);      
          operant_write(box_id, FEEDER, 0);
        } //else { usleep(200000);}
    } else if (left == 1) {
        int coin = 1+(int) (100*rand()/(RAND_MAX+1.0) );
        if (coin < leftProb) {
          operant_write(box_id, FEEDER, 1);
          usleep(leftTime);
          operant_write(box_id, FEEDER, 0);
        } else { usleep(20000000);}
    } else if (right == 1) {
          int coin = 1+(int) (100*rand()/(RAND_MAX+1.0) );
          if (coin < rightProb) {
            operant_write(box_id, FEEDER, 1);
            usleep(rightTime);
            operant_write(box_id, FEEDER, 0);
          } else { usleep(20000000);}
    }


	/* Pause for ITI */
	operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	nanosleep(&iti, NULL);                                   /* wait intertrial interval */
                                        
                   
	/* Write trial data to output file */
	strftime (tod, 256, "%H%M", loctime);
	strftime (date_out, 256, "%m%d", loctime);
	fprintf(datafp, "%d\t%d\t%d\t%d\t%d\t%s\t%s\n", session_num, trial_num, 
		left, center, right, tod, date_out);
	fflush (datafp);
    }
	
	    
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
	currtime=(atoi(hour)*60)+atoi(min);
	if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
	    
      }while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); /* correction trial loop */
	    
    }                                                                  /* trial loop */
	  
    /* You'll end up here if its night time or if you skip a day 
       The latter only happens if the night window elapses between the time a stimulus is chosen
       and the time its actually played--i.e. if the subject doesn't initiates a cued trial during 
       the night window, which is rare
    */

	  
    /* Loop while lights out */
    if (DEBUG){printf("minutes since midnight: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
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
      if (DEBUG){printf("minutes since midnight: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
    }
    operant_write(box_id, HOUSELT, 1);
	
    /*reset some vars for the new day */
    ++session_num;                                                                     
    trial_num = 0;
    for(i = 0; i<nstims;++i){
      Rstim[i].C = Rstim[i].X = Rstim[i].no = Rstim[i].count = Rstim[i].left = Rstim[i].center = Rstim[i].right = 0;
    }
    for(i=1;i<=nclasses;i++){
      Rclass[i].C = Rclass[i].X = Rclass[i].no = Rclass[i].count = Rclass[i].left = Rclass[i].center = Rclass[i].right = 0;
    }
    for(i=0;i<(nclasses*ncue_cond);i++){
      RclassQ[i].C = RclassQ[i].X = RclassQ[i].no = RclassQ[i].count = RclassQ[i].left = RclassQ[i].center = RclassQ[i].right = 0;
    }
    for (i = 0; i<nclasses*ncue_cond;++i){
      RsplT[i].C = RsplT[i].X = RsplT[i].no = RsplT[i].count = RsplT[i].left = RsplT[i].center = RsplT[i].right = 0;
    }




    f.hopper_wont_go_down_failures = f.hopper_already_up_failures = f.hopper_failures =f.response_failures = fed = reinfor_sum = 0;
	  
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
      if(DEBUG){fprintf(stderr,"Sessions start at sunrise(Today: '%s')\n",buffer);}
    }
    if(dosunset){
      set_tt = sunset(atoi(year), atoi(month), atoi(day), latitude, longitude);
      strftime(temphour, 16, "%H", localtime(&set_tt));
      strftime(tempmin, 16, "%M", localtime(&set_tt));
      stophour=atoi(temphour);
      stopmin=atoi(tempmin);
      strftime(buffer,30,"%m-%d-%Y  %T",localtime(&set_tt));
      if(DEBUG){fprintf(stderr,"Session stop at sunset(Today: '%s')\n",buffer);}
    }
	  
    if(DEBUG){fprintf(stderr, "starthour: %d\tstartmin: %d\tstophour: %d\tstopmin: %d\n", starthour,startmin,stophour,stopmin);}

	  
	 	  
  }while (1);/*main trial loop*/
  
  
  /*  Cleanup */
  fclose(datafp);
  fclose(dsumfp);
  free(playlist);
  return 1;
}

