
#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <math.h>
#include <signal.h>
#include <sys/time.h>

#define ALSA_PCM_NEW_HW_PARAMS_API

#include <alsa/asoundlib.h>
#include "/usr/local/src/operantio/operantio_6509.c"
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
#define MAXCLASS                 256            /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define TIMEOUT_DURATION         10000000       /* duration of timeout in microseconds */
#define DEF_REF                  5             /* default reinforcement for corr. resp. set to 100% */
#define STIMPATH       "/usr/local/stimuli/"
#define HOPPER_DROP_MS           300           /*time for hopper to fall before checking that it did */
#define MAXFILESIZE 5292000    /* max samples allowed in soundfile */
#define MAXPECKS		256 /* Maximum number of pecks recorded. This should be enough */

long feed_duration = FEED_DURATION;
int  trial_max = MAX_NO_OF_TRIALS;
long timeout_duration = TIMEOUT_DURATION;
int  starthour = EXP_START_TIME; 
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval = SLEEP_TIME;
int  reinf_val = DEF_REF;
const char exp_name[] = "CHANGEPECK";
int box_id = -1;
int pecknum = 0;
short *soundvecr = NULL, *soundveca = NULL; /* Trying global vars for soundvecs */
struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};

struct PECK{
  int left;
  int center;
  int right;
} ;

typedef struct {
  int hopper_failures;
  int hopper_wont_go_down_failures;
  int hopper_already_up_failures;
  int response_failures;
} Failures; 

struct Savedpecks{
  int left;
  int center;
  int right;
  struct timeval timestamp;
};

snd_pcm_t *handle;
unsigned int channels = 1;                      /* count of channels */
unsigned int rate = 44100;                      /* stream rate */
snd_pcm_format_t format = SND_PCM_FORMAT_S16;   /* sample format */
unsigned int buffer_time = 500000;              /* ring buffer length in us */
unsigned int period_time = 100000;              /* period time in us */
int resample = 1;                               /* enable alsa-lib resampling */

snd_pcm_uframes_t buffer_size;
snd_pcm_uframes_t period_size;
snd_output_t *output = NULL;

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


/*********************************************************************************
 * PCM SETUP 
 ********************************************************************************/
