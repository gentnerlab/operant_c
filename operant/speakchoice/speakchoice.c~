
#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <math.h>
#include <signal.h>
#include <sys/time.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include "/usr/local/src/operantio/operantio.c"
#include <sunrise.h>

#ifndef SQR
#define SQR(a)  ((a) * (a))
#endif

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
#define SAMPMAX                  1323000        /* max samples allowed in soundfile */        
#define MAXCLASS                 256            /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define TIMEOUT_DURATION         2000000       /* default duration of timeout in microseconds */
#define CUE_DURATION             1000000       /* cue duration in microseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define OUTSR                    44100         /* sample rate for output soundfiles */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define MAXTONES                 64            /* maximum possible number of tones per trial */
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
const char exp_name[] = "SPKCHOICE";
int box_id = -1;
int flash = 0;
int xresp = 0;
int nchan = 2;

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};

typedef struct {
  int hopper_failures;
  int hopper_wont_go_down_failures;
  int hopper_already_up_failures;
  int response_failures;
} Failures; 

snd_pcm_t *handle;

/* -------- Signal handling --------- */
int client_fd = -1;
static void sig_pipe(int signum)
{ fprintf(stdout,"SIGPIPE caught\n"); client_fd = -1;}
static void termination_handler (int signum){
  snd_pcm_close(handle);
  fprintf(stdout,"closed pcm device: term signal caught: exiting\n");
  exit(-1);
}

/* --------- PCM Setup ------------*/

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
    return(-1);
  }
  
  /* Init params with full configuration space */
  if (snd_pcm_hw_params_any(handle, params) < 0) {
    fprintf(stderr, "Can not configure this PCM device.\n");
    return(-1);
  }
  
  /* set interleaved mode */
  if (snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
    fprintf(stderr, "Error setting access.\n");
    return(-1);
  }
  
  /* Set sample format */
  if (snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE) < 0) {
    fprintf(stderr, "Error setting format.\n");
    return(-1);
  }
  
  /* Set sample rate.*/ 
  if (snd_pcm_hw_params_set_rate_near(handle, params, &rate, 0) < 0) {
    fprintf(stderr, "Error setting rate.\n");
    return(-1);
  }
  
  /* Set number of channels */
  if (snd_pcm_hw_params_set_channels(handle, params, nchan) < 0) {
    fprintf(stderr, "Error setting channels.\n");
    return(-1);
  }
  
  /* Set period size to n frames (samples). */
  persize = 1024; dir=0;
  if (snd_pcm_hw_params_set_period_size_near(handle,params, &persize, &dir)< 0) {
    fprintf(stderr, "Error setting period size to %d.\n", (int) persize);
    return(-1);
  }
    
  
  /* Apply HW parameter settings to PCM device */
  if (snd_pcm_hw_params(handle, params) < 0) {
    fprintf(stderr, "Error setting HW params.\n");
    return(-1);
  }

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



