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
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>
#include <sndfile.h>

#include "/usr/local/src/operantio/operantio.c"
//#include "/usr/local/src/audioio/audout.c"

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
int vrmin = 1;
int vrmax = 10;
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
int timeout(int rval);
int probeGO(int rval, int mirr, Failures *f);

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};

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
  fprintf(stderr, "    gng [-help] [-B int] [-t float][-on int:int] [-off int:int] [-x] [-VR] [-S <subject number>] <filename>\n\n");
  fprintf(stderr, "        -help          = show this help message\n");
  fprintf(stderr, "        -B int         = use '-B 1' '-B 2' ... '-B 12' \n");
 
  fprintf(stderr, "        -t float       = set the timeout duration to float secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -f             = flash the center key light during the response window \n");
  fprintf(stderr, "        -on int:int    = set hour:min for exp to start eg: '-on 7:30' (default is 7:00 AM)\n");
  fprintf(stderr, "        -off int:int   = set hour for exp to stop eg: '-off 19:45' (default is 7:00 PM)\n");
  fprintf(stderr, "                         To use daily sunset or sunrise times set 'on' or 'off' to '99'\n");
  fprintf(stderr, "        -VR int:int    = min:max number of variable anchor stimulus presentation\n");
  fprintf(stderr, "        -w 'x'         = set the response window duration to 'x' secs (use an integer)\n");
  fprintf(stderr, "        -S int         = specify the subject ID number (required)\n");
  fprintf(stderr, "        filename       = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "                         where each line is: 'Class' 'Sndfile' 'Freq'\n");
  fprintf(stderr, "                         'Class'= 1 for all stimuli \n");
  fprintf(stderr, "                         'Sndfile' is the name of the stimulus soundfile (use WAV format 44.1kHz only)\n");
  fprintf(stderr, "                         'Freq' is the overall stimulus presetation rate (relative to the other stimuli). \n"); 
  fprintf(stderr, "                           The actual rate for each stimulus (expressed as an integer) is that value divded by the\n");  fprintf(stderr, "                           sum for all stimuli. Set all prefreq values to 1 for equal probablility \n"); 
  fprintf(stderr, "                          no probe stimuli \n");
  exit(-1);
}

