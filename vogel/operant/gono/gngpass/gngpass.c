/*****************************************************************************
** gng.c - code for running go/nogo operant training procedure
******************************************************************************
**
** 9-19-01 TQG: Adapted from most current 2choice.c
** 6-22-05 TQG: Now runs using alsa sound driver
** 7-12-05 EDF: Adding support for beam break hopper verification
** 4-13-06 TQG: added sunrise/sunset timing and cleaned up functions
** 9-06-07 TQG: added capacity to include blocks for passive stimulus exposure
**              during passive blocks the apparatus is not responsive to any behavior
**              passive blocks alternate with acitve blocks and are matched in duration and 
**              the total numbers of stimuli delivered .passive stimuli should be added to
**              the .stim file as class '0', and you must have the same total number of passive
**              and active stimuli.
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>

#include "/usr/local/src/operantio/operantio.c"
#include "/usr/local/src/audioio/audout.c"

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
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
#define PASSLT    6

/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             2             /* seconds from simulus end until NORESP is registered  (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  1024          /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 256           /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define TIMEOUT_DURATION         10000000      /* duration of timeout in microseconds */
#define FEED_DURATION            4000000       /* duration of feeder access in microseconds */
#define DACSAMPLERATE            20000         /* stimulus sampling rate */  
#define DACBITDEPTH              16            /* stimulus bit depth */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define HOPPER_DROP_MS           300           /*time to give hopper to fall before checking if it actually fell after command to do so*/

long timeout_duration = TIMEOUT_DURATION;
long feed_duration    = FEED_DURATION;
int  trial_max        = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME;
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval   = SLEEP_TIME;
const char exp_name[] = "GNG";
int box_id = -1;
int flash = 0;
int xresp = 0;
int mirror = 0;
int resp_sel, resp_acc;

typedef struct {
  int hopper_failures;
  int hopper_wont_go_down_failures;
  int hopper_already_up_failures;
  int response_failures;
} Failures;

int feed(int rval, Failures *f);
int timeout(int rval);
int probeGO(int rval, int mirr, Failures *f);

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};
struct timeval blocklen = {0,0};

