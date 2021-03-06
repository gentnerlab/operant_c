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
int pnoq = -1;
int pgoodq = -1;
int pbadq = -1;
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
  fprintf(stderr, "        -S int       = specify the subject ID number (required)\n");
  fprintf(stderr, "        filename     = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "        -lag         = specify lag range (e.g. 800:1800 for minlag 800ms, maxlag 1800ms)\n");
  fprintf(stderr, "        -splT        = specify sound level range (e.g. 45:75 for min 45 dB, max 75 dB) \n");
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
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *resp_wind, float *timeout_val, int *pnoq, int *pgoodq, int * pbadq, int *minlag, int *maxlag, int *minsplT, int *maxsplT, char **stimfname)
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
      else if (strncmp(argv[i], "-on", 3) == 0)
        sscanf(argv[++i], "%i:%i", starthour, startmin);
      else if (strncmp(argv[i], "-off", 4) == 0)
        sscanf(argv[++i], "%i:%i", stophour, stopmin);
      else if (strncmp(argv[i], "-Q", 2) == 0)
        sscanf(argv[++i], "%i:%i:%i", pnoq, pgoodq, pbadq);
      else if (strncmp(argv[i], "-lag", 4) == 0)
        sscanf(argv[++i], "%i:%i", minlag, maxlag);
      else if (strncmp(argv[i], "-splT", 5) == 0)
	sscanf(argv[++i], "%i:%i", minsplT, maxsplT);
      //else if (strncmp(argv[i], "-dB2", 3) == 0)
      //sscanf(argv[++i], "%i:%i:%i", mindb2, maxdb2, db2step);
      else if (strncmp(argv[i], "-help", 5) == 0){
        do_usage();
      }
      else{
        fprintf(stderr, "Unknown option: %s\t", argv[i]);
        fprintf(stderr, "Try '3ac -help'\n");
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
  const char noq[] = "NOCUE";
  const char delimiters[] = " .,;:!-";
  char datafname[128], hour[16], min[16], month[16],day[16], year[16], dsumfname[128], stimftemp[128], pcm_name[128];
  char  buf[128],cuexm[128],stimexm[128],fstim[256],timebuff[64],tod[256],date_out[256],buffer[30],temphour[16],tempmin[16], cstim[128];
  int nclasses, nstims, stim_class, stim_reinf, stim_punish, resp_sel, resp_acc, subjectid, period, tot_trial_num, 
    played, resp_wind=0,trial_num, session_num, i,j,k, correction,playval,loop,stim_number,minlag,maxlag,minsplT,maxsplT,qcond,cueval,
    *playlist=NULL,totnstims=0,dosunrise=0,dosunset=0,starttime,stoptime,currtime,classtest,doprobes,class_idx,doq,UBsplT,spl_target,
    wn_spl,wn_dur,UBlag,onsetlag;
  float timeout_val=0.0, resp_rxt=0.0;
  int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval stimoff, resp_window, resp_lag, resp_rt;
  struct tm *loctime;
  Failures f = {0,0,0,0};
  int left = 0, right= 0, center = 0, fed = 0;
  int reinfor_sum = 0, reinfor = 0;
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
  }Rstim[MAXSTIM], Tstim[MAXSTIM], Rclass[MAXCLASS], Tclass[MAXCLASS];
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
  command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &resp_wind, &timeout_val, &pnoq, &pgoodq, &pbadq, &minlag, &maxlag, &minsplT, &maxsplT, &stimfname); 
  if(DEBUG){
    fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, xresp=%d, resp_wind=%d timeout_val=%f flash=%d stimfile:%s pnoq=%d, pgoodq=%d, pbadq=%d, minlag=%d, maxlag=%d\n",
	    box_id, subjectid, starthour, stophour, startmin, stopmin, xresp, resp_wind, timeout_val, flash, stimfname, pnoq, pgoodq, pbadq, minlag, maxlag);
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
    fprintf(stderr, "\tERROR: try '3ac -help' for available options\n");
    exit(-1);
  }
  if ((pnoq < 0)||(pgoodq<0)||(pbadq<0)){
    fprintf(stderr, "\tYou must enter presentation frequenicies for the different cue conditions\n"); 
    fprintf(stderr, "\tERROR: try '3ac -help' for options\n");
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
  nstims = nclasses = 0;
  classtest = -9999;
  if ((stimfp = fopen(stimfname, "r")) != NULL){
    while (fgets(buf, sizeof(buf), stimfp))
      nstims++;
    fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
    rewind(stimfp);
	      
    for (i = 0; i < nstims; i++){
      fgets(buf, 128, stimfp);
      stimulus[i].freq = stimulus[i].reinf=0;
      sscanf(buf, "%d\%s\%s\%d\%d\%d", 
	     &stimulus[i].class, stimulus[i].exemplar, stimulus[i].cue, &stimulus[i].freq, &stimulus[i].reinf, &stimulus[i].punish);
      if((stimulus[i].freq==0) || (stimulus[i].reinf==0)|| (stimulus[i].punish==0)){
	printf("ERROR: insufficnet data or bad format in '.stim' file. Try '3ac -help'\n");
	exit(0);} 
      totnstims += stimulus[i].freq;
      if(DEBUG){printf("totnstims: %d\n", totnstims);}
      
      /* count stimulus classes*/
      if (classtest<stimulus[i].class){/*this only works if the stimclasses increase throught the stim file*/
	classtest=stimulus[i].class;
	nclasses++;
      }
      if (DEBUG){printf("nclasses: %d\n", nclasses);}
      
      /*check the reinforcement rates */
      if(stimulus[i].class==-1){
	fprintf(stderr, "Correct nogo responses are not reinforced\n");
	fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect LEFT/CENTER/RIGHT responses\n", 
		stimulus[i].exemplar, stimulus[i].punish);
      }
      else if(stimulus[i].class==0){
	fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct CENTER responses\n", 
		stimulus[i].exemplar, stimulus[i].reinf);
	fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect LEFT/RIGHT responses\n", 
		stimulus[i].exemplar, stimulus[i].punish);
      }
      else if (stimulus[i].class==1){
	fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct LEFT responses\n", 
		stimulus[i].exemplar, stimulus[i].reinf);
	fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect CENTER/RIGHT responses\n", 
		stimulus[i].exemplar, stimulus[i].punish);
      }
      else if (stimulus[i].class==2){
	fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct RIGHT responses\n", 
		stimulus[i].exemplar, stimulus[i].reinf);
	fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect CENTER/LEFT responses\n", 
		stimulus[i].exemplar, stimulus[i].punish);
      }
      else{
	fprintf(stderr, "p(food)= %d%%,  p(timeout)=%d%% on probe trials with '%s'\n", 
		stimulus[i].reinf, stimulus[i].punish, stimulus[i].exemplar);
	doprobes=1;
      }
    }
  }
  else{
    fprintf(stderr,"Error opening stimulus input file! Try 'gng_probe -help' for proper file formatting.\n");  
    snd_pcm_close(handle);
    exit(0);     
  }
  fclose(stimfp);
  if(DEBUG){printf("Done reading in stims; %d stims in %d classes found\n", nstims, nclasses);}
  
  /* Don't allow correction trials when probe stimuli are presented */
  if(xresp==1 && doprobes==1){
    fprintf(stderr, "ERROR!: You cannot use correction trials and probe stimuli in the same session.\n  Exiting now\n");
    snd_pcm_close(handle);
    exit(-1);
  }
  
  /* build the stimulus playlist */
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
  sprintf(datafname, "%i_%s.3ac_rDAT", subjectid, stimfroot);
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
  fprintf (datafp, "Sess#\tTrl#\tType\tStimulus\tClass\tR_sel\tR_acc\tRT\tReinf\tTOD\tDate\n");
  
  /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
  ********************************************/
  session_num = 1;
  trial_num = 0;
  tot_trial_num = 0;
  correction = 1;
	
  /*zero out the response tallies */
  for(i = 0; i<nstims;++i){
    Rstim[i].C = Rstim[i].X = Rstim[i].no = Rstim[i].count =0;
    Tstim[i].C = Tstim[i].X = Tstim[i].no = Tstim[i].count = 0;
    Rstim[i].left = Rstim[i].center = Rstim[i].right =0;
    Tstim[i].left = Tstim[i].center = Tstim[i].right = 0; }
  for(i=1;i<=nclasses;i++){
    Rclass[i].C = Rclass[i].X = Rclass[i].no = Rclass[i].count = 0;
    Tclass[i].C = Tclass[i].X = Tclass[i].no = Tclass[i].count = 0;
    Rclass[i].left = Rclass[i].center = Rclass[i].right = 0;
    Tclass[i].left = Tclass[i].center = Tclass[i].right = 0;
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
      playval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));            /* select stim exemplar at random */ 
      if (DEBUG){printf(" %d\t", playval);}
      stim_number = playlist[playval];
      stim_class = stimulus[stim_number].class;                               /* set stimtype variable */
      class_idx = stim_class+2;
      strcpy (stimexm, stimulus[stim_number].exemplar);                       /* get exemplar filename */
      stim_reinf = stimulus[stim_number].reinf;
      stim_punish = stimulus[stim_number].punish;
      sprintf(fstim,"%s%s", STIMPATH, stimexm);                                /* add full path to file name */
	
      /*figure out which cue condition you want to run for this trial*/
      qcond = 1+(int) (100.0*rand()/(RAND_MAX+1.0)); 
      if(DEBUG){fprintf(stderr, "qcond=%d\n", qcond);}
      if (qcond <= pgoodq){
	/*run 'valid cue' trial  */
	strcpy(cuexm, stimulus[stim_number].cue);
	doq=1;
	if(DEBUG){fprintf(stderr, "using '%s' as the cue stimulus for this trial\n", cuexm);}
      }
      else if (qcond <= (pgoodq+pbadq)){
	/*run 'invalid cue' trial*/
	cueval=(int)((nstims+0.0)*rand()/(RAND_MAX+1.0));
	while (cueval == stim_number)
	  cueval=(int)((nstims+0.0)*rand()/(RAND_MAX+1.0));
	strcpy(cuexm, stimulus[cueval].cue);
	doq=1;
	if(DEBUG){fprintf(stderr, "using '%s' as the cue stimulus for this trial\n", cuexm);}	
      }
      else {
	/*run 'no cue' trial*/
	strcpy(cuexm, noq);
      	doq=0;
	if(DEBUG){fprintf(stderr, "using '%s' as the cue stimulus for this trial\n", cuexm);}
      } 
      sprintf(cstim,"%s%s", STIMPATH, cuexm);      
      
      /*set the target level*/
      UBsplT = (maxsplT-minsplT)+0.0;
      spl_target = minsplT + (int) (UBsplT*rand()/(RAND_MAX+0.0)); 

      /*noise: white 2 sec @ 75dB */
      wn_spl = 75.0;
      wn_dur = 2000;

      /*get the onset lag*/
      UBlag = (maxlag-minlag)+0.0;
      onsetlag = minlag + (int) (UBlag*rand()/(RAND_MAX+0.0)); 



      do{                                             /* start correction trial loop */
	left = right = center = 0;        /* zero trial peckcounts */
	resp_sel = resp_acc = resp_rxt = 0;                 /* zero trial variables        */
	++trial_num;++tot_trial_num;
	    
	/* Wait for center key press */
	if (DEBUG){printf("\n\nWaiting for left or right key press\n");}
	operant_write (box_id, HOUSELT, 1);        /* house light on */
	right=left=center=0;
	do{                                         
	  nanosleep(&rsi, NULL);	               	       
	  center = operant_read(box_id, CENTERPECK);   /*get value at center response port*/	
	}while (center==0);  

	sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/

	/*play the cue (or not) depending on the condition*/
	if(doq){
	  if ((played = playwav(cstim, period))!=1){
	    fprintf(stderr, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		    pcm_name, stimexm, asctime(localtime (&curr_tt)) );
	    fprintf(datafp, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		    pcm_name, stimexm, asctime(localtime (&curr_tt)) );
	    fclose(datafp);
	    fclose(dsumfp);
	    exit(-1);
	  }
	  nanosleep(&isi, NULL);
	}  
		
	/* play the stimulus*/
	if (DEBUG){printf("START '%s'\n", stimexm);}
	if((played = playmaskedwav(wn_dur, wn_spl, fstim, onsetlag, spl_target, period))!=1){
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
	
	/* Wait for response */
	if (DEBUG){printf("flag: waiting for right/left response\n");}
	timeradd (&stimoff, &respoff, &resp_window);
	if (DEBUG){respwin_sec = resp_window.tv_sec;}
	if (DEBUG){respwin_usec = resp_window.tv_usec;}
	if (DEBUG){printf("resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);}
	
	loop=left=right=center=0;
	do{
	  nanosleep(&rsi, NULL);
	  left = operant_read(box_id, LEFTPECK);
	  right = operant_read(box_id, RIGHTPECK );
	  center = operant_read(box_id, CENTERPECK);
	  if((left==0) && (right==0) && (center==0) && flash){
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
		operant_write (box_id, CTRKEYLT,0);
	      }
	    }
	  }
	  gettimeofday(&resp_lag, NULL);
	  if (DEBUG==2){printf("flag: value at right=%d, left=%d, center=%d\t", right, left, center);}
	}while ( (left==0) && (right==0) && (center==0) && (timercmp(&resp_lag, &resp_window, <)) );
                   
	operant_write (box_id, LFTKEYLT, 0);    /*make sure the key lights are off after resp interval*/
	operant_write (box_id, RGTKEYLT, 0);
	operant_write (box_id, CTRKEYLT, 0);

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
	if (DEBUG){printf("flag: stim_class = %d\n", stim_class);}
	if (DEBUG){printf("flag: exit value left = %d, right = %d\n", left, right);}


	if(stim_class == -1){/*no go stim*/
	  if ( (left==0 ) && (right==0) && (center==0) ){
	    resp_sel = -1;
	    resp_acc = 1;
	    ++Rstim[stim_number].C;++Tstim[stim_number].C;
	    ++Rclass[class_idx].C;++Tclass[class_idx].C; 
	    ++Rstim[stim_number].no;++Tstim[stim_number].no;
	    ++Rclass[class_idx].no;++Tclass[class_idx].no; 
	    reinfor = 0;
	    if (DEBUG){ printf("flag: correct no response to stimtype -1\n");}
	  }
	  else if (left == 1){
	    resp_sel = 1;
	    resp_acc = 0;	
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rclass[class_idx].X; ++Tclass[class_idx].X; 
	    ++Rstim[stim_number].left;++Tstim[stim_number].left;
	    ++Rclass[class_idx].left; ++Tclass[class_idx].left; 
	    reinfor =  timeout(stim_punish);
	    if (DEBUG){printf("flag: incorrect response to stimtype -1\n");}
	  }
	  else if (right == 1){
	    resp_sel = 2;
	    resp_acc = 0;
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rclass[class_idx].X; ++Tclass[class_idx].X; 
	    ++Rstim[stim_number].right;++Tstim[stim_number].right;
	    ++Rclass[class_idx].right;++Tclass[class_idx].right; 
	    reinfor =  timeout(stim_punish);
	    if (DEBUG){printf("flag: incorrect response to stimtype -1\n");}
	  } 
	  else if (center == 1){
	    resp_sel = 0;
	    resp_acc = 0;
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rclass[class_idx].X; ++Tclass[class_idx].X; 
	    ++Rstim[stim_number].center;++Tstim[stim_number].center;
	    ++Rclass[class_idx].center;++Tclass[class_idx].center; 
	    reinfor =  timeout(stim_punish);
	    if (DEBUG){printf("flag: incorrect response to stimtype -1\n");}
	  } 
	  else
	    fprintf(datafp, "DEFAULT SWITCH for bit value:ERROR CODE REMOVED");
	}
	else if (stim_class == 0){ /* GO CENTER*/
	  if ( (left==0 ) && (right==0) && (center==0) ){
	    resp_sel = -1;
	    resp_acc = 0;
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rclass[class_idx].X; ++Tclass[class_idx].X; 
	    ++Rstim[stim_number].no;++Tstim[stim_number].no;
	    ++Rclass[class_idx].no;++Tclass[class_idx].no; 
	    reinfor = 0;
	    if (DEBUG){ printf("flag: no response to stimtype 0\n");}
	  }
	  else if (left == 1){
	    resp_sel = 1;
	    resp_acc = 0;	
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rclass[class_idx].X; ++Tclass[class_idx].X; 
	    ++Rstim[stim_number].left; ++Tstim[stim_number].left;
	    ++Rclass[class_idx].left; ++Tclass[class_idx].left; 
	    reinfor =  timeout(stim_punish);
	    if (reinfor == 1) { ++fed;}
	    if (DEBUG){printf("flag: incorrect response to stimtype 0\n");}
	  }
	  else if (right == 1){
	    resp_sel = 2;
	    resp_acc = 0;
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rclass[class_idx].X; ++Tclass[class_idx].X; 
	    ++Rstim[stim_number].right; ++Tstim[stim_number].right;
	    ++Rclass[class_idx].right; ++Tclass[class_idx].right; 
	    reinfor =  timeout(stim_punish);
	    if (DEBUG){printf("flag: incorrect response to stimtype 0\n");}
	  } 
	  else if (center == 1){
	    resp_sel = 0;
	    resp_acc = 1;
	    ++Rstim[stim_number].C;++Tstim[stim_number].C;
	    ++Rclass[class_idx].C; ++Tclass[class_idx].C; 
	    ++Rstim[stim_number].center; ++Tstim[stim_number].center;
	    ++Rclass[class_idx].center; ++Tclass[class_idx].center; 
	    reinfor=feed(stim_reinf, &f);
	    if (DEBUG){printf("flag: correct response to stimtype 0\n");}
	  } 
	  else
	    fprintf(datafp, "DEFAULT SWITCH for bit value:ERROR CODE REMOVED");
	} 
	else if (stim_class == 1){                                 /* GO LEFT */                          
	  if ( (left==0 ) && (right==0) && (center==0) ){
	    resp_sel = -1;
	    resp_acc = 2;
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rclass[class_idx].X; ++Tclass[class_idx].X; 
	    ++Rstim[stim_number].no;++Tstim[stim_number].no;
	    ++Rclass[class_idx].no;++Tclass[class_idx].no; 
	    reinfor = 0;
	    if (DEBUG){ printf("flag: no response to stimtype 1\n");}
	  }
	  else if (left == 1){
	    resp_sel = 1;
	    resp_acc = 1;	
	    ++Rstim[stim_number].C;++Tstim[stim_number].C;
	    ++Rclass[class_idx].C; ++Tclass[class_idx].C; 
	    ++Rstim[stim_number].left; ++Tstim[stim_number].left;
	    ++Rclass[class_idx].left; ++Tclass[class_idx].left; 
	    reinfor=feed(stim_reinf, &f);
	    if (reinfor == 1) { ++fed;}
	    if (DEBUG){printf("flag: correct response to stimtype 1\n");}
	  }
	  else if (right == 1){
	    resp_sel = 2;
	    resp_acc = 0;
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rclass[class_idx].X; ++Tclass[class_idx].X; 
	    ++Rstim[stim_number].right; ++Tstim[stim_number].right;
	    ++Rclass[class_idx].right; ++Tclass[class_idx].right; 
	    reinfor =  timeout(stim_punish);
	    if (DEBUG){printf("flag: incorrect response to stimtype 1\n");}
	  } 
	  else if (center == 1){
	    resp_sel = 0;
	    resp_acc = 0;
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rclass[class_idx].X; ++Tclass[class_idx].X; 
	    ++Rstim[stim_number].center;++Tstim[stim_number].center;
	    ++Rclass[class_idx].center; ++Tclass[class_idx].center; 
	    reinfor =  timeout(stim_punish);
	    if (DEBUG){printf("flag: incorrect response to stimtype 1\n");}
	  } 
	  else
	    fprintf(datafp, "DEFAULT SWITCH for bit value:ERROR CODE REMOVED");
	}
	else if (stim_class == 2){                           /* GO RIGHT */
	  if ( (left==0) && (right==0) && (center==0) ){
	    resp_sel = -1;
	    resp_acc = 2;
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rclass[class_idx].X; ++Tclass[class_idx].X; 
	    ++Rstim[stim_number].no;++Tstim[stim_number].no;
	    ++Rclass[class_idx].no;++Tclass[class_idx].no; 
	    reinfor = 0;
	    if (DEBUG){printf("flag: no response to stimtype 2\n");}
	  }
	  else if (left == 1){
	    resp_sel = 1;
	    resp_acc = 0;
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rclass[class_idx].X; ++Tclass[class_idx].X; 
	    ++Rstim[stim_number].left; ++Tstim[stim_number].left;
	    ++Rclass[class_idx].left; ++Tclass[class_idx].left; 
	    reinfor =  timeout(stim_punish);
	    if (DEBUG){printf("flag: incorrect response to stimtype 2\n");}
	  }
	  else if (right == 1){
	    resp_sel = 2;
	    resp_acc = 1;
	    ++Rstim[stim_number].C;++Tstim[stim_number].C;
	    ++Rclass[class_idx].C; ++Tclass[class_idx].C; 
	    ++Rstim[stim_number].right;++Tstim[stim_number].right;
	    ++Rclass[class_idx].right; ++Tclass[class_idx].right; 
	    reinfor=feed(stim_reinf, &f);
	    if (reinfor == 1) { ++fed;}
	    if (DEBUG){printf("flag: correct response to stimtype 2\n");}
	  } 
	  else if (center == 1){
	    resp_sel = 1;
	    resp_acc = 0;
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rclass[class_idx].X; ++Tclass[class_idx].X; 
	    ++Rstim[stim_number].center; ++Tstim[stim_number].center;
	    ++Rclass[class_idx].center; ++Tclass[class_idx].center; 
	    reinfor =  timeout(stim_punish);
	    if (DEBUG){printf("flag: incorrect response to stimtype 2\n");}
	  } else
	    fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
	}


	/*TODO: check the probe contingencies to work with the attention test stims*/
	
	else if (stim_class >= 3){                           /* PROBE STIMULUS */
	  if ( (left==0) && (right==0) ){ /*no response to probe */
	    resp_sel = 0;
	    resp_acc = 2;
	    ++Rstim[stim_number].no;++Tstim[stim_number].no;
	    ++Rclass[class_idx].no;++Tclass[class_idx].no; 
	    reinfor = 0;
	    if (DEBUG){printf("flag: no response to probe stimulus\n");}
	  }
	  else if (left!=0){
	    resp_sel = 1;
	    if(DEBUG){printf("flag: LEFT response to PROBE\n");} 
	    resp_acc = 3;
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rstim[class_idx].X; ++Tstim[class_idx].X; 
	    reinfor =  probeRx(stim_reinf, stim_punish,&f);
	    if (reinfor){++fed;}
	  }
	  else if (right!=0){
	    resp_sel = 2;
	    if(DEBUG){printf("flag: RIGHT response to PROBE\n");}
	    resp_acc = 3;
	    ++Rstim[stim_number].X;++Tstim[stim_number].X;
	    ++Rstim[class_idx].X; ++Tstim[class_idx].X; 
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
	    fprintf(datafp, "%d\t%d\t%d\t%s\t%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, 
		    correction, stimexm, cuexm, stim_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
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

	    /*TODO: fix summary output to reflect the new response options */
	    /* Update summary data */
	    if(freopen(dsumfname,"w",dsumfp)!= NULL){
	      fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
	      fprintf (dsumfp, "\tPROPORTION CORRECT RESPONSES (by stimulus, including correction trials)\n");
	      fprintf (dsumfp, "\tStim\t\tCount\tToday     \t\tCount\tTotals\t(excluding 'no' responses)\n");
	      for (i = 0; i<nstims;++i){
		fprintf (dsumfp, "\t%s\t\t%d\t%1.4f     \t\t%d\t%1.4f\n", 
			 stimulus[i].exemplar, Rstim[i].count, (float)Rstim[i].C/(float)Rstim[i].count, Tstim[i].count, (float)Tstim[i].C/(float)Tstim[i].count );
	      }
	      fprintf (dsumfp, "\n\nPROPORTION CORRECT RESPONSES (by stim class, including correction trials)\n");
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
	    Rstim[i].C = Rstim[i].X = Rstim[i].no = Rstim[i].count = Rstim[i].left = Rstim[i].center = Rstim[i].right = 0;
	  }
	  for(i=1;i<=nclasses;i++){
	    Rclass[i].C = Rclass[i].X = Rclass[i].no = Rclass[i].count = Rclass[i].left = Rclass[i].center = Rclass[i].right = 0;
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
	  
	  
	  
  }while (1);
    
  /*  Cleanup */
  fclose(datafp);
  fclose(dsumfp);
  return 0;
}