/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *vrmin, int *vrmax, int *resp_wind, float *timeout_val, char **stimfname)
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
     else if (strncmp(argv[i], "-t", 2) == 0)
       sscanf(argv[++i], "%f", timeout_val);
     else if (strncmp(argv[i], "-on", 3) == 0){
       sscanf(argv[++i], "%i:%i", starthour,startmin);}
     else if (strncmp(argv[i], "-off", 4) == 0){
       sscanf(argv[++i], "%i:%i", stophour, stopmin);}
     else if (strncmp(argv[i], "-VR", 3) == 0){                                                                  
       sscanf(argv[++i], "%i:%i", vrmin, vrmax);} 
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
  short *sigbuff, *noisebuff, *obuff;
  unsigned short *ptr;
  sf_count_t incount;
  double inframes, padded,LLrms;
  long pad = 0;
  snd_pcm_uframes_t noiseframes, outframes, totframesout;
  int err, count, init,observe=0;
 int nbits, bw, j, k, i, max, foo;
   float dcoff1, dcoff2, total, rms1, rms2, db1, db2,noisedb, signaldb, newrms, scale;
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
  /*****
  *make outframes of white noise
  *noiseframes=outframes;
  *  if(DEBUG){printf("noise is %d frames long\n", (int)noiseframes);}
  *noisebuff = (short *) malloc(sizeof(int)*noiseframes);
  *srand(time(0));
  *for (i=0;i<noiseframes;i++)
  *  noisebuff[i]= (int) (32767*2*(float) rand()/RAND_MAX+0.0)-32767;
 
 scale the noise to correct db
  *nbits = 16;
  *bw=6.0206*nbits;  we assume 16bit soundfiles for now
  
  *total=0;
  *for (j=0; j < outframes; j++)
    *total += sigbuff[j];
  *dcoff1 = (float) (total * 1.0 / outframes);
  *if(DEBUG){printf("DC offset for '%s' is %f\n", sfname, dcoff1);}

  *total=0;
  *for (k=0; k < noiseframes; k++)
  *  total += noisebuff[k];
  *dcoff2 = (float) (total * 1.0 / noiseframes);
  if(DEBUG){printf("DC offset for the noise is %f\n",dcoff2);}

  LLrms=0.0;
  for (j=0; j<outframes; j++)
    LLrms += SQR(sigbuff[j] - dcoff1);
  rms1 = sqrt(LLrms / (double)outframes) / (double) pow(2,15);
  db1 = bw + (20.0 * log10(rms1) );
  if(DEBUG){printf("rms for '%s' is %f dB SPL\n", sfname, db1);}
  
  LLrms=0.0;
  for (k=0; k < noiseframes; k++)
    LLrms += SQR(noisebuff[k] - dcoff2);
  rms2 = sqrt(LLrms / (double)noiseframes)/ (double) pow(2,15);
  db2 = bw + (20.0 * log10(rms2) );
  if(DEBUG){printf("rms for the noise is %f dB SPL\n", db2);}

  scale sfs to the new levels 
  *newrms=scale=0.0;
  *if(signaldb != 0){
  *  foo=(signaldb-bw)/20.0;
  *  newrms = pow(10,foo);
   * scale = newrms/rms1;
 * }
  *if(DEBUG){printf("newrms:%g, tmp: %d, rms1: %g, scale: %g\n", newrms, foo, rms1, scale);}
  *for (j=0; j<outframes; j++)   
    sigbuff[j] = scale * sigbuff[j];
  
  *newrms=scale=0.0;
  *if(noisedb != 0){
  *  foo=(noisedb-bw)/20.0;
  *  newrms = pow(10, foo);
  *  scale = newrms/rms2;
  *}
  *else
        scale = 0;
  
      if(DEBUG){printf("newrms:%g, tmp: %d, rms1: %g, scale: %g\n", newrms, foo, rms1, scale);}
  
      for (j=0; j<noiseframes; j++)
        noisebuff[j] = scale * noisebuff[j];
  
      if(DEBUG){
    LLrms=0.0;
        for (j=0; j<outframes; j++)
          LLrms += SQR(sigbuff[j] - dcoff1);
        rms1 = sqrt(LLrms / (double)outframes) / (double) pow(2,15);
        db1 = bw + (20.0 * log10(rms1) );
    printf("scaled rms for '%s' is %f dB SPL\n", sfname, db1);
    
        LLrms=0.0;
        for (k=0; k < noiseframes; k++)
      LLrms += SQR(noisebuff[k] - dcoff2);
        rms2 = sqrt(LLrms / (double)noiseframes) /(double) pow(2,15);
    db2 = bw + (20.0 * log10(rms2) );
        printf("scaled rms for the noise is %f dB SPL\n", db2);
      }
  
      obuff = (short *) malloc(sizeof(int)*padded);
  
add signal to the noise 
          for (i = 0; i<outframes; i++)
            obuff[i] = sigbuff[i] + noisebuff[i];
  
  set peak 
          for(i=0; i<outframes;i++){
        if( max < SQR(obuff[j]) )
          max = SQR(obuff[j]);
      }
  








  snd_pcm_nonblock(handle,1); /*make sure you set playback to non-blocking*/
  
  /*playback with polling*/
  count = snd_pcm_poll_descriptors_count (handle);
  if (count <= 0) {
    printf("Invalid poll descriptors count\n");
    sf_close(sf);
    free(sfinfo);
    free(obuff);
    return count;
  }
  ufds = malloc(sizeof(struct pollfd) * count);
  if (ufds == NULL) {
    printf("Not enough memory\n");
    sf_close(sf);
    free(sfinfo);
    free(obuff);
    return -ENOMEM;
  }
  if ((err = snd_pcm_poll_descriptors(handle, ufds, count)) < 0) {
    printf("Unable to obtain poll descriptors for playback: %s\n", snd_strerror(err));
    sf_close(sf);
    free(sfinfo);
    free(obuff);
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
	  free(obuff);
          exit(EXIT_FAILURE);
        }
        init = 1;
      } 
      else {
        printf("Wait for poll failed\n");
	sf_close(sf);
	free(sfinfo);
	free(obuff);
        return err;
      }
    }
  }
  
  totframesout=0;
  ptr=obuff;
  observe=0;
  
  /*playback loop*/
  while (outframes > 0)  
    {
      err = snd_pcm_writei(handle, ptr, outframes); /*should outframes be period???*/
      if (err < 0) {
	if (xrun_recovery(handle, err) < 0) {
	  printf("Write error: %s\n", snd_strerror(err));
	  sf_close(sf);
	  free(sfinfo);
	  free(obuff);
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
	free(obuff);
	return 0;  
      }
      if(operant_read(box_id, obsvresp)){ 
	gettimeofday(&resp_lag, NULL);
	observe=1;
	if(respfcn==0){  //this might trigger a rapid abort when the target comes on close to an obs response!!
	  sf_close(sf);
	  free(sfinfo);
	  free(obuff);
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
  free(obuff);
  obuff=NULL;
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
  int dinfd=0, doutfd=0, nstims, stim_number, stim_reinf=100 ,subjectid, period,resp_wind,trial_num,targetplay, targetresp,session_num,targetval,anchorval,coinflip,vrdiff,vrtmp,vr,vrcount,playresp,targetpeck, i,j,k, totnstims=0,sessionTrials, dosunrise=0,dosunset=0,starttime,stoptime,currtime;
  float resp_rxt=0.0, timeout_val=0.0;
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval stimoff,stimon, resp_window, resp_rt;
  struct tm *loctime;
  int center = 0, fed = 0;
  Failures f = {0,0,0,0};
  int reinfor_sum = 0, reinfor = 0;
  int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
  struct stim {
    char exemplar[128];
    int freq;
    int playnum;
  }stimulus[MAXSTIM];
  struct response {
    int count;
    int targetabort;
    int targetgo;
    int targetnoresp;
    int anchorabort;
    int anchorgo;
    int anchornoresp;
  } classRses[2], classRtot[2];
  
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
  command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &vrmin, &vrmax, &resp_wind, &timeout_val, &stimfname);
  if(DEBUG){
    fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, xresp=%d, resp_wind=%d timeout_val=%f flash=%d stimfile: %s\n",box_id, subjectid, starthour, stophour, startmin, stopmin, xresp, resp_wind, timeout_val, flash, stimfname);
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
    {respoff.tv_sec = resp_wind;
    fprintf(stderr, "response window duration set to %d secs\n", (int) respoff.tv_sec);}
  if(timeout_val>0.0)
    {timeout_duration = (int) (timeout_val*1000000);
    fprintf(stderr, "timeout duration set to %d microsecs\n", (int) timeout_duration);}
  
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
 
  if(flash){fprintf(stderr, "!!WARNING: Flashing keylights during response window!!\n");}
  
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
  fprintf (datafp, "Sess#\tTrl#\tTrlTyp\tStimulus\t\t\tClass\tRspSL\tRspAC\tRspRT\tTOD\tDate\n");
  
  
   /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
   ********************************************/
  session_num = 1;
  trial_num = 0;
  /*took out option for correction trials*/
  
  if (DEBUG){printf("stimulus counters zeroed!\n");} 
  
  for(i = 0; i<2;++i){   /*zero out the response tallies */
    classRses[i].targetabort = classRses[i].targetgo =classRses[i].targetnoresp = classRses[i].anchorabort= classRses[i].anchorgo = classRses[i].anchornoresp =  classRtot[i].targetabort = classRtot[i].targetgo =classRtot[i].targetnoresp = classRtot[i].anchorabort= classRtot[i].anchorgo = classRtot[i].anchornoresp = 0.0;
  }

  curr_tt = time(NULL);
  loctime = localtime (&curr_tt);
  strftime (hour, 16, "%H", loctime);
  strftime(min, 16, "%M", loctime);
  if (DEBUG){printf("hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
  currtime=(atoi(hour)*60)+atoi(min);
  
  operant_write (box_id, HOUSELT, 1);        /* house light on */
  do{
    while ((currtime>=starttime) && (currtime<stoptime)){         /* start main trial loop */
      if (DEBUG){printf("minutes since midnight at loop start: %d\t starttime: %d\tstoptime: %d\n",
			currtime,starttime,stoptime);}
      
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
      
      /*choose trial type:same or different*/
      coinflip = (1+(int)(100.0*rand()/(RAND_MAX+1.0)))%2;
      if (DEBUG){
	printf("coinflip was %d :if it was 1 do different trial if 0 do same trial \n",coinflip);}
      if (coinflip==1){ /*do 'different' trial */
	targetval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));   /*select target stim at random*/ 
	targetresp = LEFTPECK;
	/*make sure target is different from anchor*/
	while(targetval == anchorval)
	  targetval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));   /*make sure targ != anchor*/ 
      }
      else{ 
	targetval=anchorval; /*'same' trial so target is same as anchor*/	
	targetresp = CENTERPECK;
      }
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
      /*choose number of anchor repetitions*/
      vrdiff = (vrmax-vrmin)+1;
      vrtmp = 1+(int)(100.0*rand()/(RAND_MAX+1.0));
      vr = (vrtmp % vrdiff)+ vrmin;
      vrcount =0;
      if (DEBUG){printf("variable ratio for anchor presentation is: %d \n",vr);}
      /* start correction trial loop, but not using correction trials... so do not use the loop */
      resp_sel = resp_acc = resp_rxt = 0;       /* zero trial variables        */
      ++trial_num;
      curr_tt = time(NULL);
      
      /* Wait for center key press */
      if (DEBUG){printf("flag: waiting for center key press\n");}
      operant_write (box_id, HOUSELT, 1);        /* house light on */
      center = 0;
      do{                                         
	nanosleep(&rsi, NULL);	               	       
	center = operant_read(box_id, CENTERPECK);      /*get value at center peck position*/	  
      }while (center==0);  
      
      
      sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
      
      
      /*play the anchor for VR reps while observ key pecked*/
      targetplay=0; /*initialize targetplay=0 because no target has played*/
      do{
	playresp = playwavwhile(1, fanchor, period, box_id, CENTERPECK, LEFTPECK);
	vrcount += 1;
	if (DEBUG){
	  printf("playresp returned: %d\n", playresp);
	}
	if (playresp<0){
	  fprintf(stderr, "playwavwhile failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		  pcm_name, anchorexm, asctime(localtime (&curr_tt)) );
	  fprintf(datafp, "playwavwhile failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		  pcm_name, anchorexm, asctime(localtime (&curr_tt)) );
	}
	if (DEBUG){
	  printf("on %d of %d anchors\n", vrcount, vr);
	}	
      }while ((vrcount<vr) && (playresp==1)); 
      
      /*play the target if anchor repetitions completed*/
      if (playresp==1)
	{targetplay=1; /*if observed all anchor presentations, then target plays and target responses rewarded or ignored accordingly*/
	playresp=3; /*reset the response flag so that this playwavwhile will set it; returns error if stays at ==3*/
	gettimeofday(&stimon, NULL);
	playresp = playwavwhile(0, ftarget, period, box_id, LEFTPECK,CENTERPECK);/*now leftpeck is target, but allow abortion for target*/
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
	
	if (playresp==2)
	  {
	    /* Wait for target response (during resp window) if you haven't gotten one yet */
	    timeradd (&stimoff, &respoff, &resp_window);
	    if (DEBUG){printf("flag: waiting for response\n");}
	    targetpeck=0; 
	    do{
	      targetpeck=operant_read(box_id, targetresp);
	      gettimeofday(&resp_lag, NULL);  
	      nanosleep(&rsi, NULL);
	    }while((targetpeck == 0) && (timercmp(&resp_lag, &resp_window, <) ) ); 
	    if (targetpeck==0)
	      playresp=2; /*if still NR, playresp reflects that and remains == 2*/
	    else
	      playresp=targetpeck; /*if targetpeck then playresp=1*/
	  }
	}
      timersub (&resp_lag, &stimon, &resp_rt);           /* reaction time */
      
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
      if (DEBUG){printf("flag: resp_rxt = %.4f\n", resp_rxt);}
      
      strftime (hour, 16, "%H", loctime);                    /* format wall clock times for trial end*/
      strftime (min, 16, "%M", loctime);
      strftime (month, 16, "%m", loctime);
      strftime (day, 16, "%d", loctime);
      
      ++stimRses[coinflip].count; ++stimRtot[coinflip].count; 
      
      if (DEBUG){
	printf("about to consequate\n");} /*code does not get to here...*/
      
      
      /* Consequate responses */
      /* resp_sel: 1=diff 2=same */
      /* resp_acc: 0=incorrect 1=correct 3=NR */
      /* stim_reinf hard-coded to == 100*/
      
      if (DEBUG){
	printf("flag: trial type = %d ; 1==diff 0==same\n",coinflip);
	
	if (targetplay==0){ /*if only the anchor was ever played: either aborted due to NR or LTR during some repetition*/
	  if (playresp==0){/*if aborted during anchor due to abberant LTR punish*/
	    if (DEBUG){
	      printf("timeout is called");}
	    reinfor=timeout(stim_reinf);
	    
	    if (coinflip==1){
	      resp_sel=1;
	      resp_acc = 0; /*incorrect because if no target ever played then was a left response to an anchor presentation*/
	      ++stimRses[coinflip].anchorabort; ++stimRtot[coinflip].anchorabort; 
	      if (DEBUG){printf("incorrect because left response during anchor even though diff trial \n");}
	    }
	    else if (coinflip==0){
	      resp_sel=0;
	      resp_acc = 0;/*incorrect Left response during anchor*/
	      ++classRses[coinflip].anchorabort; ++classRtot[coinflip].anchorabort;  
	      if (DEBUG){ printf("flag: left response during anchor; same trial anyway\n");}
	    }
	  }
	  if (playresp==2){ /*if there was no CTR by the end of an anchor stimulus*/
	    resp_sel=coinflip;
	    resp_acc = 2; /*no response*/
	    ++classRses[coinflip].anchorNR; ++classRtot[coinflip].anchorNR; 
	    if (DEBUG){ printf("flag: no response during anchor, but no reinforcement\n");}
	  }
	}
	
	if (targetplay==1){ /*if a target was played*/
	  if(coinflip == 1){   /* If different trial */
	    if (playresp==0){   /*incorrect CTR to different target*/
	      resp_sel = 1;
	      resp_acc = 0;
	      if (DEBUG){ printf("flag: CTR response to different stimulus\n");}
	      ++classRses[coinflip].targetabort; ++classRtot[coinflip].targetabort;
	      reinfor =  timeout(stim_reinf); /*incorrect responses are reinforced with timeout*/
	    }
	    if (playresp==1){ /*correct LTR to different target*/
	      resp_sel = 1;
	      resp_acc = 1;
	      ++classRses[coinflip].targetgo; ++classRtot[coinflip].targetgo;
	      if (DEBUG){ printf("flag: LTR  correct response to different stimulus\n");}
	      reinfor = feed(stim_reinf, &f); /*correct responses are reinforced with feed*/
	      if (reinfor == 1) { ++fed;} /*correct responses are reinforced with feed; keep track of feeds*/
	    }
	    if (playresp==2){ /*no response*/
	      resp_sel = 1;
	      resp_acc = 2;
	      if (DEBUG){ printf("flag:no response\n");}
	      ++classRses[coinflip].targetNR; ++classRtot[coinclip].targetNR; 
	      reinfor = 0;
	    }
	  }
	  if (coinflip == 0){ /*if same trial*/
	    if (playresp==0){   /*correct CTR to same target*/
	      resp_sel = 0;
	      resp_acc = 1;
	      if (DEBUG){ printf("flag: CTR correct  response to same stimulus\n");}
	      ++classRses[coinflip].targetgo; ++classRtot[coinflip].targetgo; 
	      reinfor = feed(stim_reinf, &f); /*correct responses are reinforced with feed*/
	      if (reinfor == 1) { ++fed;}
	    }
	    if (playresp==1){ /*incorrect LTR to same target*/
	      resp_sel = 0;
	      resp_acc = 0;
	      ++classRses[coinflip].targetabort; ++classRtot[coinflip].targetabort; 
	      if (DEBUG){ printf("flag: incorrect LTR response to same stimulus \n");}
	      reinfor =  timeout(stim_reinf); /*incorrect responses are reinforced with timeout*/
	    }
	    if (playresp==2){ /*no response*/
	      resp_sel = 0;
	      resp_acc = 2;
	      if (DEBUG){ printf("flag:no response\n");}
	      ++classRses[coinflip].targetNR; ++classRtot[coinflip].targetNR; 
	      reinfor = 0;
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
	fprintf(datafp, "%d\t%d\%d\t%s\t%s\%d\%d\%d\t%d\t%d\t%d\t%.4f\t%s\t%s\n", session_num, trial_num,coinflip,anchorexm,targetexm, vrcount,VR,targetplay,resp_sel, resp_acc,reinf, resp_rxt, tod, date_out );
	fflush (datafp);
	if (DEBUG){printf("flag: trail data written\n");}
	
	/*generate some output numbers*/
	for (i = 0; i<nstims;++i){
	  stimRses[i].ratio = (float)(stimRses[i].go) /(float)(stimRses[i].count);
	  stimRtot[i].ratio = (float)(stimRtot[i].go) /(float)(stimRtot[i].count);	
	}
	sessionTrials=0;
	
	if (DEBUG){printf("flag: ouput numbers done\n");}
	/* Update summary data */
	
	if(freopen(dsumfname,"w",dsumfp)!= NULL){
	  fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
	  fprintf (dsumfp, "\tRESPONSE RATIOS (by stimulus)\n");
	  fprintf (dsumfp, "\tStim     \t\tSession     \t\tTotals\n");
	  for (i = 0; i<nstims;++i){
	    fprintf (dsumfp, "\t%s     \t\t%1.4f     \t\t%1.4f\n", 
		     stimulus[i].exemplar, stimRses[i].ratio, stimRtot[i].ratio);
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
	/*if(xresp)*/
	/*if (stim_class==2 && resp_acc == 0){correction = 0;}else{correction = 1;} */  /*run correction for GO resp to s-*/
	/*	else 
		correction = 1; */ /* make sure you don't invoke a correction trial by accident */
	
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
	currtime=(atoi(hour)*60)+atoi(min);
	if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
	
      }
      
      /*}while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); */ /* correction trial loop */
      
    }
    /*think i need to put back in one of the do/while loops to put the ++session_num outside of the trial loop, so put this closed bracket here and then need to still wonder what that last while (1) statement means and whether i need it*/
    
    
    
    /*stim_number = -1;       not using correction trial so do not need to reset the stim number for correct trial*/
    /* main trial loop*/
    
    /* Loop with lights out during the night */
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
    
    /*reset some vars for a new day */
    ++session_num;                                                                     /* increase sesion number */ 
    for (i = 0; i<nstims;++i){
      stimRses[i].go = 0;
      stimRses[i].no = 0;
      stimRses[i].ratio = 0.0;
      stimRses[i].count = 0;
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
    
    
    /*}while (1) if (1) loop forever) dont know what this is but take it out to make do/whiles match up  ???*/
    
  }while (1); /*loops forever might need to be here...how get it to clean up if it loops forever?*/
  /*  Cleanup */
  free(playlist);
  fclose(datafp);
  fclose(dsumfp);
  snd_pcm_close(handle);
  return 0;
  
}

