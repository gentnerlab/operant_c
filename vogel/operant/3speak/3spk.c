#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include "/usr/local/src/operantio/operantio.c"
#include <sunrise.h>
#include "/usr/local/src/audioio/audoutCH.c"
#include "/usr/local/src/audioio/audoutCH.h" /* contains parameters of PECK struct, including  max pecks */
//#define MAXPECKS 1024
/*   struct PECK{ */
/*   int left; */
/*   int center; */
/*   int right; */
/*   float time_left [MAXPECKS]; */
/*   float time_centner [MAXPECKS]; */
/*   float time_right [MAXPECKS]; */
/* }; */

#define DEBUG 3

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
/*inputs: */
#define LEFTPECK   1
#define CENTERPECK 2
#define RIGHTPECK  3
#define HOPPEROP   4
/* outputs: */
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
#define MAXCLASS                 256           /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define CUE_STIM_INTERVAL        100000000     /* input polling rate in nanoseconds */
#define CUE_INTERVAL             2000000      /* duration of cue in microseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define TIMEOUT_DURATION         2000000       /* default duration of timeout in microseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define DEF_REF                  5             /* default reinforcement for corr. resp. set to 100% */
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
int  reinf_val = DEF_REF;
const char exp_name[] = "3SPK";
int box_id = -1;
int flash = 0;
int xresp = 0;

typedef struct {
  int hopper_failures;
  int hopper_wont_go_down_failures;
  int hopper_already_up_failures;
  int response_failures;
} Failures; 

const char pcm_name[] = "test";                    /* name of pcm - note: specific to box _ */
snd_pcm_t *handle;
unsigned int channels = 1;                      /* count of channels */
unsigned int rate = 44100;                      /* stream rate */
snd_pcm_format_t format = SND_PCM_FORMAT_S16;   /* sample format */
unsigned int buffer_time = 500000;              /* ring buffer length in us */
unsigned int period_time = 100000;              /* period time in us */
int resample = 1;                               /* enable alsa-lib resampling */

snd_pcm_sframes_t buffer_size;
snd_pcm_sframes_t period_size;
snd_output_t *output = NULL;

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timespec isi = { 0, CUE_STIM_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};

/* -------- Signal handling --------- */
int client_fd = -1;
static void sig_pipe(int signum)
{ fprintf(stdout,"SIGPIPE caught\n"); client_fd = -1;}
static void termination_handler (int signum){
  snd_pcm_close(handle);
  fprintf(stdout,"closed pcm device: term signal caught: exiting\n");
  exit(-1);
}

/***********************************************(box_id,HOPPEROP**********************************
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
  fprintf(stderr, "3ac2 usage:\n");
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
  fprintf(stderr, "        -spk int      = specify number of speakers to use (maximum determined by DAC setup and number of speakers.\n");
  fprintf(stderr, "                        default = 3 speakers.\n");
  fprintf(stderr, "        -wav int      = specify number of wav targets to use (maximum determined by stimfile, default = 2).\n");
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
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *resp_wind, float *timeout_val, int *pnoq, int *pgoodq, int * pbadq, int *n_speak, int *n_wav char **stimfname)
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
      else if (strncmp(argv[i], "-help", 5) == 0){
        do_usage();
      }
      else if (strncmp(argv[i], "-spk", 4) == 0)
        sscanf(argv[++i], "%i",n_speak);
      else if (strncmp(argv[i], "-wav", 4) == 0)
        sscanf(argv[++i], "%i",n_wav);
      else{
        fprintf(stderr, "Unknown option: %s\t", argv[i]);
        fprintf(stderr, "Try '3ac2 -help'\n");
      }
    }
    else
      {
        *stimfname = argv[i];
      }
  }
  return 1;
}

/*******************************************************************************
 ** main                                                                       *
 ** input should be: 3spk pcmname maskerwav wav1 wav2 ... wavnchan          ** * 
 ** note, cannot play more wave files than channels available on pcm handle ** *
 **                                                                            *
 ******************************************************************************/
