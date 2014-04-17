// added optional cue lights ec 11/8/07 // 
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
#define GREENLT   6
#define BLUELT    7
#define REDLT     8


/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             2             /* seconds from simulus end until NORESP is registered  (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  1024          /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 256            /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define TIMEOUT_DURATION         2000000       /* default duration of timeout in microseconds */
#define CUE_DURATION             1000000       /* cue duration in microseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define HOPPER_DROP_MS           300           /*time for hopper to fall before checking that it did */


long feed_duration = FEED_DURATION;
long timeout_duration = TIMEOUT_DURATION;
long cue_duration = CUE_DURATION;
int  trial_max = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME; 
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval = SLEEP_TIME;
const char exp_name[] = "2AC";
int box_id = -1;
int flash = 0;
int xresp = 0;
int cueflag = 0;

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
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
  fprintf(stderr, "2ac usage:\n");
  fprintf(stderr, "     [-help] [-B int] [-R int] [-fx] [-t int] [-w int] [-on int] [-off int] [-S int] <filename>\n\n");
  fprintf(stderr, "        -help        = show this help message\n");
  fprintf(stderr, "        -B int       = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "        -f           = flash left & right pokelights during response window\n");
  fprintf(stderr, "        -t 'x'       = set the timeout duration to 'x' secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -w 'x'       = set the response window duration to 'x' secs (use an integer)\n");
  fprintf(stderr, "        -x           = use this flag to enable correction trials for 'no-response' trials,\n");
  fprintf(stderr, "        -q           = use this flag to enable cue lights. will only work in appropriately modified boxes. Class 1 = red, class 2 = green\n");
  fprintf(stderr, "        -n           = use this flag to change the number of times a target stimuli is repeated\n");
  fprintf(stderr, "        -on int:int      = set hour:min for exp to start eg: '-on 7:35' (default is 7AM, use 99 for sunrise)\n");
  fprintf(stderr, "        -off int:int     = set hour:min for exp to stop eg: '-off 19:01' (default is 7PM, use 99 for sunset)\n");
  fprintf(stderr, "        -S int       = specify the subject ID number (required)\n");
  fprintf(stderr, "        -td f:f      = specify minimum and maximum trial duration in seconds(float:float)\n");
  fprintf(stderr, "        -dd f        = specify distracter duration in seconds (float)\n");  
  fprintf(stderr, "        filename     = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "                       where each line is: 'Wavfile' 'Present_freq' 'Rnf_rate 'Pun_rate''\n");
  fprintf(stderr, "                       'NO 'Class' ASSIGNMENT REQUIRED \n");
  fprintf(stderr, "                       'Wavfile' is the name of the stimulus soundfile (must be 44100 Hz sample rate\n");
  fprintf(stderr, "                       'Presfreq' is the overall rate (compared to the other stimuli) at which the stimulus is presented. \n"); 
  fprintf(stderr, "                         The actual integer rate for each stimulus is that value divided by the sum for all stimuli.\n");
  fprintf(stderr, "                         Use '1' for equal probablility \n"); 
  fprintf(stderr, "                       'Rnf_rate' percentage of time that food is available following correct responses to this stimulus.\n");
  fprintf(stderr, "                       'Pun_rate' percentage of time that a timeout follows incorrect responses to this stimulus.\n"); 
  exit(-1);
}

/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *resp_wind, float *timeout_val, float *mintdur, float *maxtdur, float *distdur, int *new_trialtarg, char **stimfname)
{
  int i=0;
  
  for (i = 1; i < argc; i++){
    if (*argv[i] == '-'){
      if (strncmp(argv[i], "-B", 2) == 0) 
        sscanf(argv[++i], "%i", box_id);
      else if (strncmp(argv[i], "-S", 2) == 0)
        sscanf(argv[++i], "%i", subjectid);
      else if (strncmp(argv[i], "-x", 2) == 0)
	xresp = 1;
      else if (strncmp(argv[i], "-w", 2) == 0){
	sscanf(argv[++i], "%i", resp_wind);
      }
      else if (strncmp(argv[i], "-t", 2) == 0){ 
	sscanf(argv[++i], "%f", timeout_val);
      }
      else if (strncmp(argv[i], "-f", 2) == 0)
	flash = 1;
      else if (strncmp(argv[i], "-n", 2) == 0)
	sscanf(argv[++i], "%i", new_trialtarg);
      else if (strncmp(argv[i], "-q", 2) == 0)
	cueflag = 1;
      else if (strncmp(argv[i], "-on", 3) == 0)
        sscanf(argv[++i], "%i:%i", starthour, startmin);
      else if (strncmp(argv[i], "-off", 4) == 0)
        sscanf(argv[++i], "%i:%i", stophour, stopmin);
      else if (strncmp(argv[i], "-td", 3) == 0)
	sscanf(argv[++i], "%f:%f", mintdur, maxtdur);
      else if (strncmp(argv[i], "-dd", 3) == 0)
	sscanf(argv[++i], "%f", distdur);
      else if (strncmp(argv[i], "-help", 5) == 0){
        do_usage();
      }
      else{
        fprintf(stderr, "Unknown option: %s\t", argv[i]);
        fprintf(stderr, "Try '2ac -help'\n");
      }
    }
    else
      {
        *stimfname = argv[i];
      }
  }
  return 1;
}

