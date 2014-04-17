/*****************************************************************************
** gng.c - code for running go/nogo operant training procedure
******************************************************************************
**
** 9-19-01 TQG: Adapted from most current 2choice.c
** 6-22-05 TQG: Now runs using alsa sound driver
** 7-12-05 EDF: Adding support for beam break hopper verification
** 4-13-06 TQG: added sunrise/sunset timing and cleaned up functions
** 4-6-11 TQG: fixed playlist memory leak
** 4-9-11 JAC: playwav error no longer aborts the program
** 5-22-13 TQG(KEP): modified for same-different task -- nolonger works for regular gono
**
** gngKP.c works... gngKP_tmp.c is the one that i am trying to get new implementations working with...
** KP 22 June added flashing keylights as secondary reinforcer. and that is already in gngKP. 
** now i want to add in 1) a reinforcement schedule 2) restrict times of day that bird can behave 3) drop cue lights on some percent of trials til they are on auditory 
** KP 26 june fixed response/reinf schedule is implemented as well as a secondary reinforcer
** KP 26 june adding in a -freerun flag so that -on and -off mean time on and time off on a freerun schedule instead of meaning the time of day
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>
#include <sndfile.h>

#include "/usr/local/src/operantio/operantio.c"
/*#include "/usr/local/src/audioio/audout.c"*/

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include <sunrise.h>
#define MAXFILESIZE 5292000    /* max samples allowed in soundfile */

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


/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             2             /* seconds from simulus end until NORESP is registered  (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define REINF_INT_SEC             4             /* seconds for secondary reinforcement on no-feed hits (same length as feed_duration */
#define REINF_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  1024            /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 256           /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define TIMEOUT_DURATION         10000000       /* duration of timeout in microseconds */
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
int  daystophour = EXP_END_TIME;
int  daystarthour = EXP_START_TIME;
int  stophour = EXP_END_TIME;
int  starthour = EXP_START_TIME;
int  stopmin = 0;
int  startmin = 0;
int  daystopmin = 0;
int  daystartmin = 0;
int freerun = 0;
int  sleep_interval   = SLEEP_TIME;
const char exp_name[] = "SD";
int box_id = -1;
int flash = 0;
int resp_wind = 4;
int maxreps = 10;
int diffval = 20;
int cuelights = 0;
int fixratio = 0;
int loop = 0;
int numconsecutivecorrect = 0;
int resp_sel, resp_acc, *playlist=NULL;

typedef struct {
  int hopper_failures;
  int hopper_wont_go_down_failures;
  int hopper_already_up_failures;
  int response_failures;
} Failures;

snd_pcm_t *handle;
struct timeval resp_lag;

int feed(int rval, Failures *f);
int secondary(int rval);
int timeout(int rval);
int probeGO(int rval, int mirr, Failures *f);

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};
struct timeval pausefeed = {REINF_INT_SEC,REINF_INT_USEC}; /* this time variable is for the secondary key flashing reinforcer before feeding */
struct timeval consequate_begin,pause_lag,pause_window; /* pause lag and pause window are for key flashing secondary reinforcer*/

/* -------- Signal handling --------- */
int client_fd = -1;
static void sig_pipe(int signum){ 
  fprintf(stdout,"SIGPIPE caught\n"); client_fd = -1;
}
static void termination_handler (int signum){
  snd_pcm_close(handle);
  free(playlist);
  fprintf(stdout,"closed soundserver: term signal caught: exiting\n");
  exit(-1);
}


/*****************************************************************************
 *   PLAYBACK UTILITIES:Underrun and suspend recovery
 *                      wait_for_poll
 *****************************************************************************/
static int xrun_recovery(snd_pcm_t *handle, int err)
{
  if (err == -EPIPE) {
    err = snd_pcm_prepare(handle);
    if (err < 0)
      printf("Can't recovery from underrun, prepare failed: %s\n", snd_strerror(err));
    return 0;
  } else if (err == -ESTRPIPE) {
    while ((err = snd_pcm_resume(handle)) == -EAGAIN)
      sleep(1);       
    if (err < 0) {
      err = snd_pcm_prepare(handle);
      if (err < 0)
	printf("Can't recovery from suspend, prepare failed: %s\n", snd_strerror(err));
    }
    return 0;
  }
  return err;
}

static int wait_for_poll(snd_pcm_t *handle, struct pollfd *ufds, unsigned int count)
{
  unsigned short revents;
  while (1) {
    poll(ufds, count, -1);
    snd_pcm_poll_descriptors_revents(handle, ufds, count, &revents);
    if (revents & POLLERR)
      return -EIO;
    if (revents & POLLOUT)
      return 0;
  }
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

void doReinfFeed(Failures *f)
{
  gettimeofday(&consequate_begin, NULL);
  if(operant_read(box_id,HOPPEROP)!=0){
    printf("error -- hopper found in up position when it shouldn't be -- box %d\n",box_id);
    f->hopper_already_up_failures++;
  }
  operant_write(box_id, FEEDER, 1);
  timeradd (&consequate_begin, &pausefeed, &pause_window);
  loop = 0;
  do{
    nanosleep(&rsi, NULL);
    gettimeofday(&pause_lag, NULL);
    ++loop;
    if(loop%80==0){
      if(loop%160==0){
	operant_write(box_id, CTRKEYLT, 1);
	operant_write(box_id, LFTKEYLT, 1);
	operant_write(box_id, RGTKEYLT, 1);}
      else{
	operant_write(box_id, CTRKEYLT, 0);
	operant_write(box_id, LFTKEYLT, 0);
	operant_write(box_id, RGTKEYLT, 0);}
    }
  }while (timercmp(&pause_lag, &pause_window, <));
  /* make sure lights back off */
  operant_write(box_id, CTRKEYLT, 0);
  operant_write(box_id, LFTKEYLT, 0);
  operant_write(box_id, RGTKEYLT, 0);
  
  if(operant_read(box_id, CENTERPECK)!=0){
    printf("WARNING -- suspicious center key peck detected during feeding -- box %d\n",box_id);
    f->response_failures++;
  }
  if(operant_read(box_id, HOPPEROP)!=1){
    printf("error -- hopper not raised when it should be -- box %d\n",box_id);
    f->hopper_failures++;
  }
  
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
    if(DEBUG){fprintf(stderr,"feedleft\n");}
    return(1);
  }
  else{return (0);}
}