int main(int argc, char *argv[])
{	
  FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
  char *stimfname = NULL;
  char *stimfroot, fullsfname[256];
  const char delimiters[] = " .,;:!-";
  char datafname[128], hour [16], min[16], month[16], year[16], 
    day[16], dsumfname[128], stimftemp[128];
  char  buf[128], stimexm[128],fstim[256],temphour[16],tempmin[16],
    timebuff[64], tod[256], date_out[256], buffer[30];
  int nclasses, nstims, stim_class, C2_stim_number,C1_stim_number, stim_reinf,offstep1,offstep2,stepval, 
    subjectid, num_c1stims, num_c2stims,stimdurtest,swapval=-1,rswap,swapcnt,
     resp_wind=0, trial_num, session_num, C2_pval,C1_pval, *C2_plist=NULL, *C1_plist=NULL, 
    tot_c1stims=0, tot_c2stims=0,dosunrise=0,dosunset=0, starttime,stoptime,currtime,stim_num, cur_tt, loctime;
  long tot_trial_num;
  unsigned long int temp_dur,offset1,offset2,offset,sd;
  int fed = 0;
  Failures f = {0,0,0,0};
  int reinfor_sum = 0, reinfor = 0, stimrange;
  struct stim {
    char exemplar[128];
    int class;
    int reinf;
    int freq;
    unsigned long int dur; 
    int num;
  }C1stim[MAXSTIM], C2stim[MAXSTIM],tmp;
  struct data {
    int trials;
    int rLFT;
    int rCTR;
    int rRGT;
    char name[128];
  } class[MAXCLASS], stim[MAXSTIM];
  
  struct PECK trial, *trl;
  trl = &trial;	
  sigset_t trial_mask;
  
  char *wavf, *maskwav, *outsfname;
  int n_speak = 3, n_wav = 2;
  SNDFILE *sf, *sfout=NULL;
  SF_INFO *sfinfo, *sfout_info;
  short *obuff, *obuffm,*obuff1;
  sf_count_t incount, fcheck=0;
  double inframes;
  snd_pcm_uframes_t outframes;
  int i, j,played, ranchan;
  int wavsr, masksr;
  
  struct  PECK pkstr, *pkptr;
  pkptr = &pkstr;
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
  
  fprintf("hi")

  /* Parse the command line */
  command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &resp_wind, &timeout_val, &pnoq, &pgoodq, &pbadq, &n_speak, &n_wav, &stimfname); 

  if(DEBUG){
    fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, xresp=%d,resp_wind=%d timeout_val=%f flash=%d stimfile:%s pnoq=%d, pgoodq=%d, pbadq=%d, nspeak=%d, nwav=%d\n",box_id, subjectid, starthour, stophour, startmin, stopmin, xresp, resp_wind, timeout_val, flash, stimfname, pnoq, pgoodq, pbadq, n_speak, n_wav);
  }
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
    fprintf(stderr, "\tERROR: try '3spk -help' for available options\n");
    exit(-1);
  }
  if ((pnoq < 0)||(pgoodq<0)||(pbadq<0)){
    fprintf(stderr, "\tYou must enter presentation frequenicies for the different cue conditions\n"); 
    fprintf(stderr, "\tERROR: try '3spk -help' for options\n");
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
  classtest = 0;
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
	printf("ERROR: insufficent data or bad format in '.stim' file. Try '3spk -help'\n");
	exit(0);} 
      totnstims += stimulus[i].freq;
      if(DEBUG){printf("totnstims: %d\n", totnstims);}
      
      /* count stimulus classes*/
      if (classtest<stimulus[i].class){/*this only works if the stimclasses increase throught the stim file*/
	classtest=stimulus[i].class;
	nclasses++;
      }
     
      /*check the reinforcement rates */
      
      if(stimulus[i].class==2){
	fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct CENTER responses\n", 
		stimulus[i].exemplar, stimulus[i].reinf);
	fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect LEFT/RIGHT responses\n", 
		stimulus[i].exemplar, stimulus[i].punish);
      }
    }  
  } 
  else{
    fprintf(stderr,"Error opening stimulus input file! Try 'gng_probe -help' for proper file formatting.\n");  
    snd_pcm_close(handle);
    exit(0);     
  }
  if (nclasses != 2)_{
	printf("ERROR: stimfile must have two classes: 1=masker, 2=target wavs'\n");
	exit(0);} 
  fclose(stimfp);
  if(DEBUG){printf("Done reading in stims; %d stims in %d classes found\n", nstims, nclasses);}
  
  
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
  sprintf(datafname, "%i_%s.3spk_rDAT", subjectid, stimfroot);
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
  
  if (n_wav==2){
  /* Write data file header info */
  fprintf (stderr, "Data output to '%s'\n", datafname);
  fprintf (datafp, "File name: %s\n", datafname);
  fprintf (datafp, "Procedure source: %s\n", exp_name);
  fprintf (datafp, "Start time: %s", asctime(loctime));
  fprintf (datafp, "Subject ID: %d\n", subjectid);
  fprintf (datafp, "Stimulus source: %s\n", stimfname);  
  fprintf (datafp, "Sess#\tTrl#\tType\tStimulus\t\tCue\t\t\tTarget\tsplT\tR_sel\tR_acc\tRT\tReinf\tTOD\tDate\n");
  }
  n_speak = argc-2; 
  printf("number of channels: %d\n", n_speak) ;
  if (argc < 3){
    fprintf(stderr,"must enter at least one wav file\n"); 
    exit (-1);}   
  
  if((period=setup_pcmdev(pcmname, n_speak))<0){ 
    fprintf(stderr,"FAILED to set up the pcm device %s\n", pcmname);
    exit (-1);}
  if(DEBUG==3){printf("period size is %d frames\n",period);}

  /* open masker */
  maskwav = argv [2];
  sf=NULL;
  printf("opening masker %s\n", maskwav);
  sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));
  
  if(!(sf = sf_open(wavf,SFM_READ,sfinfo))){
    fprintf(stderr,"error opening input file %s\n",wavf);
    free(&sfinfo);
    return -1;
  }
  
  masksr= sfinfo->samplerate;
  inframes = (int)sfinfo->frames;
  obuffm = (short *) malloc(sizeof(int)*inframes);
  
  if(DEBUG==3){printf("trying to sf_readf %d frames\n",(int)inframes);}
  incount = sf_readf_short(sf, obuffm, inframes);
  if(DEBUG==3){printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount, (int)inframes);}
  
  /* combine individual buffers into obuff */
  outframes = inframes * n_speak;  
  printf("outframes:%d\n", (int)outframes);
  obuff = (short *) malloc(sizeof(int)*outframes);
  
  int loops=0;
  for (j=0; j<outframes; j=j+n_speak){ /* not sure if i can do this in C */
    obuff[j] = obuffm[j/n_speak];
    obuff[j+1] = obuffm[j/n_speak];
    obuff[j+1] = obuffm[j/n_speak];
    loops=j;
  }

  /* open input files*/
  for (i=3; i<argc; i++){
    ranchan = (int)floor(((double)rand()/((double)RAND_MAX+(double)1))*n_speak);
    wavf = argv[i];
    printf("opening %s, sending to channel %d\n", wavf, ranchan);
    /* open wav file */  
    sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));
   
    if(!(sf = sf_open(wavf,SFM_READ,sfinfo))){
      fprintf(stderr,"error opening input file %s\n",wavf);
      free(&sfinfo);
    return -1;
    }
    
    wavsr= sfinfo->samplerate;
    inframes = (int)sfinfo->frames;
    obuff1 = (short *) malloc(sizeof(int)*inframes);
  
    if(DEBUG==3){printf("trying to sf_readf %d frames\n",(int)inframes);}
    incount = sf_readf_short(sf, obuff1, inframes);
    if(DEBUG==3){printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount, (int)inframes);}
    
    /* combine individual buffers into obuff */
     int loops=0;
    for (j=0; j<outframes; j=j+n_speak){ /* not sure if i can do this in C */
      obuff[j+ranchan] = obuff1[j/n_speak];
      loops=j;
    }
    printf("loops: %d\n", loops);
  }
  
  sfout_info = (SF_INFO *) malloc(sizeof(SF_INFO));
  sfout_info->channels = n_speak;
  sfout_info->samplerate = wavsr;
  sfout_info->format =sfinfo->format;
  
  if(DEBUG==3){fprintf(stderr,"output file format:%x \tchannels: %d \tsamplerate: %d\n",sfout_info->format, sfout_info->channels, sfout_info->samplerate);}
  
  /*write the ouput file*/
  outsfname="3spktest.wav";
  if(!(sfout = sf_open(outsfname,SFM_WRITE,sfout_info))){
    fprintf(stderr,"error opening output file '%s'\n",outsfname);
    return -1;
  }
  // printf("sfout: %d\n", sfout);

  outframes=inframes;
  fcheck=sf_writef_short(sfout, obuff, outframes);
  if(fcheck!=outframes){
    fprintf(stderr,"UH OH!:I could only write %lu out of %lu frames!\n", (long unsigned int)fcheck, (long unsigned int)outframes);
    return -1;
  }
  else
    if(DEBUG==3){fprintf(stderr,"success!  outframes: %lu \tfcheck: %lu \tduration: %g secs\n",
                     (long unsigned int)outframes,(long unsigned int)fcheck,(double)outframes/sfout_info->samplerate);}
  played = play_and_count
