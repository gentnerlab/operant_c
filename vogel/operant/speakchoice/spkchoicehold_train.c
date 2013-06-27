/*********************************************************************************
TRAINING PROTOCOL FOR SPKCHOICE ROUTINE - TO BE USED AFTER HOLDBEAK SHAPING - EC 10/07
NOTE: RIGHT SPEAKER MUST BE FIRST CHANNEL IN STEREO CONFIGURATION
**********************************************************************************/
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
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define TIMEOUT_DURATION         2000000       /* default duration of timeout in microseconds */
#define INTER_CUE_INTERVAL       600000000     /* inter cue interval in nanoseconds (needs to be in nanoseconds to agree with RSI */
#define CUE_DURATION             100000        /* cue duration in microseconds */
#define POSTHOLD_DURATION        500000000     /* posthold pause duration in microseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define OUTSR                    44100         /* sample rate for output soundfiles */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define HOPPER_DROP_MS           300           /* time for hopper to fall before checking that it did */


long feed_duration = FEED_DURATION;
long timeout_duration = TIMEOUT_DURATION;
long cue_duration = CUE_DURATION;
int  trial_max = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME; 
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval = SLEEP_TIME;
const char exp_name[] = "SPKCHCHOLDTRN";
int box_id = -1;
int flash = 0;
int xresp = 0;
int nchan = 2;

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timespec phd = { 0, POSTHOLD_DURATION};
struct timespec ici = { 0, INTER_CUE_INTERVAL};
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
int playstereohold_trn(int playside, int graceper,int nkeysamp, int fval, float tone_db, float noise_db, int toneframes, int totalframes, int period)
{
  
  SF_INFO *sfout_info;
  short *obuff, *obufft;
  double padded, LLrms;
  long pad = 0;
  int i=0, j= 0, loops = 0, outsamps, graceframes, startframe, err, init;
  snd_pcm_uframes_t noutframes,outframes, totframesout;
  unsigned short *ptr;
  int outsamprate = OUTSR;
  float foo, rampdenom;
  int nbits, bw, count, center, sampsum;
  signed int total;
  int ramp = 10; /* length of noise ramp in ms */
  float dcoff, rms, db, newrms, scale ,peak_db, peakrms, max = 0.0;
  struct pollfd *ufds;
int keysampvec[nkeysamp],loop,keysamp,keysampx;
  
  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use\n");
    return -1;
  }
  if (snd_pcm_reset(handle)<0){
    fprintf (stderr, "cannot reset audio interface for use\n");
    return -1;
  }

  graceframes = (int)(graceper*outsamprate/1000.0);

  /* determine start of tone */
  startframe = rand()/(RAND_MAX+0.0)*(totalframes-toneframes);

  
  /* pad the file size up to next highest period count*/
  pad = (period * ceil((totalframes)/ (float)period)-(totalframes));
  padded = totalframes+pad;
  outframes = padded;
  outsamps =(int) nchan*padded;
  
  /* allocate memory to buffer */
  obuff = (short *) malloc(sizeof(int)*(outsamps));
  if(DEBUG){fprintf(stderr,"outframes:%d, nchan:%d, pad:%i, totalframes:%i, outsamps:%i, target chan:%i\n", (int)outframes, nchan, (int) pad, totalframes, outsamps, playside);}
  /*get the levels of the target soundfile*/
  nbits = 16;
  bw=6.0206*nbits;  /*we assume 16bit soundfiles for now*/
  
  /* create white noise signal */
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
  
  /* add in tone at specified dB level and target side*/
  obufft = (short *) malloc(sizeof(int)*(toneframes));
  loops = total = 0;
  LLrms = dcoff = rms = db = 0.0;
  for (i=0; i<toneframes; i++){
    obufft[i] = 1000*sin(2*M_PI*fval*i/outsamprate); 
    total += obufft[i];
    loops++;
  }
  if(DEBUG){fprintf(stderr,"tone: ran %d loops out of %d, ", loops, toneframes);}
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
  if(DEBUG){fprintf(stderr,"adding tone to noise at frame %d\n", startframe);}
  if (playside == 1){ /* right tone */
    for (i=startframe; i<(startframe+toneframes); i++)
      obuff[i*nchan] = obuff[i*nchan] + obufft[i-startframe];
  }
  else if (playside == 2){ /*left tone */ 
    for (i=startframe; i<(startframe+toneframes); i++)
      obuff[i*nchan+1] = obuff[i*nchan+1] + obufft[i-startframe];
  }
  free(obufft);
 
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
    fprintf(stderr, "PLAYSTEREOHOLD_TRN: SPL in the combined soundfile exceeds 90 dB, SETTING THE MAX TO 90 dB \n");
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

  if(DEBUG){printf("I'll try to write %d frames\n", (int)outframes);}
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

  noutframes=outframes;
  totframesout=0;
  ptr=obuff;
  loop = 0;
  sampsum = -1;
  if(DEBUG){fprintf(stderr,"nkeysamp: %d\n", nkeysamp);}
  for (i=0;i<nkeysamp;i++){ /* create vector of centerpeck history with memory proportional to length */
    keysampvec[i]=1;
    loop++;
    sampsum++;
  }
  operant_write(box_id, CTRKEYLT, 1);
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
           
    center = operant_read(box_id, CENTERPECK);           
    if(DEBUG){fprintf(stderr," c%d", center);}
    if(outframes < ((int)noutframes-graceframes)){
      keysamp=loop % nkeysamp;
      keysampx=(loop+1) % nkeysamp;
      keysampvec[keysamp]=center;
      sampsum=sampsum+keysampvec[keysamp]-keysampvec[keysampx];
      loop++;
    }
    if (outframes > ((int)noutframes-totalframes)){
      if(DEBUG){fprintf(stderr,"s%d\n", sampsum);}
      if(sampsum == 0){ /* if too many "center=0" values in a row, returns*/
	free(sfout_info);
	free(obuff);
	free(ufds);
	ufds=NULL;
	operant_write(box_id, CTRKEYLT, 0);
	return 0;
      }
    }
    else if (outframes <= ((int)noutframes-totalframes)){
      if(DEBUG){fprintf(stderr,"s%d\n", sampsum);}
      if(sampsum == 0){ /* if too many "center=0" values in a row, returns*/
	free(sfout_info);
	free(obuff);
	free(ufds);
	ufds=NULL;
	operant_write(box_id, CTRKEYLT, 0);
	return 1;
      }
    }
    else{
      operant_write (box_id, CTRKEYLT, 0);
      //if(DEBUG){fprintf(stderr,"out %i", (int)outframes);}
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
  fprintf(stderr, "spkchcholdtrn usage:\n");
  fprintf(stderr, "     [-help] [-B int] [-R int] [-fx] [-t int] [-w int] [-on int] [-off int] [-S int] <filename>\n\n");
  fprintf(stderr, "        -help        = show this help message\n");
  fprintf(stderr, "        -B int       = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "        -t 'x'       = set the timeout duration to 'x' secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -w 'x'     = set the response window duration in seconds. default = 2.0 seconds\n");
  fprintf(stderr, "        -x           = use this flag to enable correction trials\n");
  fprintf(stderr, "        -n           = use this flag to manually set background levels, default = 60.0 dB SPL,\n");
  fprintf(stderr, "        -db          = use this flag to manually set tone levels, default = 75.0 dB SPL\n");
  fprintf(stderr, "        -R float     = set the probability of a right presentation. probability of left presentation = 1-right. default = 0.5\n");
  fprintf(stderr, "        -tdur float  = set the tone duration (default = 50 ms)\n");
  fprintf(stderr, "        -sdur float  = set the stimulus duration (default = 1.0 seconds)\n");
  fprintf(stderr, "        -2acp float  = set the trial probability for 2ac training (default = 0.0)\n");
  fprintf(stderr, "        -rf  int     = set the reinforcement rate. default = 100 percent\n");
  fprintf(stderr, "        -pn  int     = set the punishment rate. default = 100 percent\n");
  fprintf(stderr, "        -samp int    = set number of samples that need to be consecutively blank to count as 'beak removed'. default = 4. \n");
  fprintf(stderr, "        -on int:int      = set hour:min for exp to start eg: '-on 7:35' (default is 7AM, use 99 for sunrise)\n");
  fprintf(stderr, "        -off int:int     = set hour:min for exp to stop eg: '-off 19:01' (default is 7PM, use 99 for sunset)\n");
  fprintf(stderr, "        -S int       = specify the subject ID number (required)\n");
  fprintf(stderr, "        -L int       = specify the training Level (required)\n");
  fprintf(stderr, "                       Level 1: cue on, tone on cued side; with 2ac training\n");
  fprintf(stderr, "                       Level 2: same as Level 1 with cue only and tone only trials (punish)\n");
  fprintf(stderr, "                       Level 3: same as Level 2 with cue on, tone on uncued side (punish)\n");
  fprintf(stderr, "                       Level 4: same as Level 3 with suc\n");
  exit(-1);
}

/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *resp_wind, float *timeout_val, float *stimlength_man, int *nkeysamp_man, float *twoactrnp_man, float *tonelength_man, float *tonedb_man, float *brnoise_man, float *rightprob_man, int *reinf_val, int *punish_val, int *trainlevel)
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
      else if (strncmp(argv[i], "-samp", 5) == 0)
	sscanf(argv[++i], "%i", nkeysamp_man);
      else if (strncmp(argv[i], "-tdur", 5) == 0)
	sscanf(argv[++i], "%f", tonelength_man);
      else if (strncmp(argv[i], "-sdur", 5) == 0)
	sscanf(argv[++i], "%f", stimlength_man);
      else if (strncmp(argv[i], "-2acp", 5) == 0)
	sscanf(argv[++i], "%f", twoactrnp_man);
      else if (strncmp(argv[i], "-db", 3) == 0)
	sscanf(argv[++i], "%f", tonedb_man);	
      else if (strncmp(argv[i], "-R", 2) == 0)
	sscanf(argv[++i], "%f", rightprob_man);
      else if (strncmp(argv[i], "-L", 2) == 0)
	sscanf(argv[++i], "%i", trainlevel);
      else if (strncmp(argv[i], "-rf", 3) == 0)
	sscanf(argv[++i], "%i", reinf_val);
      else if (strncmp(argv[i], "-pn", 3) == 0)
	sscanf(argv[++i], "%i", punish_val);	     
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
  int resp_sel, resp_acc, subjectid, period, tot_trial_num, respside, toneframes,ontframes,totalframes,trialtype,loop=0, twoactrn,
    played, trial_num, session_num, i, correction, punish_val = 0, reinf_val = 0, earlyerrR = 0, earlyerrT = 0, graceper = 200,earlybreakto_dur=1000000,
    dosunrise=0,dosunset=0,starttime,stoptime,currtime, nkeysamp = 4, nkeysamp_man = 0, resp_wind = 0, trialreport;
  float timeout_val=0.0, resp_rxt=0.0, tonelength = 0.050, stimlength = 1.0, tonestart = 0.800, tonedb = 75.0, brnoise = 60.0, rightprob = 0.5;
  float  tonelength_man=0.0, tonestart_man = 0.0, stimlength_man = 0.0, tonedb_man = 0.0, brnoise_man = 0.0, rightprob_man = 0.0;
  float cueonly_p = 0.0, stimonly_p = 0.0, stimc_p = 1.0, stimx_p = 0.0, twoactrn_p = 0.0, twoactrnp_man,  twoactrnrand, stimrand; /* training condition variables set to train level 1 */
  int stimoff_sec, stimoff_usec, resp_sec, resp_usec;  /* debugging variables */
  int punish = 100, reinf = 100, frangemin = 1000, frangemax = 10000, trainlevel = 0, sr = OUTSR, playside, cueside, fval;
  int leftcue = REDLT, rightcue = GREENLT, CKEYLT, cuelight, cueonwait, cueoffwait, cueoffloop;
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval stimoff, resp_window,resp_lag, resp_rt;
  struct tm *loctime;
  Failures f = {0,0,0,0};
  int left = 0, right= 0, center = 0, fed = 0;
  int reinfor_sum = 0, reinfor =0, countsum[4];
  struct resp{
    int C;
    int X;
    int R;
    int L;
    int M;
    int cue;
    int tone;
    int N;
    int count;
  }R[4], T[4];
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
  command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &resp_wind, &timeout_val, &stimlength_man, &nkeysamp_man, &twoactrnp_man, &tonelength_man, &tonedb_man, &brnoise_man, &rightprob_man, &reinf_val, &punish_val, &trainlevel); 
  if(DEBUG){
    fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, xresp=%d, resp_wind=%d timeout_val=%f flash=%d tonelength_man=%f, tonedb_man=%f, brnoise_man=%f, rightprob_man=%f, reinf_val=%d, punish_val=%d, trainlevel=%d\n",
	    box_id, subjectid, starthour, stophour, startmin, stopmin, xresp, resp_wind, timeout_val, flash, tonelength_man, tonedb_man, brnoise_man, rightprob_man, reinf_val, punish_val, trainlevel);
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
    fprintf(stderr, "\tERROR: try 'spkchcholdtrn -help' for available options\n");
    exit(-1);
  }
  if (trainlevel <= 0){
    fprintf(stderr, "\tYou must enter a training level!\n"); 
    fprintf(stderr, "\tERROR: try 'spkchcholdtrn -help' for available options\n");
    exit(-1);
  }
  
  /*set some variables as needed*/
  if (resp_wind > 0){
    respoff.tv_sec = resp_wind;
  }
  fprintf(stderr, "response window duration set to %d secs\n", resp_wind);
  if (tonelength_man > 0.0)
    tonelength = tonelength_man;
  toneframes = (int) (tonelength*sr);
  fprintf(stderr, "tone duration set to %f secs, toneframes set to %d\n", tonelength, toneframes);
  if (stimlength_man > 0.0)
    stimlength = stimlength_man;
  totalframes = (int) (stimlength*sr);
  fprintf(stderr, "stimulus duration set to %f secs, totalframes set to %d\n", stimlength, totalframes);
  if(nkeysamp_man>0){
    nkeysamp = nkeysamp_man;
    fprintf(stderr, "number of consecutive samples that determine break manually set to %d\n", (int) nkeysamp);}
  fprintf(stderr, "tone onset set to %f secs, ontframes set to %d\n", tonestart, ontframes);
  if (tonedb_man > 0.0)
    tonedb = tonedb_man;
  fprintf(stderr, "tone sound level set to %f dB\n", tonedb);
  if (twoactrnp_man > 0.0)
    twoactrn_p = twoactrnp_man;
  fprintf(stderr, "2ac training trial probability manually set to %f\n", twoactrn_p);
  if(timeout_val>0.0)
    timeout_duration = (int) (timeout_val*1000000);
  fprintf(stderr, "timeout duration set to %d microsecs\n", (int) timeout_duration);
  if(reinf_val > 0)
    reinf = reinf_val;	
  fprintf(stderr, "reinforcement rate set to %d percent for correct trials\n", reinf);
  if(punish_val > 0)
    punish = punish_val;
  fprintf(stderr, "punishment rate set to %d percent for incorrect trials\n", punish);
  if(brnoise_man > 0.0)
    brnoise = brnoise_man;
  fprintf(stderr, "background noise set to %f dB\n", brnoise);
 if(rightprob_man > 0.0)
    rightprob = rightprob_man;
  fprintf(stderr, "probability of right target set to %f dB\n", rightprob);
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
  if(xresp){fprintf(stderr, "!!WARNING: Enabling correction trials for all trials, including no response trials !!\n");}


  /*  Open & setup data logging files */
  curr_tt = time (NULL);
  loctime = localtime (&curr_tt);
  strftime (timebuff, 64, "%d%b%y", loctime);
  sprintf(datafname, "%i_spkchcholdtrn.rDAT", subjectid);
  sprintf(dsumfname, "%i.SCHTsummaryDAT", subjectid);
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
  fprintf (datafp, "Sess#\tTrl#\tCrxn\tType\tCue\tTone\tChoice\tR_acc\tRT\tReinf\tTOD\tDate\n");
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
  for(i = 0; i<=3; ++i){
    R[i].C = R[i].X = R[i].R = R[i].L = R[i].M = R[i].cue = R[i].tone = R[i].N = R[i].count = 0;
    T[i].C = T[i].X = T[i].R = T[i].L = T[i].M = T[i].cue = T[i].tone = T[i].N = T[i].count = 0;
  }
  earlyerrR = earlyerrT = 0;
  if (trainlevel == 2){
    cueonly_p = 0.0; stimonly_p = 0.0; stimc_p = .80; stimx_p = 0.20;}
  if (trainlevel == 3){
    cueonly_p = 0.0; stimonly_p = 0.0; stimc_p = .50; stimx_p = 0.50;}
  if (DEBUG){fprintf(stderr,"probabilties for training level %d: cue only:%1.2f  stim only:%1.2f stimC:%1.2f  stimX %1.2f\n",trainlevel, cueonly_p, stimonly_p, stimc_p, stimx_p);}
  
  if (DEBUG){fprintf(stderr,"getting time info tallies\n");} 
  curr_tt = time(NULL);
  loctime = localtime (&curr_tt);
  strftime (hour, 16, "%H", loctime);
  strftime(min, 16, "%M", loctime);
  if (DEBUG){fprintf(stderr,"hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
  currtime=(atoi(hour)*60)+atoi(min);
	
  operant_write (box_id, HOUSELT, 1);        /* make sure houselight is on */
  
  do{                                                                               /* start the main loop */
    while ((currtime>=starttime) && (currtime<stoptime)){                        
      if (DEBUG){printf("minutes since midnight at loop start: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
      srand(time(0));
      /* determine trial type: 2ac train */
      twoactrn = 0;
      twoactrnrand = rand()/(RAND_MAX+0.0);
      if (twoactrnrand <= (twoactrn_p))
	twoactrn = 1;

      /* determine trial type: correct stim, mismatched stim, cue only, stim only */
      playside=fval=cueside=0;
      stimrand = rand()/(RAND_MAX+0.0);
      if (DEBUG){printf("stimrand = %1.2f ", stimrand);}
      if (stimrand <= (stimc_p/2)){
	if (DEBUG){printf("trial type = correct stimulus to right");}
	trialtype = 0;
	playside = 1;
	cueside = 1;
      }
      else if (((stimc_p/2) < stimrand) & (stimrand <= stimc_p)){
	if (DEBUG){printf("trial type = correct stimulus to left");}
	trialtype = 0;
	playside = 2;
	cueside = 2;
      }
      else if ((stimc_p < stimrand) & (stimrand <= (stimc_p+(cueonly_p/2)))){	
	if (DEBUG){printf("trial type = cue only to right");}
	trialtype = 1;
	cueside = 1;
      }
      else if (((stimc_p +(cueonly_p/2)) < stimrand)& (stimrand <= (stimc_p+cueonly_p))){	
	if (DEBUG){printf("trial type = cue only to left");}
	trialtype = 1;
	cueside = 2; 
      }
      else if (((stimc_p + cueonly_p) < stimrand) & (stimrand <= (stimc_p+ cueonly_p +(stimonly_p/2)))) {
	if (DEBUG){printf("trial type = stimulus only to right");}
	trialtype = 2;
	playside=1;
      }
      else if (((stimc_p + cueonly_p + (stimonly_p/2)) < stimrand) & (stimrand <= (stimc_p + cueonly_p + stimonly_p))) {
	if (DEBUG){printf("trial type = stimulus only to left");}
	trialtype= 2;
	playside=2;
      }
      else if (((1-stimx_p) < stimrand) & (stimrand <= (1-(stimx_p/2)))) {
	if (DEBUG){printf("trial type = incorrect stimulus: cue right, tone left");}
	trialtype= 3;
	playside = 2;
	cueside = 1;									       
      } 
      else if ((1-(stimx_p/2)) < stimrand) {
	if (DEBUG){printf("trial type = incorrect stimulus: cue left, tone right");}
	trialtype= 3;
	playside = 1;
	cueside = 2;									       
      }
      if (playside>0){
	fval=(int) (((frangemax-frangemin+0.0)*rand()/(RAND_MAX+0.0))+ frangemin);  /* select stimulus frequency at random */
	if(DEBUG){printf(" %1.3f KHz tone\n",(float)fval/1000.0);}
      }

      if (cueside == 2){
	cuelight = leftcue;
      }
      else if (cueside == 1){
        cuelight = rightcue;  
      }

      do{                                             /* start correction trial loop */
	if (DEBUG & (correction == 0)){fprintf(stderr,"\nstart correction loop\n");}
	
	left = right = center = 0;                          /* zero trial peckcounts */
	resp_sel = resp_acc = resp_rxt = 0;                 /* zero trial variables        */
	++trial_num;++tot_trial_num;
	respside = 0;
	cueonwait = (int) INTER_CUE_INTERVAL/(float)RESPONSE_SAMPLE_INTERVAL;
	cueoffwait = (int) (CUE_DURATION*1000)/(float)RESPONSE_SAMPLE_INTERVAL;

	/* wait for center key press */
	if(twoactrn == 0){
	  if (DEBUG){fprintf(stderr,"\n\nWaiting for center key press\n");}
	  timeradd (&stimoff, &respoff, &resp_window);
	  operant_write (box_id, HOUSELT, 1);        /* house light on */
	  right=left=center=loop=cueoffloop=0;
	  do{
	    if ((loop%cueonwait == 0) && cueside){
	      operant_write(box_id, cuelight, 1);
	      cueoffloop=loop+cueoffwait;
	    }
	    else if ((cueoffloop==loop)&& cueside){
	      operant_write(box_id, cuelight, 0);
	    }
	    loop++;
	    nanosleep(&rsi, NULL);
	    center = operant_read(box_id, CENTERPECK);   /*get value at center response port*/
	  }while (center==0);
	
	  sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	}
	else if (twoactrn)
	  if (DEBUG){fprintf(stderr,"\n\nflashing target lights\n");}


	/*turn on cue - note, may want to remove this later */
	if (cueside){	 
	  operant_write(box_id, cuelight, 1);}
	
        /* play the stimulus*/
	if ((twoactrn == 0)& (playside > 0)){
	  if ((played = playstereohold_trn(playside, graceper,nkeysamp,fval, tonedb, brnoise, toneframes, totalframes, period))==-1){
	    fprintf(stderr, "playstereo2_trn failed on pcm:%s. played = %d, Program aborted %s\n",
		    pcm_name, played, asctime(localtime (&curr_tt)) );
	    fclose(datafp);
	    fclose(dsumfp);
	    exit(-1);
	  }
	}
	else if (twoactrn)
	  played = 2;

	gettimeofday(&stimoff, NULL);
	if (trainlevel > 1){
	  operant_write(box_id, leftcue, 0);
	  operant_write(box_id, rightcue, 0);
	}
	if(played > 0){	
	  center = 1;
	  do {
	    nanosleep(&rsi, NULL);
	    center = operant_read(box_id, CENTERPECK);
	  }while (center == 1);

	  /* pause so no accidental center selection */
	  nanosleep(&phd, NULL);

	  flash = 1;
	  if (trialtype == 3){CKEYLT = CTRKEYLT;}
	  else if (cueside == 1){CKEYLT = RGTKEYLT;}
	  else if (cueside == 2){CKEYLT = LFTKEYLT;}
	
	  else flash = 0;
	  /* get response  */
	  if (DEBUG){printf("flag: waiting for right/left response\n");}
	  timeradd (&stimoff, &respoff, &resp_window);
	  loop=right=left=center=0;
	  do{
	    nanosleep(&rsi, NULL);	               	       
	    right = operant_read(box_id, RIGHTPECK);
	    left = operant_read(box_id, LEFTPECK);
	    center = operant_read(box_id, CENTERPECK);
	    if((left==0) && (right==0) && (center==0) &&  flash && (trainlevel < 3)){
	      ++loop;
	      if(loop%80==0){
		if(loop%160==0){ 
		  operant_write (box_id, CKEYLT, 1);
		}
		else{
		  operant_write (box_id, CKEYLT, 0);
		}
	      }
	    }
	    gettimeofday(&resp_lag, NULL);
	  }while ((right==0) && (left== 0) && (center == 0) && (timercmp(&resp_lag, &resp_window, <)) ); /* & within response interval */
	  if (DEBUG){printf("left = %d, right = %d, center = %d\n", left, right, center);}
	}
	
        
	operant_write(box_id, leftcue, 0);
	operant_write(box_id, rightcue, 0);
        operant_write (box_id, CTRKEYLT, 0);	
        operant_write(box_id, LFTKEYLT, 0);
        operant_write(box_id, RGTKEYLT, 0);

	/* if (DEBUG){ */
	/* 	  stimoff_sec = stimoff.tv_sec; */
	/* 	  stimoff_usec = stimoff.tv_usec; */
	/* 	  printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);}	     */
	
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
	R[trialtype].count ++; T[trialtype].count ++;
	if(played == 0){
	  earlyerrR++; earlyerrT++;
	  resp_sel = 0;
	  resp_acc = 0;
	  R[trialtype].X++; T[trialtype].X++;	
	  timeout_duration = earlybreakto_dur;
	  reinfor =  timeout(punish);
          timeout_duration = (int) (timeout_val*1000000);
	  if (DEBUG){printf("flag: early response to target: incorrect, lights out\n");}
	}	   
	else if((trialtype == 0) & (right != left) & (center == 0)){
	  if ((cueside == 1)&(right == 1)){
	    resp_sel = 1;
	    resp_acc = 1;
	    R[0].C++; R[0].R++; T[0].C++; T[0].R++;
	    reinfor=feed(reinf, &f);
	    if (reinfor == 1) { ++fed;}
	    if (DEBUG){printf("flag: right response to right target: correct, hopper up\n");}
	  }
	  else if ((cueside == 1)&(left == 1)){
	    resp_sel = 2;
	    resp_acc = 0;
	    R[0].X++; R[0].L++; T[0].X++; T[0].L++;
	    reinfor =  timeout(punish);
	    if (DEBUG){printf("flag: left response to right target: incorrect, lights out\n");}
	  }  
	  else if((cueside == 2)&(left == 1)){
	    resp_sel = 2;
	    resp_acc = 1;
	    R[0].C++; R[0].L++; T[0].C++; T[0].L++;
	    reinfor=feed(reinf, &f);
	    if (reinfor == 1) { ++fed;}
	    if (DEBUG){printf("flag: left response to left target: correct, hopper up\n");}
	  }
	  else if ((cueside == 2)&(right == 1)){
	    resp_sel = 2;
	    resp_acc = 0;
	    R[0].X++; R[0].R++; T[0].X++; T[0].R++;
	    reinfor =  timeout(punish);
	    if (DEBUG){printf("flag: right response to left target: incorrect, lights out\n");}
	  }
	}
	else if ((trialtype == 0) & (center != 0)){
	    resp_sel = 0;
	    resp_acc = 0;
	    R[0].X++; R[0].M++; T[0].X++; T[0].M++;
	    reinfor =  timeout(punish);
	    if (DEBUG){printf("flag: center response to left target: incorrect, lights out\n");}  
	}
	else if ((trialtype > 0) & (right != left)){
	  if (DEBUG){fprintf(stderr,"flag: responded on catch trial type %d, lights out\n", trialtype);}
          resp_acc=0;
	  reinfor =  timeout(punish);
	  R[trialtype].X++; T[trialtype].X++;
	  if (right == 1){
	    resp_sel = 1;
	    R[trialtype].R++; T[trialtype].R++;
	  }
	  else if (left == 1){
	    resp_sel = 2;
	    R[trialtype].L++; T[trialtype].L++;
	  }
	  if (resp_sel==cueside){
	    R[trialtype].cue++; T[trialtype].cue++;
	  }
	  if (resp_sel==playside){
	    R[trialtype].tone++; T[trialtype].tone ++;
	  }	  
	}
	else if ((trialtype > 0) & (center != 0)){
	    resp_sel = 0;
	    resp_acc=1;
	    R[trialtype].C++; R[trialtype].M++; T[trialtype].C++; T[trialtype].M++;
	    reinfor=feed(reinf, &f);
	    if (reinfor == 1) { ++fed;}
	}
	else if ((center==0) & (right == 0)& (left == 0 )){
	  resp_sel = -1;
	  resp_acc = 2;
	  R[trialtype].N++; T[trialtype].N++;
	  reinfor =  0; 
	  if (DEBUG){printf("flag: no response, no reinforcement\n");}
	}
	else
          fprintf(stderr,"WARNING: last trial did not fall into an analysis class: trialtype = %d, respside = %d, cueside %d, playside %d\n", trialtype,respside,cueside,playside);
	
	trialreport = trialtype;
	if (twoactrn)
	  trialreport = -1;

	/* Pause for ITI */
	reinfor_sum = reinfor + reinfor_sum;
	operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	nanosleep(&iti, NULL);                                   /* wait intertrial interval */
	if (DEBUG){printf("flag: ITI passed\n");}
	
	
	/* Write trial data to output file */
	strftime (tod, 256, "%H%M", loctime);
	strftime (date_out, 256, "%m%d", loctime);
	fprintf(datafp, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num,
		correction, trialreport, cueside, playside, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
	fflush (datafp);
	if (DEBUG){printf("flag: trial data written\n");}
	
	/*generate some output numbers*/
	for (i=0; i<=3; i++){
	  countsum[i] = T[i].X + T[i].C + T[i].N ;
	  if (countsum[i] != T[i].count)
	    fprintf(stderr,"WARNING: potential problem with trial type %d total tally: sum of responses = %d, count = %d", i, countsum[i], T[i].count);
	}
	
	/* Update summary data */
	if(freopen(dsumfname,"w",dsumfp)!= NULL){
	  fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name);
	  fprintf (dsumfp, "\tPROPORTION CORRECT RESPONSES (by stimulus, including correction trials)\n");
	  fprintf (dsumfp, "\tCount\tToday     \t\tCount\tTotals\t(excluding 'no' responses)\n");
	  fprintf (dsumfp, "\t%d\t%1.3f     \t\t%d\t%1.4f\n",
		   R[0].count, (float)R[0].C/(float)R[0].count, T[0].count, (float)T[0].C/(float)T[0].count );
	  fprintf (dsumfp, "\n\n\tCheck for response bias to keys.trialtype 0 only\n");
	  fprintf (dsumfp, "\tResponses Today: R %d  L %d  C %d \tTotal: R %d  L %d  C %d\n",
		   R[0].R, R[0].L, R[0].M, T[0].R,T[0].L,T[0].M);
	  fprintf (dsumfp, "\n\n\tExamine Behavior on Catch Trials\n");
	  fprintf (dsumfp, "\tType\tToday\tX\tw/cue\tw/tone\t\tTotal\tX\tw/cue\tw/tone\n");
	  for (i=0; i<=3; i++){
	    fprintf (dsumfp, "\t%d\t%d\t%1.2f\t%1.2f\t%1.2f\t\t%d\t%1.2f\t%1.2f\t%1.2f\n",
		     i, R[i].count, (float)R[i].X/(float)R[i].count,(float)R[i].cue/(float)R[i].count,(float)R[i].tone/(float)R[i].count, T[i].count, (float)T[i].X/(float)T[i].count,(float)T[i].cue/(float)T[i].count,(float)T[i].tone/(float)T[i].count);
	  }
	  fprintf (dsumfp, "\n\n\tCheck for early errors\n");
	  fprintf (dsumfp, "\tToday: %d  \tTotal: %d \n",
		   earlyerrR, earlyerrT);
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
	if (xresp & (resp_acc == 0))
	  correction = 0;
	else
	  correction = 1;                                              /* set correction trial var */
	/*	if (xresp &&(resp_acc == 2)) */
	/*  correction = 0; */                                               /* set correction trial var for no-resp */
	
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
	currtime=(atoi(hour)*60)+atoi(min);
	if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
	
	}while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); /* correction trial loop */
	
      }                                                 /* trial loop */
    
    /*You'll end up here if its night time or if you skip a day
      The latter only happens if the night window elapses between the time a stimulus is chosen
      and the time its actually played--i.e. if the subject doesn't initiates a cued trial during
      the night window, which is rare*/
    
    
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
    for(i = 0; i<=3;++i){
      R[i].C = R[i].X = R[i].R = R[i].L = R[i].M =  R[i].cue = R[i].tone = R[i].N = R[i].count = 0;
    }
    earlyerrR = 0;
    
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
  
