/*****************************************************************************
** holdbeak.c - code for training bird to hold beak in center key  ec 10/07
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>

#include "/usr/local/src/operantio/operantio.c"

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
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


/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             2             /* seconds from simulus end until NORESP is registered  (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXLENGTHS               512           /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 256           /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define TIMEOUT_DURATION         10000000      /* duration of timeout in microseconds */
#define FEED_DURATION            4000000       /* duration of feeder access in microseconds */
#define DACSAMPLERATE            20000         /* stimulus sampling rate */  
#define OUTSR                    44100         /* sample rate for output soundfiles */
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
const char exp_name[] = "HOLDBEAK";
int box_id = -1;
int flash = 0;
int xresp = 0;
int mirror = 0;
int nchan = 1;
int resp_sel, resp_acc;

typedef struct {
  int hopper_failures;
  int hopper_wont_go_down_failures;
  int hopper_already_up_failures;
  int response_failures;
} Failures;

snd_pcm_t *handle;

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
  fprintf(stdout,"closed soundserver: term signal caught: exiting\n");
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



/* ------------ PLAYNOISE_MONC: PLAYS NOISE WHILE MONITORING CENTER KEY INPUT  ---------*/
int playnoise_monC(float stimlength, float stim_db, int graceper,int period, int nkeysamp, float maxholdlength)
{
  
  SF_INFO *sfout_info;
  short *obuff;
  double padded, LLrms;
  long pad = 0;
  int i=0, j= 0, loops = 0, stimframes, holdframes, outsamps,err, init;
  snd_pcm_uframes_t noutframes,outframes, totframesout;
  unsigned short *ptr;
  int outsamprate = OUTSR;
  float foo, rampdenom;
  int nbits, bw, count, center, sampsum, graceframes;
  signed int total;
  int ramp = 20; /* length of noise ramp in ms */
  float dcoff, rms, db, newrms, scale ,peak_db, peakrms, max = 0.0;
  struct pollfd *ufds;
  int keysampvec[nkeysamp],loop,keysamp,keysampx;
  
  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use\n");
    return -1;
  }
  graceframes = (int)(graceper*outsamprate/1000.0);
  
  /* determine length inframes*/
  stimframes = (int) (stimlength*outsamprate);
  if (maxholdlength >= stimlength)
    holdframes = stimframes;
  else 
    holdframes = (int)(maxholdlength*outsamprate);
 
 /* pad the file size up to next highest period count*/
  pad = (period * ceil((stimframes)/ (float)period)-(stimframes));
  padded = stimframes+pad;
  outframes = padded;
  outsamps =(int) nchan*padded;
  
  /* allocate memory to buffer */
  obuff = (short *) malloc(sizeof(int)*(outsamps));
  if(DEBUG){fprintf(stderr,"outframes:%d, nchan:%d, pad:%i, stimframes:%i, holdframes:%i\n", (int)outframes, nchan, (int) pad,(int) stimframes,(int) holdframes);}
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
  foo=(stim_db-bw)/20.0;
  newrms = pow(10,foo);
  scale = newrms/rms;
  if(DEBUG){fprintf(stderr,"changing to: %1.2f dB SPL with scale %1.2f\n", stim_db, scale);}
  for (j=0; j<outsamps; j++)
    obuff[j] = scale * obuff[j];
  
  
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
    fprintf(stderr, "PLAYNOISE_MONC: SPL in the combined soundfile exceeds 90 dB, SETTING THE MAX TO 90 dB \n");
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
  center = 1;
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
      } 
      else {
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
/*   if(DEBUG){fprintf(stderr,"centerpeck output during play:");}  */
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
    if (outframes > ((int)noutframes-holdframes)){
      if(DEBUG){fprintf(stderr,"s%d\n", sampsum);}
      if(sampsum == 0){ /* if too many "center=0" values in a row, returns*/
	free(sfout_info);
	free(obuff);
	free(ufds);
	ufds=NULL;
	operant_write(box_id, CTRKEYLT, 0);
	return -1;
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
      if(DEBUG){printf("\noutframes is zero so I'll break\n");}
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

  
  if(DEBUG){
    printf("frames not played: %d \t", (int)outframes);
    printf("frames played: %d \n", (int) totframesout);
  }
  
  /*free up resources*/
  free(sfout_info);
  free(obuff);
  free(ufds);
  ufds=NULL;
  return 1;
  if(DEBUG){fprintf(stderr,"returned 1");}
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
  fprintf(stderr, "        -S int         = specify the subject ID number (required)\n");
  fprintf(stderr, "        -t float       = set the timeout duration to float secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -r int         = set the reinforcement rate. default = 100\n");
  fprintf(stderr, "        -on int:int    = set hour:min for exp to start eg: '-on 7:30' (default is 7:00 AM)\n");
  fprintf(stderr, "        -off int:int   = set hour for exp to stop eg: '-off 19:45' (default is 7:00 PM)\n");
  fprintf(stderr, "                         To use daily sunset or sunrise times set 'on' or 'off' to '99'\n");
  fprintf(stderr, "        -max float     = set maximum length of hold duration. default = 2.0 seconds\n");
  fprintf(stderr, "        -min int       = set minimum number of trials that need to be run before increasing stimlength. default = 100.\n");
  fprintf(stderr, "        -samp int      = set number of samples that need to be consecutively blank to count as 'beak removed'. default = 4. \n");
  fprintf(stderr, "        -len float     = set initial length of stimulus. default = 0.050 seconds \n");  
  fprintf(stderr, "        -db float      = set dB SPL of stimulus. default = 65.0 dB \n");
  fprintf(stderr, "        -step float    = set size of step for stimulus length increase in seconds. default = 0.050 seconds \n");
  exit(-1);
}

/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, float *timeout_val, int *reinf_man, int *mintrials_man, float *lengthmax_man, float *maxholdlength_man, int *nkeysamp_man, float *stimlength_man, float *stim_db_man, float *lengthstep_man)
{
  int i=0;
  for (i = 1; i < argc; i++){
    if (*argv[i] == '-'){
      if (strncmp(argv[i], "-B", 2) == 0)
	sscanf(argv[++i], "%i", box_id);
      else if (strncmp(argv[i], "-S", 2) == 0)
	sscanf(argv[++i], "%i", subjectid);
      else if (strncmp(argv[i], "-t", 2) == 0)
	sscanf(argv[++i], "%f", timeout_val);
      else if (strncmp(argv[i], "-r", 2) == 0)
	sscanf(argv[++i], "%i", reinf_man);
      else if (strncmp(argv[i], "-min", 4) == 0)
	sscanf(argv[++i], "%i", mintrials_man);
      else if (strncmp(argv[i], "-maxl", 5) == 0)
	sscanf(argv[++i], "%f", lengthmax_man);
      else if (strncmp(argv[i], "-maxh", 5) == 0)
	sscanf(argv[++i], "%f", maxholdlength_man);
      else if (strncmp(argv[i], "-samp", 5) == 0)
	sscanf(argv[++i], "%i", nkeysamp_man);
      else if (strncmp(argv[i], "-len", 4) == 0)
	sscanf(argv[++i], "%f", stimlength_man);
      else if (strncmp(argv[i], "-db", 3) == 0)
	sscanf(argv[++i], "%f", stim_db_man);
      else if (strncmp(argv[i], "-step", 5) == 0)
	sscanf(argv[++i], "%f", lengthstep_man);
      else if (strncmp(argv[i], "-on", 3) == 0){
	sscanf(argv[++i], "%i:%i", starthour,startmin);}
      else if (strncmp(argv[i], "-off", 4) == 0){
	sscanf(argv[++i], "%i:%i", stophour, stopmin);}
      else if (strncmp(argv[i], "-help", 5) == 0)
	do_usage();
      else{
	fprintf(stderr, "Unknown option: %s\t", argv[i]);
	fprintf(stderr, "Try 'holdbeak -help' for help\n");
      }
    }
  }
  return 1;
}

/********************************************
 ** MAIN
 ********************************************/
int main(int argc, char *argv[])
{
  FILE *datafp = NULL, *dsumfp = NULL;
  char *stimfname = NULL;
  char *stimfroot;
  const char delimiters[] = " .,;:!-";
  char datafname[128], hour [16], min[16], month[16], day[16], year[16], pcm_name[128], 
    dsumfname[128], stimftemp[128],timebuff[64], tod[256], date_out[256],temphour[16],tempmin[16], buffer[30];
  int dinfd=0, doutfd=0, stim_reinf = 100,reinf_man = 0, 
    subjectid, loop, period, played, resp_wind,correction,stim_punish = 100, mintrials = 80, mintrials_man = 0, graceper = 200,
    trial_num, session_num, i, nkeysamp = 4, nkeysamp_man = 0, sessionTrials, dosunrise=0,dosunset=0,starttime,stoptime,currtime,lengthind=0;
  float resp_rxt=0.0, timeout_val=0.0; 
  float stimlength = 0.100, lengthmax = 3.0, stim_db = 65.0, criterion = 0.50, lengthstep = 0.050, maxholdlength = 2.0, lengthvec[MAXLENGTHS];
  float stimlength_man = 0.0, stim_db_man = 0.0, lengthstep_man = 0.0, lengthmax_man = 0.0, maxholdlength_man = 0.0, ratio;
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval stimoff, resp_lag, resp_rt;
  struct tm *loctime;
  int center = 0, fed = 0;
  Failures f = {0,0,0,0};
  int reinfor_sum = 0, reinfor = 0, checkperf[mintrials], trialind, trialindx, trialsum;
  int resp_sec, resp_usec;  /* debugging variables */
  struct response {
    int count;
    int C;
    int X;
    float ratio;
  } Rses[MAXLENGTHS], Rtot[MAXLENGTHS];


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
  command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &timeout_val, &reinf_man, &mintrials_man, &lengthmax_man, &maxholdlength_man, &nkeysamp_man, &stimlength_man, &stim_db_man, &lengthstep_man);	
  sprintf(pcm_name, "dac%i", (int) box_id);
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
    fprintf(stderr, "\tERROR: try 'holdbeak -help' for available options\n");
    snd_pcm_close(handle);
    exit(-1);
  }

  /*set some variables as needed*/
  if (resp_wind>0)
    respoff.tv_sec = resp_wind;
  fprintf(stderr, "response window duration set to %d secs\n", (int) respoff.tv_sec);
  if(timeout_val>0.0){
    timeout_duration = (int) (timeout_val*1000000);
    fprintf(stderr, "timeout duration manually set to %d microsecs\n", (int) timeout_duration);}
  if(reinf_man>0){
    stim_reinf = reinf_man;
    fprintf(stderr, "reinforcement rate manually set to %d percent\n", stim_reinf);}
  if(mintrials_man>0){
    mintrials = mintrials_man;
    fprintf(stderr, "minimum number of trials before stimulus duration increase manually set to %d\n", (int) mintrials);}
  if(lengthmax_man>0){
    lengthmax = lengthmax_man;
    fprintf(stderr, "maximum stimulus duration manually set to %1.2f seconds\n", lengthmax);}
  if(maxholdlength_man>0){
    maxholdlength = maxholdlength_man;
    fprintf(stderr, "maximum holding duration manually set to %1.2f\n", maxholdlength);}
  if(nkeysamp_man>0){
    nkeysamp = nkeysamp_man;
    fprintf(stderr, "number of consecutive samples that determine break manually set to %d\n", (int) nkeysamp);}
  if(stimlength_man>0.0){
    stimlength = stimlength_man;
    fprintf(stderr, "stimulus duration manually set to %1.2f seconds\n", stimlength);}
  if(stim_db_man>0.0){
    stim_db = stim_db_man;
    fprintf(stderr, "stimulus volume manually set to %1.2f dB SPL\n", stim_db);}
  if(lengthstep_man>0.0){
    lengthstep = lengthstep_man;
    fprintf(stderr, "size of stimulus duration step manually set to %1.2f seconds\n", lengthstep);}
	
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

  /*  Open & setup data logging files */
  curr_tt = time (NULL);
  loctime = localtime (&curr_tt);
  strftime (timebuff, 64, "%d%b%y", loctime);
  sprintf (stimftemp, "%s", stimfname);
  stimfroot = strtok (stimftemp, delimiters); 
  sprintf(datafname, "%i_holdbeak.rDAT", subjectid);
  sprintf(dsumfname, "%i.HBsummaryDAT", subjectid);
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
  fprintf (datafp, "Sess#\tTrl#\tStimlen\tRspSL\tRspAC\tRspRT\tReinf\tTOD\tDate\n");


  /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
  ********************************************/
  session_num = 1;
  trial_num = 0;
  correction = 1;

  for(i = 0; i<MAXLENGTHS;++i){   /*zero out the response tallies */
    Rses[i].C = Rses[i].X = Rses[i].count = Rtot[i].C = Rtot[i].X = Rtot[i].count = 0;
    Rses[i].ratio = Rtot[i].ratio = 0.0;
  }
  for (i=0; i<mintrials; i++)
    checkperf[i]=0;
  trialsum = 0;
	
  curr_tt = time(NULL);
  loctime = localtime (&curr_tt);
  strftime (hour, 16, "%H", loctime);
  strftime(min, 16, "%M", loctime);
  if (DEBUG){printf("hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
  currtime=(atoi(hour)*60)+atoi(min);

  operant_write (box_id, HOUSELT, 1);        /* house light on */

  do{                                                                              /* start the block loop */
    while ((currtime>=starttime) && (currtime<stoptime)){                          /* start main trial loop */
      if (DEBUG){printf("minutes since midnight at loop start: %d\t starttime: %d\tstoptime: %d\n",
			currtime,starttime,stoptime);}
      
      do{                                             /* start correction trial loop */
	resp_sel = resp_acc = resp_rxt = 0;        /* zero trial variables        */
	++trial_num;
	curr_tt = time(NULL);
	lengthvec[lengthind] = stimlength;

	/* Wait for center key press */
	if (DEBUG){printf("flag: waiting for center key press\n");}
	operant_write (box_id, HOUSELT, 1);        /* house light on */
	center = 0;
	do{                                         
	  nanosleep(&rsi, NULL);	               	       
	  center = operant_read(box_id, CENTERPECK);   /*get value at center peck position*/		 	       
	  
	}while (center==0);  
	
	sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	
	/* Play noise while beak is in key */
	if ((played = playnoise_monC(stimlength, stim_db, graceper, period, nkeysamp, maxholdlength))==-1){
	  /*didn't keep beak in center key long enough */
	  resp_sel = 0;
	  resp_acc = 0;
	  if (DEBUG){ printf("broke beam too early\n");}
	  ++Rses[lengthind].X; ++Rtot[lengthind].X;
	  reinfor =  timeout(stim_punish); 
	}
	else if (played!=1){
	  fprintf(stderr, "playstereo2 failed on pcm:%s. Program aborted %s\n", 
		  pcm_name, asctime(localtime (&curr_tt)) );
	  fprintf(datafp, "playstereo2 failed on pcm:%s. Program aborted %s\n", 
		  pcm_name, asctime(localtime (&curr_tt)) );
	  fclose(datafp);
	  fclose(dsumfp);
	  exit(-1);
	}
	else if (played == 1){	  
	  operant_write (box_id, CTRKEYLT, 0); 
	  loop = 0; center = 1;
	  do{   /* flash center light to cue safe beak removal*/
	    nanosleep(&rsi, NULL);
	    center = operant_read(box_id, CENTERPECK);
	    ++loop;
	    if(loop%20==0){
	      if(loop%40==0){ 
		operant_write (box_id, CTRKEYLT, 1);
	      }
	      else{
		operant_write (box_id, CTRKEYLT, 0);
	      } 
	    }
	  }while (center==1);
	  operant_write (box_id, CTRKEYLT, 0);    /*make sure the key lights are off after resp interval*/
	  resp_sel = 1;
	  resp_acc = 1;
	  reinfor = feed(stim_reinf, &f);	    
	  if (reinfor == 1) { ++fed;}
	  ++Rses[lengthind].C; ++Rtot[lengthind].C;
	}
	
	/* Calculate response time */
	curr_tt = time (NULL); 
	loctime = localtime (&curr_tt);                     /* date and wall clock time of resp */
	timersub (&resp_lag, &stimoff, &resp_rt);           /* reaction time */
	//if (DEBUG){
	//resp_sec = resp_rt.tv_sec;      
	//resp_usec = resp_rt.tv_usec;
	//printf("resp rt sec: %d \t usec: %d\n", resp_sec, resp_usec);} 
	resp_rxt = (float) resp_rt.tv_sec + ( (float) resp_rt.tv_usec/1000000);  /* format reaction time */
	if (DEBUG){printf("flag: resp_rxt = %.4f\n", resp_rxt);}
	
	strftime (hour, 16, "%H", loctime);                    /* format wall clock times for trial end*/
	strftime (min, 16, "%M", loctime);
	strftime (month, 16, "%m", loctime);
	strftime (day, 16, "%d", loctime);
	++Rses[lengthind].count; ++Rtot[lengthind].count; 
	
	
	/* Pause for ITI */
	reinfor_sum = reinfor + reinfor_sum;
	operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	nanosleep(&iti, NULL);                     /* wait intertrial interval */
	if (DEBUG){printf("flag: ITI passed\n");}
	
	/* Write trial data to output file */
	strftime (tod, 256, "%H%M", loctime);
	strftime (date_out, 256, "%m%d", loctime);
	fprintf(datafp, "%d\t%d\t%1.3f\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, stimlength,
		resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
	fflush (datafp);
	if (DEBUG){printf("flag: trail data written\n");}

	trialind=Rses[lengthind].count % mintrials;
	trialindx=(Rses[lengthind].count+1) % mintrials;
	checkperf[trialind]=resp_acc;
	trialsum=trialsum+checkperf[trialind]-checkperf[trialindx];

	/*generate some output numbers*/
	ratio = (float)(trialsum) /(float)(mintrials);
	
	sessionTrials=0;
	
	if (DEBUG){printf("flag: ouput numbers done\n");}
	/* Update summary data */
	
	if(freopen(dsumfname,"w",dsumfp)!= NULL){
	  fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
	  fprintf (dsumfp, "\tRESPONSE RATIOS (by stimulus length)\n");
	  fprintf (dsumfp, "\tCriterion set at %1.2f\n", criterion);
	  fprintf (dsumfp, "\tStimlength  \tSession     \tTotals \tPerformance (last %d trials)\n", mintrials);
	  for (i = 0; i<=lengthind;++i){
	    fprintf (dsumfp, "\t%1.3f     \t%d\t%d\t%1.4f\n", 
		     lengthvec[i], Rses[i].count, Rtot[i].count, ratio);
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
	
	/* If performance has reached criterion level, enough trials have passed, and stimulus length is not at max, bump up to longer stimulus */
	if (ratio > criterion && Rtot[lengthind].count >= mintrials && stimlength < lengthmax){
	  lengthind++;
	  stimlength = stimlength + lengthstep;
	  for (i=0; i<mintrials; i++)
	    checkperf[i]=0;
	  trialsum = 0;
	}
	
	/* End of trial chores */
	sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                   /* unblock termination signals */ 
	correction = 1; /* make sure you don't invoke a correction trial by accident */
	
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
	currtime=(atoi(hour)*60)+atoi(min);
	if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
	
      }while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); /* correction trial loop */
    }                                                        /* main trial loop*/
    
    /* Loop with lights out during the night */
    /*   if (DEBUG){printf("minutes since midnight: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);} */
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
    for (i = 0; i<lengthind;++i){
      Rses[i].C = 0;
      Rses[i].X = 0;
      Rses[i].ratio = 0.0;
      Rses[i].count = 0;
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

  }while (1); /* if (1) loop forever */
	
  /*  Cleanup */
  fclose(datafp);
  fclose(dsumfp);
  snd_pcm_close(handle);
  return 0;
}                         