/* ------------ PLAYSTEREO2: PLAYS 2 SIGNALS IN 2 INDEPENDENT SPKR CHANNELS ---------*/
int playstereo2(int jr, int jl, int *rpptr, int *lpptr, int *rfvptr, int *lfvptr, float tonelength, int targside, float tone_db, float noise_db, int period, int *respframe, int *respside)
{
  
  SF_INFO *sfout_info;
  short *obuff, *obufft;
  double padded, LLrms;
  long pad = 0;
  int i=0, j= 0, loops = 0, totalframes, toneframes, outsamps, startframe = 0,err, init;
  snd_pcm_uframes_t outframes, totframesout;
  unsigned short *ptr;
  int outsamprate = OUTSR;
  float foo, rampdenom;
  int nbits, bw, count, right, left;
  signed int total;
  int ramp = 20; /* length of noise ramp in ms */
  float dcoff, rms, db, newrms, scale ,peak_db, peakrms, max = 0.0;
  struct pollfd *ufds;
  
  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use\n");
    return -1;
  }
  if (snd_pcm_reset(handle)<0){
    fprintf (stderr, "cannot reset audio interface for use\n");
    return -1;
  }
  
  
  /* determine length inframes*/
  totalframes = (int) (SAMPMAX);
  toneframes = (int) (tonelength*outsamprate);
  
  /* pad the file size up to next highest period count*/
  pad = (period * ceil((totalframes)/ (float)period)-(totalframes));
  padded = totalframes+pad;
  outframes = padded;
  outsamps =(int) nchan*padded;
  
  /* allocate memory to buffer */
  obuff = (short *) malloc(sizeof(int)*(outsamps));
  if(DEBUG){fprintf(stderr,"outframes:%d, nchan:%d, pad:%i, totalframes:%i, outsamps:%i, target chan:%i\n", (int)outframes, nchan, (int) pad, totalframes, outsamps, targside);}
  /*get the levels of the target soundfile*/
  nbits = 16;
  bw=6.0206*nbits;  /*we assume 16bit soundfiles for now*/
  
  loops=0;
  total=0;
  rampdenom = ceil(ramp*outsamprate/1000.0);
  for (j=0; j<rampdenom; j++){
    loops++;
    obuff[j] = 1000*(j/rampdenom)*(2*rand()/(RAND_MAX+1.0))-1;
    total += obuff[j];
  }
  for (j=rampdenom; j<outsamps; j++){
    loops++;
    obuff[j] = 1000*(2*rand()/(RAND_MAX+1.0))-1;
    total += obuff[j];
  }
  if(DEBUG){fprintf(stderr,"noise: ran %d loops out of %d", loops, outsamps);}
  
  /*rescale noise to set SPL  */ 
  dcoff = (float) (total * 1.0 /(outsamps));
  LLrms = 0.0;
  for (j=0; j<outsamps; j++)
    LLrms += SQR(obuff[j] - dcoff);
  rms = sqrt(LLrms / (double)(outsamps)) / (double) pow(2,15);
  db = bw + (20.0 * log10(rms) );
  if(DEBUG){fprintf(stderr,"noise: dcoff = %1.2f, old rms = %1.2f dB SPL,  ", dcoff, db);}
  foo=(noise_db-bw)/20.0;
  newrms = pow(10,foo);
  scale = newrms/rms;
  if(DEBUG){fprintf(stderr,"changing to: %1.2f dB SPL with scale %1.2f\n", noise_db, scale);}
  for (j=0; j<outsamps; j++)
    obuff[j] = scale * obuff[j];
  
  /* add in right tones at specified dB level*/
  for (j=0; j<jr; j++){
 
    obufft = (short *) malloc(sizeof(int)*(toneframes));
    loops = total = startframe = 0;
    LLrms = dcoff = rms = db = 0.0;
    for (i=0; i<toneframes; i++){
      obufft[i] = 1000*sin(2*M_PI*rfvptr[j]*i/outsamprate); /* not sure if i can do this in C */
      total += obufft[i];
      loops++;
    }
    if(DEBUG){fprintf(stderr,"right tone %d of %d: ran %d loops out of %d, ", j+1, jr, loops, toneframes);}
    dcoff = (float) (total * 1.0 /(toneframes));
    for (i=0; i<toneframes; i++)
      LLrms += SQR(obufft[i] - dcoff);
    rms = sqrt(LLrms / (double)(toneframes)) / (double) pow(2,15);
    db = bw + (20.0 * log10(rms) );
    if(DEBUG){fprintf(stderr,"dcoff = %1.2f, old rms = %1.2f dB SPL,  ", dcoff, db);}
    foo=(tone_db-bw)/20.0;
    newrms = pow(10,foo);
    scale = newrms/rms;
    if(DEBUG){fprintf(stderr,"changing to: %1.2f dB SPL with scale %1.2f \n", tone_db, scale);}
    for (i=0; i<toneframes; i++)
      obufft[i] = scale * obufft[i];
    startframe = rpptr[j];
    if(DEBUG){fprintf(stderr,"adding tone to noise at frame %d\n", startframe);}
    for (i=startframe; i<(startframe+toneframes); i++){
      obuff[i*nchan] = obuff[i*nchan] + obufft[i-startframe];
    }    
    free(obufft);
  }
  
  /* add in left tones at specified dB level*/
  for (j=0; j<jl; j++){
    obufft = (short *) malloc(sizeof(int)*(toneframes));
    loops = total = startframe = 0;
    LLrms = dcoff = rms = db = 0.0;
    for (i=0; i<toneframes; i++){
      obufft[i] = 1000*sin(2*M_PI*lfvptr[j]*i/outsamprate); /* not sure if i can do this in C */
      total += obufft[i];
      loops++;
    }
    if(DEBUG){fprintf(stderr,"left tone %d of %d: ran %d loops out of %d, ", j+1, jl, loops, toneframes);}
    dcoff = (float) (total * 1.0 /(toneframes));
    for (i=0; i<toneframes; i++)
      LLrms += SQR(obufft[i] - dcoff);
    rms = sqrt(LLrms / (double)(toneframes)) / (double) pow(2,15);
    db = bw + (20.0 * log10(rms) );
    if(DEBUG){printf("dcoff = %1.2f, old rms = %1.2f dB SPL,  ", dcoff, db);}
    foo=(tone_db-bw)/20.0;
    newrms = pow(10,foo);
    scale = newrms/rms;
    if(DEBUG){printf("changing to: %1.2f dB SPL with scale %1.2f\n", tone_db, scale);}
    for (i=0; i<toneframes; i++)
      obufft[i] = scale * obufft[i];
    startframe = lpptr[j];
    if(DEBUG){fprintf(stderr,"adding tone to noise at frame %d\n", startframe);}
    for (i=startframe; i<(startframe+toneframes); i++){
      obuff[i*nchan+1] = obuff[i*nchan+1] + obufft[i-startframe];
    }   
    free(obufft);
  }	
  
  /*peak check*/
  scale = 0;
  max = 0.0;
  for(i=0; i<outsamps;i++){
    if( max < SQR(obuff[j]) )
      max = SQR(obuff[j]);
  }
  peakrms=sqrt(max);
  peak_db = bw + (20*log10(peakrms));
  if(peak_db>90){
    fprintf(stderr, "PLAYSTEREO2: SPL in the combined soundfile exceeds 90 dB, SETTING THE MAX TO 90 dB \n");
    scale = 90/peakrms;
    for(i=0; i<outsamps;i++)
      obuff[j] = scale * obuff[j];
  }
  
  /* output info */
  sfout_info = (SF_INFO *) malloc(sizeof(SF_INFO));
  sfout_info->channels = nchan;
  sfout_info->samplerate = outsamprate; 
  sfout_info->format = 10002; /* wav file code = 10002 */
  if(DEBUG){fprintf(stderr,"output file format:%x \tchannels: %d \tsamplerate: %d\n",sfout_info->format, sfout_info->channels, sfout_info->samplerate);}
  
  ptr = obuff;
  totframesout = 0;
  
  /* start peck and count paste */
  if(DEBUG){printf("I'll try to write %d frames\n", (int)outframes);}
  right = left = 0;
  snd_pcm_nonblock(handle,1); 
  
  count = snd_pcm_poll_descriptors_count (handle);
  if (count <= 0) {
    printf("Invalid poll descriptors count\n");
    return count;
  }
  ufds = malloc(sizeof(struct pollfd) * count);
  if (ufds == NULL) {
    printf("Not enough memory\n");
    return -ENOMEM;
  }
  if ((err = snd_pcm_poll_descriptors(handle, ufds, count)) < 0) {
    printf("Unable to obtain poll descriptors for playback: %s\n", snd_strerror(err));
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
	  exit(EXIT_FAILURE);
	}
	init = 1;
      } else {
	printf("Wait for poll failed\n");
	return err;
      }
    }
  }
  totframesout=0;
  ptr=obuff;

  while (outframes > 0) {
    err = snd_pcm_writei(handle,ptr, outframes);
    if (err < 0) {
      if (xrun_recovery(handle, err) < 0) {
	printf("Write error: %s\n", snd_strerror(err));
	exit(EXIT_FAILURE);
      }
      init = 1;
      break;  
    }
    if (snd_pcm_state(handle) == SND_PCM_STATE_RUNNING)
      init = 0;
    
    right = operant_read(box_id, RIGHTPECK);    
    left = operant_read(box_id, LEFTPECK);        
    
    if(right == 1){
      *respside = 2;
      *respframe = totframesout;
      free(sfout_info);
      free(obuff);
      free(ufds);
      ufds=NULL;
      return 1;
    }
    else if(left == 1){
      *respside = 1;
      *respframe = totframesout;
      free(sfout_info);
      free(obuff);
      free(ufds);
      ufds=NULL;
      return 1;
    }
    
    totframesout += err; 
    ptr += err * nchan;
    outframes -= err;
    
    if (outframes == 0){
      if(DEBUG){printf("outframes is zero so I'll break\n");}
  break;
    }
    
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
      } else {
	printf("Wait for poll failed\n");
	return err;
      }
    }
  }
  /*end peck and play paste */

  if(DEBUG){
    printf("exited while writei loop, so what am I waiting for?\n");
    printf("frames not played: %d \t", (int)outframes);
    printf("frames played: %d \n", (int) totframesout);
  }
  
  /*free up resources*/
  free(sfout_info);
  free(obuff);
  free(ufds);
  ufds=NULL;
  return 1;
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
  fprintf(stderr, "        -w float     = set the response window duration to 'x' secs. default = 2.0 seconds\n");
  fprintf(stderr, "        -x           = use this flag to enable correction trials for 'no-response' trials,\n");
  fprintf(stderr, "        -len float   = set the tone length. default = 100 msec\n");
  fprintf(stderr, "        -db  float   = set the tone sound level. default = 65 dB\n");
  fprintf(stderr, "        -n  float    = set the background noise sound level. default = 75 dB\n");
  fprintf(stderr, "        -rf  int     = set the reinforcement rate. default = 100 percent\n");
  fprintf(stderr, "        -pn  int     = set the punishment rate. default = 100 percent\n");
  fprintf(stderr, "        -pt  float   = set the probability of a target per second. default = 0.1 targets per second\n");
  fprintf(stderr, "        -on int:int      = set hour:min for exp to start eg: '-on 7:35' (default is 7AM, use 99 for sunrise)\n");
  fprintf(stderr, "        -off int:int     = set hour:min for exp to stop eg: '-off 19:01' (default is 7PM, use 99 for sunset)\n");
  fprintf(stderr, "        -S int       = specify the subject ID number (required)\n");
  fprintf(stderr, "        filename     = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "                       where each line is: 'Class' 'Wavfile' 'Present_freq' 'Rnf_rate 'Pun_rate''\n");
  fprintf(stderr, "                       'Class'= 1 for LEFT-, 2 for RIGHT-key assignment \n");
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
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, float *resp_wind, float *timeout_val, float *tonelength_man, float *tonedb_man, float *brnoise_man, float *ptar_man, int *reinf_val, int *punish_val)
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
	sscanf(argv[++i], "%f", resp_wind);
      }
      else if (strncmp(argv[i], "-t", 2) == 0){ 
	sscanf(argv[++i], "%f", timeout_val);
      }
      else if (strncmp(argv[i], "-f", 2) == 0)
	flash = 1;
      else if (strncmp(argv[i], "-len", 4) == 0)
	sscanf(argv[++i], "%f", tonelength_man);
      else if (strncmp(argv[i], "-db", 3) == 0)
	sscanf(argv[++i], "%f", tonedb_man);	
      else if (strncmp(argv[i], "-rf", 3) == 0)
	sscanf(argv[++i], "%i", reinf_val);
      else if (strncmp(argv[i], "-pn", 3) == 0)
	sscanf(argv[++i], "%i", punish_val);	  
      else if (strncmp(argv[i], "-pt", 3) == 0)
	sscanf(argv[++i], "%f", ptar_man);
      else if (strncmp(argv[i], "-n", 2) == 0)
	sscanf(argv[++i], "%f", brnoise_man);    
      else if (strncmp(argv[i], "-on", 3) == 0)
        sscanf(argv[++i], "%i:%i", starthour, startmin);
      else if (strncmp(argv[i], "-off", 4) == 0)
        sscanf(argv[++i], "%i:%i", stophour, stopmin);
      else if (strncmp(argv[i], "-help", 5) == 0){
        do_usage();
      }
      else{
        fprintf(stderr, "Unknown option: %s\t", argv[i]);
        fprintf(stderr, "Try 'spkrchoice -help'\n");
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
  FILE *datafp = NULL, *dsumfp = NULL;
  const char stereopcm [] = "doubledac";
  char datafname[128], hour[16], min[16], month[16],day[16], year[16], dsumfname[128], pcm_name[128];
  char timebuff[64], tod[256], date_out[256], buffer[30],temphour[16],tempmin[16];
  int resp_sel, resp_acc, subjectid, period, tot_trial_num, respframe, respside,nrespframes,toneframes,
    played,endplay, trial_num, session_num, i, correction, punish_val = 0, reinf_val = 0, 
    dosunrise=0,dosunset=0,starttime,stoptime,currtime;
  float timeout_val=0.0, resp_rxt=0.0, ptar = 0.10, playme = 0.00, rightorleft, tonelength = 0.100, tonedb = 65.0;
  float  tonelength_man=0.0, tonedb_man = 0.0, respoff_wind = 2.0, resp_wind = 0.0, brnoise_man = -0.2, ptar_man = -0.2, brnoise = 75.0;
  int stimon_sec, stimon_usec, resp_sec, resp_usec;  /* debugging variables */
  int punish = 100, reinf = 100, frangemin = 1000, frangemax = 10000, sr = OUTSR, targside;
  int rightplay[64], *rpptr[64], rightfval[64], *rfvptr[64], leftplay[64], *lpptr[64], leftfval[64], *lfvptr[64];
  int leftcue = GREENLT, rightcue = REDLT;
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval stimon, resp_lag, resp_rt;
  struct tm *loctime;
  Failures f = {0,0,0,0};
  int left = 0, right= 0, center = 0, fed = 0, jr = 0, jl = 0;
  int Lind = 0, Rind = 1, totrightR, totleftR, totrightT, totleftT; 
  int reinfor_sum = 0, reinfor = 0, miss, hit, fa, cr, R_miss, R_hit, R_fa, R_cr, T_miss, T_hit, T_fa, T_cr;
  struct resp{
    int C;
    int X;
    int X2;
    int N;
    int count;
  }R[2], T[2];
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
  command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &resp_wind, &timeout_val, &tonelength_man, &tonedb_man,&brnoise_man, &ptar_man, &reinf_val, &punish_val); 
  if(DEBUG){
    fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, xresp=%d, resp_wind=%f timeout_val=%f flash=%d tonelength_man=%f, tonedb_man=%f, brnoise_man=%f, ptar_man=%f, reinf_val=%d, punish_val=%d\n",
	    box_id, subjectid, starthour, stophour, startmin, stopmin, xresp, resp_wind, timeout_val, flash, tonelength_man, tonedb_man, brnoise_man, ptar_man, reinf_val, punish_val);
	}
  sprintf(pcm_name, "%s%d", stereopcm, box_id);
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
    fprintf(stderr, "\tERROR: try 'spkchoice -help' for available options\n");
    exit(-1);
  }
  
  /*set some variables as needed*/
  if (resp_wind>0.0){
    //respoff.tv_sec = resp_wind;
    respoff_wind = resp_wind;
  }
  nrespframes = (int)(sr * respoff_wind);
  fprintf(stderr, "response window duration set to %f secs, nrespframes set to %d\n", respoff_wind, nrespframes);
  if (tonelength_man > 0.0)
    tonelength = tonelength_man;
  toneframes = (int) (tonelength*sr);
  fprintf(stderr, "tone duration set to %f secs, toneframes set to %d\n", tonelength, toneframes);
  if (tonedb_man > 0.0)
    tonedb = tonedb_man;
  fprintf(stderr, "tone sound level set to %f dB\n", tonedb);
  if(timeout_val>0.0)
    timeout_duration = (int) (timeout_val*1000000);
  fprintf(stderr, "timeout duration set to %d microsecs\n", (int) timeout_duration);
  if(reinf_val > 0)
    reinf = reinf_val;	
  fprintf(stderr, "reinforcement rate set to %d percent for correct trials\n", reinf);
  if(punish_val > 0)
    punish = punish_val;
  fprintf(stderr, "punishment rate set to %d percent for incorrect trials\n", punish);
  if(brnoise_man > - 0.1)
    brnoise = brnoise_man;
  fprintf(stderr, "background noise set to %f dB\n", brnoise);
  if(ptar_man > - 0.1)
    ptar = ptar_man;
  fprintf(stderr, "target probability set to %f tones per second\n", ptar);
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
    fprintf(stderr,"Initializing box %d ...\n", box_id);
    fprintf(stderr,"trying to execute setup(%s)\n", pcm_name);
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
  if(DEBUG){fprintf(stderr,"done\n");}

  fprintf(stderr, "Subject ID number: %i\n", subjectid);
  if(flash){fprintf(stderr, "!!WARNING: Flashing keylights during response window!!\n");}
  if(xresp){fprintf(stderr, "!!WARNING: Enabling correction trials for 'No' responses !!\n");}


  /*  Open & setup data logging files */
  curr_tt = time (NULL);
  loctime = localtime (&curr_tt);
  strftime (timebuff, 64, "%d%b%y", loctime);
  sprintf(datafname, "%i_spkchoice.rDAT", subjectid);
  sprintf(dsumfname, "%i.SPKsummaryDAT", subjectid);
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
  fprintf (stderr,"Writing header info to '%s',", datafname);
  fprintf (datafp, "File name: %s\n", datafname);
  fprintf (datafp, "Procedure source: %s\n", exp_name);
  fprintf (datafp, "Start time: %s", asctime(loctime));
  fprintf (datafp, "Subject ID: %d\n", subjectid);
  fprintf (datafp, "Sess#\tTrl#\tCrxn\tTarg\tChoice\tR_acc\tRT\tReinf\tMiss\tCR\tRT1\tRF1\tRT2\tRF2\tRT3\tRF3\tLT1\tLF1\tLT2\tLF2\tLT3\tLF3\tTOD\tDate\n");
  fprintf (stderr,"done");
	 
  /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
  ********************************************/
  session_num = 1;
  trial_num = 0;
  tot_trial_num = 0;
  correction = 1;

  /*zero out the response tallies */
  if (DEBUG){fprintf(stderr,"zeroing out response tallies\n");}
  for(i = 0; i<2; ++i){
    R[i].C = R[i].X = R[i].X2 = R[i].N = R[i].count = 0;
    T[i].C = T[i].X = T[i].X2 = T[i].N = T[i].count = 0;
    R_hit = R_miss = R_fa = R_cr = T_hit = T_miss = T_fa = T_cr = 0;
  }

  if (DEBUG){fprintf(stderr,"getting time info tallies\n");} 
  curr_tt = time(NULL);
  loctime = localtime (&curr_tt);
  strftime (hour, 16, "%H", loctime);
  strftime(min, 16, "%M", loctime);
  if (DEBUG){fprintf(stderr,"hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
  currtime=(atoi(hour)*60)+atoi(min);
	
  operant_write (box_id, HOUSELT, 1);        /* make sure houselight is on */

  do{                                                                               /* start the main loop */
    while ((currtime>=starttime) && (currtime<stoptime)){                          /* start main trial loop */

      /*clear out previous values */
      for (i=0;i<64; i++){
	rightplay[i]=0.0;
	rightfval[i]=0.0;
	leftplay[i]=0.0;
	leftfval[i]=0.0;
      }
      if (DEBUG){printf("minutes since midnight at loop start: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
      srand(time(0));
      jr=0;
      jl=0;
      if (DEBUG){fprintf(stderr,"tone probability  = %f/frame,\t %1.2f/trial\n", ptar/sr, ptar/sr*SAMPMAX);}
      for(i=0; i<(SAMPMAX-((int)(tonelength*sr))); i++){
	playme = rand()/(RAND_MAX+0.0);
	if (playme<(ptar/sr)){
	  rightorleft = rand()/(RAND_MAX+0.0);
	  if (DEBUG){printf("play tone at t = %1.2f seconds, rand = %1.2f\t", i/(float)sr, rightorleft);}
	  if (rightorleft > 0.5){
	    rightplay[jr]=i;
	    rpptr[jr] = &rightplay[jr];
	    rightfval[jr]=(int) (((frangemax-frangemin+0.0)*rand()/(RAND_MAX+0.0))+ frangemin);  /* select stimulus frequency at random */ 
	    rfvptr[jr] = &rightfval[jr];
	    if(DEBUG){printf("right,\t %1.3f KHz tone\n",(float)rightfval[(jr)]/1000.0);}	    
	    jr++;

	  }
	  else{
	    leftplay[jl]=i; 
	    lpptr[jl] = &leftplay[jl];
	    leftfval[jl]=(int) (((frangemax-frangemin+0.0)*rand()/(RAND_MAX+0.0))+ frangemin);  /* select stimulus frequency at random */ 
	    lfvptr[jl] = &leftfval[jl];
	    if(DEBUG){printf("left,\t %1.3f KHz tone\n",(float)leftfval[(jl)]/1000.0);}
	    jl++;

	  }
	}
      }
      rightorleft = rand()/(RAND_MAX+0.0);
      if (DEBUG){fprintf(stderr,"%d tones, ", jr+jl);}
      if (rightorleft > 0.5){
	targside = 2;
	if (DEBUG){fprintf(stderr,"target channel is right\n");}
      }
      else{
	targside = 1;
       	if (DEBUG){fprintf(stderr,"target channel is left\n");}
      }
      
      
      //      do{                                             /* start correction trial loop */
      //if (DEBUG & (correction == 0)){fprintf(stderr,"\nstart correction loop\n");}
	  
	left = right = center = 0;        /* zero trial peckcounts */
	resp_sel = resp_acc = resp_rxt = 0;                 /* zero trial variables        */
	++trial_num;++tot_trial_num;
	
	/* Wait for center key press */
	if (DEBUG){fprintf(stderr,"\n\nWaiting for center key press\n");}
	operant_write (box_id, HOUSELT, 1);        /* house light on */
	right=left=center=0;
	do{
	  nanosleep(&rsi, NULL);
	  center = operant_read(box_id, CENTERPECK);   /*get value at center response port*/
	}while (center==0);
	
	sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	
	/* wait for cue */
	if (targside == 1){
	  operant_write(box_id, leftcue, 1);
	  usleep(cue_duration);	  
	  if (DEBUG){fprintf(stderr,"displaying cue light for left speaker\n");}	 
/* 	  operant_write(box_id, leftcue, 0); */
	}
	if (targside == 2){
	  operant_write(box_id, rightcue, 1);  
	  usleep(cue_duration);	  
	  if (DEBUG){fprintf(stderr,"displaying cue light for right speaker\n");}	 
/* 	  operant_write(box_id, rightcue, 0); */
	}
	gettimeofday(&stimon, NULL);
	if (DEBUG){
	  stimon_sec = stimon.tv_sec;
	  stimon_usec = stimon.tv_usec;
	  printf("stim_on sec: %d \t usec: %d\n", stimon_sec, stimon_usec);}	    
	
	/* play the stimulus*/
	if ((played = playstereo2(jr, jl,*rpptr, *lpptr, *rfvptr, *lfvptr, tonelength, targside, tonedb, brnoise, period, &respframe, &respside))!=1){
	  fprintf(stderr, "playstereo2 failed on pcm:%s. Program aborted %s\n", 
		  pcm_name, asctime(localtime (&curr_tt)) );
	  fprintf(datafp, "playstereo2 failed on pcm:%s. Program aborted %s\n", 
		  pcm_name, asctime(localtime (&curr_tt)) );
	  fclose(datafp);
	  fclose(dsumfp);
	  exit(-1);
	}
	
	gettimeofday(&resp_lag, NULL);

	operant_write(box_id, leftcue, 0); /* note, cue stays on throughout trial. may want to change */
	operant_write(box_id, rightcue, 0);

	/* Calculate response time */
	curr_tt = time (NULL); 
	loctime = localtime (&curr_tt);                     /* date and wall clock time of resp */
	timersub (&resp_lag, &stimon, &resp_rt);           /* reaction time */
	if (DEBUG){resp_sec = resp_rt.tv_sec;}      
	if (DEBUG){ resp_usec = resp_rt.tv_usec;}
	if (DEBUG){printf("resp rt sec: %d \t usec: %d\n", resp_sec, resp_usec);} 
	//resp_rxt = (float) resp_rt.tv_sec + ( (float) resp_rt.tv_usec/1000000);  /* format reaction time */
	resp_rxt = respframe/(float)OUTSR;
	if (DEBUG){printf("flag: resp_rxt = %.4f\n", resp_rxt);}
	    
	strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
	strftime (min, 16, "%M", loctime);
	strftime (month, 16, "%m", loctime);
	strftime (day, 16, "%d", loctime);


	/* Consequate responses */

        if (DEBUG){fprintf(stderr,"respframe: %d, respside: %d ",respframe,respside);}
	miss = hit = fa = cr = 0;
	
	if ((targside == 1)&(respframe > 0)){ /* target is on left */
	  if (respside == 2){ /* bird responded to distractor */
	    fa = 1;
	    resp_sel = 2;
	    resp_acc = 0;
	    R[Lind].X++;T[Lind].X++;
	    reinfor =  timeout(punish);
	    if (DEBUG){printf("flag: right response to left target: incorrect, lights out\n");}
	  }  
	  for (i=0; i<jl; i++){ /*process left tones */
	    endplay = leftplay[i]+toneframes;
            if (DEBUG){fprintf(stderr,"leftend%d: %d ",i, endplay);}
	    if (endplay<(respframe-nrespframes)) /* bird missed target */
	      miss++;
	    else if ((endplay>=(respframe-nrespframes)) & (endplay<respframe) & (respside == 2))/* bird missed target */
	      miss++;
	    else if ((endplay>=(respframe-nrespframes)) & (endplay<respframe) & (respside == 1)){ /* bird caught target */
	      hit = 1;
	      resp_sel=1;
	      resp_acc=1;
	      R[Lind].C++; T[Lind].C++;
	      reinfor=feed(reinf, &f);
	      if (reinfor == 1) { ++fed;}
	      if (DEBUG){printf("flag: left response to left target: correct, hopper up\n");}
	    }
	  }
	  for (i=0; i<jr; i++){/* process right tones */
	    endplay = rightplay[i]+toneframes;            
	    if (DEBUG){fprintf(stderr,"rightend%d: %d ",i, endplay);}

	    if (endplay < (respframe-nrespframes)) /* bird correctly rejected distractor */
	      cr++;
     	    else if ((endplay>=(respframe-nrespframes)) & (endplay<respframe) & (respside == 1)) /*bird correctly rejected distractor */
	      cr++; 
	  }
	  if((hit == 0) & (fa ==0) & (respside == 1)){ /* bird responded to correct side, but not within response window */
	    resp_sel = 3;
	    resp_acc = 0;
	    R[Lind].X2++; T[Lind].X2++;
	    reinfor =  timeout(punish); 
	    if (DEBUG){printf("flag: left response to left target outside of response window: trial over, no reinforcement\n");}
	  }
	}
	else if ((targside == 2)&(respframe>0)){ /* target is on right*/
	  if (respside == 1){ /*bird responded to distractor */
	    fa = 1;
	    resp_sel = 2;
	    resp_acc = 0;
	    R[Rind].X++; T[Rind].X++;
	    reinfor =  timeout(punish);
	    if (DEBUG){printf("flag: left response to right target: incorrect, lights out\n");}
	  }  	  
	  for (i=0; i<jr; i++){/* process right tones */
	    endplay = rightplay[i]+toneframes;
            if (DEBUG){fprintf(stderr,"rightend%d: %d ",i, endplay);}
	    if (endplay<(respframe-nrespframes)) /* bird missed target */
	      miss++;
	    else if ((endplay>(respframe-nrespframes)) & (endplay<respframe) & (respside == 1))/* bird missed target */
	      miss++;
	    else if ((endplay>(respframe-nrespframes)) & (endplay<respframe) & (respside == 2)){ /* bird caught target */
	      hit = 1;
	      resp_sel=1;
	      resp_acc=1;
	      R[Rind].C++; T[Rind].C++;
	      reinfor=feed(reinf, &f);
	      if (reinfor == 1) { ++fed;}
	      if (DEBUG){printf("flag: right response to right target: correct, hopper up\n");}
	    }
	  }
	  for (i=0; i<jl; i++){/* process left tones */
	    endplay = leftplay[i]+toneframes;            
            if (DEBUG){fprintf(stderr,"leftend%d: %d ",i, endplay);}

	    if (endplay < (respframe-nrespframes)) /* correctly rejected distractor */
	      cr++;
     	    else if ((endplay>(respframe-nrespframes)) & (endplay<respframe) & (respside == 2)) /*correctly rejected distractor */
	      cr++;
	  }
	  if((hit == 0) & (fa ==0) & (respside == 2)){ /* bird responded to correct side, but not within response window */
	    resp_sel = 3;
	    resp_acc = 0;
	    R[Rind].X2++; T[Rind].X2++;
	    reinfor =  timeout(punish); 
	    if (DEBUG){printf("flag: right response to right target outside of response window: trial over, no reinforcement\n");}
	  }	    
	}
	else if (respframe == 0){
	  resp_sel = 0;
	  resp_acc = 0;
	  R[targside-1].N++; T[targside-1].N++;
	  reinfor =  0; 
	  if (DEBUG){printf("flag: no response, no reinforcement\n");}
	}
	else
	  fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
	
	/* Pause for ITI */
	reinfor_sum = reinfor + reinfor_sum;
	operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	nanosleep(&iti, NULL);                                   /* wait intertrial interval */
	if (DEBUG){printf("flag: ITI passed\n");}
                                        
                   
	/* Write trial data to output file */
	strftime (tod, 256, "%H%M", loctime);
	strftime (date_out, 256, "%m%d", loctime);
	fprintf(datafp, "%d\t%d\t%d\t%d\t%d\t%d\t%.4f\t%d\t%d\t%d\t%.3f\t%d\t%.3f\t%d\t%.3f\t%d\t%.3f\t%d\t%.3f\t%d\t%.3f\t%d\t%s\t%s\n", session_num, trial_num,
		correction, targside, resp_sel, resp_acc, resp_rxt, reinfor,miss,cr,(float)rightplay[0]/(float)OUTSR,rightfval[0],(float)rightplay[1]/(float)OUTSR,rightfval[1],(float)rightplay[2]/(float)OUTSR,rightfval[2],(float)leftplay[0]/(float)OUTSR,leftfval[0],(float)leftplay[1]/(float)OUTSR,leftfval[1],(float)leftplay[2]/(float)OUTSR,leftfval[2], tod, date_out );
	fflush (datafp);
	if (DEBUG){printf("flag: trial data written\n");}

	/*generate some output numbers*/
	if (DEBUG){printf("R[Lind].C = %d, R[Lind].X2 = %d, R[Rind].X = %d\n", R[Lind].C, R[Lind].X2, R[Rind].X);}	
	totleftR = R[Lind].C + R[Lind].X2 + R[Rind].X; totleftT = T[Lind].C + T[Lind].X2 + T[Rind].X;
	totrightR = R[Lind].X + R[Rind].X2 +  R[Rind].C; totrightT = T[Lind].X + T[Rind].X2 + T[Rind].C;
	for (i=0; i<2; i++){
	  R[i].count = R[i].X + R[i].X2 + R[i].C; T[i].count = T[i].X + T[i].X2 + T[i].C;
	}
	R_hit = R_hit + hit;	T_hit = T_hit + hit;
	R_fa = R_fa + fa;	T_fa = T_fa + fa;
	R_miss = R_miss + miss;	T_miss = T_miss + miss;
	R_cr = R_cr + cr;	T_cr = T_cr + cr;
	
	
	    
	/* Update summary data */
	if(freopen(dsumfname,"w",dsumfp)!= NULL){
	  fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name);
	  fprintf (dsumfp, "\tPROPORTION CORRECT RESPONSES (by stimulus, including correction trials)\n");
	  fprintf (dsumfp, "\tTargLoc\tCount\tToday     \t\tCount\tTotals\t(excluding 'no' responses)\n");
	  fprintf (dsumfp, "\tRight\t%d\t%1.4f     \t\t%d\t%1.4f\n",
		   R[Rind].count, (float)R[Rind].C/(float)R[Rind].count, T[Rind].count, (float)T[Rind].C/(float)T[Rind].count );
	  fprintf (dsumfp, "\tLeft\t%d\t%1.4f     \t\t%d\t%1.4f\n",
		   R[Lind].count, (float)R[Lind].C/(float)R[Lind].count, T[Lind].count, (float)T[Lind].C/(float)T[Lind].count );
	  fprintf (dsumfp, "\n\n\tCheck for response bias to either side\n");
	  fprintf (dsumfp, "\tRight \tToday: %d responses,\t %d targets\tTotal: %d responses,\t %d targets\n",
		    totrightR, R[Rind].count,totrightT, T[Rind].count);
	  fprintf (dsumfp, "\tLeft \tToday: %d responses,\t%d targets\tTotal: %d responses,\t %d targets\n",
		    totleftR, R[Lind].count,totleftT, T[Lind].count);
	  fprintf (dsumfp, "\n\n\tCheck out hits and misses\n");
	  fprintf (dsumfp, "\tToday\t\t\tTotal\n");
	  fprintf (dsumfp, "\tHIT:  %i\tCR:   %i\t\tHIT:  %i\tCR:   %i\n",
		    R_hit, R_cr, T_hit, T_cr);
	  fprintf (dsumfp, "\tFA:   %i\tMISS: %i\t\tFA:   %i\tMISS: %i\n",
		    R_fa, R_miss, T_fa, T_miss);
	  fprintf (dsumfp, "\nLast trial run @: %s\n", asctime(loctime) );
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
	//if (resp_acc == 0)
	//correction = 0;
	//else
	// correction = 1;                                              /* set correction trial var */
	//if ((xresp==1)&&(resp_acc == 2))
	//  correction = 0;                                              /* set correction trial var for no-resp */
	    
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
	currtime=(atoi(hour)*60)+atoi(min);
	if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
	    
	//    }while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); /* correction trial loop */
      
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
    for(i = 0; i<2;++i){
      R[i].C = R[i].X = R[i].X2 = R[i].N = R[i].count = R_hit = R_miss = R_fa = R_cr = 0;
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