int secondary(int rval) /* to give a secondary reinforcement of all lights flashing on no-feed hit trials */
{
  gettimeofday(&consequate_begin, NULL);
  int feed_me=0;
  
  feed_me = 1+(int) (100.0*rand()/(RAND_MAX+1.0) ); 
  if(DEBUG){fprintf(stderr,"feed_me = %d\t rval = %d\n", feed_me, rval);}
  
  if (feed_me <= rval){
    /* flash all 3 keylights for 2 seconds before feeding to give secondary reinforcer */
    if (DEBUG){printf("flag: flashing keylights while waiting to feed\n");}
    timeradd (&consequate_begin, &pausefeed, &pause_window);
    loop = 0;
    do{
      nanosleep(&rsi, NULL);
      gettimeofday(&pause_lag, NULL);
      ++loop;
      if(loop%80==0){
	if(loop%160==0){
	  operant_write(box_id, CTRKEYLT, 1);
	  operant_write(box_id, LFTKEYLT, 1);
	  operant_write(box_id, RGTKEYLT, 1);}
	else{
	  operant_write(box_id, CTRKEYLT, 0);
	  operant_write(box_id, LFTKEYLT, 0);
	  operant_write(box_id, RGTKEYLT, 0);}
      }
    }while (timercmp(&pause_lag, &pause_window, <));
    /* make sure lights back off */
    operant_write(box_id, CTRKEYLT, 0);
    operant_write(box_id, LFTKEYLT, 0);
    operant_write(box_id, RGTKEYLT, 0);
    /* hopper does not actually come up */
    if(DEBUG){fprintf(stderr,"feedleft\n");}
    return(1);
  }
  else{return (0);}
}