int do_setup(char *pcm_name)
{
  snd_pcm_hw_params_t *params;
  snd_pcm_access_t access = SND_PCM_ACCESS_RW_INTERLEAVED;
  snd_pcm_sw_params_t *swparams;
  unsigned int rrate;
  int err, dir;
  snd_pcm_uframes_t persize;
  snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;

  snd_pcm_hw_params_alloca(&params);
  snd_pcm_sw_params_alloca(&swparams);

  /*open the pcm device*/
  if ((err = snd_pcm_open(&handle, pcm_name, stream, 0)) < 0) {
    printf("Playback open error: %s\n", snd_strerror(err));
    return 0;
  }
  if(DEBUG){printf("opened %s\n", pcm_name);}
  /* choose all parameters */
  err = snd_pcm_hw_params_any(handle, params);
  if (err < 0) {
    printf("Broken configuration for playback: no configurations available: %s\n", snd_strerror(err));
    return err;
  }
  /* set the interleaved read/write format */
  err = snd_pcm_hw_params_set_access(handle, params, access);
  if (err < 0) {
    printf("Access type not available for playback: %s\n", snd_strerror(err));
    return err;
  }
  /* set the sample format */
  err = snd_pcm_hw_params_set_format(handle, params, format);
  if (err < 0) {
    printf("Sample format not available for playback: %s\n", snd_strerror(err));
    return err;
  }
  /* set the count of channels */
  err = snd_pcm_hw_params_set_channels(handle, params, channels);
  if (err < 0) {
    printf("Channels count (%i) not available for playbacks: %s\n", channels, snd_strerror(err));
    return err;
  }
  /* set the stream rate */
  rrate = rate;
  err = snd_pcm_hw_params_set_rate_near(handle, params, &rrate, 0);
  if (err < 0) {
    printf("Rate %iHz not available for playback: %s\n", rate, snd_strerror(err));
    return err;
  }
  if (rrate != rate) {
    printf("Rate doesn't match (requested %iHz, get %iHz)\n", rate, err);
    return -EINVAL;
  }
  /* set the buffer time */
  err = snd_pcm_hw_params_set_buffer_time_near(handle, params, &buffer_time, &dir);
  if (err < 0) {
    printf("Unable to set buffer time %i for playback: %s\n", buffer_time, snd_strerror(err));
    return err;
  }
  err = snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
  if (err < 0) {
    printf("Unable to get buffer size for playback: %s\n", snd_strerror(err));
    return err;
  }
  if(DEBUG){printf("buffer size is %d\n", (int)buffer_size);}  
  /* set the period time */
  err = snd_pcm_hw_params_set_period_time_near(handle, params, &period_time, &dir);
  if (err < 0) {
    printf("Unable to set period time %i for playback: %s\n", period_time, snd_strerror(err));
    return err;
  }
  err = snd_pcm_hw_params_get_period_size(params, &period_size, &dir);
  if (err < 0) {
    printf("Unable to get period size for playback: %s\n", snd_strerror(err));
    return err;
  }
  if(DEBUG){printf("period size is %d\n", (int)period_size);}  
  /* write the parameters to device */
  err = snd_pcm_hw_params(handle, params);
  if (err < 0) {
    printf("Unable to set hw params for playback: %s\n", snd_strerror(err));
    return err;
  }

  /* --------- set up software parameters ---------*/ 
  /* get the current swparams */
  err = snd_pcm_sw_params_current(handle, swparams);
  if (err < 0) {
    printf("Unable to determine current swparams for playback: %s\n", snd_strerror(err));
    return err;
  }
  /* start the transfer when the buffer is almost full: */
  /* (buffer_size / avail_min) * avail_min */
  err = snd_pcm_sw_params_set_start_threshold(handle, swparams, (buffer_size / period_size) * period_size);
  if (err < 0) {
    printf("Unable to set start threshold mode for playback: %s\n", snd_strerror(err));
    return err;
  }
  /* allow the transfer when at least period_size samples can be processed */
  err = snd_pcm_sw_params_set_avail_min(handle, swparams, period_size);
  if (err < 0) {
    printf("Unable to set avail min for playback: %s\n", snd_strerror(err));
    return err;
  }
  /* align all transfers to 1 sample */
  err = snd_pcm_sw_params_set_xfer_align(handle, swparams, 1);
  if (err < 0) {
    printf("Unable to set transfer align for playback: %s\n", snd_strerror(err));
    return err;
  }
  /* write the parameters to the playback device */
  err = snd_pcm_sw_params(handle, swparams);
  if (err < 0) {
    printf("Unable to set sw params for playback: %s\n", snd_strerror(err));
    return err;
  }

  snd_pcm_hw_params_get_period_size (params, &persize, &dir);
  if(DEBUG){printf("done with setup\n");}
  return (double) persize;
}

/*****************************************************************************
 *   PLAYBACK UTILITIES:Underrun and suspend recovery
 *                      wait_for_poll
 *****************************************************************************/