/**********************************************************************
 MAIN
**********************************************************************/	
int main(int argc, char *argv[])
{
  FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
  char *stimfname = NULL;
  char *stimfroot;
  const char delimiters[] = " .,;:!-";
  char datafname[128], hour[16], min[16], month[16],day[16], year[16], dsumfname[128], stimftemp[128], pcm_name[128];
  char  buf[128], stimexm[128],distexm[128], dstim[256], fstim[256], timebuff[64], tod[256], date_out[256], buffer[30],temphour[16],tempmin[16];
  int nclasses, nstims, stim_class, stim_reinf, stim_punish, resp_sel, resp_acc, subjectid, period, tot_trial_num, ntargtrials = 100,numtrials, 
    played, resp_wind=0,trial_num, session_num, i,j,k, correction, targval,distval, loop, stim_number, dist_number, cue[3], 
    *playlist=NULL, totnstims=0, mirror=0, dosunrise=0,dosunset=0,starttime,stoptime,currtime, new_trialtarg = 0;
  float timeout_val=0.0, resp_rxt=0.0, distdur = 2.0, trialdur, maxtdur = 10.0, mintdur = 5.0;
  int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval stimoff, resp_window, resp_lag, resp_rt;
  struct tm *loctime;
  Failures f = {0,0,0,0};
  int left = 0, right= 0, center = 0, fed = 0;
  int reinfor_sum = 0, reinfor = 0;
  struct stim{
    char exemplar[128];
    int class;
    int reinf;
    int punish;
    int freq;
    unsigned long int dur; 
    int num;
  }stimulus[MAXSTIM];

  struct resp{
    int C;
    int X;
    int N;
    int count;
  }Rstim[MAXSTIM], Tstim[MAXSTIM], Rclass[MAXCLASS], Tclass[MAXCLASS];
  sigset_t trial_mask;
  cue[1]=REDLT; cue[2]=GREENLT; cue[3]=BLUELT;  
							      
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
  command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &resp_wind, &timeout_val, &mintdur, &maxtdur, &distdur, &new_trialtarg, &stimfname); 
  if(DEBUG){
    fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, xresp=%d, cue=%d, resp_wind=%d, timeout_val=%f, flash=%d, mintdur=%f, maxtdur=%f, distdur= %f, trialtarg= %d, stimfile: %s\n",box_id, subjectid, starthour, stophour, startmin, stopmin, xresp, cueflag, resp_wind, timeout_val, flash, mintdur, maxtdur, distdur, new_trialtarg, stimfname);
  }
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
    fprintf(stderr, "\tERROR: try '2ac -help' for available options\n");
    exit(-1);
  }

  /*set some variables as needed*/
  if (resp_wind>0)
    respoff.tv_sec = resp_wind;
  fprintf(stderr, "response window duration set to %d secs\n", (int) respoff.tv_sec);	
  if(timeout_val>0.0)
    timeout_duration = (int) (timeout_val*1000000);
  fprintf(stderr, "timeout duration set to %d microsecs\n", (int) timeout_duration);
  if (new_trialtarg > 0)
    ntargtrials = new_trialtarg;
  fprintf(stderr, "timeout duration set to %d microsecs\n", (int) timeout_duration);
	
  if(DEBUG){fprintf(stderr, "starthour: %d\tstartmin: %d\tstophour: %d\tstopmin: %d\n", starthour,startmin,stophour,stopmin);}
	
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
  fprintf(stderr, "Loading stimuli from file '%s' for session in box '%d' \n", stimfname, box_id); 
  fprintf(stderr, "Subject ID number: %i\n", subjectid);
  if(flash){fprintf(stderr, "!!WARNING: Flashing keylights during response window!!\n");}
  if(xresp){fprintf(stderr, "!!WARNING: Enabling correction trials for 'No' responses !!\n");}
	
  /* Read in the list of exmplars */
  nclasses = 2;
  nstims = 0;
  if ((stimfp = fopen(stimfname, "r")) != NULL){
    while (fgets(buf, sizeof(buf), stimfp))
      nstims++;
    fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
    rewind(stimfp);
	                
    for (i = 0; i < nstims; i++){
      fgets(buf, 128, stimfp);
      /* stimulus[i].freq = stimulus[i].reinf = 0;*/
      sscanf(buf, "%s\%d\%d\%d", stimulus[i].exemplar, &stimulus[i].freq, &stimulus[i].reinf, &stimulus[i].punish);
      if((stimulus[i].freq==0) || (stimulus[i].reinf==0)|| (stimulus[i].punish==0)){
	printf("ERROR: insufficent data or bad format in '.stim' file. Try '2acstreamchg -help'\n");
	exit(0);} 
      totnstims += stimulus[i].freq;
      if(DEBUG){printf("totnstims: %d\n", totnstims);}
	    
      /*check the reinforcement rates */
      fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct responses\n", 
	      stimulus[i].exemplar, stimulus[i].reinf);
      fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect responses\n", 
	      stimulus[i].exemplar, stimulus[i].punish);
      if((mirror==1)){
	if (stimulus[i].reinf>50){
	  fprintf(stderr, "ERROR!: To mirror food and timeout reinforcement values you must use a base rate less than or equal to 50%%\n");
	  snd_pcm_close(handle);
	  exit(0);
	}
	else{
	  fprintf(stderr, "p(food) reward and p(timeout) on probe trials with %s is set to %d%%\n", stimulus[i].exemplar, stimulus[i].reinf );
	}
      }
      else{fprintf(stderr, "Reinforcement rate on probe trials is set to %d%% pct for correct GO responses, 100%% for incorrect GO responses\n", 
		   stimulus[i].reinf);
      }
	    
    }/* no set up for probe trials */
  }
  else 
    {
      fprintf(stderr,"Error opening stimulus input file! Try '2acstreamchg_train -help' for proper file formatting.\n");  
      snd_pcm_close(handle);
      exit(0);     
    }
  fclose(stimfp);
  if(DEBUG){printf("Done reading in stims; %d stims found\n", nstims);}

  /* need to copy stimulus values to stimptr pointees? */
  /* stimptr = &stimulus;*/

  /* Don't allow correction trials when probe stimuli are presented */
  if(xresp==1 && nclasses>2){
    fprintf(stderr, "ERROR!: You cannot use correction trials and probe stimuli in the same session.\n  Exiting now\n");
    snd_pcm_close(handle);
    exit(-1);
  }
	

  /* build the stimulus playlists */
  if(DEBUG){printf("flag: making the playlist\n");}
  free(playlist);
  playlist = malloc( (totnstims+1)*sizeof(int) );
  i=j=0;
  for (i=0;i<nstims; i++){
    k=0;
    for(k=0;k<stimulus[i].freq;k++){
      playlist[j]=i;
      if(DEBUG){printf("value for playlist entry '%d' is '%d'\n", j, i);}
      j++;
    }
  }
  if(DEBUG){printf("there are %d stims in the playlist\n", totnstims);}
        

  /*  Open & setup data logging files */
  curr_tt = time (NULL);
  loctime = localtime (&curr_tt);
  strftime (timebuff, 64, "%d%b%y", loctime);
  sprintf (stimftemp, "%s", stimfname);
  stimfroot = strtok (stimftemp, delimiters); 
  sprintf(datafname, "%i_%s.SCtrain_rDAT", subjectid, stimfroot);
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
  fprintf (datafp, "Stimulus source: %s\n", stimfname);  
  fprintf (datafp, "Sess#\tTrl#\tType\tStim\t\tDist\t\tT_class\tR_sel\tR_acc\tRT\tTdur\tDdur\tReinf\tTOD\tDate\n");
	 
  /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
  ********************************************/
  session_num = 1;
  trial_num = 0;
  tot_trial_num = 0;
  correction = 1;

  /*zero out the response tallies */
  for(i = 0; i<nstims;++i){
    Rstim[i].C = Rstim[i].X = Rstim[i].N = Rstim[i].count =0;
    Tstim[i].C = Tstim[i].X = Tstim[i].N = Tstim[i].count = 0;
  }
  for(i=1;i<=nclasses;i++){
    Rclass[i].C = Rclass[i].X = Rclass[i].N = Rclass[i].count = 0;
    Tclass[i].C = Tclass[i].X = Tclass[i].N = Tclass[i].count = 0;
  }

  curr_tt = time(NULL);
  loctime = localtime (&curr_tt);
  strftime (hour, 16, "%H", loctime);
  strftime(min, 16, "%M", loctime);
  if (DEBUG){printf("hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
  currtime=(atoi(hour)*60)+atoi(min);
	
  operant_write (box_id, HOUSELT, 1);        /* make sure houselight is on */

  do{                                                                               /* start the main loop */
    while ((currtime>=starttime) && (currtime<stoptime)){                          /* start main trial loop */
      if (DEBUG){printf("minutes since midnight at loop start: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
      srand(time(0));


      /* select target stim exemplar at random */ 
      targval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));           
      if (DEBUG){printf(" %d\t", targval);}
      stim_number = playlist[targval];
      strcpy (stimexm, stimulus[stim_number].exemplar);                       /* get exemplar filename */
      stim_reinf = stimulus[stim_number].reinf;
      stim_punish = stimulus[stim_number].punish;
      sprintf(fstim,"%s%s", STIMPATH, stimexm);                                /* add full path to file name */

    
      numtrials = 0;
      while (numtrials < ntargtrials){
	numtrials ++;
	
	/* select distracter stim exemplar at random */ 
	dist_number = stim_number;                            
	while(dist_number == stim_number){
	  distval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));           
	  dist_number = playlist[distval];
	}
	strcpy (distexm, stimulus[dist_number].exemplar);                       /* get exemplar filename */   
	sprintf(dstim,"%s%s", STIMPATH, distexm);                                /* add full path to file name */
	
	/* determine trial duration */
	trialdur = (float)(((maxtdur-mintdur+0.0)*rand()/(RAND_MAX + 0.0))+(mintdur)); /* rand goes 0-1 */
	    
	/* determine trial class*/
	stim_class = (int) (2.0*(rand()/(RAND_MAX + 0.0)))+ 1;
	
	if (DEBUG){printf("trialdur: %.4f, stim_class = %d\n", trialdur, stim_class);}
	    
	do{                                             /* start correction trial loop */
	  left = right = center = 0;        /* zero trial peckcounts */
	  resp_sel = resp_acc = resp_rxt = 0;                 /* zero trial variables        */
	  ++trial_num;++tot_trial_num;
	    
	  /* Wait for center key press */
	  if (DEBUG){printf("\n\nWaiting for center key press\n");}
	  operant_write (box_id, HOUSELT, 1);        /* house light on */
	  right=left=center=0;
	  do{                                         
	    nanosleep(&rsi, NULL);	               	       
	    center = operant_read(box_id, CENTERPECK);   /*get value at center response port*/	
	  }while (center==0);  

	  sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	  /* if cueflag, turn on cue light */
	  if(cueflag){
	    operant_write(box_id, cue[stim_class], 1);
	    usleep(cue_duration);	  
	    if (DEBUG){fprintf(stderr,"displaying cue light for class %d\n", stim_class);}	 
	    operant_write(box_id, cue[stim_class], 0);
	  }
	  /* play the stimulus*/
	  if (DEBUG){printf("START '%s'\n", stimexm);}
	  if ((played = playstreamchg_train(fstim, dstim, period, trialdur, distdur, stim_class ))!=1){ /* send target, distracters, length of trial, distracter volume */
	    fprintf(stderr, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		    pcm_name, stimexm, asctime(localtime (&curr_tt)) );
	    fprintf(datafp, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		    pcm_name, stimexm, asctime(localtime (&curr_tt)) );
	    fclose(datafp);
	    fclose(dsumfp);
	    exit(-1);
	  }
	  if (DEBUG){printf("STOP  '%s', played = %d\n", stimexm, played);}
	  gettimeofday(&stimoff, NULL);
	  if (DEBUG){
	    stimoff_sec = stimoff.tv_sec;
	    stimoff_usec = stimoff.tv_usec;
	    printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);}
	    
	  /* Wait for response */
	  if (DEBUG){printf("flag: waiting for right/left response\n");}
	  timeradd (&stimoff, &respoff, &resp_window);
	  if (DEBUG){respwin_sec = resp_window.tv_sec;}
	  if (DEBUG){respwin_usec = resp_window.tv_usec;}
	  if (DEBUG){printf("resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);}
                    
	  loop=left=right=0;
	  do{
	    nanosleep(&rsi, NULL);
	    left = operant_read(box_id, LEFTPECK);
	    right = operant_read(box_id, RIGHTPECK );
	    if((left==0) && (right==0) && flash){
	      ++loop;
	      if(loop%80==0){
		if(loop%160==0){ 
		  operant_write (box_id, LFTKEYLT, 1);
		  operant_write (box_id, RGTKEYLT, 1);
		}
		else{
		  operant_write (box_id, LFTKEYLT, 0);
		  operant_write (box_id, RGTKEYLT, 0);
		}
	      }
	    }
	    gettimeofday(&resp_lag, NULL);
	    if (DEBUG==2){printf("flag: values at right & left = %d %d\t", right, left);}
	  }while ( (left==0) && (right==0) && (timercmp(&resp_lag, &resp_window, <)) );
                   
	  operant_write (box_id, LFTKEYLT, 0);    /*make sure the key lights are off after resp interval*/
	  operant_write (box_id, RGTKEYLT, 0);

	  /* Calculate response time */
	  curr_tt = time (NULL); 
	  loctime = localtime (&curr_tt);                     /* date and wall clock time of resp */
	  timersub (&resp_lag, &stimoff, &resp_rt);           /* reaction time */
	  if (DEBUG){resp_sec = resp_rt.tv_sec;}      
	  if (DEBUG){ resp_usec = resp_rt.tv_usec;}
	  if (DEBUG){printf("resp rt sec: %d \t usec: %d\n", resp_sec, resp_usec);} 
	  resp_rxt = (float) resp_rt.tv_sec + ( (float) resp_rt.tv_usec/1000000);  /* format reaction time */
	  if (DEBUG){printf("flag: resp_rxt = %.4f\n", resp_rxt);}
	    
	  strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
	  strftime (min, 16, "%M", loctime);
	  strftime (month, 16, "%m", loctime);
	  strftime (day, 16, "%d", loctime);


	  /* Consequate responses */
	  if (DEBUG){printf("flag: exit value left = %d, right = %d\n", left, right);}
	  if (stim_class == 1){                                 /* GO LEFT */                          
	    if ( (left==0 ) && (right==0) ){
	      resp_sel = 0;
	      resp_acc = 2;
	      ++Rstim[stim_number].N;++Tstim[stim_number].N;
	      ++Rclass[stim_class].N;++Tclass[stim_class].N; 
	      reinfor = 0;
	      if (DEBUG){ printf("flag: no response to stimtype 1\n");}
	    }
	    else if (left != 0){
	      resp_sel = 1;
	      resp_acc = 1;	
	      ++Rstim[stim_number].C;++Tstim[stim_number].C;
	      ++Rclass[stim_class].C; ++Tclass[stim_class].C; 
	      reinfor=feed(stim_reinf, &f);
	      if (reinfor == 1) { ++fed;}
	      if (DEBUG){printf("flag: correct response to stimtype 1\n");}
	    }
	    else if (right != 0){
	      resp_sel = 2;
	      resp_acc = 0;
	      ++Rstim[stim_number].X;++Tstim[stim_number].X;
	      ++Rclass[stim_class].X; ++Tclass[stim_class].X; 
	      reinfor =  timeout(stim_punish);
	      if (DEBUG){printf("flag: incorrect response to stimtype 1\n");}
	    } 
	    else
	      fprintf(datafp, "DEFAULT SWITCH for bit value:ERROR CODE REMOVED");
	  }
	  else if (stim_class == 2){                           /* GO RIGHT */
	    if ( (left==0) && (right==0) ){
	      resp_sel = 0;
	      resp_acc = 2;
	      ++Rstim[stim_number].N;++Tstim[stim_number].N;
	      ++Rclass[stim_class].N;++Tclass[stim_class].N; 
	      reinfor = 0;
	      if (DEBUG){printf("flag: no response to stimtype 2\n");}
	    }
	    else if (left!=0){
	      resp_sel = 1;
	      resp_acc = 0;
	      ++Rstim[stim_number].X;++Tstim[stim_number].X;
	      ++Rclass[stim_class].X; ++Tclass[stim_class].X; 
	      reinfor =  timeout(stim_punish);
	      if (DEBUG){printf("flag: incorrect response to stimtype 2\n");}
	    }
	    else if (right!=0){
	      resp_sel = 2;
	      resp_acc = 1;
	      ++Rstim[stim_number].C;++Tstim[stim_number].C;
	      ++Rclass[stim_class].C; ++Tclass[stim_class].C; 
	      reinfor=feed(stim_reinf, &f);
	      if (reinfor == 1) { ++fed;}
	      if (DEBUG){printf("flag: correct response to stimtype 2\n");}
	    } 
	    else
	      fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
	  }
	  else if (stim_class >= 2){                           /* PROBE STIMULUS */
	  if ( (left==0) && (right==0) ){ /*no response to probe */
	  resp_sel = 0;
	      resp_acc = 2;
	      ++Rstim[stim_number].N;++Tstim[stim_number].N;
	      ++Rclass[stim_class].N;++Tclass[stim_class].N; 
	      reinfor = 0;
	      if (DEBUG){printf("flag: no response to probe stimulus\n");}
	    }
	    else if (left!=0){
	      resp_sel = 1;
	      if(DEBUG){printf("flag: LEFT response to PROBE\n");} 
	      resp_acc = 3;
	      ++Rstim[stim_number].X;++Tstim[stim_number].C;
	      ++Rstim[stim_class].X; ++Tstim[stim_class].C; 
	      reinfor =  probeRx(stim_reinf, stim_punish,&f);
	      if (reinfor){++fed;}
	    }
	    else if (right!=0){
	      resp_sel = 2;
	      if(DEBUG){printf("flag: RIGHT response to PROBE\n");}
	      resp_acc = 3;
	      ++Rstim[stim_number].X;++Tstim[stim_number].X;
	      ++Rstim[stim_class].X; ++Tstim[stim_class].X; 
	      reinfor =  probeRx(stim_reinf, stim_punish, &f);
	      if (reinfor){++fed;}
	    }
	    else
	      fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
	  }
	  else{
	    fprintf(stderr, "Unrecognized stimulus class: Fatal Error");
	    fclose(datafp);
	    fclose(dsumfp);
	    exit(-1);
	  } 
	    
	  /* Pause for ITI */
	  reinfor_sum = reinfor + reinfor_sum;
	  operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	  nanosleep(&iti, NULL);                                   /* wait intertrial interval */
	  if (DEBUG){printf("flag: ITI passed\n");}
                                        
                   
	  /* Write trial data to output file */
	  strftime (tod, 256, "%H%M", loctime);
	  strftime (date_out, 256, "%m%d", loctime);
	  fprintf(datafp, "%d\t%d\t%d\t%s\t%s\t%d\t%d\t%d\t%.4f\t%.4f\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, 
		  correction, stimexm, distexm, stim_class, resp_sel, resp_acc, trialdur, distdur, resp_rxt, reinfor, tod, date_out );
	  fflush (datafp);
	  if (DEBUG){printf("flag: trial data written\n");}

	  /*generate some output numbers*/
	  for (i = 0; i<nstims;++i){
	    Rstim[i].count = Rstim[i].X + Rstim[i].C;
	    Tstim[i].count = Tstim[i].X + Tstim[i].C;
	  }
	  for (i = 1; i<=nclasses;++i){
	    Rclass[i].count = Rclass[i].X + Rclass[i].C;
	    Tclass[i].count = Tclass[i].X + Tclass[i].C;
	  }
	    
	  /* Update summary data */
	  if(freopen(dsumfname,"w",dsumfp)!= NULL){
	    fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
	    fprintf (dsumfp, "\tPROPORTION CORRECT RESPONSES (by stimulus, including correction trials)\n");
	    fprintf (dsumfp, "\tPROBE STIMULI: PROPORTION LEFT RESPONSES\n");
	    fprintf (dsumfp, "\tStim\t\tCount\tToday     \t\tCount\tTotals\t(excluding 'no' responses)\n");
	    for (i = 0; i<nstims;++i){
	      fprintf (dsumfp, "\t%s\t\t%d\t%1.4f     \t\t%d\t%1.4f\n", 
		       stimulus[i].exemplar, Rstim[i].count, (float)Rstim[i].C/(float)Rstim[i].count, Tstim[i].count, (float)Tstim[i].C/(float)Tstim[i].count );
	    }
	    fprintf (dsumfp, "\n\nPROPORTION CORRECT RESPONSES (by trial type, including correction trials)\n");
	    fprintf (dsumfp, "PROBE STIMULI: PROPORTION LEFT RESPONSES\n");
	    for (i = 1; i<=nclasses;++i){
	      fprintf (dsumfp, "\t%d\t\t%d\t%1.4f     \t\t%d\t%1.4f\n", 
		       i, Rclass[i].count, (float)Rclass[i].C/(float)Rclass[i].count, Tclass[i].count, (float)Tclass[i].C/(float)Tclass[i].count );
	    }
	      
	    fprintf (dsumfp, "Last trial run @: %s\n", asctime(loctime) );
	    fprintf (dsumfp, "Trials this session: %d\n",trial_num);
	    fprintf (dsumfp, "Feeder ops today: %d\n", fed );
	    fprintf (dsumfp, "Hopper failures today: %d\n", f.hopper_failures);
	    fprintf (dsumfp, "Hopper won't go down failures today: %d\n",f.hopper_wont_go_down_failures);
	    fprintf (dsumfp, "Hopper already up failures today: %d\n",f.hopper_already_up_failures);
	    fprintf (dsumfp, "Responses during feed: %d\n", f.response_failures); 
	    fprintf (dsumfp, "Rf'd responses: %d\n\n", reinfor_sum); 
	    fflush (dsumfp);

	  }
	  else
	    fprintf(stderr, "ERROR!: problem re-opening summary output file!: %s\n", dsumfname);
	    
	    
	  if (DEBUG){printf("flag: summaries updated\n");}


	    
	  /* End of trial chores */
	  sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                  /* unblock termination signals */ 
	  if (resp_acc == 0)
	    correction = 0;
	  else
	    correction = 1;                                              /* set correction trial var */
	  if ((xresp==1)&&(resp_acc == 2))
	    correction = 0;                                              /* set correction trial var for no-resp */
	    
	  curr_tt = time(NULL);
	  loctime = localtime (&curr_tt);
	  strftime (hour, 16, "%H", loctime);
	  strftime(min, 16, "%M", loctime);
	  currtime=(atoi(hour)*60)+atoi(min);
	  if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
	    
	}while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); /* correction trial loop */
      }
	    
      stim_number = -1;                                                /* reset the stim number for correct trial*/
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
      Rstim[i].C = Rstim[i].X = Rstim[i].N = Rstim[i].count =0;
    }
    for(i=1;i<=nclasses;i++){
      Rclass[i].C = Rclass[i].X = Rclass[i].N = Rclass[i].count = 0;
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

	  
	 	  
  }while (1);// main loop
	
	
  /*  Cleanup */
  fclose(datafp);
  fclose(dsumfp);
  return 0;
}                         