(outsfname, period, n_speak, pkptr);
  if(DEBUG){fprintf(stderr,"return from play wav was %d\n", played);}

  /*free up resources*/
  sf_close(sf);
  free(sfout_info);
  free(obuff);
  free(obuffm);
  free(obuff1);
  free(sfinfo);
 
 return 1;

 /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
 ********************************************/
 session_num = 1;
 trial_num = 0;
 rswap = 0; swapcnt = 0;
 tot_trial_num = 0;
 
 curr_tt = time(NULL);
 loctime = localtime (&curr_tt);
 strftime (hour, 16, "%H", loctime);
 if (DEBUG){printf("atoi(hour) at loop start: %d \n", atoi(hour));}
 
 curr_tt = time(NULL);
 loctime = localtime (&curr_tt);
 strftime (hour, 16, "%H", loctime);
 strftime(min, 16, "%M", loctime);
 if (DEBUG){printf("hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
 currtime=(atoi(hour)*60)+atoi(min);
 
 operant_write (box_id, HOUSELT, 1);        /* make sure houselight is on */
 
 do{                                                                               /* start the block loop */
   while((currtime>=starttime) && (currtime<stoptime)){
     if (DEBUG){printf("minutes since midnight at loop start: %d\t starttime: %d\tstoptime: %d\n",
		       currtime,starttime,stoptime);}
     
     /*cue two randomly chosen stimulus source files, one from each playlist */
     srand(time(0));
     C1_pval = (int) ((tot_c1stims+0.0)*rand()/(RAND_MAX+0.0));       /* select playlist1 entry at random */ 
     C2_pval = (int) ((tot_c2stims+0.0)*rand()/(RAND_MAX+0.0));       /* select playlist2 entry at random */ 
     C1_stim_number = C1_plist[C1_pval];
     C2_stim_number = C2_plist[C2_pval];
     if (DEBUG){printf("cued stim for c1: %s\t c2: %s\n", C1stim[C1_stim_number].exemplar, C2stim[C2_stim_number].exemplar);} 
     
     
     /* randomly select stim duration (in 250 ms increments) */
     stepval = (int) (((stimdur_range/0.25)+1)*rand()/(RAND_MAX+0.0)); 
     sd = (((float)stepval*0.25)+stimdurLB)*1000;
     
     if((C1stim[C1_stim_number].dur<=sd) || (C2stim[C2_stim_number].dur<=sd)){
       if(C1stim[C1_stim_number].dur<C2stim[C2_stim_number].dur){
	 sd=C1stim[C1_stim_number].dur;}
       else{
	 sd=C2stim[C2_stim_number].dur;}
     }
     
     /* randomly select stim offset duration (in 500 ms increments) for each stimulus*/
     offstep1 = (500*(rint((C1stim[C1_stim_number].dur - sd)/500)))/500;
     offset1 = (int)((offstep1+1.0)*rand()/(RAND_MAX+0.0))*500 ;
     offstep2 = (500*(rint((C2stim[C2_stim_number].dur - sd)/500)))/500;
     offset2 = (int)((offstep2+1.0)*rand()/(RAND_MAX+0.0))*500 ;   
     
     if(DEBUG){printf("stepval:  %d\tstim_dur: %lu\n", stepval, sd);}
     if(DEBUG){printf("stim1-offset steps:%d\toffset duration (msecs):%lu\n",offstep1, offset1);}
     if(DEBUG){printf("stim2-offset steps:%d\toffset duration (msecs):%lu\n",offstep2, offset2);}
     
     trial.left = trial.right = trial.center = 0;        /* zero trial peckcounts */
     ++trial_num;++tot_trial_num;++swapcnt;
     
     /* Wait for left or right key press */
     if (DEBUG){printf("\n\nWaiting for left or right key press\n");}
     operant_write (box_id, HOUSELT, 1);        /* house light on */
     trial.right= trial.left = trial.center = 0;
     do{                                         
       nanosleep(&rsi, NULL);	               	       
       trial.right = operant_read(box_id, RIGHTPECK);   /*get value at right peck position*/	
       trial.left = operant_read(box_id, LEFTPECK);   /*get value at left peck position*/		 	       
     }while ((trial.right==0) && (trial.left==0));  
     
     /*set your trial variables*/
     if(rswap){
       if(trial.right){
	 if (DEBUG){printf("***PECK RIGHT***\n");}
	 offset=offset1;
	 stimulus_duration = sd;
	 stim_class = C1stim[C1_stim_number].class;                              
	 strcpy (stimexm, C1stim[C1_stim_number].exemplar);                      
	 stim_reinf = C1stim[C1_stim_number].reinf;
	 stim_num = C1stim[C1_stim_number].num;
	 sprintf(fstim,"%s%s", STIMPATH, stimexm);                               
       }
       else{
	 if (DEBUG){printf("***PECK LEFT***\n");}
	 offset=offset2;
	 stimulus_duration = sd;
	 stim_class = C2stim[C2_stim_number].class;                              
	 strcpy (stimexm, C2stim[C2_stim_number].exemplar);                      
	 stim_reinf = C2stim[C2_stim_number].reinf;
	 stim_num = C2stim[C2_stim_number].num;
	 sprintf(fstim,"%s%s", STIMPATH, stimexm);                               
       }
     }
     else{
       if(trial.left){
	 if (DEBUG){printf("***PECK LEFT***\n");}
	 offset=offset1;
	 stimulus_duration = sd;
	 stim_class = C1stim[C1_stim_number].class;                              
	 strcpy (stimexm, C1stim[C1_stim_number].exemplar);                      
	 stim_reinf = C1stim[C1_stim_number].reinf;
	 stim_num = C1stim[C1_stim_number].num;
	 sprintf(fstim,"%s%s", STIMPATH, stimexm);                               
       }
       else{
	 if (DEBUG){printf("***PECK RIGHT***\n");}
	 offset=offset2;
	 stimulus_duration = sd;
	 stim_class = C2stim[C2_stim_number].class;                              
	 strcpy (stimexm, C2stim[C2_stim_number].exemplar);                      
	 stim_reinf = C2stim[C2_stim_number].reinf;
	 stim_num = C2stim[C2_stim_number].num;
	 sprintf(fstim,"%s%s", STIMPATH, stimexm);                               
       }
     }
     if(DEBUG){
       printf("class: %d\t", stim_class);
       printf("reinf: %d\t", stim_reinf);
       printf("name: %s\n", stimexm);
       printf("full stim path: %s\n", fstim);
     }
     
     sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
     
     /* Play stimulus file */
     if(DEBUG){printf("STARTING PLAYBACK '%s'\n", stimexm);}
     if(DEBUG){printf("pre-play L-C-R peckcounts: %d-%d-%d\n",trial.left, trial.center, trial.right);}
     if (play_and_count(fstim, period,stimulus_duration, offset, box_id, trl)!=1){
       fprintf(stderr, "play_and_count error on pcm:%s stimfile:%s. Program aborted %s\n", 
	       pcm_name, stimexm, asctime(localtime (&curr_tt)) );
       fprintf(datafp, "play_and_count error on pcm:%s stimfile:%s. Program aborted %s\n", 
	       pcm_name, stimexm, asctime(localtime (&curr_tt)) );
       fclose(datafp);
       fclose(dsumfp); 
       snd_pcm_close(handle);
       exit(-1);
     }
     if (DEBUG){printf("PLAYBACK COMPLETE  '%s'\n", stimexm);}
     if (DEBUG){printf("post-play L-C-R peckcounts: %d-%d-%d\n", trial.left, trial.center, trial.right);}
     
     /* note time that trial ends */
     curr_tt = time (NULL); 
     loctime = localtime (&curr_tt);                     /* date and wall clock time of trial*/
     strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
     strftime (min, 16, "%M", loctime);
     strftime (month, 16, "%m", loctime);
     strftime (day, 16, "%d", loctime);
     currtime=(atoi(hour)*60)+atoi(min);
     
     /*deliver some food */
     if((reinfor = feed(stim_reinf, &f)) == 1)
       ++fed;
     
     /*update the data counters*/
     ++stim[stim_num].trials; 
     stim[stim_num].rLFT += trial.left;
     stim[stim_num].rCTR += trial.center;
     stim[stim_num].rRGT += trial.right;
     
     ++class[stim_class].trials; 
     class[stim_class].rLFT += trial.left;
     class[stim_class].rCTR += trial.center;
     class[stim_class].rRGT += trial.right;
     
     /* Pause for ITI */
     reinfor_sum += reinfor;
     operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
     nanosleep(&iti, NULL);                     /* wait intertrial interval */
     if (DEBUG){printf("ITI passed\n");}
     
     /* Write trial data to output file */
     strftime (tod, 256, "%H%M", loctime);
     strftime (date_out, 256, "%m%d", loctime);
     fprintf(datafp, "%d\t%lu\t%d\t%d\t%s\t%lu\t%lu\t\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n", 
	     session_num, tot_trial_num, trial_num, rswap, stimexm, offset,stimulus_duration,
	     stim_class, trial.left, trial.center, trial.right, reinfor, tod, date_out );
     fflush (datafp);
     if (DEBUG){
       printf("%d\t%lu\t%d\t%d\t%s\t%lu\t%lu\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n", 
	      session_num, tot_trial_num, trial_num, rswap, stimexm, offset,stimulus_duration, 
	      stim_class, trial.left, trial.center, trial.right, reinfor, tod, date_out );
     }
     
     // Update summary data 
     if(freopen(dsumfname,"w",dsumfp)!= NULL){
       fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
       fprintf (dsumfp, "SESSION TOTALS BY STIMULUS CLASS\n\n");
       fprintf (dsumfp, "\t\tClass \tTrials \tLeft \tCenter \tRight\n");
       for (i = 1; i<=nclasses;++i){
	 fprintf (dsumfp, "\t\t%s  \t %d    \t %d  \t %d    \t %d\n", 
		  class[i].name,class[i].trials, class[i].rLFT, class[i].rCTR, class[i].rRGT);
       }
       
       fprintf (dsumfp, "\n\n\n\tSESSION TOTALS BY SOURCE SOUNDFILE\n");
       fprintf (dsumfp, "\t\tSoundfile \tTrials \tLeft \tCenter \tRight\n");
       for (i = 0; i<nstims;++i){
	 fprintf (dsumfp, "\t\t%s  \t %d    \t %d  \t %d    \t %d\n", 
		  stim[i].name, stim[i].trials, stim[i].rLFT, stim[i].rCTR, stim[i].rRGT);
       }
       
       fprintf (dsumfp, "\n\n\nLast trial run @: %s\n", asctime(loctime) );
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
     
     
     if (DEBUG){printf("flag: summaries updated\n\n");}
     
     
     /* End of trial chores */
     sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);        /* unblock termination signals */ 
     C1_stim_number = C2_stim_number = -1;                /* reset the stim number for correct trial*/
     if (DEBUG){printf("swapcount:%d\tswapval:%d\trswap:%d\n",swapcnt,swapval,rswap);}
     if(swapcnt==swapval){                                /*check if you've run enough trials to swap the stim-resp pairings */ 
       if (DEBUG){printf("swapped the contingencies\n");}
       if(rswap==1){rswap=0;}else{rswap=1;}
       if (DEBUG){printf("rswap:%d\n",rswap);}
       swapcnt=0;
     }
     if(DEBUG){printf("currtime: %d\n", currtime);}
   }                                  /*  trial loop */
   
   curr_tt = time (NULL);
   loctime = localtime (&curr_tt);
   strftime (hour, 16, "%H", loctime);
   strftime(min, 16, "%M", loctime);
   currtime=(atoi(hour)*60)+atoi(min);
   if (DEBUG){printf("minutes since midnight at trial loop exit end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
   
   /* Loop while lights out */
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
   ++session_num;
   if (DEBUG){printf("swapcount:%d\tswapval:%d\trswap:%d\n",swapcnt,swapval,rswap);}
   if(swapval==0){                            /*check if we need to swap the stim-resp pairings for the new session*/	
     if(rswap==1){rswap=0;}else{rswap=1;}
     if (DEBUG){printf("rswap:%d\n",rswap);}
     swapcnt=0; /*zero the swap count for the day*/
   }
   trial_num = 0;
   f.hopper_wont_go_down_failures = f.hopper_already_up_failures = f.hopper_failures = f.response_failures = fed = reinfor_sum = 0;
   
 }while (1);// main loop
 
 curr_tt = time(NULL);
 
 
 /*  Cleanup */
 fclose(datafp);
 fclose(dsumfp);
 snd_pcm_close(handle);
 return 0;
}                         