static int xrun_recovery(snd_pcm_t *handle, int err)
{
  if (err == -EPIPE) {    /* under-run */
    err = snd_pcm_prepare(handle);
    if (err < 0)
      printf("Can't recovery from underrun, prepare failed: %s\n", snd_strerror(err));
    return 0;
  } else if (err == -ESTRPIPE) {
    while ((err = snd_pcm_resume(handle)) == -EAGAIN)
      sleep(1);       /* wait until the suspend flag is released */
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

/**************************************************************************************
 * OUTPUT SOUND
 * Play buffer directly and count pecks
 **************************************************************************************/
int playvec_count(short *obuff, int frames, int box_id ,struct Savedpecks *GRABPECKS, struct PECK *peckcounts)
{
  short *ptr;
  snd_pcm_uframes_t outframes, totframesout;
  int  err, count, init;
  struct pollfd *ufds;
  struct PECK old, new;

  old.left=0;
  old.right=0;
  old.center=0;
if(DEBUG){printf("Frames input were: %d\n", frames);}

  /* memory for SF_INFO structures */
  outframes = (snd_pcm_uframes_t) frames;
  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use\n");
    return -1;
  }

  snd_pcm_nonblock(handle,1); /*make sure you set playback to non-blocking*/
	if(DEBUG){printf(" Finished setting up PCM for playback\n");}


  /*playback with polling*/
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

  ptr=obuff; 

  totframesout=0;
  err = 0;

  while (outframes > 0) {
	 err = snd_pcm_writei(handle,ptr, outframes); 


    if (err < 0) {
      if (xrun_recovery(handle, err) < 0) {
	printf("Write error: %s\n", snd_strerror(err));
	exit(EXIT_FAILURE);
      }
      init = 1;
      break;  /* skip one period */
    }

/*    if (snd_pcm_state(handle) == SND_PCM_STATE_RUNNING) 
      init = 0; */
    /*get key values now*/  
    new.right = operant_read(box_id, RIGHTPECK);    /*get value at right peck position*/
    new.left = operant_read(box_id, LEFTPECK);      /*get value at left peck position*/
    new.center = operant_read(box_id, CENTERPECK);  /*get value at left peck position*/

    /*find zero-to-one key transition: this means you saw a response start*/  
    if(new.right > old.right) {
      GRABPECKS[pecknum].right = 1;
      GRABPECKS[pecknum].left = 0;
      GRABPECKS[pecknum].center = 0;
      gettimeofday(&GRABPECKS[pecknum].timestamp, NULL); 
      pecknum++;
      peckcounts->right++;
    }
    else if(new.left > old.left){
      GRABPECKS[pecknum].right = 0;
      GRABPECKS[pecknum].left = 1;
      GRABPECKS[pecknum].center = 0;
      /* GRABPECKS[pecknum].timestamp = clock(); */
      gettimeofday(&GRABPECKS[pecknum].timestamp, NULL); 
      pecknum++;
      peckcounts->left++;
  }
    else if (new.center > old.center) {
      GRABPECKS[pecknum].right = 0;
      GRABPECKS[pecknum].left = 0;
      GRABPECKS[pecknum].center = 1;
      gettimeofday(&GRABPECKS[pecknum].timestamp, NULL); 
      pecknum++;
      peckcounts->center++;
    old=new;}

    totframesout += err; 
    ptr += err * channels;
    outframes -= err;
    if (outframes == 0){
      if(DEBUG){printf("outframes is zero so I'll break\n");}
      break;
    }
    /* it is possible, that the initial buffer cannot store */
    /* all data from the last period, so wait awhile */
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
    printf("exited while writei loop, so what am I waiting for?\n");
    printf("frames not played: %d \n", (int)outframes);
    printf("frames played: %d \n", (int) totframesout);
  }
  free(ufds);
  ufds=NULL;
  return (1); /*successful playback*/
}

/********************************************
 ** BUILD TEMP BUFFER. This function builds the stimulus from the two
 ** wav files in the .stim file. This function takes in two full path
 ** exemplars and constructs an output buffer reflecting repeated or alternating stimuli
 ** depending on the build type option.
 ** of repetitions in reps and build_type=0 for single repeating stima, and 
 ** build_type=1 changes to alternating stim a and stim b. Use 60ms isi.
 ********************************************/
int buildtempbuff(char * stimexma, char * stimexmb, long unsigned int * framesa, long unsigned int * framesr)
{
snd_pcm_uframes_t outframesa, outframesr;
SNDFILE *sfina=NULL, *sfinb=NULL;
SF_INFO sfin_infoa, sfin_infob;
sf_count_t incounta, incountb;
short *inbuffb, *inbuffa ;
int samp=0, j;
long unsigned int pause_samps=2646;

/*open each motif*/
   sfin_infoa.format=0;
   sfin_infob.format=0;
   if(!(sfina = sf_open(stimexma,SFM_READ,&sfin_infoa))){
     fprintf(stderr,"error opening input file %s\n",stimexma);
     return -1;}
   if(!(sfinb = sf_open(stimexmb,SFM_READ,&sfin_infob))){
     fprintf(stderr,"error opening input file %s\n",stimexmb);
     return -1;}

   /*read in the file */
   inbuffa = (short *) malloc(sizeof(int) * sfin_infoa.frames);
   incounta = sf_readf_short(sfina, inbuffa, sfin_infoa.frames);
   sf_close(sfina);
   
   inbuffb = (short *) malloc(sizeof(int) * sfin_infob.frames);
   incountb = sf_readf_short(sfinb, inbuffb, sfin_infob.frames);
   sf_close(sfinb);

   if(DEBUG==1){fprintf(stderr, "samples in: %lu \n", (long unsigned int)incounta);}
           outframesr = (long unsigned int)incounta + pause_samps;
   
           outframesa = (long unsigned int) incounta + (long unsigned int) incountb + 2 * pause_samps;
  
    if(DEBUG==1){fprintf(stderr, "outframes a= %lu, outframes r= %lu\n", (long unsigned int)outframesa, (long unsigned int) outframesr);}

/* Two different kinds of build functions. One for repetitions, one for alternations */
 soundvecr = (short *) malloc(sizeof(short)*outframesr);
	   for (j=0;j<incounta;j++){
	     soundvecr[samp++] = inbuffa[j];}
	   for (j=0;j<pause_samps;j++){
	     soundvecr[samp++] = 0;}
samp = 0;
soundveca = (short *) malloc(sizeof(short)*outframesa);
    for (j=0;j<incountb;j++){
      soundveca[samp++] = inbuffb[j];}
    for (j=0;j<pause_samps;j++){
      soundveca[samp++] = 0;}
    for (j=0;j<incounta;j++){
      soundveca[samp++] = inbuffa[j];}
    for (j=0;j<pause_samps;j++){
      soundveca[samp++] = 0;}
*framesa = outframesa;
*framesr = outframesr; 

if(DEBUG){printf("Finished building soundvector.\n");}

 free(inbuffa);
 free(inbuffb);
if(DEBUG){printf("End of buildtempbuff reached successfully\n");}
 return 1;
}

/***************Find First Peck***************************************
 * Used to calculate first right peck from GRABPECKS structure*******/

int findfirstpeck(struct Savedpecks * GRABPECKS, struct timeval * firstrightpecktime)
{
int i= 0;
for (i=0; i<MAXPECKS; i++){
     if(GRABPECKS[i].right == 1) {
        *firstrightpecktime = GRABPECKS[i].timestamp;
	return 1;
            }}
return -1;
}

/**********************************************************************
 **********************************************************************/
void do_usage()
{
  fprintf(stderr, "femxpeck usage:\n");
  fprintf(stderr, "    femxpeck [-help] [-B int] [-lb float] [-ub float] [-on int] [-off int] [-S <subject number>] <filename>\n\n");
  fprintf(stderr, "        -help          = show this help message\n");
  fprintf(stderr, "        -B int         = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "        -on int:int    = set hour:min for exp to start eg: '-on 7:30' (default is 7:00 AM)\n");
  fprintf(stderr, "        -off int:int   = set hour for exp to stop eg: '-off 19:45' (default is 7:00 PM)\n");
 fprintf(stderr, "         -x		  = set correction trials for incorrect responses)\n");
  fprintf(stderr, "                         To use daily sunset or sunrise times set 'on' or 'off' to '99'\n");
    fprintf(stderr, "        -S int         = specify the subject ID number (required)\n");
  fprintf(stderr, "        filename       = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "                         where each line is: 'Class' 'Wavfile' 'Present_freq' 'Reinf_rate'\n");
  fprintf(stderr, "                         'Class'= 1 for LEFT-, 2 for RIGHT-key assignment \n");
  fprintf(stderr, "                         'Wavfile' is the name of the stimulus soundfile (must be 44100 Hz sample rate\n");
  fprintf(stderr, "                         'Presfreq' is the overall rate (compared to the other stimuli) at which the stimulus is presented. \n"); 
  fprintf(stderr, "                              The actual integer rrate for each stimulus is that value divded by the\n");
  fprintf(stderr, "                              sum for all stimuli. Set all prefreq values to 1 for equal probablility \n"); 
  fprintf(stderr, "                         'Reinf_rate' is the percentage of time that food is made available following presentation of this stimulus.\n");
  exit(-1);
}

/**********************************************************************
 ** parse the command line
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour,int *startmin, int *stopmin,int *correction_flag, char **stimfname)
{
  int i=0;
  
  for (i = 1; i < argc; i++){
    if (*argv[i] == '-'){
      if (strncmp(argv[i], "-B", 2) == 0) 
        sscanf(argv[++i], "%i", box_id);
      else if (strncmp(argv[i], "-S", 2) == 0)
        sscanf(argv[++i], "%i", subjectid);
      else if (strncmp(argv[i], "-x", 2) == 0)
	*correction_flag = 1;
      else if (strncmp(argv[i], "-on", 3) == 0){
	sscanf(argv[++i], "%i:%i", starthour,startmin);}
      else if (strncmp(argv[i], "-off", 4) == 0){
	sscanf(argv[++i], "%i:%i", stophour, stopmin);}
           else if (strncmp(argv[i], "-help", 5) == 0){
        do_usage();
      }
      else{
        fprintf(stderr, "Unknown option: %s\t", argv[i]);
        fprintf(stderr, "Try 'femxpeck -help'\n");
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
	char  buf[128], stimexma[128],stimexmb[128],fstima[256],fstimb[256], timebuff[64], tod[256], date_out[256], buffer[30],temphour[16],tempmin[16];
	int nclasses, nstims, stim_class, stim_reinf, stim_punish, resp_sel, resp_acc, subjectid, period, tot_trial_num, 
	  resp_wind=0,trial_num, session_num, i,j,k, correction, playval, stim_number, 
	  *playlist=NULL, totnstims=0, dosunrise=0,dosunset=0,starttime,stoptime,currtime;
	long unsigned int framesr, framesa;
	float timeout_val=0.0, timeout_duration;
	float resp_rxt=0.0;
	float latitude = 32.82, longitude = 117.14;
    int more_repeats;
	int resp_sec, resp_usec; 
	time_t curr_tt, rise_tt, set_tt;
	struct timeval alternating_start, firstrightpecktime, resp_rt;
	struct tm *loctime;
    struct Savedpecks *GRABPECKS= NULL;
    struct PECK peckcounts;
	Failures f = {0,0,0,0};
	int left = 0, right= 0, center = 0, fed = 0, trial_status=0,correction_flag = 0;
	int reinfor_sum = 0, reinfor = 0, sessionTrials;
	struct stim {
	  char exemplara[128];
	  char exemplarb[128];
	  int class;
	  int reinf;
	  int punish;
	  int freq;
	  unsigned long int dur; 
	  int num;
	}stimulus[MAXSTIM];
    struct response {
	  int count;
	  int go;
	  int no;
	  float ratio;
	} stimRses[MAXSTIM], stimRtot[MAXSTIM], classRses[MAXCLASS], classRtot[MAXCLASS];
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
        command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin,&correction_flag, &stimfname); 
       	if(DEBUG){
	  fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, timeout_val=%f stimfile: %s ",
		(int) box_id, (int) subjectid, (int) starthour, stophour, startmin, stopmin, timeout_val, stimfname );
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
	if((period= do_setup(pcm_name))<0){
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

		/* Read in the list of exmplars */
	nstims = nclasses = 0;
        if ((stimfp = fopen(stimfname, "r")) != NULL){
	  while (fgets(buf, sizeof(buf), stimfp))
	    nstims++;
	  fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
	  rewind(stimfp);
	                
	  for (i = 0; i < nstims; i++){
	    fgets(buf, 128, stimfp);
	    stimulus[i].freq = stimulus[i].reinf=0;
	    sscanf(buf, "%d\%s\%s\%d\%d\%d", &stimulus[i].class, stimulus[i].exemplara, stimulus[i].exemplarb, &stimulus[i].freq, &stimulus[i].reinf, &stimulus[i].punish);
	    if((stimulus[i].freq==0) || (stimulus[i].reinf==0)|| (stimulus[i].punish==0)){
	      printf("ERROR: insufficnet data or bad format in '.stim' file. Try '2ac -help'\n");
	      exit(0);} 
	    totnstims += stimulus[i].freq;
	    if(DEBUG){printf("totnstims: %d\n", totnstims);}
	    
	    /* count stimulus classes*/
	    if (nclasses<stimulus[i].class){nclasses=stimulus[i].class;}
	    if (DEBUG){printf("nclasses: %d\n", nclasses);}
	    
	    /*check the reinforcement rates */
	    if (stimulus[i].class==1){
	      fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct LEFT responses\n", 
		      stimulus[i].exemplara, stimulus[i].reinf);
	      fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect RIGHT responses\n", 
		      stimulus[i].exemplara, stimulus[i].punish);
	    }
	    else if (stimulus[i].class==2){
	      fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct RIGHT responses\n", 
		      stimulus[i].exemplara, stimulus[i].reinf);
	      fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect LEFT responses\n", 
		      stimulus[i].exemplara, stimulus[i].punish);
	    }
	     else  {
            fprintf(stderr,"Error opening stimulus input file! Try 'gng_probe -help' for proper file formatting.\n");  
            snd_pcm_close(handle);
            exit(0);     
	  }
      }}
	fclose(stimfp);
        if(DEBUG){printf("Done reading in stims; %d stims in %d classes found\n", nstims, nclasses);}

        
	

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
	sprintf(datafname, "%i_%s.gng_rDAT", subjectid, stimfroot);
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
            strcpy (stimexma, stimulus[stim_number].exemplara);                       /* get exemplar filename */
	    strcpy (stimexmb, stimulus[stim_number].exemplarb);                       /* get exemplar filename */
            stim_reinf = stimulus[stim_number].reinf;
            stim_punish = stimulus[stim_number].punish;
	    sprintf(fstima,"%s%s", STIMPATH, stimexma);                                /* add full path to file name */
	    sprintf(fstimb,"%s%s", STIMPATH, stimexmb);                                /* add full path to file name */    

