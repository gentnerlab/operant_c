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

#define DEBUG 1

#ifndef SQR
#define SQR(a)  ((a) * (a))
#endif
 
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
#define MAXSTIM                  64          /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 512            /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define TIMEOUT_DURATION         10000000       /* default duration of timeout in microseconds */
#define CUE_DURATION             1000000       /* cue duration in microseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define HOPPER_DROP_MS           300           /*time for hopper to fall before checking that it did */
#define MAXFILESZ                220500       /* max samples allowed in snipit soundfile */
#define MAXTRIALSAMP		    882000


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
int NOxresp = 0;
int cueflag = 0;
int fullflag = 0;

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
  fprintf(stderr, "2acmatch2samp usage:\n");
  fprintf(stderr, "     [-help] [-B int] [-R int] [-fx] [-t int] [-w int] [-on int] [-off int] [-S int] <filename>\n\n");
  fprintf(stderr, "        -help        = show this help message\n");
  fprintf(stderr, "        -B int       = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "        -f           = flash left & right pokelights during response window\n");
  fprintf(stderr, "        -t 'x'       = set the timeout duration to 'x' secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -w 'x'       = set the response window duration to 'x' secs (use an integer)\n");
  fprintf(stderr, "        -xnr         = use this flag to enable correction trials for 'no-response' trials,\n");
  fprintf(stderr, "        -xoff        = use this flag to disable all correction trials (default = correction for X trials, not NR trials)\n");
  fprintf(stderr, "        -n           = use this flag to change the number of times a target stimulus is repeated trial by trial (default = 1)\n");
  fprintf(stderr, "        -on int:int      = set hour:min for exp to start eg: '-on 7:35' (default is 7AM, use 99 for sunrise)\n");
  fprintf(stderr, "        -off int:int     = set hour:min for exp to stop eg: '-off 19:01' (default is 7PM, use 99 for sunset)\n");
  fprintf(stderr, "        -S int       = specify the subject ID number (required)\n");
  fprintf(stderr, "        -b f         = specify duration of silence break between match and sample in seconds(float:float)(default = 0.5 s)\n");
  fprintf(stderr, "        -pt f        = specify probability of target match (class 1)(default = 0.5)\n");  
  fprintf(stderr, "        -dBt f       = dB SPL level of target/change stimuli (default = 65.0 dB)\n");
  fprintf(stderr, "        -dBd f       = dB SPL level of distracter stimuli (default = 0 dB)\n");
  fprintf(stderr, "        -FULL        = enable full task: set distracter dB equal to target dB\n");
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
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *resp_wind, float *timeout_val, float *breakdur, float *ptarg,  int *new_trialtarg, float *targ_dB, float *dist_dB, char **stimfname)
{
  int i=0;
  
  for (i = 1; i < argc; i++){
    if (*argv[i] == '-'){
      if (strncmp(argv[i], "-B", 2) == 0)          
        sscanf(argv[++i], "%i", box_id);
      else if (strncmp(argv[i], "-S", 2) == 0)     
        sscanf(argv[++i], "%i", subjectid);
      else if (strncmp(argv[i], "-xnr", 4) == 0)   
	xresp = 1;
      else if (strncmp(argv[i], "-xoff", 5) == 0) 
	NOxresp = 1;
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
      else if (strncmp(argv[i], "-b", 2) == 0)     
	sscanf(argv[++i], "%f", breakdur);
      else if (strncmp(argv[i], "-pt", 3) == 0)    
	sscanf(argv[++i], "%f", ptarg);
      else if (strncmp(argv[i], "-dBt", 4) == 0)  
	sscanf(argv[++i], "%f", targ_dB);
      else if (strncmp(argv[i], "-dBd", 4) == 0)   
	sscanf(argv[++i], "%f", dist_dB);
      else if (strncmp(argv[i], "-FULL", 5) == 0)
	fullflag = 1;
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
  char  buf[128], stimexm[128], nonmatchexm[128], distexm[128], fstim[256], cstim[256], dstim[256],timebuff[64], tod[256], 
    date_out[256], buffer[30],temphour[16],tempmin[16];
  int nclasses, nstims, stim_class, stim_type, nonmatch_type, stim_reinf, stim_punish, resp_sel, resp_acc, subjectid, period, tot_trial_num, ntargtrials = 1,numtrials,
    played, resp_wind=0,trial_num, session_num, i,j,k, correction, targval,distval, dist_type, loop, stim_number, dist_number, nonmatch_number, cue[3],nreinfloops = 400,
    *playlist=NULL, totnstims=0, mirror=0, dosunrise=0,dosunset=0,starttime,stoptime,currtime, new_trialtarg = 0, nonmatchval, sv, start,
    thisonlen, thisont, thisofflen, thisofft,ponmin, ponmax, poffmin, poffmax;
  float timeout_val=0.0, resp_rxt=0.0, breakdur = 0.5, idealdur = 2.0, normdB = 65.0, 
    dist_rms, targ_rms, foo1, foo2, distdB = 0.0, thisp, ptarg = 0.5;
  double nidealsamps, nbreaksamps, fs = 44100, padded, noutsamps;
  int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval stimoff, resp_window, resp_lag, resp_rt;
  struct tm *loctime;
  Failures f = {0,0,0,0};
  int left = 0, right= 0, center = 0, fed = 0;
  int reinfor_sum = 0, reinfor = 0;
  short  *soundvecout, stim_spw[MAXFILESZ], nonmatch_spw[MAXFILESZ], dist_spw[MAXFILESZ], compoundout[30*44100];
  struct stim{
    char exemplar[128];

    int type;
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

  int nbits = 16;
  int bw=6.0206*nbits;
  foo1=(normdB-bw)/20.0;
  targ_rms = pow(10,foo1);
  foo2 = (distdB-bw)/20.0;
  dist_rms = pow(10,foo2);
       
  if(DEBUG){
    fprintf(stderr, "starting");}

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

  /* set up trial time period probabilities */
  poffmin = 5;
  poffmax = 20;
  ponmin = 20;
  ponmax = 40;

  /* Parse the command line */
  command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &resp_wind, &timeout_val, &breakdur, &ptarg, &new_trialtarg, &normdB, &distdB, &stimfname); 
  fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, xresp=%d, NOxresp=%d, cue=%d, resp_wind=%d, timeout_val=%f, flash=%d, breakdur=%f, ptarg=%f, trialtarg=%d, targdB= %.4f, distdB = %.4f, fullflag = %d, stimfile: %s\n",box_id, subjectid, starthour, stophour, startmin, stopmin, xresp, NOxresp, cueflag, resp_wind, timeout_val, flash, breakdur, ptarg, new_trialtarg, normdB, distdB, fullflag, stimfname);

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
  fprintf(stderr, "\nresponse window duration set to %d secs\n", (int) respoff.tv_sec);	
  if(timeout_val>0.0)
    timeout_duration = (int) (timeout_val*1000000);
  fprintf(stderr, "timeout duration set to %d microsecs\n", (int) timeout_duration);
  if (new_trialtarg > 0)
    ntargtrials = new_trialtarg;
  fprintf(stderr, "target held constant for %d consecutive trials\n", (int) ntargtrials);
  fprintf(stderr, "distracter dB set to %.4f\n", distdB);
  fprintf(stderr, "target dB set to %.4f\n", normdB);
  fprintf(stderr, "break duration set to %.4f sec\n", breakdur);
  fprintf(stderr, "probability of match set to %.4f\n\n", ptarg);

	
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
  if(NOxresp){fprintf(stderr, "!!WARNING: Disabling correction trials for incorrect responses!!\n");}
	
  nidealsamps =(ceil(idealdur*fs));
  if(DEBUG){printf("idealdur %f, fs %d, idealsamps: %d\n", idealdur, (int)fs, (int)nidealsamps);}

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
      sscanf(buf, "%s\%d\%d\%d\%d", stimulus[i].exemplar, &stimulus[i].type, &stimulus[i].freq, &stimulus[i].reinf, &stimulus[i].punish);
      if((stimulus[i].freq==0) || (stimulus[i].reinf==0)|| (stimulus[i].punish==0)){
	printf("ERROR: insufficent data or bad format in '.stim' file. Try '2acstreamchg -help'\n");
	exit(0);} 
      totnstims += stimulus[i].freq;
      if(DEBUG){printf("totnstims: %d\n", totnstims);}
      strcpy (stimexm, stimulus[i].exemplar);                       /* get exemplar filename */
      sprintf(fstim,"%s%s", STIMPATH, stimexm);                                /* add full path to file name */   
	    
      /*check the reinforcement rates */
      if (DEBUG){fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct responses\n", 
			 stimulus[i].exemplar, stimulus[i].reinf);
      fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect responses\n", 
	      stimulus[i].exemplar, stimulus[i].punish);
      }
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
      else if(DEBUG){fprintf(stderr, "Reinforcement rate on probe trials is set to %d%% pct for correct GO responses, 100%% for incorrect GO responses\n", 
			     stimulus[i].reinf);
      }
	    
    }
  }
  else 
    {
      fprintf(stderr,"Error opening stimulus input file! Try '2acstreamchg -help' for proper file formatting.\n");  
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
  sprintf(datafname, "%i.SOD_rDAT", subjectid);
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
  fprintf (datafp, "Sess#\tTrl#\tType\tStim\t\t\tNM\t\t\tDist\t\t\tT_class\tR_sel\tR_acc\tBRdur\tCHdur\tRT\tReinf\tTOD\tDate\n");
	 
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
  nidealsamps = ceil(idealdur*fs);
  nbreaksamps = ceil(breakdur*fs);
  padded = (long) (period * ceil(nidealsamps/ period));

  thisonlen = rand() % (ponmax-ponmin) + ponmin;
  thisofft = currtime+thisonlen;
  thisofflen = rand() % (poffmax-poffmin) + poffmin;
  thisont = currtime + thisonlen + thisofflen;

  do{                                                                               /* start the main loop */
   
    while ((currtime>=starttime) && (currtime<stoptime)){                          /* start main trial loop */
      srand(time(0));
      operant_write (box_id, RGTKEYLT, 0);
      operant_write (box_id, CTRKEYLT, 0);
      operant_write (box_id, LFTKEYLT, 0);
    
      stim_number = -1;
      /* select target stim exemplar at random */ 
      targval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));           
      if (DEBUG){printf("target exemplar:  %d\t", targval);}
      stim_number = playlist[targval];
      stim_type = 3;
      while(stim_type > 2){
	stim_reinf = stimulus[stim_number].reinf;
	stim_punish = stimulus[stim_number].punish;
	stim_type = stimulus[stim_number].type;
      }
      strcpy (stimexm, stimulus[stim_number].exemplar);                       /* get exemplar filename */
      sprintf(fstim,"%s%s", STIMPATH, stimexm);                                /* add full path to file name */

      soundvecout = (short *) malloc(sizeof(int)*padded);
      nidealsamps = ceil(idealdur*fs);
      if (DEBUG){printf("nidealsamps = %d, \t", (int) nidealsamps);}
      if((sv = getsoundvec(fstim, nidealsamps, soundvecout, normdB, period))!=1){
	fprintf(stderr, "could not convert stimfile %s to sound pressure wav. Program aborted %s\n",
		stimexm, asctime(localtime (&curr_tt)) );
	fprintf(datafp, "could not convert stimfile %s to sound pressure wav. Program aborted %s\n",
		stimexm, asctime(localtime (&curr_tt)) );
	free(soundvecout);
	fclose(datafp);
	fclose(dsumfp);
	exit(-1);
      }
	
      /*fill target spw*/
      if(DEBUG){printf("\t filling spw\n");}
      for (i=0; i<nidealsamps; i++){
	stim_spw[i] = soundvecout[i];
	soundvecout[i] = 0;
      }


      numtrials = 0;
      if(DEBUG){printf("numtrials:%d, ntargtrials = %d\n",numtrials, ntargtrials);}

      while (numtrials < ntargtrials){
	numtrials ++;
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
	if (DEBUG){printf("hour:min at trial start: %d:%d \n", atoi(hour),atoi(min));}
	currtime=(atoi(hour)*60)+atoi(min);
	while(currtime < thisofft){
	  if (DEBUG){printf("minutes since MIDNIGHT at loop start: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
	  if (DEBUG){printf("currtime: %d thisofflen: %d thisofft: %d  thisonlen: %d thisont: %d\n",currtime,thisofflen, thisofft, thisonlen, thisont);}

	  /* random seed */
	  srand(time(0));

	  /* turn off lights */
	  operant_write (box_id, RGTKEYLT, 0);
	  operant_write (box_id, CTRKEYLT, 0);
	  operant_write (box_id, LFTKEYLT, 0);

	  /* select nonmatch stim exemplar at random */ 
	  nonmatch_number = stim_number;
	  nonmatch_type = stim_type-1;
	  while((nonmatch_number == stim_number)||(nonmatch_type != stim_type)){
	    nonmatchval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));           
	    nonmatch_number = playlist[nonmatchval];
	    nonmatch_type = stimulus[nonmatch_number].type;
	  }
	  if (DEBUG){printf("nonmatch number:  %d\t", nonmatchval);}
	  strcpy (nonmatchexm, stimulus[nonmatch_number].exemplar);                       /* get exemplar filename */
	  sprintf(cstim,"%s%s", STIMPATH, nonmatchexm);                                /* add full path to file name */
	  if ((sv = getsoundvec(cstim, nidealsamps, soundvecout, normdB, period))!=1){ 
	    fprintf(stderr, "could not convert stimfile %s to sound pressure wav. Program aborted %s\n",
		    nonmatchexm, asctime(localtime (&curr_tt)) );
	    fprintf(datafp, "could not convert stimfile %s to sound pressure wav. Program aborted %s\n",
		    nonmatchexm, asctime(localtime (&curr_tt)) );
	    fclose(datafp);
	    fclose(dsumfp);
	    exit(-1);
	  }
  
	  /*fill change stim spw */
	  for (i=0; i<nidealsamps; i++){
	    nonmatch_spw[i] = soundvecout[i];
	    soundvecout[i] = 0;	
	  }     
	    
	  /* determine trial stim class*/
	  thisp = ((double)rand())/((double)(RAND_MAX) + 1.0);
	  if (thisp <= ptarg){
	    stim_class = 1;}
	  else{
	    stim_class = 2;}
	
	
	  if (DEBUG){printf("thisp = %.4f, ptarg = %.4f, trial class:  %d\n", thisp, ptarg, stim_class);}
	
       
	  /*add distracter*/
	  dist_type = stim_type;                           	
	  while((dist_type == stim_type)||(dist_type == (stim_type+2))){
	    distval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));           
	    dist_number = playlist[distval];
	    dist_type = stimulus[dist_number].type;
	    while(dist_number == nonmatch_number){
	      distval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));           
	      dist_number = playlist[distval];
	    }
	  }

	  strcpy (distexm, stimulus[dist_number].exemplar);                       /* get exemplar filename */
	  sprintf(dstim,"%s%s", STIMPATH, distexm);                           
      	  
	  if ((sv = getsoundvec(dstim, nidealsamps, soundvecout, distdB,period))!=1){
	    fprintf(stderr, "could not convert stimfile %s to sound pressure wav. Program aborted %s\n",
		    nonmatchexm, asctime(localtime (&curr_tt)) );
	    fprintf(datafp, "could not convert stimfile %s to sound pressure wav. Program aborted %s\n",
		    nonmatchexm, asctime(localtime (&curr_tt)) );
	    fclose(datafp);
	    fclose(dsumfp);
	    exit(-1);
	  }

	  if(DEBUG){printf("filling spw\n");}
	  /*fill dist spw */
	  for (j=0; j<nidealsamps; j++){
	    dist_spw[j] = soundvecout[j];
	    soundvecout[j] = 0;
	  }

	  /*fill compound spw */    
	  noutsamps = 0;
	  for (j=0; j<nidealsamps; j++){
	    compoundout[j] = stim_spw[j];
	    noutsamps++;
	  }
	
	  start = nidealsamps;
	  if(DEBUG){printf("start = %d    ", start);}

	  for (j=0; j<nbreaksamps; j++){
	    compoundout[start+j] = 0;
	    noutsamps++;
	  }

	  start = nidealsamps+nbreaksamps;
	  if(DEBUG){printf("start = %d    ", start);}

	  for (j=0; j<nidealsamps; j++){
	    if (stim_class == 1){
	      compoundout[start+j] = stim_spw[j]+dist_spw[j];} 
	    else {
	      compoundout[start+j] = nonmatch_spw[j]+dist_spw[j];}
	    noutsamps++;
	  }

	  if(DEBUG){printf("start+j = %d\n", start+j);}

	  /*if (DEBUG){printf("trialsamps: %d, total samp count = %d\n", (int) trialsamps, sampcount);}*/
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

	    /* play the stimulus*/
	    if (DEBUG){printf("START playback, trial type %d ", stim_class);}
	    if ((played = playsoundvec(compoundout, noutsamps, period))!=1){
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

	    /* pause for response */
	    if (DEBUG){printf("flag: pause before response");}
	    loop = 0;
	    do{
	      nanosleep(&rsi, NULL);
	      ++loop;
	    }while (loop < 300) ; 


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
		loop = 0;
		do{
		  nanosleep(&rsi, NULL);
		  ++loop;
		  if(loop%80==0){
		    if(loop%160==0){ 
		      operant_write (box_id, LFTKEYLT, 1);
		      operant_write (box_id, RGTKEYLT, 1);
		      operant_write (box_id, CTRKEYLT, 1);
		    }
		    else{
		      operant_write (box_id, LFTKEYLT, 0);
		      operant_write (box_id, RGTKEYLT, 0);
		      operant_write (box_id, CTRKEYLT, 0);
		    }
		  } 
		}while (loop < nreinfloops);
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
		loop = 0;
		do{
		  nanosleep(&rsi, NULL);
		  ++loop;
		  if(loop%80==0){
		    if(loop%160==0){ 
		      operant_write (box_id, LFTKEYLT, 1);
		      operant_write (box_id, RGTKEYLT, 1);
		      operant_write (box_id, CTRKEYLT, 1);
		    }
		    else{
		      operant_write (box_id, LFTKEYLT, 0);
		      operant_write (box_id, RGTKEYLT, 0);
		      operant_write (box_id, CTRKEYLT, 0);
		    }
		  } 
		}while(loop < nreinfloops);
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
	    fprintf(datafp, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.4f\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, 
		    correction, stim_number, nonmatch_number, dist_number, stim_class, resp_sel, resp_acc, breakdur, resp_rxt, reinfor, tod, date_out );
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
	    if ((resp_acc == 0)&&(NOxresp == 0))
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
	    	    
	  }while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); /* correction trial loop */                           
             
	} /* on off sessions loop */
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
	currtime=(atoi(hour)*60)+atoi(min);
	if (currtime >= thisofft){
	  if(DEBUG){printf("stopping work period: %d until %d", currtime, thisont);}
	  operant_write (box_id, RGTKEYLT, 1);
	  operant_write (box_id, CTRKEYLT, 1);
	  operant_write (box_id, LFTKEYLT, 1);
	  while (currtime < thisont){
	    curr_tt = time(NULL);
	    loctime = localtime (&curr_tt);
	    strftime (hour, 16, "%H", loctime);
	    strftime(min, 16, "%M", loctime);
	    currtime=(atoi(hour)*60)+atoi(min);
	    sleep (sleep_interval);
	  }
	}
	if (currtime >= thisont){	  
	  operant_write (box_id, RGTKEYLT, 0);
	  operant_write (box_id, CTRKEYLT, 0);
	  operant_write (box_id, LFTKEYLT, 0);
	  thisonlen = rand() % (ponmax-ponmin) + ponmin;
	  thisofft = currtime+thisonlen;
	  thisofflen = rand() % (poffmax-poffmin) + poffmin;
	  thisont = currtime + thisonlen + thisofflen;
	  if(DEBUG){printf("starting work period: %d until %d", currtime, thisofft);}
	}
      }
    }
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

    thisonlen = rand() % (ponmax-ponmin) + ponmin;
    thisofft = currtime+thisonlen;
    thisofflen = rand() % (poffmax-poffmin) + poffmin;
    thisont = currtime + thisonlen + thisofflen;

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