/* feedFR taken from 2ac_fr.c in 2choice folder. no documentation in file about where it came from for that code version */
/* with this function, a secondary reinforcement function is executed */
int feedFR(int rval, Failures *f, int *numconsecutivecorrect, int *fixratio)
{
  *numconsecutivecorrect = *numconsecutivecorrect + 1;
  if(DEBUG){fprintf(stderr,"numconsecutivecorrect = %d\t fixratio = %d\n", *numconsecutivecorrect, *fixratio);}
  if (*numconsecutivecorrect >= *fixratio){
    
    *numconsecutivecorrect = 0;
    int feed_me=0;
    
    feed_me = 1+(int) (100.0*rand()/(RAND_MAX+1.0) ); 
    if(DEBUG){fprintf(stderr,"feed_me = %d\t rval = %d\n", feed_me, rval);}
    
    if (feed_me <= rval){
      doReinfFeed(f);
      if(DEBUG){fprintf(stderr,"feed left\n");}
      return(1);
    }
    else{return (0);}
  }
  else{
    secondary(rval);
    return(0);}
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
  fprintf(stderr, "sd usage:\n");
  fprintf(stderr, "    sd [-help] [-B int] [-t float][-on int:int] [-off int:int] [-d] [-r] [-S <subject number>] <filename>\n\n");
  fprintf(stderr, "        -help          = show this help message\n");
  fprintf(stderr, "        -B int         = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "        -t float       = set the timeout duration to float secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -fr int        = fixed ratio of consecutive correct responses before feed reinforced \n");
  fprintf(stderr, "        -on int:int    = set hour:min for exp to start eg: '-on 7:30' (default is 7:00 AM)\n");
  fprintf(stderr, "        -off int:int   = set hour for exp to stop eg: '-off 19:45' (default is 7:00 PM)\n"); 
  fprintf(stderr, "        -free          = include this flag to make -on and -off mean duration on and duration off instead of absolute time (default is 0\n");
  fprintf(stderr, "                         To use daily sunset or sunrise times set 'on' or 'off' to '99'\n");
  fprintf(stderr, "        -d             = probability diff targetplay on every trial\n"); 
  fprintf(stderr, "        -c             = percent of trials key lights flashing to cue responses after shape \n");
  fprintf(stderr, "        -r             = max number of anchor presentations before reward same response\n");
  fprintf(stderr, "        -w 'x'         = set the response window duration to 'x' secs (use an integer)\n");
  fprintf(stderr, "        -S int         = specify the subject ID number (required)\n");
  fprintf(stderr, "        filename       = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "                         where each line is: 'Class' 'Sndfile' 'Freq'\n");
  fprintf(stderr, "                         'Class'= 1 for all stimuli \n");
  fprintf(stderr, "                         'Sndfile' is the name of the stimulus soundfile (use WAV format 44.1kHz only)\n");
  fprintf(stderr, "                         'Freq' is the overall stimulus presetation rate (relative to the other stimuli). \n"); 
  fprintf(stderr, "                           The actual rate for each stimulus (expressed as an integer) is that value divded by the\n");  
  fprintf(stderr, "                           sum for all stimuli. Set all prefreq values to 1 for equal probablility \n"); 
  fprintf(stderr, "                          no probe stimuli \n");
  exit(-1);
}

/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid,int *daystarthour, int *daystophour,int *daystartmin, int *daystopmin,  int *starthour, int *stophour, int *startmin, int *stopmin, int *freerun,int *maxreps, int *diffval, int *cuelights, int *resp_wind, int *fixratio, float *timeout_val, char **stimfname)
{
  int i=0;
  for (i = 1; i < argc; i++){
    if (*argv[i] == '-'){
      if (strncmp(argv[i], "-B", 2) == 0){
	sscanf(argv[++i], "%i", box_id);}
      else if (strncmp(argv[i], "-S", 2) == 0){
	sscanf(argv[++i], "%i", subjectid);}
      else if (strncmp(argv[i], "-fr", 3) == 0){
	sscanf(argv[++i], "%d", fixratio);}	
      else if (strncmp(argv[i], "-w", 2) == 0){
	sscanf(argv[++i], "%d", resp_wind);}
      else if (strncmp(argv[i], "-t", 2) == 0){
	sscanf(argv[++i], "%f", timeout_val);}
      else if (strncmp(argv[i], "-on", 3) == 0){
	sscanf(argv[++i], "%i:%i", daystarthour,daystartmin);}
      else if (strncmp(argv[i], "-off", 4) == 0){
	sscanf(argv[++i], "%i:%i", daystophour, daystopmin);}
      else if (strncmp(argv[i], "-Runon", 6) == 0){
	sscanf(argv[++i], "%i:%i", starthour,startmin);}
      else if (strncmp(argv[i], "-Runoff", 7) == 0){
	sscanf(argv[++i], "%i:%i", stophour, stopmin);}
      else if (strncmp(argv[i], "-runT", 5) == 0){
	sscanf(argv[++i], "%i", freerun);}
      else if (strncmp(argv[i], "-r", 2) == 0){                                                          
	sscanf(argv[++i], "%d", maxreps);} 
      else if (strncmp(argv[i], "-d", 2) == 0){                                                           
	sscanf(argv[++i], "%d", diffval);} 
      else if (strncmp(argv[i], "-c", 2) == 0){                                                           
	sscanf(argv[++i], "%d", cuelights);} 
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


/*********************************************************************************
 * PCM SETUP 
 ********************************************************************************/
int setup_pcmdev(char *pcm_name)
{
  snd_pcm_hw_params_t *params;
  snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK; 
  int rate = 44100, dir;
  snd_pcm_uframes_t persize, persize2;

  int maxrate, minrate;
  unsigned int pertime, perTmin, perTmax;
  snd_pcm_uframes_t bufsize, perSmin, perSmax, bufSmin, bufSmax;
  

  /* Allocate the snd_pcm_hw_params_t structure on the stack. */ 
  snd_pcm_hw_params_alloca(&params);
  
  /* Open PCM device for playback. */
  if (snd_pcm_open(&handle, pcm_name, stream, 0) < 0) {
    fprintf(stderr, "Error opening PCM device %s\n", pcm_name);
    return(-1);}
  
  /* Init params with full configuration space */
  if (snd_pcm_hw_params_any(handle, params) < 0) {
    fprintf(stderr, "Can not configure this PCM device.\n");
    return(-1);}
  
  /* set interleaved mode */
  if (snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
    fprintf(stderr, "Error setting access.\n");
    return(-1);}
  
  /* Set sample format */
  if (snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE) < 0) {
    fprintf(stderr, "Error setting format.\n");
    return(-1);}
  
  /* Set sample rate.*/ 
  if (snd_pcm_hw_params_set_rate_near(handle, params, &rate, 0) < 0) {
    fprintf(stderr, "Error setting rate.\n");
    return(-1);}
  
  /* Set number of channels */
 if (snd_pcm_hw_params_set_channels(handle, params, 1) < 0) {
    fprintf(stderr, "Error setting channels.\n");
    return(-1);}
  
  /* Set period size to n frames (samples). */
  persize = 1024; dir=0;
  if (snd_pcm_hw_params_set_period_size_near(handle,params, &persize, &dir)< 0) {
    fprintf(stderr, "Error setting period size to %d.\n", (int) persize);
    return(-1);}
    
  /* Apply HW parameter settings to PCM device */
  if (snd_pcm_hw_params(handle, params) < 0) {
    fprintf(stderr, "Error setting HW params.\n");
    return(-1);}

  /*get some inof about the hardware*/
  
  // printf("\n ---------- hardware parameters ------------ \n");
  snd_pcm_hw_params_get_rate_min (params, &minrate, &dir);
  //printf("min rate: %d samples per sec\n",minrate);
  snd_pcm_hw_params_get_rate_max (params, &maxrate, &dir);
  //printf("max rate: %d samples per sec\n",maxrate);
  snd_pcm_hw_params_get_period_time (params, &pertime, &dir);
  //printf("period: %d microseconds\n",pertime);
  snd_pcm_hw_params_get_period_time_min (params, &perTmin, &dir);
  snd_pcm_hw_params_get_period_time_min (params, &perTmax, &dir);
  //printf("min period time: %d microseconds\n",perTmin);
  //printf("max period time: %d microseconds\n",perTmax);
  snd_pcm_hw_params_get_period_size (params, &persize2, &dir);
  //printf("period: %d frames\n",(int) persize2);
  snd_pcm_hw_params_get_period_size_min (params, &perSmin, &dir);
  snd_pcm_hw_params_get_period_size_min (params, &perSmax, &dir);
  //printf("min period size: %d frames\n",(int) perSmin);
  //printf("max period size: %d frames\n",(int) perSmax);
  snd_pcm_hw_params_get_buffer_size (params, &bufsize);
  //printf("buffer size: %d frames\n",(int) bufsize);
  snd_pcm_hw_params_get_buffer_size_min (params, &bufSmin);
  snd_pcm_hw_params_get_buffer_size_min (params, &bufSmax);
  //printf("min buffer size: %d frames\n",(int) bufSmin);
  //printf("max buffer size: %d frames\n",(int) bufSmax);
  
  return (double) persize2;
}


/*****************************************************************************
 *   Get soundfile info and verify formats
 *****************************************************************************/
int verify_soundfile(char *sfname)
{
  SNDFILE *sf;
  SF_INFO *sfinfo; 
  long unsigned int duration;
  
  sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));
  
  if(!(sf = sf_open(sfname,SFM_READ,sfinfo))){
    fprintf(stderr,"error opening input file %s\n",sfname);
    free(sfinfo);
    return -1;
  }
  
  /*print out some info about the file you just openend */
  if(DEBUG){
    printf(" ---------- Stimulus parameters ------------ \n");
    printf ("Samples : %d\n", (int)sfinfo->frames) ;
    printf ("Sample Rate : %d\n", sfinfo->samplerate) ;
    printf ("Channels    : %d\n", sfinfo->channels) ;
  }
  
  /* check that some assumptions are met */
  /* this should be done by the operant code*/
  if (sfinfo->frames > MAXFILESIZE){
    fprintf(stderr,"File is too large!\n");
    sf_close(sf);free(sfinfo);return -1;
  }
  if (sfinfo->samplerate != 44100){
    fprintf(stderr, "Sample rate for %s is not equal to 44.1 KHz!\n", sfname);
    sf_close(sf);free(sfinfo);return -1;
  }
  if (sfinfo->channels != 1){
    fprintf(stderr, "Sound file %s is not mono!\n", sfname);
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  /* make sure format is WAV */
  if((sfinfo->format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV){
    printf("Not a WAV file\n");
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  duration = sfinfo->frames/44.1;
  free(sfinfo);
  if(DEBUG){printf("duration from soundfile verify:%lu\n",duration);}
  return (duration);
} 


/********************************************************************
* output  =  playwavwhile (respfcn, stimfile, period, boxID, observe_resp, abort_resp, noisedb, signaldb, outdb)
*
* play a soundfile for the assigned box, while checking to see if different
* responses are made during playback
* returns 0 (and immediately stops) if abort response is detected, 1 if observe response is detected,
* 2 if no response is detected, and -1 if there is an playback error
* set respfcn to 1, to permit observation responses or 0 to abort stin on observe response
* noisedb is the SPL of whitenoise to overlay on the stimulus (set to zero for no noise)
**************************************************************************************/

int playwavwhile(int respfcn, char *sfname, double period, int box_id, int obsvresp, int abortresp)
{
  SNDFILE *sf;
  SF_INFO *sfinfo;
  short  *sigbuff;
  unsigned short *ptr;
  sf_count_t incount;
  double inframes, padded;
  long pad = 0;
  snd_pcm_uframes_t outframes,totframesout;
  int err, count, init,observe=0,initial=0,loop;

   
  struct pollfd *ufds;
  
  /* memory for SF_INFO structures */
  sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));
  
  /* open input file*/
  if(!(sf = sf_open(sfname,SFM_READ,sfinfo))){
    fprintf(stderr,"error opening input file %s\n",sfname);
    free(sfinfo);
    return -1;
  }
  
  if((sfinfo->format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV){
    /* pad the file size up to next highest period count*/
    inframes=(int)sfinfo->frames;
    pad = (period * ceil( inframes/ period))-inframes;
    padded=inframes+pad;
    
    /* allocate buffer memory */
    sigbuff = (short *) malloc(sizeof(int)*padded);
    
    /* read the data */
    incount = sf_readf_short(sf, sigbuff, inframes);
    //printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount, (int)inframes);
  }
  outframes = padded;
  if(DEBUG==3){printf("I'll try to write %d frames\n", (int)outframes);}
  
  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use\n");
    sf_close(sf);
    free(sfinfo);
    free(sigbuff);
    return -1;
  }
  
  snd_pcm_nonblock(handle,1); /*make sure you set playback to non-blocking*/
  
  /*playback with polling*/
  count = snd_pcm_poll_descriptors_count (handle);
  if (count <= 0) {
    printf("Invalid poll descriptors count\n");
    sf_close(sf);
    free(sfinfo);
    free(sigbuff);
    return count;
  }
  ufds = malloc(sizeof(struct pollfd) * count);
  if (ufds == NULL) {
    printf("Not enough memory\n");
    sf_close(sf);
    free(sfinfo);
    free(sigbuff);
    return -ENOMEM;
  }
  if ((err = snd_pcm_poll_descriptors(handle, ufds, count)) < 0) {
    printf("Unable to obtain poll descriptors for playback: %s\n", snd_strerror(err));
    sf_close(sf);
    free(sfinfo);
    free(sigbuff);
    return err;
  }
  init = 1;
  if (!init) {
    err = wait_for_poll(handle, ufds, count);
    if (err < 0) {
      if (snd_pcm_state(handle) == SND_PCM_STATE_XRUN ||
          snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
        err = snd_pcm_state(handle) == SND_PCM_STATE_XRUN ? -EPIPE : -ESTRPIPE;
        if (xrun_recovery(handle, err) < 0) {
          printf("Write error: %s\n", snd_strerror(err));
	  sf_close(sf);
	  free(sfinfo);
	  free(sigbuff);
          exit(EXIT_FAILURE);
        }
        init = 1;
      } 
      else {
        printf("Wait for poll failed\n");
	sf_close(sf);
	free(sfinfo);
	free(sigbuff);
        return err;
      }
    }
  }
  
  totframesout=0;
  ptr=sigbuff;
  observe=0;
  
  /*make sure observe key is not still on*/
  
  if(operant_read(box_id, obsvresp)){
    loop = 0;
    do{
      ++loop;
      nanosleep(&rsi, NULL);
      initial = operant_read(box_id, obsvresp);
      gettimeofday(&resp_lag, NULL);
    }while(initial==1);
  }

  /*playback loop*/
  while (outframes > 0)  
    {
      err = snd_pcm_writei(handle, ptr, outframes); /*should outframes be period???*/
      if (err < 0) {
	if (xrun_recovery(handle, err) < 0) {
	  printf("Write error: %s\n", snd_strerror(err));
	  sf_close(sf);
	  free(sfinfo);
	  free(sigbuff);
	  exit(EXIT_FAILURE);
	}
	init = 1;
	break;  /* skip one period */
      }
      if (snd_pcm_state(handle) == SND_PCM_STATE_RUNNING)
	init = 0;
      
      /*check for a response */
      if(operant_read(box_id, abortresp)){
	gettimeofday(&resp_lag, NULL);
	sf_close(sf);
	free(sfinfo);
	free(sigbuff);
	return 0;  
      }
      if(operant_read(box_id, obsvresp)){ 
	gettimeofday(&resp_lag, NULL);
	observe=1;
	if(respfcn==0){  //this might trigger a rapid abort when the target comes on close to an obs response!!
	  sf_close(sf);
	  free(sfinfo);
	  free(sigbuff);
	  return 1;
	}
      }
      
      /*advance the file pointer*/ 
      totframesout += err; 
      ptr += err;
      outframes -= err;
      
      /* if(DEBUG){printf("outframes is now %d\n", (int) outframes);}*/
      if (outframes == 0){
	if(DEBUG){printf("outframes is zero so I'll break\n");}
	break;
      }
      /* it is possible, that the initial buffer cannot store */
      /* all data from the last period, so wait a while */
      err = wait_for_poll(handle, ufds, count);
      if (err < 0) {
	if (snd_pcm_state(handle) == SND_PCM_STATE_XRUN ||
	    snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
	  err = snd_pcm_state(handle) == SND_PCM_STATE_XRUN ? -EPIPE : -ESTRPIPE;
	  if (xrun_recovery(handle, err) < 0) {
	    printf("Write error: %s\n", snd_strerror(err));
	    exit(EXIT_FAILURE);
	  }
	  init = 1;
	}
	else {
	  printf("Wait for poll failed\n");
	  return err;
	}
      }
    }
  
  if(DEBUG==3){
    printf("exited while writei loop, so what am I waiting for?\n");
    printf("frames not played: %d \n", (int)outframes);
    printf("frames played: %d \n", (int) totframesout);
  }
  
  /*cleanup*/
  sf_close(sf);
  free(sfinfo);
  sfinfo=NULL;
  free(sigbuff);
  sigbuff=NULL;
  free(ufds);
  ufds=NULL;
  
  /*figure out what happend during the stimulus*/  
  if (observe==0){
    return 2; /*no response detected*/
  }
  else
    return 1;/*observe response detected*/
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
  char datafname[128], hour [16], min[16], month[16], day[16], year[16],dsumfname[128], stimftemp[128],buf[128], anchorexm[128],targetexm[128],fanchor[256],ftarget[256], timebuff[64], tod[256], date_out[256],temphour[16],tempmin[16], buffer[30];
  int dinfd=0, doutfd=0, nstims, stim_number, stim_reinf=100 ,subjectid, period,trial_num,targetplay, targetresp,session_num,targetval,anchorval,dodiff,difftest,docue,cuetest,repcount,playresp,abortpeck,abortresp,targetpeck, i,j,k, totnstims=0,sessionTrials, dosunrise=0,dosunset=0,starttime,stoptime,daytime,nighttime,currtime,onRunTime,offRunTime;
  float resp_rxt=0,timeout_val=0.0;
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  int trialson=1, trialsoff=0;
  struct timeval stimoff,stimon, resp_window, resp_rt; 
  struct tm *loctime;
  int center = 0, fed = 0;
  Failures f = {0,0,0,0};
  int reinfor_sum = 0, reinfor = 0;
  int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  
/* debugging variables */
  struct stim {
    char exemplar[128];
    int freq;
    int playnum;
  }stimulus[MAXSTIM];
  struct response {
    int count;
    float targetRatio;
    int targetabort;
    int targetgo;
    int targetnoresp;
    float anchorRatio;
    int anchorabort;
    int anchorgo;
    int anchornoresp;
  } classRses[maxreps], classRtot[maxreps];
  
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
  command_line_parse(argc, argv, &box_id, &subjectid, &daystarthour, &daystophour, &daystartmin, &daystopmin, &starthour, &stophour, &startmin, &stopmin,&freerun,&maxreps, &diffval, &cuelights, &resp_wind, &fixratio, &timeout_val, &stimfname);
  if(DEBUG){
    fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, daystartH=%d, daystopH=%d, daystartM=%d, daystopM=%d,startH=%d, stopH=%d, startM=%d, stopM=%d,freerun=%d,maxreps=%d, diffval=%d, cuelights=%d,resp_wind=%d, fixratio=%d,timeout_val=%f stimfile: %s\n",box_id, subjectid, daystarthour, daystophour, daystartmin, daystopmin,starthour, stophour, startmin, stopmin, freerun,maxreps, diffval,cuelights,resp_wind,fixratio,timeout_val, stimfname);
  }
  sprintf(pcm_name, "dac%i", box_id);
  if(DEBUG){fprintf(stderr,"dac: %s\n",pcm_name);}
  if(DEBUG){fprintf(stderr,"commandline done, now checking for errors\n");}
  if(DEBUG){fprintf(stderr,"freerun flag set to: %d\n",freerun);} 
  /* watch for terminal errors*/
  if( (daystophour!=99) && (daystarthour !=99) ){
    if ((daystophour!=0) && (daystarthour !=0)){
	if (daystophour <= daystarthour){
	  fprintf(stderr, "\tTERMINAL ERROR: exp start-time must be greater than stop-time\n");
	  exit(-1);
	}
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
    {respoff.tv_sec = resp_wind;
    fprintf(stderr, "response window duration set to %d secs\n", (int) respoff.tv_sec);}
  if(timeout_val>0.0)
    {timeout_duration = (int) (timeout_val*1000000);
    fprintf(stderr, "timeout duration set to %d microsecs\n", (int) timeout_duration);}
  
  if(DEBUG){fprintf(stderr, "daystarthour: %d\tdaystartmin: %d\tdaystophour: %d\tdaystopmin: %d\n", daystarthour,daystartmin,daystophour,daystopmin);
  fprintf(stderr, "trialstarthour: %d\ttrialstartmin: %d\ttrialstophour: %d\ttrialstopmin: %d\n", starthour,startmin,stophour,stopmin);}
  
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
    daystarthour=atoi(temphour);
    daystartmin=atoi(tempmin);
    strftime(buffer,30,"%m-%d-%Y %H:%M:%S",localtime(&rise_tt));
    printf("Sessions start at sunrise (Today: '%s')\n",buffer);
  }
  if(stophour==99){
    dosunset=1;
    set_tt = sunset(atoi(year), atoi(month), atoi(day), latitude, longitude);
    strftime(temphour, 16, "%H", localtime(&set_tt));
    strftime(tempmin, 16, "%M", localtime(&set_tt));
    daystophour=atoi(temphour);
    daystopmin=atoi(tempmin);
    strftime(buffer,30,"%m-%d-%Y  %T",localtime(&set_tt));
    printf("Sessions stop at sunset (Today: '%s')\n",buffer);
  }
  
  daytime=(daystarthour*60)+daystartmin;
  nighttime=(daystophour*60)+daystopmin;
  if(DEBUG){fprintf(stderr, "daystarthour: %d\tdaystartmin: %d\tdaystophour: %d\tdaystopmin: %d\n", daystarthour,daystartmin,daystophour,daystopmin);}
  
  
  /* Initialize for a freerun session in which durations of times trials are able to be run are set by -onRun and -offRun values */
  if (freerun==1){
    /* starthour is current time */
    /* initialized trialson==1 and trialsoff==0  */
    /* stophour is current time plus the session duration of the onRunTime when trialson==1 and trialsoff==0 */
    onRunTime=(starthour*60)+startmin;
    offRunTime=(stophour*60)+stopmin;
    curr_tt = time(NULL);
    loctime = localtime (&curr_tt);
    strftime (hour, 16, "%H", loctime);
    strftime(min, 16, "%M", loctime);
    if (DEBUG){printf("hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
    currtime=(atoi(hour)*60)+atoi(min);
    if ((trialson==1)&&(trialsoff==0)){
      starttime=currtime;
      stoptime=currtime+onRunTime;
    }
  }
  if (freerun==0){
    starttime=daytime;
    stoptime=nighttime;
  }
  
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
 
  if(flash==1){fprintf(stderr, "!!WARNING: Flashing all keylights for hopper feeds!!\n");}
  
  /* Read in the list of exmplars from stimulus file */
  nstims = 0;
  if ((stimfp = fopen(stimfname, "r")) != NULL){
    while (fgets(buf, sizeof(buf), stimfp))
      nstims++;
    fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
    rewind(stimfp);
    
    for (i = 0; i < nstims; i++){
      fgets(buf, 128, stimfp);
      stimulus[i].freq =0; 
      sscanf(buf, "%s\%d", stimulus[i].exemplar, &stimulus[i].freq); 
      if((stimulus[i].freq==0)){ 
	printf("ERROR: insufficnet data or bad format in '.stim' file. Try 'gng -help'\n");
	snd_pcm_close(handle);
	exit(-1);} 
      totnstims += stimulus[i].freq;
      if(DEBUG){printf("totnstims: %d\n", totnstims);}
    }
  }
  
  else{ 
    printf("Error opening stimulus input file! Try 'gng_probe -help' for proper file formatting.\n");  
    snd_pcm_close(handle);
    exit(-1);	  
  }
  fclose(stimfp);
  if(DEBUG){printf("Done reading in stims; %d stims \n", nstims);}
  
  /* make the stimulus playlist */
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
  sprintf(datafname, "%i_%s.SD_rDAT", subjectid, stimfroot);
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
    free(playlist);
    exit(-1);
  }
  
  /* Write data file header info */
  fprintf (stderr, "Data output to '%s'\n", datafname);
  fprintf (datafp, "File name: %s\n", datafname);
  fprintf (datafp, "Procedure source: %s\n", exp_name);
  fprintf (datafp, "Start time: %s", asctime(loctime));
  fprintf (datafp, "Subject ID: %d\n", subjectid);
  fprintf (datafp, "Stimulus source: %s\n", stimfname);  
  fprintf (datafp, "Sess#\tTrl#\tSD\tAnchor\t\t\tTarget\t\t\tREPcnt\tmax\tTargPl\tRspAc\tReinf\tCUE\tRXT\tTOD\tDate\n");
  
  
   /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
   ********************************************/
  session_num = 1;
  trial_num = 0;
  /*took out option for correction trials*/
  
  if (DEBUG){printf("stimulus counters zeroed!\n");} 
  
  for(i = 0; i<maxreps;++i){   /*zero out the response tallies */
    classRses[i].count = classRses[i].targetabort = classRses[i].targetgo =classRses[i].targetnoresp = classRses[i].anchorabort= classRses[i].anchorgo = classRses[i].anchornoresp =  classRtot[i].count = classRtot[i].targetabort = classRtot[i].targetgo =classRtot[i].targetnoresp = classRtot[i].anchorabort= classRtot[i].anchorgo = classRtot[i].anchornoresp = 0.0;
  }
  
  curr_tt = time(NULL);
  loctime = localtime (&curr_tt);
  strftime (hour, 16, "%H", loctime);
  strftime(min, 16, "%M", loctime);
  if (DEBUG){printf("hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
  currtime=(atoi(hour)*60)+atoi(min);

  operant_write (box_id, HOUSELT, 1);        /* house light on */
  do{
    while ((currtime>=daytime) && (currtime<nighttime)){       /* start main trial loop */
      if (freerun==1){
	/* since calculating a new starttime and stoptime with each trialon period run do some error checking here*/
	if (DEBUG){
	  printf("frerunning and trialson: %d, trialsoff: %d \n",trialson,trialsoff);
	  printf("newstarttime is: %d, currentTime: %d, and newstoptime: %d\n",starttime,currtime,stoptime);
	}
	if (starttime >= stoptime){
	  fprintf(stderr, "\tTERMINAL ERROR: stoptime was not re-initialized properly and it is less than starttime\n");
	  exit(-1);
	}
      }
      while ((currtime>=starttime) && (currtime<stoptime)){       /* start "trials on" loop */
	/* if freerun==0 then starttime and stoptime equals daytime and nighttime */
	
	if (DEBUG){printf("at loop start:currtime %d\t starttime: %d\tstoptime: %d daystart: %d daystop:%d\n",
			  currtime,starttime,stoptime,daytime,nighttime);}
	
	/*choose anchor stim*/
	anchorval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));   /* select sample stim at random */ 
	if (DEBUG){printf("archorval: %d\t", anchorval);}
	stim_number = playlist[anchorval];
	strcpy (anchorexm, stimulus[stim_number].exemplar);          /* get exemplar filename */
	sprintf(fanchor,"%s%s", STIMPATH, anchorexm);                  /* add full path to file name */
	if(DEBUG){
	  printf("anchor stim_num: %d\t", stim_number);
	  printf("anchor name: %s\n", anchorexm);
	  printf("full path anchor: %s\n", fanchor);
	  printf("anchor chosen: %s\tnumber: %d\n", stimulus[stim_number].exemplar, stim_number );
	}
	
	/*choose potential target */
	
	targetval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));   /*select target stim at random*/ 
	while(targetval == anchorval)  
	  targetval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));   /*make sure targ != anchor*/
	
	if (DEBUG){printf("targetval: %d\t", targetval);}
	stim_number = playlist[targetval];
	strcpy (targetexm, stimulus[stim_number].exemplar);          /* get exemplar filename */
	sprintf(ftarget,"%s%s", STIMPATH, targetexm);                  /* add full path to file name */
	if(DEBUG){
	  printf("target stim_num: %d\t", stim_number);
	  printf("target name: %s\n", targetexm);
	  printf("full path target: %s\n", ftarget);
	  printf("target chosen: %s\tnumber: %d\n", stimulus[stim_number].exemplar, stim_number );
	}	
	
	resp_sel = resp_acc = resp_rxt = 0;       /* zero trial variables        */
	++trial_num;
	curr_tt = time(NULL);
	
	/* Wait for center key press while flashing center key light*/
	if (DEBUG){printf("flag: waiting for center key press\n");}
	operant_write (box_id, HOUSELT, 1);        /* house light on */
	center = 0;
	loop = 0;
	do{
	  nanosleep(&rsi, NULL);
	  ++loop;
	  if(loop%80==0){
	    if(loop%160==0){
	      operant_write(box_id, CTRKEYLT, 1);
	    }
	    else{
	      operant_write(box_id, CTRKEYLT, 0);
	    }
	  }
	  center = operant_read(box_id, CENTERPECK);      /*get value at center peck position*/	  
	}while (center==0);  

	/* make sure lights back off */
	nanosleep(&rsi, NULL);	               	       
	operant_write(box_id, CTRKEYLT, 0);
	operant_write(box_id, LFTKEYLT, 0);
	operant_write(box_id, RGTKEYLT, 0);
	
	sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	
	docue=0;
	cuetest = (1+(int)(100.0*rand()/(RAND_MAX+1.0)));
	if (cuetest<=cuelights){
	  docue=1;
	}
	if(DEBUG){
	  printf("docue is %d to control whether response is cued by lights\n",docue);}
	if (docue==1){
	  operant_write (box_id, CTRKEYLT, 1);    
	}    /* center light on during anchor play*/      
	
	/*play the anchor for up to maxreps while observ key pecked*/
	targetplay=0; /*initialize targetplay=0 because no target has played*/
	playresp=3; /*initialize playresp so that needs to change according to respose */
	if(DEBUG){
	  printf("playresp initialized at %d\n",playresp);}
	repcount=0; /*initialize repcount to keep track of number anchor plays*/
	dodiff=0; /*initialize dodiff to play anchor until difftest<=diffval*/
	do{
	  gettimeofday(&stimon, NULL);
	  playresp = playwavwhile(1, fanchor, period, box_id, CENTERPECK, LEFTPECK);
	  repcount += 1;
	  if (DEBUG){printf("playresp returned: %d\n", playresp);	}
	  if (playresp<0){
	    fprintf(stderr, "playwavwhile failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		    pcm_name, anchorexm, asctime(localtime (&curr_tt)) );
	    fprintf(datafp, "playwavwhile failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		    pcm_name, anchorexm, asctime(localtime (&curr_tt)) );
	  }
	  if (DEBUG){printf("on %d of %d anchors\n", repcount,maxreps);}
	  
	  ++classRses[repcount].count;
	  ++classRtot[repcount].count;
	  
	  if (playresp==1){
	    ++classRses[repcount].anchorgo; ++classRtot[repcount].anchorgo; 
	    /*weighted fork for same-diff*/
	    difftest = (1+(int)(100.0*rand()/(RAND_MAX+1.0)));
	    if (difftest<=diffval){
	      dodiff=1;
	      if (DEBUG){printf("if diff test was less than diffval: do different trial, dodiff= %d  \n", dodiff);}
	    }
	  }
	}while ((repcount<maxreps) && (playresp==1) && (dodiff==0)); 
	
	if(DEBUG){
	  printf("anchor played and repcount= %d playresp= %d targetplay=%d dodiff=%d \n",repcount,playresp,targetplay,dodiff);}
	time (&curr_tt);
	loctime = localtime (&curr_tt);
	if(DEBUG){printf("time: %s\n" , ctime(&curr_tt));} 
	strftime (hour, 16, "%H", loctime);
	
	operant_write (box_id, CTRKEYLT, 0);        /* center light off after anchor play*/
	
	/*play the target if anchor repetitions completed*/
	if (playresp==1){ /*correct observations by bird so far */
	  if(dodiff==1){ /*time to play a different sound for the target*/
	    targetplay=1;
	    playresp=3; 
	    /*reset the response flag so that this playwavwhile will set it; returns error if stays at ==3*/
	    gettimeofday(&stimon, NULL);
	    /*play target stim*/

	    if (docue==1){
	      /* left light on during target if diff trial play*/ 
	      operant_write (box_id, LFTKEYLT, 1);        
	      operant_write (box_id, CTRKEYLT, 0);
	    }

	    playresp = playwavwhile(0, ftarget, period, box_id, LEFTPECK,CENTERPECK);/*now leftpeck is target, but allow abortion for target*/
	    if (playresp==2){
	      /* Wait for target response (during resp window) if you haven't gotten one yet */
	      timeradd (&stimoff, &respoff, &resp_window);
	      if (DEBUG){printf("flag: waiting for response\n");}
	      targetpeck=0; 
	      abortpeck=0;
	      if (dodiff==1);{	    
		targetresp=LEFTPECK;
		abortresp=CENTERPECK;
		do{
		  targetpeck=operant_read(box_id, targetresp);
		  abortpeck=operant_read(box_id,abortresp);
		  gettimeofday(&resp_lag, NULL);  
		  nanosleep(&rsi, NULL);
		}while((targetpeck == 0) && (abortpeck == 0) && (timercmp(&resp_lag, &resp_window, <) ) );
		if (targetpeck==1);
		playresp=1;
		if (abortpeck==1);
		playresp=0;
	      }
	      
	      if ((targetpeck == 0) && ( abortpeck == 0));
	      {playresp=2;} /*if still NR, playresp reflects that and remains == 2*/
	    }
	  }
	  if(repcount==maxreps){
	    dodiff=0;
	    targetplay=1; 
	    /*if observed all anchor presentations up to max reps then consider last anchor play to be target play and will reward accordingly*/
	    operant_write (box_id, CTRKEYLT, 0); 
	    /*turn center key light off cause trial over*/
	    /* feed this is a catch trial*/
	  }
	}
	
	if (playresp<0){
	  fprintf(stderr, "playwavwhile failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		  pcm_name, anchorexm, asctime(localtime (&curr_tt)) );
	  fprintf(datafp, "playwavwhile failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		  pcm_name, anchorexm, asctime(localtime (&curr_tt)) );
	}
	if (DEBUG){
	  stimoff_sec = stimoff.tv_sec;
	  stimoff_usec = stimoff.tv_usec;
	  printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);}
	
	
	/*at this point, playresp will ==1 if pecked respond key on either anchor or target
	  ==0 if pecked abort key on anchor or target
	  ==2 if did not respond to anchor or to target
	  <0 if error in playwavwhile*/
	
	gettimeofday(&resp_lag, NULL); 
	timersub (&resp_lag, &stimon, &resp_rt);           /* reaction time */
	operant_write (box_id, LFTKEYLT, 0); 
	operant_write (box_id, CTRKEYLT, 0);     /*make sure both key lights off*/   
	
	if (DEBUG) {
	  printf("playresp: %d \t targetplay %d ; targetplay is only ==1 if target played, else ==0 \n",playresp, targetplay);}
	if (DEBUG){ 
	  respwin_sec = resp_window.tv_sec;
	  respwin_usec = resp_window.tv_usec;
	  printf("resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);}
	
	if (DEBUG){
	  resp_sec = resp_rt.tv_sec;      
	  resp_usec = resp_rt.tv_usec;
	  printf("resp rt sec: %d \t usec: %d\n", resp_sec, resp_usec);} 
	resp_rxt = (float) resp_rt.tv_sec + ( (float) resp_rt.tv_usec/1000000);  /* format reaction time */
	if (DEBUG){printf("flag: resp_rt = %.4f\n", resp_rxt);}
	
	strftime (hour, 16, "%H", loctime);                    /* format wall clock times for trial end*/
	strftime (min, 16, "%M", loctime);
	strftime (month, 16, "%m", loctime);
	strftime (day, 16, "%d", loctime);
	
	++classRses[repcount].count; ++classRtot[repcount].count; 
	
	if (DEBUG){
	  printf("about to consequate\n"); /*code does not get to here...*/
	  printf("end of all play now and  repcount= %d playresp= %d targetplay=%d",repcount,playresp,targetplay);
	  printf("flag: trial type (dodiff)= %d \n",dodiff);}
	
	/* Consequate responses */
	/* resp_sel: 1=diff 0=same kept track of by dodiff*/
	/* resp_acc: 0=incorrect 1=correct 3=NR */
	/* stim_reinf hard-coded to == 100*/
	gettimeofday(&consequate_begin, NULL);
	
	if (targetplay==0){ /*if only the anchor was ever played: either aborted due to NR or LTR during some repetition ; dodiff is still 0 if no target ever played*/
	  if (DEBUG){printf("target never played and dodiff= %d",dodiff);}
	  
	  if (playresp==0){/*aborted during anchor due to abberant LTR so need to punish*/
	    if (DEBUG){printf("timeout is called");}
	    resp_sel = dodiff;
	    resp_acc = 0; /*incorrect left response to anchor play*/
	    ++classRses[repcount].anchorabort; ++classRtot[repcount].anchorabort;
	    reinfor=timeout(stim_reinf);
	    numconsecutivecorrect = 0;
	  }
	  
	  if (playresp==2){ /*if there was no CTR by the end of an anchor stimulus*/
	    resp_sel= dodiff;
	    resp_acc = 2; /*no response*/
	    ++classRses[repcount].anchornoresp; ++classRtot[repcount].anchornoresp; 
	    reinfor=0;
	    numconsecutivecorrect = 0;
	    if (DEBUG){ printf("flag: no response during anchor, but no reinforcement\n");}
	  }
	  /*if observed all anchor presentations, then target responses rewarded accordingly;  playresp==1 and dodiff=0 and targetplay was now set to ==1 */
	}
	
	if (targetplay==1){ /*if a target was played; includes if reached maxresponses for same trial*/
	  if (DEBUG){printf("target played and dodiff= %d",dodiff);}	  
	  if(dodiff == 1){   /* If different trial... dodiff ==1 during target  playback */
	    
	    if (playresp==0){   /*incorrect CTR to different target*/
	      resp_sel = dodiff;
	      resp_acc = 0;
	      if (DEBUG){ printf("flag: CTR response to different stimulus\n");}
	      ++classRses[repcount].targetabort; ++classRtot[repcount].targetabort;
	      reinfor =  timeout(stim_reinf); /*incorrect responses are reinforced with timeout*/
	      numconsecutivecorrect = 0;
	    }
	    
	    if (playresp==1){ /*correct LTR to different target*/
	      resp_sel = dodiff;
	      resp_acc = 1;
	      ++classRses[repcount].targetgo; ++classRtot[repcount].targetgo;
	      if (DEBUG){ printf("flag: LTR  correct response to different stimulus\n");}
	      
	      if (fixratio>=1){
		reinfor=feedFR(stim_reinf, &f, &numconsecutivecorrect, &fixratio);
	      }
	      if (fixratio==0){
		reinfor = feed(stim_reinf, &f); /*correct responses are reinforced with feed*/
	      }
	      
	      if (reinfor == 1) { ++fed;} /*correct responses are reinforced with feed; keep track of feeds*/
	    }
	    
	    if (playresp==2){ /*no response*/
	      resp_sel = dodiff;
	      resp_acc = 2;
	      if (DEBUG){ printf("flag:no response\n");}
	      ++classRses[repcount].targetnoresp; ++classRtot[repcount].targetnoresp; 
	      reinfor = 0;
	      numconsecutivecorrect = 0;
	    }
	  }
	  
	  if (dodiff == 0){ /*this is only possible if maxcount was reached*/
	    if (playresp==1){   /*correct CTR to same target*/
	      resp_sel = dodiff;
	      resp_acc = 1;
	      if (DEBUG){ printf("flag: CTR correct  response to same stimulus\n");}
	      /* ++classRses[repcount].anchorgo; ++classRtot[repcount].anchorgo; already took care of this in the anchor playback loop */
	      
	      if (fixratio>=1){
		reinfor=feedFR(stim_reinf, &f, &numconsecutivecorrect, &fixratio);
	      }
	      if (fixratio==0){
		reinfor = feed(stim_reinf, &f); /*correct responses are reinforced with feed*/
	      } 
	      if (reinfor == 1) { ++fed;}	 
	    }
	  } 
	}
	
	/* Pause for ITI */
	reinfor_sum = reinfor + reinfor_sum;
	operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	nanosleep(&iti, NULL);                     /* wait intertrial interval */
	if (DEBUG){printf("flag: ITI passed\n");}
	/* Write trial data to output file */
	strftime (tod, 256, "%H%M", loctime);
	strftime (date_out, 256, "%m%d", loctime);
	fprintf(datafp, "%d\t%d\t%d\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%.4f\t%s\t%s\n", session_num, trial_num,dodiff,anchorexm,targetexm, repcount,maxreps,targetplay,resp_acc,reinfor, docue,resp_rxt, tod, date_out );
	fflush (datafp);
	if (DEBUG){printf("flag: trail data written\n");}
	
	/*generate some output numbers*/
	for (i = 0; i<nstims;++i){
	  classRses[i].anchorRatio = (float)(classRses[i].anchorgo) /(float)(classRses[i].anchorabort);
	  classRtot[i].anchorRatio = (float)(classRtot[i].anchorgo) /(float)(classRtot[i].anchorabort);
	  classRses[i].targetRatio = (float)(classRses[i].targetgo) /(float)(classRses[i].targetabort);
	  classRtot[i].targetRatio = (float)(classRtot[i].targetgo) /(float)(classRtot[i].targetabort);
	}	 
	
	sessionTrials=0;
	
	if (DEBUG){printf("flag: ouput numbers done\n");}
	/* Update summary data */
	
	if(freopen(dsumfname,"w",dsumfp)!= NULL){
	  fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
	  fprintf (dsumfp, "\tRESPONSE RATIOS hit/false (by trial type)\n");
	  fprintf (dsumfp, "\tAnchor     \t\tTarget     \t\tAnchorTotals \t\tTargetTotals\n");
	  for (i = 0; i<maxreps;++i){
	    fprintf (dsumfp, "\t\t%1.4f  \t\t%1.4f    \t\t%1.4f     \t\t%1.4f\n", 
		     classRses[i].anchorRatio, classRses[i].targetRatio, classRtot[i].anchorRatio, classRtot[i].targetRatio);
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
	
	/* End of trial chores */
	sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                   /* unblock termination signals */ 
	
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
	currtime=(atoi(hour)*60)+atoi(min);
	if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
	
      } /* this should close the while loop for starttime<currtime<stoptime "trials on" period */
      

      /* if restricting trial access then need to calculate new starttime while stoptime carries over from trialson loop */
      if (freerun==1){
	trialson=0;
	trialsoff=1;
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);      
	strftime (min, 16, "%M", loctime);
	currtime=(atoi(hour)*60)+atoi(min);
	stoptime=currtime;
	starttime=currtime+offRunTime;
	if (DEBUG){
	  printf("frerunning and right now trialson: %d, trialsoff: %d \n",trialson,trialsoff);
	  printf("current time: %d, newstarttime: %d\n",currtime,starttime);
	}
	
	/* this starts a while loop for trials off period */
	while( currtime<=starttime ) {
	  /* keep houselights on, but inactivate all lights and feeder  */
	  operant_write(box_id, HOUSELT, 1);
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
	  if (DEBUG){
	    printf("trials off because currtime: %d\t starttime: %d\t=stoptime: %d\n",currtime,starttime,stoptime);} 
	}/* this should close the while loop for starttime>currtime>stoptime "trials off" period */
	
	/* so now need to calculate a new stoptime to stop the next block of access to trials while starttime carries over from trialsoff loop */
	trialson=1;
	trialsoff=0;
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);      
	strftime (min, 16, "%M", loctime);
	currtime=(atoi(hour)*60)+atoi(min);
	starttime=currtime;
	stoptime=currtime+onRunTime;
	if (DEBUG){
	  printf("frerunning and right now doing trialson: %d, trialsoff: %d \n",trialson,trialsoff);
	  printf("current time: %d, starttime: %d, newstoptime: %d\n",currtime,starttime,stoptime);
	}
      }
      
    } /* this ends the loop "while" currtime>=daytime || currtime<=nighttime */
    
    
    /* Loop with lights out during the night */
    if (DEBUG){printf("minutes since midnight: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
    while( (currtime<=daytime) || (currtime>=nighttime) ){
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
      if (DEBUG){
	printf("minutes since midnight: %d\t daytime: %d\t=nighttime: %d\n",currtime,daytime,nighttime);}
    }
    operant_write(box_id, HOUSELT, 1);
    
    /*reset some vars for a new day */
    ++session_num;       /* increase sesion number */ 
    
    for (i = 0; i<maxreps;++i){ /*each structure has a line for each type of anchor rep trial*/
      classRses[i].targetabort = classRses[i].targetgo =classRses[i].targetnoresp=  classRses[i].anchorabort= classRses[i].anchorgo = classRses[i].anchornoresp = 0;
      classRses[i].targetRatio = classRses[i].anchorRatio=0.0;
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
      daystarthour=atoi(temphour);
      daystartmin=atoi(tempmin);
      strftime(buffer,30,"%m-%d-%Y %H:%M:%S",localtime(&rise_tt));
      if(DEBUG){fprintf(stderr,"Sessions start at sunrise(Today: '%s')\n",buffer);}
    }
    if(dosunset){
      set_tt = sunset(atoi(year), atoi(month), atoi(day), latitude, longitude);
      strftime(temphour, 16, "%H", localtime(&set_tt));
      strftime(tempmin, 16, "%M", localtime(&set_tt));
      daystophour=atoi(temphour);
      daystopmin=atoi(tempmin);
      strftime(buffer,30,"%m-%d-%Y  %T",localtime(&set_tt));
      if(DEBUG){fprintf(stderr,"Session stop at sunset(Today: '%s')\n",buffer);}
    }
    
    daytime=(daystarthour*60)+daystartmin;
    nighttime=(daystophour*60)+daystopmin;      
    
    
    if(DEBUG){fprintf(stderr, "daystarthour: %d\tdaystartmin: %d\tdaystophour: %d\tdaystopmin: %d\n", daystarthour,daystartmin,daystophour,daystopmin);}
    
  }while (1); 
  /*  Cleanup */
  free(playlist);
  fclose(datafp);
  fclose(dsumfp);
  snd_pcm_close(handle);
  return 0;
}