if(DEBUG){printf("fullpath a=%s.\n", fstima);}

		if (DEBUG){printf("building stimulus buffers.\n");}
		if (buildtempbuff(fstima, fstimb, &framesa, &framesr)!=1) {
		printf("Building stimulus buffers failed.\n");
		return -1;}	

	    do{                                             /* start correction trial loop */
	      if(DEBUG){printf("Starting correction trial loop now.\n");}
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

          /* Beginning of sound playback */
          if(DEBUG==1){printf("Starting sound playback while checking for pecks.\n");}

	/* First we initialize pointer for GRABPECKS */  
		GRABPECKS = (struct Savedpecks *) malloc(sizeof(struct Savedpecks) * MAXPECKS);
		if(GRABPECKS == NULL){printf("Problem allocating memory for GRABPECKS. \n");}
		for(j=0;j<MAXPECKS;j++){
			GRABPECKS[j].center = 0;
			GRABPECKS[j].left = 0;
			GRABPECKS[j].right = 0;	
		}

		/* Initialize peckcounts structure as well now */
		peckcounts.left = 0;
		peckcounts.center = 0;
		peckcounts.right = 0;
		pecknum = 0;
          /* Start by playing repeated stimulus 20 times, each time checking for report key press */
          for (j=0;j<20;j++) {
	    if(DEBUG){printf("Starting first repeated playback number %i\n",j);}
	
            playvec_count(soundvecr, framesr, box_id, GRABPECKS, &peckcounts);
            /* If at least four repetitions have occurred and there is a key-press, break and move to alternating stim */
            if (j>5 && peckcounts.left>0) {

              if(DEBUG==1){printf("report key detected and enough reps have occurred, starting random delay\n");}

              /* Reset obs_pecks structure and peck_times */
              peckcounts.left = 0;
              peckcounts.right = 0;
              peckcounts.center = 0;
              pecknum = 0;

		for(j=0;j<MAXPECKS;j++){
			GRABPECKS[j].center = 0;
			GRABPECKS[j].left = 0;
			GRABPECKS[j].right = 0;	
		}

          /* Choose random number of continued repetitions  (2-9) before switching to alternation */
          more_repeats = (rand() % 7) + 3;
	  if(DEBUG){printf("more repeats=%d\n", more_repeats);}

          for (j=0;j<more_repeats;j++) {
            playvec_count(soundvecr,framesr,  box_id, GRABPECKS, &peckcounts);
          }

          /* Now switching to playback of alternating buffer.*/

          /* Reset obs_pecks structure and peck_times */
              peckcounts.left = 0;
              peckcounts.right = 0;
              peckcounts.center = 0;

	for(j=0;j<MAXPECKS;j++){
			GRABPECKS[j].center = 0;
			GRABPECKS[j].left = 0;
			GRABPECKS[j].right = 0;	
		}
	  gettimeofday(&alternating_start, NULL);

         /*  alternating_start = clock(); */

          for (j=0;j<10;j++) {
            playvec_count(soundveca, framesa, box_id, GRABPECKS, &peckcounts);
            if (peckcounts.right>0){
              trial_status=1;
              break;}
          }
          /*Check whether bird responded left but did not respond right after change to alternation */
          
         if(trial_status==0){
            if(DEBUG==1){printf("Report peck but no response peck.\n");}
            trial_status=2; /*trial status=2 implies left but no right peck */
            break;
         }
            
          break;
            }
          /* Exiting without having seen any pecks on the left */
            trial_status=0;
          } 

	/* End of stimulus presentation code */
        /* code to grab time of first peck on the right key after alternation starts */
	if(DEBUG){printf("Trial Status was: %d\n", trial_status);}
          i=0;
        if (trial_status == 1) {findfirstpeck(GRABPECKS, &firstrightpecktime);}


	if (trial_status == 0) {resp_rxt = 10.0;}
	else if (trial_status == 1){
		/* Calculate response time */
	      curr_tt = time (NULL); 
	      loctime = localtime (&curr_tt);                     /* date and wall clock time of resp */
	      timersub (&firstrightpecktime, &alternating_start, &resp_rt);           /* reaction time */
	      if (DEBUG){
		resp_sec = resp_rt.tv_sec;      
		resp_usec = resp_rt.tv_usec;
		printf("resp rt sec: %d \t usec: %d\n", resp_sec, resp_usec);} 
	      resp_rxt = (float) resp_rt.tv_sec + ( (float) resp_rt.tv_usec/1000000);  /* format reaction time */
	      if (DEBUG){printf("flag: resp_rxt = %.4f\n", resp_rxt);}}

	else if (trial_status == 2) {resp_rxt = 10;}

	if(DEBUG){printf("resp_rxt: %f\n", resp_rxt);}
	     /* Code here to figure out response and put it in the right format */

        /* Consequate responses */
	      if (DEBUG){
		printf("flag: stim_class = %d\n", stim_class);

	      if(stim_class == 1){   /* If S+ stimulus */
		if (trial_status==0 || trial_status==2){   /*no response*/
		  resp_sel = 0;
		  resp_acc = 0;
		  if (DEBUG){ printf("flag: no response to s+ stim\n");}
		  ++stimRses[stim_number].no; ++stimRtot[stim_number].no; ++classRses[stim_class].no; ++classRtot[stim_class].no; 
		  reinfor = 0;
		}
		else if (trial_status == 1){  /*switched to response port*/
		  resp_sel = 1;
		  resp_acc = 1; 
		  reinfor = feed(stim_reinf, &f);
		  if (DEBUG){printf("flag: go response to s+ stim\n");}
		  ++stimRses[stim_number].go; ++stimRtot[stim_number].go; ++classRses[stim_class].go; ++classRtot[stim_class].go;
		  if (reinfor == 1) { ++fed;}
		}
	      }
	      else if(stim_class == 2){  /* If sham stimulus */
		if (trial_status==0 || trial_status==2){ /*no response*/
		  resp_sel = 0;
		  resp_acc = 1;
		  if (DEBUG){printf("flag: no response to s- stim\n");}
		  ++stimRses[stim_number].no; ++stimRtot[stim_number].no; ++classRses[stim_class].no; ++classRtot[stim_class].no;
		  reinfor = 0;
		}
		else if (trial_status == 1){ /*switched to  response port*/
		  resp_sel = 1;
		  resp_acc = 0;
		  if (DEBUG){printf("flag: go response to s- stim\n");}
		  ++stimRses[stim_number].go; ++stimRtot[stim_number].go; ++classRses[stim_class].go; ++classRtot[stim_class].go;	 
		  reinfor =  timeout(stim_reinf);
		}
	      }
	  }
				    
	            
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


	      operant_write (box_id, LFTKEYLT, 0);    /*make sure the key lights are off after resp interval*/
	      operant_write (box_id, RGTKEYLT, 0);

	     	      /* Pause for ITI */
	      reinfor_sum = reinfor + reinfor_sum;
	      operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	      nanosleep(&iti, NULL);                                   /* wait intertrial interval */
	      if (DEBUG){printf("flag: ITI passed\n");}
                                        
                   
	      /* Write trial data to output file */
	      strftime (tod, 256, "%H%M", loctime);
	      strftime (date_out, 256, "%m%d", loctime);
	      fprintf(datafp, "%d\t%d\t%d\t%s\t%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, 
		      correction, stimexma, stimexmb, stim_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
	      fflush (datafp);
	      if (DEBUG){printf("flag: trial data written\n");}

	      	    /* Update summary data */
	     
	      if(freopen(dsumfname,"w",dsumfp)!= NULL){
		fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
		fprintf (dsumfp, "\tRESPONSE RATIOS (by stimulus)\n");
		fprintf (dsumfp, "\tStim     \t\tSession     \t\tTotals\n");
		for (i = 0; i<nstims;++i){
		  fprintf (dsumfp, "\t%s\t%s     \t\t%1.4f     \t\t%1.4f\n", 
			   stimulus[i].exemplara,stimulus[i].exemplarb, stimRses[i].ratio, stimRtot[i].ratio);
		}
		fprintf (dsumfp, "\n\n RESPONSE DATA (by stim class)\n");
		fprintf (dsumfp, "               Today's Session               Totals\n");
		fprintf (dsumfp, "Class \t\t Count \tResponse Ratio \t\t Count \tResponse Ratio\n");
		for (i = 1; i<nclasses+1;++i){
		  fprintf (dsumfp, "%d \t\t %d \t%1.4f \t\t %d \t%1.4f\n", 
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


	    
	      /* End of trial chores */
	      sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                  /* unblock termination signals */ 
	      if (resp_acc == 0 && correction_flag == 1){
		correction = 0;}
	      else{
		correction = 1;}                                              /* set correction trial var */

	  	  free(GRABPECKS);
	      curr_tt = time(NULL);
	      loctime = localtime (&curr_tt);
	      strftime (hour, 16, "%H", loctime);
	      strftime(min, 16, "%M", loctime);
	      currtime=(atoi(hour)*60)+atoi(min);
	      if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
		if (DEBUG) {printf("correction trial is now set to %d, \n", correction);}	    

	    }while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); /* correction trial loop */
	    
	    stim_number = -1;                                                /* reset the stim number for correct trial*/
        /* Don't free sound vecs until new stimulus is selected */
	free(soundvecr);
          free(soundveca);

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
	
/*reset some vars for a new day */
	  ++session_num;                                                                     /* increase sesion number */ 
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

	free(soundvecr);
	free(soundveca);
	}while (1);// main loop
	
	
	/*  Cleanup */
	fclose(datafp);
	fclose(dsumfp);
	return 0;
}                         