/* -------- Signal handling --------- */
int client_fd = -1;
static void sig_pipe(int signum){ 
  fprintf(stdout,"SIGPIPE caught\n"); client_fd = -1;
}
static void termination_handler (int signum){
  snd_pcm_close(handle);
  fprintf(stdout,"closed pcm device\n");
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

int probeGO( int rval, int mirr, Failures *f)
{
  int outcome=0;

  if (mirr==0){
    outcome = 1+(int) (100.0*rand()/(RAND_MAX+1.0)); 
    if (outcome <= rval){
      doCheckedFeed(f);
      if(DEBUG){printf("mirror OFF, reinforced GO to probe stim\n");}
      return(1);
    }
    else{ 
      if(DEBUG){printf("mirror OFF, unreinforced GO to probe stim\n");}
      return (0);} 
  }
  else{  /*mirror=1, so p(feed)=p(timeout)=rval*/
    outcome = 1+ (int) (100.0*rand()/(RAND_MAX+1.0) );
    if (outcome <= rval){
      doCheckedFeed(f);
      if(DEBUG){printf("mirror ON, reinforced GO to probe stim\n");}
      return(1);
    }
    else if(outcome <= (2*rval) ){
      operant_write(box_id, HOUSELT, 0);
      usleep(timeout_duration);
      operant_write(box_id, HOUSELT, 1);
      if(DEBUG){printf("mirror ON, punished GO to probe stim\n");}
      return (1);
    }
    else{
      if(DEBUG){printf("mirror ON, unreinforced GO to probe stim\n");}
      return (0);
    }
  }
}

/*******************************************************
 ******************************************************/
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

/*******************************************************
 ******************************************************/
void do_usage()
{
  fprintf(stderr, "gng usage:\n");
  fprintf(stderr, "    gng [-help] [-B int] [-mirrorP] [-t float][-on int:int] [-off int:int] [-x] [-S <subject number>] <filename>\n\n");
  fprintf(stderr, "        -help          = show this help message\n");
  fprintf(stderr, "        -B int         = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "        -mirrorP       = set the timeout rate equal to the 'GO_reinforcement rate' in the .stim file\n");
  fprintf(stderr, "                         ***** MirrorP applies only to probe trials ****\n");		  
  fprintf(stderr, "                         NOTE:If you use 'mirrorP' the base rate in the .stim file must be <= to 50%%\n"); 
  fprintf(stderr, "        -t float       = set the timeout duration to float secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -f             = flash the center key light during the response window \n");
  fprintf(stderr, "        -on int:int    = set hour:min for exp to start eg: '-on 7:30' (default is 7:00 AM)\n");
  fprintf(stderr, "        -off int:int   = set hour for exp to stop eg: '-off 19:45' (default is 7:00 PM)\n");
  fprintf(stderr, "                         To use daily sunset or sunrise times set 'on' or 'off' to '99'\n");
  fprintf(stderr, "        -x             = use correction trials for incorrect GO responses\n");
  fprintf(stderr, "        -w int         = set the response window duration to 'x' secs (use an integer)\n");
  fprintf(stderr, "        -Atime int         = set the number of secs in the active stimulus presentation blocks\n");
  fprintf(stderr, "                         after each active block a passive block replays the preceding block's isi sequence with class 0 stims\n");
  fprintf(stderr, "        -S int         = specify the subject ID number (required)\n");
  fprintf(stderr, "        filename       = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "                         where each line is: 'Class' 'Sndfile' 'Freq' 'Go_Rf'\n");
  fprintf(stderr, "                         'Class'= 1 for S+, 2 for S-, 3 or greater for nondifferential (e.g. probe stimuli) \n");
  fprintf(stderr, "                             use class='0' for passive presentation stimuli.Yoked in order listed to the class 1 & 2 stims\n");
  fprintf(stderr, "                             The total number of passive and active stimuli must be equal.\n");
  fprintf(stderr, "                             Set the 'Freq' and 'Go_Rf' for passive stimuli to  one and zero, respectively.\n"); 
  fprintf(stderr, "                         'Sndfile' is the name of the stimulus soundfile (use WAV format 44.1kHz only)\n");
  fprintf(stderr, "                         'Freq' is the overall stimulus presetation rate (relative to the other stimuli). \n"); 
  fprintf(stderr, "                           The actual rate for each stimulus (expressed as an integer) is that value divded by the\n");
  fprintf(stderr, "                           sum for all stimuli. Set all prefreq values to 1 for equal probablility \n"); 
  fprintf(stderr, "                         'Go_Rf' is the rate at which GO responses to the Sndfile are reinforced.\n");
  fprintf(stderr, "                           For class 1 Go_Rf is the percentage of food reward following a correct response.\n");
  fprintf(stderr, "                           For class 2 Go_Rf is the percentage of timeouts following an incorrect response.\n"); 
  fprintf(stderr, "                           For probe classes (class > 2) Go_Rf is the percentage of nondifferential reinforcement.\n");
  exit(-1);
}

/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *resp_wind, int *blockdur, float *timeout_val, char **stimfname)
{
int i=0;
 for (i = 1; i < argc; i++){
   if (*argv[i] == '-'){
     if (strncmp(argv[i], "-B", 2) == 0)
       sscanf(argv[++i], "%i", box_id);
     else if (strncmp(argv[i], "-S", 2) == 0)
       sscanf(argv[++i], "%i", subjectid);
     else if (strncmp(argv[i], "-f", 2) == 0)
       flash=1;
     else if (strncmp(argv[i], "-w", 2) == 0)
       sscanf(argv[++i], "%d", resp_wind);
     else if (strncmp(argv[i], "-mirrorP", 8) == 0)
       mirror = 1;
     else if (strncmp(argv[i], "-t", 2) == 0)
       sscanf(argv[++i], "%f", timeout_val);
     else if (strncmp(argv[i], "-on", 3) == 0)
       sscanf(argv[++i], "%i:%i", starthour,startmin);
     else if (strncmp(argv[i], "-off", 4) == 0)
       sscanf(argv[++i], "%i:%i", stophour, stopmin);
     else if (strncmp(argv[i], "-Atime", 6) ==0)
       sscanf(argv[++i], "%i", blockdur);
     else if (strncmp(argv[i], "-x", 2) == 0)
       xresp = 1;
     else if (strncmp(argv[i], "-help", 5) == 0)
       do_usage();
     else{
       fprintf(stderr, "Unknown option: %s\t", argv[i]);
       fprintf(stderr, "Try 'gng -help' for help\n");
     }
   }
   else
     *stimfname = argv[i];
 }
 return 1;
}

/********************************************
 ** MAIN
 ********************************************/
int main(int argc, char *argv[])
{
  FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
  char *stimfname = NULL;
  char *stimfroot, *pcm_name;
  const char delimiters[] = " .,;:!-";
  char datafname[128], hour [16], min[16], month[16], day[16], year[16], 
    dsumfname[128], stimftemp[128],buf[128], stimexm[128],fstim[256],
    timebuff[64], tod[256], date_out[256],temphour[16],tempmin[16], buffer[30];
  int dinfd=0, doutfd=0, nclasses, nstims, stim_class, stim_number, stim_reinf, 
    subjectid, loop, period, played, resp_wind, correction, blockdur=0, do_pblock=0,
    trial_num, session_num, playval, i, j, k, *playlist=NULL, *passplaylist=NULL,blocks_done=0,
    totnstims=0, nblocks, sessionTrials, dosunrise=0, dosunset=0, session_start, on_time, off_time, 
    currtime, isistart=0, npstims=0,maxblktrials=0, ntrials=0, fallthru=0;
  float resp_rxt=0.0, timeout_val=0.0;
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval stimstart, stimoff, resp_window, resp_lag, resp_rt, blockstart, now, blockend;
  struct tm *loctime;
  int center = 0, fed = 0;
  Failures f = {0,0,0,0};
  int reinfor_sum = 0, reinfor = 0;
  int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
  struct stim {
    char exemplar[128];
    int class;
    int reinf;
    int freq;
    int playnum;
  }stimulus[MAXSTIM];
  struct response {
    int count;
    int go;
    int no;
    float ratio;
  } stimRses[MAXSTIM], stimRtot[MAXSTIM], classRses[MAXCLASS], classRtot[MAXCLASS];

  struct block_info{
    int *stimnum;
    int *isi;
  }block;

  sigset_t trial_mask;
  srand(time(0));
  
  /* Make sure the server can see you, and set up termination handler*/
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
  command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &resp_wind, &blockdur, &timeout_val, &stimfname);
  if(DEBUG){
    fprintf(stderr, "box_id=%d subjectid=%d startH=%d stopH=%d startM=%d stopM=%d blockdur=%d xresp=%d resp_wind=%d timeout_val=%f flash=%d stimfile:%s\n",
	    box_id, subjectid, starthour, stophour, startmin, stopmin, blockdur, xresp, resp_wind, timeout_val, flash, stimfname);
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
    fprintf(stderr, "\tERROR: try 'gng -help' for available options\n");
    snd_pcm_close(handle);
    exit(-1);
  }
  
  /*set some variables as needed*/
  if (resp_wind>0)
    respoff.tv_sec = resp_wind;
  fprintf(stderr, "response window duration set to %d secs\n", (int) respoff.tv_sec);
  if(timeout_val>0.0)
    timeout_duration = (int) (timeout_val*1000000);
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
  
  on_time=(starthour*60)+startmin;
  off_time=(stophour*60)+stopmin;


  /* adjust the off_time to fit an even number of blocks */
  /* for every active block you must run a passive block */ 
  if(difftime(curr_tt, rise_tt)!=0){
    strftime(temphour, 16, "%H", localtime(&curr_tt));
    strftime(tempmin, 16, "%M", localtime(&curr_tt));
    session_start = (60*atoi(temphour)) + atoi(tempmin);
  }
  else
    session_start=on_time;
  
  if(DEBUG){printf("session start:%d\tsession stop:%d\n", session_start, off_time );}

  nblocks=floor((off_time-session_start)/(blockdur/60));
  if(nblocks%2) /*if nblocks is odd*/
    nblocks--; /*make it even*/
  off_time = session_start+(nblocks*blockdur/60);
  blocklen.tv_sec = blockdur;
  if(DEBUG){printf("session starts at:%d, \tsession stops at:%d; I'll run %d active and %d passive blocks, each %d secs long\n", 
		   session_start, off_time, nblocks/2, nblocks/2, blockdur);}


  /* Initialize box */
  if(DEBUG){
    fprintf(stderr,"Initializing box %d ... ", box_id);
    fprintf(stderr, "trying to execute setup(%s)\n", pcm_name);
  }
  if((period=setup_pcmdev(pcm_name))<0){
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
  if (mirror==0){
    fprintf(stderr, "GO responses to S- (class2) stimuli always yeild a timeout. \n");
    fprintf(stderr, "GO responses to probe stimuli never yield a timeout.\n");
  }
  if(flash){fprintf(stderr, "!!WARNING: Flashing keylights during response window!!\n");}
  if(xresp){fprintf(stderr, "!!WARNING: Enabling correction trials for 'No' responses !!\n");}
  
  
  /* Read in the list of exmplars from stimulus file */
  nstims = nclasses = 0;
  if ((stimfp = fopen(stimfname, "r")) != NULL){
    while (fgets(buf, sizeof(buf), stimfp))
      nstims++;
    fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
    rewind(stimfp);
    
    for (i = 0; i < nstims; i++){
      fgets(buf, 128, stimfp);
      stimulus[i].freq = stimulus[i].reinf=0;
      sscanf(buf, "%d\%s\%d\%d", &stimulus[i].class, stimulus[i].exemplar, &stimulus[i].freq, &stimulus[i].reinf);
          
      if(stimulus[i].class==0){
	/*its a passive stimulus*/
	if ((stimulus[i].reinf!=0) ||(stimulus[i].freq!=1)) {
	  fprintf(stderr, "FATAL .STIM ERROR: Freq must be '1' and reinf must be '0' for passive stimulus '%s'\n", 
		  stimulus[i].exemplar);
	  snd_pcm_close(handle);
	  exit(-1);	  
	}	
	npstims++;
      }
      else{
	if((stimulus[i].freq==0) || (stimulus[i].reinf==0)){
	  printf("ERROR: insufficnet data or bad format in '.stim' file. 'freq' or 'reinf' set to zero for class 1 or 2 stim'\n");
	  snd_pcm_close(handle);
	  exit(-1);}
	
	totnstims += stimulus[i].freq;
	if(DEBUG){printf("totnstims: %d\n", totnstims);}
	
	/* count stimulus classes*/
	if (nclasses<stimulus[i].class){nclasses=stimulus[i].class;}
	if (DEBUG){printf("nclasses: %d\n", nclasses);}
	
	/*check the reinforcement rates */
	if (stimulus[i].class==1){
	  fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct GO responses\n", 
		  stimulus[i].exemplar, stimulus[i].reinf);
	}
	else if (stimulus[i].class==2){
	  fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect GO responses\n", 
		  stimulus[i].exemplar, stimulus[i].reinf);
	}
	else{
	  if((mirror==1)){
	    if (stimulus[i].reinf>50){
	      fprintf(stderr, "ERROR!: To mirror food and timeout reinforcement values you must use a base rate less than or equal to 50%%\n");
	      snd_pcm_close(handle);
	      exit(-1);
	    }
	    else{
	      fprintf(stderr, "p(food) reward and p(timeout) on probe trials with %s is set to %d%%\n", stimulus[i].exemplar, stimulus[i].reinf );
	    }
	  }
	  else{fprintf(stderr, "Reinforcement rate on probe trials is set to %d%% pct for correct GO responses, 100%% for incorrect GO responses\n", 
		       stimulus[i].reinf);
	  }
	}
      }
    }//for loop
  }//fopen 
  else{ 
    printf("Error opening stimulus input file! Try 'gng_probe -help' for proper file formatting.\n");  
    snd_pcm_close(handle);
    exit(-1);	  
  }
  fclose(stimfp);
  if(DEBUG){printf("Done reading in %d stims; %d stims in %d active classes, %d stims in 1 passive class\n", 
		   nstims, nstims-npstims, nclasses, npstims);}
  
  /* Don't allow correction trials when probe stimuli are presented */
  if(xresp==1 && nclasses>2){
    fprintf(stderr, "ERROR!: You cannot use correction trials and probe stimuli in the same session.\n  Exiting now\n");
    snd_pcm_close(handle);
    exit(-1);
  }
  
  /* make the the passive stimulus playlist */
  if(DEBUG){printf("flag: making the passive playlist\n");}
  free(passplaylist);
  passplaylist = malloc((npstims+1)*sizeof(int));
  i=j=0;
  for (i=0;i<npstims; i++){
    k=0;
    for(k=0;k<stimulus[i].freq;k++){
      passplaylist[j]=i;
      if(DEBUG){printf("value for passive playlist entry '%d' is '%d'\n", j, i);}
      j++;
    }
  }
  
  /* make the the active stimulus playlist */
  if(DEBUG){printf("flag: making the active playlist\n");}
  free(playlist);
  playlist = malloc( (totnstims+1)*sizeof(int) );
  i=j=0;
  for (i=npstims;i<nstims; i++){
    k=0;
    for(k=0;k<stimulus[i].freq;k++){
      playlist[j]=i;
      if(DEBUG){printf("value for active playlist entry '%d' is '%d'\n", j, i);}
      j++;
    }
  }
  if(DEBUG){printf("there are %d stims in the active playlist\n", totnstims);}

  /*allocate arrays for block info*/
  maxblktrials= ceil((blockdur*60)/(INTER_TRIAL_INTERVAL+3))+1;/*assumes min trial length is 3 sec--2 sec no resp, 1 sec stim*/
  block.stimnum = malloc((maxblktrials)*sizeof(int)); 
  block.isi = malloc((maxblktrials)*sizeof(int)); 

  /*  Open & setup data logging files */
  curr_tt = time (NULL);
  loctime = localtime (&curr_tt);
  strftime (timebuff, 64, "%d%b%y", loctime);
  sprintf (stimftemp, "%s", stimfname);
  stimfroot = strtok (stimftemp, delimiters); 
  sprintf(datafname, "%i_%s.gonogo_rDAT", subjectid, stimfroot);
  sprintf(dsumfname, "%i.summaryDAT", subjectid);
  datafp = fopen(datafname, "a");
  dsumfp = fopen(dsumfname, "w");
  
  if ( (datafp==NULL) || (dsumfp==NULL) ){
    fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", datafname);
    snd_pcm_close(handle);
    close(dinfd);
    close(doutfd);
    fclose(datafp);
    fclose(dsumfp);
    exit(-1);
  }
  
  /* Write data file header info */
  fprintf (stderr, "Data output to '%s'\n", datafname);
  fprintf (datafp, "File name: %s\n", datafname);
  fprintf (datafp, "Procedure source: %s\n", exp_name);
  fprintf (datafp, "Start time: %s", asctime(loctime));
  fprintf (datafp, "Subject ID: %d\n", subjectid);
  fprintf (datafp, "Stimulus source: %s\n", stimfname);  
  fprintf (datafp, "reinforcement is set in the .stim\t  mirror:%d \n", mirror );
  fprintf (datafp, "Sess#\tTrl#\tTrlTyp\tStimulus\t\t\tClass\tRspSL\tRspAC\tRspRT\tReinf\tTOD\tDate\n");
  

   /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
   ********************************************/
  session_num = 1;
  trial_num = 0;
  correction = 1;
  
  for(i = 0; i<nstims;++i){   /*zero out the response tallies */
    stimRses[i].go = stimRses[i].no = stimRtot[i].go = stimRtot[i].no = 0;
    stimRses[i].ratio = stimRtot[i].ratio = 0.0;
  }
  if (DEBUG){printf("stimulus counters zeroed!\n");} 
  for(i=0;i<nclasses;i++){
    classRses[i].go = classRses[i].no = classRtot[i].go = classRtot[i].no = 0;
    classRses[i].ratio = classRtot[i].ratio = 0.0;;
  }
  if (DEBUG){printf("class counters zeroed!\n");} 
  
  curr_tt = time(NULL);
  loctime = localtime (&curr_tt);
  strftime (hour, 16, "%H", loctime);
  strftime(min, 16, "%M", loctime);
  if (DEBUG){printf("hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
  currtime=(atoi(hour)*60)+atoi(min);
  
  /*make sure you shouldn't be looping at night */
  if( (currtime<on_time) || (currtime>=off_time) ){
    fallthru=1;
    nblocks=0;
  }
  else
    fallthru=0;
  
  /*set some block vars*/	
  do_pblock=0; /*start by running active blocks*/
  ntrials=0;
  blocks_done=0;
  for(i = 0; i<maxblktrials;++i){   /*clear out the block info struct */
    block.stimnum[i] = block.isi[i] = -1;
  }
  gettimeofday(&blockstart, NULL); /*note the start time for the block*/
  timeradd(&blockstart, &blocklen, &blockend);
  if(DEBUG){printf("block starts at %d secs and ends at %d secs\n", (int)blockstart.tv_sec, (int)blockend.tv_sec);}

  operant_write (box_id, HOUSELT, 1);        /* house light on */
  
  do{/* start the main run loop */
    while ((blocks_done < nblocks) && (!do_pblock) && (!fallthru)){/* start main trial loop */ 
      if (DEBUG){printf("minutes since midnight at loop start: %d\t starttime: %d\tstoptime: %d\n",
			currtime,on_time,off_time);}
      playval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));                     /* select stim exemplar at random */ 
      if (DEBUG){printf("playval: %d\t", playval);}
      stim_number = playlist[playval];
      stim_class = stimulus[stim_number].class;                               /* set stimtype variable */
      strcpy (stimexm, stimulus[stim_number].exemplar);                       /* get exemplar filename */
      stim_reinf = stimulus[stim_number].reinf;
      sprintf(fstim,"%s%s", STIMPATH, stimexm);                                /* add full path to file name */
      
      if(DEBUG){
	printf("stim_num: %d\t", stim_number);
	printf("class: %d\t", stim_class);
	printf("reinf: %d\t", stim_reinf);
	printf("name: %s\n", stimexm);
	printf("full stim path: %s\n", fstim);
	printf("exemplar chosen: %s\tnumber: %d\n", stimulus[stim_number].exemplar, stim_number );
      }
      
      do{/* start correction trial loop */
	resp_sel = resp_acc = resp_rxt = 0;        /* zero trial variables        */
	++trial_num;
	curr_tt = time(NULL);
	
	/* Wait for center key press */
	if (DEBUG){printf("flag: waiting for center key press\n");}
	operant_write (box_id, HOUSELT, 1);        /* house light on */
	center = 0;
	do{                                         
	  nanosleep(&rsi, NULL);	               	       
	  center = operant_read(box_id, CENTERPECK);   /*get value at center peck position*/		 	       
	  gettimeofday(&now, NULL);
	}while ( (center==0) && (now.tv_sec<=blockend.tv_sec) );
	
	if(DEBUG){printf("elapsed time in block: %d secs\n", (int)(now.tv_sec-blockstart.tv_sec));}
	if(DEBUG){printf("time left in block: %d secs\n", (int)(blockend.tv_sec-now.tv_sec));}
	
	if(center==1){ //do_trial conditional
	  sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	  
	  /* Play stimulus file */
	  if (DEBUG){printf("START '%s'\n", stimexm);}
	  gettimeofday(&stimstart, NULL);
	  if ((played = playwav(fstim, period))!=1){
	    fprintf(stderr, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		    pcm_name, stimexm, asctime(localtime (&curr_tt)) );
	    fprintf(datafp, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		    pcm_name, stimexm, asctime(localtime (&curr_tt)) );
	    fclose(datafp);
	    fclose(dsumfp);
	    exit(-1);
	  }
	  if (DEBUG){printf("STOP  '%s'\n", stimexm);}
	  gettimeofday(&stimoff, NULL);
	  if (DEBUG){
	    stimoff_sec = stimoff.tv_sec;
	    stimoff_usec = stimoff.tv_usec;
	    printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);}
	  
	  /* Wait for center key press */
	  if (DEBUG){printf("flag: waiting for response\n");}
	  timeradd (&stimoff, &respoff, &resp_window);
	  if (DEBUG){ 
	    respwin_sec = resp_window.tv_sec;
	    respwin_usec = resp_window.tv_usec;
	    printf("resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);}
	  
	  loop = 0; center = 0;
	  do{
	    nanosleep(&rsi, NULL);
	    center = operant_read(box_id, CENTERPECK);
	    gettimeofday(&resp_lag, NULL);
	    if((center==0) && flash){
	      ++loop;
	      if(loop%80==0){
		if(loop%160==0){
		  operant_write(box_id, CTRKEYLT, 1);}
		else{
		  operant_write(box_id, CTRKEYLT, 0);}
	      } 
	    }
	  }while ( (center==0) && (timercmp(&resp_lag, &resp_window, <)) );
	  
	  operant_write (box_id, CTRKEYLT, 0);    /*make sure the key lights are off after resp interval*/
	  
	  /* Calculate response time */
	  curr_tt = time (NULL); 
	  loctime = localtime (&curr_tt);                     /* date and wall clock time of resp */
	  timersub (&resp_lag, &stimoff, &resp_rt);           /* reaction time */
	  if (DEBUG){
	    resp_sec = resp_rt.tv_sec;      
	    resp_usec = resp_rt.tv_usec;
	    printf("resp rt sec: %d \t usec: %d\n", resp_sec, resp_usec);} 
	  resp_rxt = (float) resp_rt.tv_sec + ( (float) resp_rt.tv_usec/1000000);  /* format reaction time */
	  if (DEBUG){printf("flag: resp_rxt = %.4f\n", resp_rxt);}
	  
	  strftime (hour, 16, "%H", loctime);                    /* format wall clock times for trial end*/
	  strftime (min, 16, "%M", loctime);
	  strftime (month, 16, "%m", loctime);
	  strftime (day, 16, "%d", loctime);
	  ++stimRses[stim_number].count; ++stimRtot[stim_number].count; 
	  ++classRses[stim_class].count; ++classRtot[stim_class].count;	   
	  
	  /* Consequate responses */
	  if (DEBUG){
	    printf("flag: stim_class = %d\n", stim_class);
	    printf("flag: exit value center = %d\n",center);}
	  
	  if(stim_class == 1){   /* If S+ stimulus */
	    if (center==0){   /*no response*/
	      resp_sel = 0;
	      resp_acc = 0;
	      if (DEBUG){ printf("flag: no response to s+ stim\n");}
	      ++stimRses[stim_number].no; ++stimRtot[stim_number].no; ++classRses[stim_class].no; ++classRtot[stim_class].no; 
	      reinfor = 0;
	    }
	    else if (center != 0){  /*go response*/
	      resp_sel = 1;
	      resp_acc = 1; 
	      reinfor = feed(stim_reinf, &f);
	      if (DEBUG){printf("flag: go response to s+ stim\n");}
	      ++stimRses[stim_number].go; ++stimRtot[stim_number].go; ++classRses[stim_class].go; ++classRtot[stim_class].go;
	      if (reinfor == 1) { ++fed;}
	    }
	  }
	  else if(stim_class == 2){  /* If S- stimulus */
	    if (center==0){ /*no response*/
	      resp_sel = 0;
	      resp_acc = 1;
	      if (DEBUG){printf("flag: no response to s- stim\n");}
	      ++stimRses[stim_number].no; ++stimRtot[stim_number].no; ++classRses[stim_class].no; ++classRtot[stim_class].no;
	      reinfor = 0;
	    }
	    else if (center!=0){ /*go response*/
	      resp_sel = 1;
	      resp_acc = 0;
	      if (DEBUG){printf("flag: go response to s- stim\n");}
	      ++stimRses[stim_number].go; ++stimRtot[stim_number].go; ++classRses[stim_class].go; ++classRtot[stim_class].go;	 
	      reinfor =  timeout(stim_reinf);
	    }
	  }
	  else{	        /* probe trial */
	    if (center==0){ /*no response*/
	      resp_sel = 0;
	      resp_acc = 3;
	      if (DEBUG){printf("flag: no response to probe stim\n");}
	      ++stimRses[stim_number].no; ++stimRtot[stim_number].no; ++classRses[stim_class].no; ++classRtot[stim_class].no;
	      reinfor=0;
	    }
	    else if (center!=0){ /*go response*/
	      resp_sel = 1;
	      resp_acc = 3;
	      if (DEBUG){printf("flag: go response to probe stim\n");}
	      ++stimRses[stim_number].go; ++stimRtot[stim_number].go; ++classRses[stim_class].go; ++classRtot[stim_class].go;
	      reinfor =  probeGO(stim_reinf, mirror, &f);
	      if (reinfor == 1) { ++fed;}
	    }
	  }
	  
	  /*save some info for upcoming passive block*/
	  if(DEBUG){printf("\n\nstim_number:%d \tnpstims:%d \t ntrials:%d\n\n", stim_number, npstims, ntrials );}
	  block.stimnum[ntrials]=stim_number-npstims;                       /* note the yoked stimulus for the passive block */
	  if(ntrials==0)
	    block.isi[ntrials]=2;  /*set the first isi to 2 sec */
	  else
	    block.isi[ntrials]=stimstart.tv_sec-isistart;
	  isistart = stimoff.tv_sec;
	  ntrials++;                                                        /*count trials in the current active block*/
	  if(DEBUG){printf("set passive block trial %d to use stim num '%d' with a %d sec isi\n", 
			   ntrials, block.stimnum[ntrials-1], block.isi[ntrials-1]);}
	  
	  /* Write trial data to output file */
	  strftime (tod, 256, "%H%M", loctime);
	  strftime (date_out, 256, "%m%d", loctime);
	  fprintf(datafp, "%d\t%d\t%d\t%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, 
		  correction, stimexm, stim_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
	  fflush (datafp);
	  if (DEBUG){printf("flag: trail data written\n");}
	  
	  /*generate some output numbers*/
	  for (i = 0; i<nstims;++i){
	    stimRses[i].ratio = (float)(stimRses[i].go) /(float)(stimRses[i].count);
	    stimRtot[i].ratio = (float)(stimRtot[i].go) /(float)(stimRtot[i].count);	
	  }
	  sessionTrials=0;
	  for (i = 1; i<nclasses+1;++i){
	    classRses[i].ratio = (float) (classRses[i].go)/ (float)(classRses[i].count);
	    sessionTrials+=classRses[i].count;
	    classRtot[i].ratio = (float) (classRtot[i].go)/ (float)(classRtot[i].count);
	  }
	  
	  if (DEBUG){printf("flag: ouput numbers done\n");}
	  /* Update summary data */
	  
	  if(freopen(dsumfname,"w",dsumfp)!= NULL){
	    fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
	    fprintf (dsumfp, "\tSTIM COUNT AND RESPONSE RATIOS (by stimulus)\n");
	    fprintf (dsumfp, "\tStim     \t\tSession     \t\tTotals\n");
	    for (i = 0; i<nstims;++i){
	      fprintf (dsumfp, "\t%s     \t\t%d\t%1.4f     \t\t%d\t%1.4f\n", 
		       stimulus[i].exemplar, stimRses[i].count, stimRses[i].ratio, stimRtot[i].count, stimRtot[i].ratio);
	    }
	    fprintf (dsumfp, "\n\n RESPONSE DATA (by stim class)\n");
	    fprintf (dsumfp, "               Today's Session               Totals\n");
	    fprintf (dsumfp, "Class \t\t Count \tResponse Ratio \t\t Count \tResponse Ratio\n");
	    for (i = 0; i<nclasses+1;++i){
	      fprintf (dsumfp, "%d \t\t %d \t%1.4f \t\t\t %d \t%1.4f\n", 
		       i, classRses[i].count, classRses[i].ratio, classRtot[i].count, classRtot[i].ratio);
	    }
	    
	    fprintf (dsumfp, "Last trial run @: %s\n", asctime(loctime) );
	    fprintf (dsumfp, "Trials this session: %d\n",sessionTrials);
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
	  
	  /* Pause for ITI */
	  reinfor_sum = reinfor + reinfor_sum;
	  operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	  nanosleep(&iti, NULL);                     /* wait intertrial interval */
	  if (DEBUG){printf("flag: ITI passed\n");}
	  
	  /* End of trial chores */
	  if(xresp)
	    if (stim_class==2 && resp_acc == 0){correction = 0;}else{correction = 1;}   /*run correction for GO resp to s-*/
	  else 
	    correction = 1; /* make sure you don't invoke a correction trial by accident */
	  
	} /*end of do_trial conditional*/
	
	sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                   /* unblock termination signals */ 
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
	currtime=(atoi(hour)*60)+atoi(min);
	if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,on_time,off_time);}
	if(DEBUG){printf("correction is set to '%d'\n", correction);}
	
      }while (correction==0 ); /* correction trial loop */
      
      /* check to see if we're ready to switch to a passive block*/
      gettimeofday(&now, NULL);
      if(timercmp(&now, &blockend, >)){
	do_pblock=1;
	++blocks_done;
      }
      if(DEBUG){printf("do_pblock is set to '%d'\n", do_pblock);}
      stim_number = -1;                                          /* reset the stim number for correct trial*/
    } /*main trial loop*/
    
    if ((do_pblock) && (blocks_done<=nblocks) && (!fallthru)){ /*loop for passive block ntrials long*/
      if(DEBUG){printf("\n\nIN PASSIVE BLOCK\n");}
      
      sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
      
      /* set some vars that are meaningless in passive block*/
      resp_sel=resp_acc=reinfor=0;
      resp_rxt=0.0;
      correction=1;
      
      /*Change the lighting & make sure all operant stuff is off*/
      operant_write(box_id, HOUSELT, 1);
      operant_write(box_id, PASSLT, 1); /*house light and passive light must both be on*/
      operant_write(box_id, FEEDER, 0);
      operant_write(box_id, LFTKEYLT, 0);
      operant_write(box_id, CTRKEYLT, 0);
      operant_write(box_id, RGTKEYLT, 0);
      
      for (j = 0; j<ntrials; ++j){
	
	/* get the stimulus*/
	playval=block.stimnum[j];
	stim_number = passplaylist[playval];
	stim_class = stimulus[stim_number].class;                               /* set stimtype variable */
	strcpy (stimexm, stimulus[stim_number].exemplar);                       /* get exemplar filename */
	sprintf(fstim,"%s%s", STIMPATH, stimexm);                                /* add full path to file name */
	
	if(DEBUG){printf("passive trial %d of %d: waiting %d sec to play stim %d: '%s'\n", 
			 j+1, ntrials, block.isi[j], block.stimnum[j],stimexm);}
	
	/* wait for isi */
	sleep (block.isi[j]);
	
	/*play the stimulus*/
	if ((played = playwav(fstim, period))!=1){
	  fprintf(stderr, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		  pcm_name, stimexm, asctime(localtime (&curr_tt)) );
	  fprintf(datafp, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		  pcm_name, stimexm, asctime(localtime (&curr_tt)) );
	  fclose(datafp);
	  fclose(dsumfp);
	  exit(-1);
	}
	++stimRses[stim_number].count; ++stimRtot[stim_number].count; 
	++classRses[stim_class].count; ++classRtot[stim_class].count;
	
	curr_tt = time (NULL); 
	loctime = localtime (&curr_tt);                     /* date and wall clock time of resp */
	
	/* Write trial data to output file */
	strftime (tod, 256, "%H%M", loctime);
	strftime (date_out, 256, "%m%d", loctime);
	fprintf(datafp, "%d\t%d\t%d\t%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, 
		correction, stimexm, stim_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
	fflush (datafp);
	
	/* Update summary data */
	if(freopen(dsumfname,"w",dsumfp)!= NULL){
	  fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
	  fprintf (dsumfp, "\tSTIM COUNT AND RESPONSE RATIOS (by stimulus)\n");
	  fprintf (dsumfp, "\tStim     \t\tSession     \t\tTotals\n");
	  for (i = 0; i<nstims;++i){
	    fprintf (dsumfp, "\t%s     \t\t%d\t%1.4f     \t\t%d\t%1.4f\n", 
		     stimulus[i].exemplar, stimRses[i].count, stimRses[i].ratio, stimRtot[i].count, stimRtot[i].ratio);
	  }
	  fprintf (dsumfp, "\n\n RESPONSE DATA (by stim class)\n");
	  fprintf (dsumfp, "               Today's Session               Totals\n");
	  fprintf (dsumfp, "Class \t\t Count \tResponse Ratio \t\t Count \tResponse Ratio\n");
	  
	  for (i = 0; i<nclasses+1;++i){
	    fprintf (dsumfp, "%d \t\t %d \t%1.4f \t\t\t %d \t%1.4f\n", 
		     i, classRses[i].count, classRses[i].ratio, classRtot[i].count, classRtot[i].ratio);
	  }
	  
	  fprintf (dsumfp, "Last trial run @: %s\n", asctime(loctime) );
	  fprintf (dsumfp, "Trials this session: %d\n",sessionTrials);
	  fprintf (dsumfp, "Feeder ops today: %d\n", fed );
	  fprintf (dsumfp, "Hopper failures today: %d\n", f.hopper_failures);
	  fprintf (dsumfp, "Hopper won't go down failures today: %d\n",f.hopper_wont_go_down_failures);
	  fprintf (dsumfp, "Hopper already up failures today: %d\n",f.hopper_already_up_failures);
	  fprintf (dsumfp, "Responses during feed: %d\n", f.response_failures); 
	  fprintf (dsumfp, "Rf'd responses: %d\n\n", reinfor_sum); 
	  fflush (dsumfp);
	}
      } /*passive block trial loop*/
      
      /*now your done with the passive block*/
      /* reset the passive block variables */
      do_pblock=0;
      ntrials=0;
      ++blocks_done;
      for(i = 0; i<maxblktrials;++i)   /*zero out the block info struct */
	block.stimnum[i]=block.isi[i]=-1;

      /*reset block timer*/
      gettimeofday(&blockstart, NULL); /*note the start time for the next block*/
      timeradd(&blockstart, &blocklen, &blockend);
      if(DEBUG){printf("block starts at %d secs and ends at %d secs\n", (int)blockstart.tv_sec, (int)blockend.tv_sec);}
      operant_write(box_id, PASSLT, 0);
      nanosleep(&iti, NULL);                     /* wait intertrial interval */
      
      sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);       /*unblock term signals */
      if(DEBUG){printf("DONE WITH PASSIVE BLOCK\n\n");}
    }/*do passive block loop*/
    
    
    /* check the time*/
    curr_tt = time (NULL); 
    loctime = localtime (&curr_tt);                   
    /* if its night time then loop with lights out until morning */
    if (DEBUG){printf("minutes since midnight: %d\t starttime: %d\tstoptime: %d\n",currtime,on_time,off_time);}
    if(blocks_done==nblocks ){
      off_time=currtime-1; /*this ensures that  that once you get here you'll loop unitl 12:01 AM at least*/
      while( (currtime<on_time) || (currtime>=off_time) ){
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
	if (DEBUG){printf("minutes since midnight: %d\t starttime: %d\tstoptime: %d\n",currtime,on_time,off_time);}
      }
      
      /*now wake up*/
      /*turn on the house light*/ 
      operant_write(box_id, PASSLT, 0);
      operant_write(box_id, HOUSELT, 1);
      
      /*reset some vars for the new day */
      ++session_num;                                                                     /* increase sesion number */ 
      blocks_done=0;
      for (i = 0; i<nstims;++i){
	stimRses[i].go = 0;
	stimRses[i].no = 0;
	stimRses[i].ratio = 0.0;
	stimRses[i].count = 0;
      }
      for(i=1; i<nclasses+1; ++i){
	classRses[i].go = 0;
	classRses[i].no = 0;
	classRses[i].ratio = 0.0;
	classRses[i].count = 0;
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
      
      on_time=(starthour*60)+startmin;
      off_time=(stophour*60)+stopmin;
      
      /* adjust the off_time to fit an even number of blocks */
      /* for every active block you must run a passive block */ 
      if(difftime(curr_tt, rise_tt)!=0){
	strftime(temphour, 16, "%H", localtime(&curr_tt));
	strftime(tempmin, 16, "%M", localtime(&curr_tt));
	session_start = (60*atoi(temphour)) + atoi(tempmin);
      }
      else
	session_start=on_time;
      
      /* figure out how many blocks to run today*/
      if(DEBUG){printf("session start: %d, \tsession stop:%d\n", session_start, off_time);}
      nblocks=floor((off_time-session_start)/(blockdur/60));
      if(nblocks%2) /*if nblocks is odd*/
	nblocks--; /*make it even*/
      off_time = session_start+(nblocks*blockdur/60);
      blocklen.tv_sec = blockdur;
      if(DEBUG){printf("session started at:%d\tsession stops at:%d; We'll run %d active and %d passive blocks, each %d secs long\n", 
		       session_start, off_time, nblocks/2, nblocks/2, blockdur);}
      
      /*reset block timer--because its been a long time since the last block ended*/
      gettimeofday(&blockstart, NULL); /*note the start time for the block*/
      timeradd(&blockstart, &blocklen, &blockend);
      if(DEBUG){printf("the %d sec block starts at %d secs and ends at %d secs\n", (int) blocklen.tv_sec, (int)blockstart.tv_sec, (int)blockend.tv_sec);}
      
    }/*end night timer conditional, now start a new session*/
    
  }while (1); /* if (1) loop forever */
  
  /*  Cleanup */
  fclose(datafp);
  fclose(dsumfp);
  snd_pcm_close(handle);
  return 0;
}                         
