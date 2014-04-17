/* 
7-26-06:TQG
perch-hop operant preference procedure based on gentner and hulse (2000)   
bird lands on one of three perches to trigger sound playback at that location, 
only two of the three perches are active for a given bird, and the stimulus-to-perch 
mapping switches after a set number of trials.

NOTE:requires three audio channels in a single box, and perch hop apparatus 

11-20-11: MRB
Small changes to continuously loop stimulus playback until bird leaves perch.
Also removed code to randomly choose offset and only play a part of the stimulus.
*/

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
#define L_PERCH 1
#define C_PERCH 2
#define R_PERCH 3
#define HOUSELT	4   

/*----- AUDIO OUTPUT CHANNELS -----*/
#define L_PCM "dac17"
#define C_PCM "dac18"
#define R_PCM "dac19"

/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             1             /* seconds that bird must stay on the same perch to start playback (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  1024          /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 256            /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define MAXFILESIZE         26460000           /*maximum nuber of samples in any soundfile: 10 min at 44.1kHz */

int  trial_max = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME; 
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval = SLEEP_TIME;
const char exp_name[] = "FEMXPERCH";
int box_id = -1;
int perch1=0, perch2=0, xperch=0;


struct timespec iti = {INTER_TRIAL_INTERVAL, 0};
struct timespec rsi = {0, RESPONSE_SAMPLE_INTERVAL};
struct timeval min_perch = {RESP_INT_SEC, RESP_INT_USEC};

struct PERCH{
  int perch1;
  int perch2;
  int xperch;
} trial;

snd_pcm_t *pcm_left, *pcm_center, *pcm_right, *aud1, *aud2, *xaud;
unsigned int channels = 1;                      /* count of channels on a single DAC*/
unsigned int rate = 44100;                      /* stream rate */
snd_pcm_format_t format = SND_PCM_FORMAT_S16;   /* sample format */
unsigned int buffer_time = 500000;              /* ring buffer length in us */
unsigned int period_time = 100000;              /* period time in us */
int resample = 1;                               /* enable alsa-lib resampling */
int period =0;

snd_pcm_sframes_t buffer_size;
snd_pcm_sframes_t period_size;
snd_output_t *output = NULL;

/* -------- Signal handling --------- */
int client_fd = -1;
static void sig_pipe(int signum)
{ fprintf(stdout,"SIGPIPE caught\n"); client_fd = -1;}
static void termination_handler (int signum){
  snd_pcm_close(pcm_left);snd_pcm_close(pcm_center);snd_pcm_close(pcm_right);
  fprintf(stdout,"closed pcm devices: term signal caught: exiting\n");
  exit(-1);
}

/*********************************************************************************
 * PCM SETUP 
 ********************************************************************************/
snd_pcm_t *do_setup(char *pcm_name)
{
  snd_pcm_t *handle;
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
    printf("FATAL ERROR IN do_setup: Playback open error: %s\n", snd_strerror(err));
    exit (-1);
  }
  /* choose all parameters */
  err = snd_pcm_hw_params_any(handle, params);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Broken configuration for playback: no configurations available: %s\n", snd_strerror(err));
    exit (-1);
  }
  /* set the interleaved read/write format */
  err = snd_pcm_hw_params_set_access(handle, params, access);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Access type not available for playback: %s\n", snd_strerror(err));
    exit(-1);
  }
  /* set the sample format */
  err = snd_pcm_hw_params_set_format(handle, params, format);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Sample format not available for playback: %s\n", snd_strerror(err));
    exit(-1);
  }
  /* set the count of channels */
  err = snd_pcm_hw_params_set_channels(handle, params, channels);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Channels count (%i) not available for playbacks: %s\n", channels, snd_strerror(err));
    exit (-1);
  }
  /* set the stream rate */
  rrate = rate;
  err = snd_pcm_hw_params_set_rate_near(handle, params, &rrate, 0);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Rate %iHz not available for playback: %s\n", rate, snd_strerror(err));
    exit(-1);
  }
  if (rrate != rate) {
    printf("FATAL ERROR IN do_setup: Rate doesn't match (requested %iHz, get %iHz)\n", rate, err);
    exit(-1);
  }
  /* set the buffer time */
  err = snd_pcm_hw_params_set_buffer_time_near(handle, params, &buffer_time, &dir);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Unable to set buffer time %i for playback: %s\n", buffer_time, snd_strerror(err));
    exit(-1);
  }
  err = snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Unable to get buffer size for playback: %s\n", snd_strerror(err));
    exit (-1);
  }
  if(DEBUG){printf("buffer size is %d\n", (int)buffer_size);}  
  /* set the period time */
  err = snd_pcm_hw_params_set_period_time_near(handle, params, &period_time, &dir);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Unable to set period time %i for playback: %s\n", period_time, snd_strerror(err));
    exit(-1);
  }
  err = snd_pcm_hw_params_get_period_size(params, &period_size, &dir);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Unable to get period size for playback: %s\n", snd_strerror(err));
    exit(-1);
  }
  if(DEBUG){printf("period size is %d\n", (int)period_size);}  
  /* write the parameters to device */
  err = snd_pcm_hw_params(handle, params);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Unable to set hw params for playback: %s\n", snd_strerror(err));
    exit(-1);
  }

  /* --------- set up software parameters ---------*/ 
  /* get the current swparams */
  err = snd_pcm_sw_params_current(handle, swparams);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Unable to determine current swparams for playback: %s\n", snd_strerror(err));
    exit(-1);
  }
  /* start the transfer when the buffer is almost full: */
  err = snd_pcm_sw_params_set_start_threshold(handle, swparams, (buffer_size / period_size) * period_size);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Unable to set start threshold mode for playback: %s\n", snd_strerror(err));
    exit(-1);
  }
  /* allow the transfer when at least period_size samples can be processed */
  err = snd_pcm_sw_params_set_avail_min(handle, swparams, period_size);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Unable to set avail min for playback: %s\n", snd_strerror(err));
    exit(-1);
  }
  /* align all transfers to 1 sample */
  err = snd_pcm_sw_params_set_xfer_align(handle, swparams, 1);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Unable to set transfer align for playback: %s\n", snd_strerror(err));
    exit(-1);
  }
  /* write the parameters to the playback device */
  err = snd_pcm_sw_params(handle, swparams);
  if (err < 0) {
    printf("FATAL ERROR IN do_setup: Unable to set sw params for playback: %s\n", snd_strerror(err));
    exit(-1);
  }

  snd_pcm_hw_params_get_period_size (params, &persize, &dir);
  if(DEBUG){printf("done with setup\n");}
  period=persize;
  return handle;

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
 * extract a stimulus from the soundfile, output sound to a pcm(handle). 
 * this play will watch the perch during playback and abort if bird leaves the target perch
 * returns -1 on playback error, 1 for success
 **************************************************************************************/
int playall_and_watch(snd_pcm_t *handle, char *sfname, int box_id, int tperch)
{

  SNDFILE *sf;
  SF_INFO *sfinfo;
  short *obuff;
  unsigned short *ptr;
  sf_count_t incount;
  double inframes;
  snd_pcm_uframes_t outframes, totframesout;
  int err, count, init;
  
  struct pollfd *ufds;

  /* memory for SF_INFO structures */
  sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));
  
  /* open input file*/
  if(!(sf = sf_open(sfname,SFM_READ,sfinfo))){
    fprintf(stderr,"error opening input file %s\n",sfname);
    free(sfinfo);
    return -1;
  }
  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use\n");
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  
  inframes = (int)sfinfo->frames;
  obuff = (short *) malloc(sizeof(int)*inframes);
  if(DEBUG==3){printf("trying to sf_readf %d frames\n",(int)inframes);}
  incount = sf_readf_short(sf, obuff, inframes);
  if(DEBUG==3){printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount, (int)inframes);}  
  outframes = inframes;
  if(DEBUG==3){printf("I'll try to write %d frames\n", (int)outframes);}
  
  snd_pcm_nonblock(handle,1); /*make sure you set playback to non-blocking*/

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
	  sf_close(sf);
	  free(sfinfo);
	  sfinfo=NULL;
	  free(obuff);
	  obuff=NULL;
	  free(ufds);
	  ufds=NULL;
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

  /*start the actual playback*/
  while ( (outframes > 0) && (operant_read(box_id, tperch)) ) {
    err = snd_pcm_writei(handle,ptr, outframes);
    if (err < 0) {
      if (xrun_recovery(handle, err) < 0) {
	printf("Write error: %s\n", snd_strerror(err));
	sf_close(sf);
	free(sfinfo);
	sfinfo=NULL;
	free(obuff);
	obuff=NULL;
	free(ufds);
	ufds=NULL;
	exit(EXIT_FAILURE);
      }
      init = 1;
      break;  /* skip one period */
    }
    if (snd_pcm_state(handle) == SND_PCM_STATE_RUNNING)
      init = 0;

    totframesout += err; 
    ptr += err * channels;
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
	sf_close(sf);
	free(sfinfo);
	sfinfo=NULL;
	free(obuff);
	obuff=NULL;
	free(ufds);
	ufds=NULL;
	exit(EXIT_FAILURE);
	}
	init = 1;
      } else {
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

  /*clean up before you leave*/  
  sf_close(sf);
  free(sfinfo);
  sfinfo=NULL;
  free(obuff);
  obuff=NULL;
  free(ufds);
  ufds=NULL;
  return 1; /*successful playback*/
}


int play_and_watch(snd_pcm_t *handle, char *sfname, int stimdur, int stimoffset, int box_id, int tperch)
{
  float ramp_time = 0.05;  /*onset offset ramp duration in secs */
  SNDFILE *sf;
  SF_INFO *sfinfo;
  short *obuff;
  unsigned short *ptr;
  sf_count_t incount;
  double inframes, maxframes;
  int tmp;
  snd_pcm_uframes_t outframes, totframesout;
  sf_count_t offset;
  int i, err, count, init;
  int ramp_dur=0;

  struct pollfd *ufds;

  /* memory for SF_INFO structures */
  sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));
  
  /* open input file*/
  if(!(sf = sf_open(sfname,SFM_READ,sfinfo))){
    fprintf(stderr,"error opening input file %s\n",sfname);
    free(sfinfo);
    return -1;
  }
  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use\n");
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  /* extract the stimulus to play */
  if (stimoffset< 0){
    fprintf (stderr, "Stimulus offset value must be greater than or equal to zero\n");
    sf_close(sf);free(sfinfo);return -1;}

  offset = (int) rint((float)sfinfo->samplerate*(float)stimoffset/1000.0);
  if(DEBUG==3){printf("offset  is %d msec, which is %d samples\n", stimoffset, (int) offset );}
  tmp = sf_seek(sf, offset, SEEK_SET);
  if(DEBUG==3){printf("trying to seek by %d samples; sf_seek returned %d \n", (int)offset, tmp);}
  maxframes = (int)sfinfo->frames;
  if (stimdur==-1) /* play the whole soundfile (less any offset)*/
    inframes = maxframes - (int)offset;
  else if(stimdur>0)/* play the define stimulus duration */
    inframes = (int) rint((float)sfinfo->samplerate*(float)stimdur/1000.0);
  else{
    fprintf (stderr, "Invalid stimulus duration '%d'\n",stimdur);
    sf_close(sf);free(sfinfo);return -1;
  }
  if(DEBUG==3){printf("stimdur  is %d msec, so I want %d samples after the offset\n", stimdur, (int) inframes );}
  
  obuff = (short *) malloc(sizeof(int)*inframes);
  if( ( maxframes - (offset + inframes) ) <0){/* read the data */
    fprintf (stderr, "The sum of the stimulus duration and offset exceeds the soundfile length\n");
    sf_close(sf);free(sfinfo);free(obuff);return -1;
  }
  if(DEBUG==3){printf("trying to sf_readf %d frames\n",(int)inframes);}
  incount = sf_readf_short(sf, obuff, inframes);
  if(DEBUG==3){printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount, (int)inframes);}
  
  /* ramp the first (and last) 50 ms from zero to normal amplitude*/
  ramp_dur= ramp_time * sfinfo->samplerate; //get number of samples for ramp
  for (i = 0; i<ramp_dur; i++)
    obuff[i] = obuff[i]*((float)i/(float)ramp_dur);
  for (i = (incount-ramp_dur); i<=incount; i++) 
    obuff[i] = obuff[i]* ((float)(incount-i)/(float)ramp_dur);
  
  outframes = inframes;
  if(DEBUG==3){printf("I'll try to write %d frames\n", (int)outframes);}
  
  snd_pcm_nonblock(handle,1); /*make sure you set playback to non-blocking*/

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
  totframesout=0;
  ptr=obuff;

  /*start the actual playback*/
  while ( (outframes > 0) && (operant_read(box_id, tperch)) ) {
    err = snd_pcm_writei(handle,ptr, outframes);
    if (err < 0) {
      if (xrun_recovery(handle, err) < 0) {
	printf("Write error: %s\n", snd_strerror(err));
	exit(EXIT_FAILURE);
      }
      init = 1;
      break;  /* skip one period */
    }
    if (snd_pcm_state(handle) == SND_PCM_STATE_RUNNING)
      init = 0;

    totframesout += err; 
    ptr += err * channels;
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
      } else {
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
  
  sf_close(sf);
  free(sfinfo);
  sfinfo=NULL;
  free(obuff);
  obuff=NULL;
  free(ufds);
  ufds=NULL;
  return 1; /*successful playback*/
}


/**********************************************************************
 **********************************************************************/
void do_usage()
{
  fprintf(stderr, "femxperch usage:\n");
  fprintf(stderr, "    femxperch [-help] [-B int] [-lb float] [-ub float] [-on int:int] [-off int:int] [-S int] <filename>\n\n");
  fprintf(stderr, "        -help          = show this help message\n");
  fprintf(stderr, "        -B int         = must be '-B 9' unless you change the DACs\n");
  fprintf(stderr, "        -lb float      = lower bound (in secs) for stimulus duration. eg. 7.25\n");
  fprintf(stderr, "        -ub float      = upper bound (in secs) for stimulus duration. eg. 15.0 \n");
  fprintf(stderr, "                         NOTE: LB and UB should use .25 sec resolution because the\n");
  fprintf(stderr, "                           stimulus durations are uniformly distributed between LB and UB\n");
  fprintf(stderr, "                           in 250 msec steps\n");
  fprintf(stderr, "        -on int:int    = set hour:min for exp to start eg: '-on 7:30' (default is 7:00 AM)\n");
  fprintf(stderr, "        -off int:int   = set hour for exp to stop eg: '-off 19:45' (default is 7:00 PM)\n");
  fprintf(stderr, "                         To use daily sunset or sunrise times use '-on 99' and/or '-off 99'\n");
  fprintf(stderr, "        -swap int      = set the number of trials between stim-resp swap\n");
  fprintf(stderr, "                         use '-1' to never swap(default), '0' to swap each session\n");
  fprintf(stderr, "        -state int     = start state of the app is sum of 1,3,5 (L,C,R perch inactive) \n");
  fprintf(stderr, "                         and 0,1 (to set the stimclass-perch mappings in block 1)\n");
  fprintf(stderr, "                          For example use '-state 4' if you want to deactivate the center\n"); 
  fprintf(stderr, "                          perch and start with stimclass2 mapped to perch1\n");
  fprintf(stderr, "        -S int         = specify the subject ID number (required)\n");
  fprintf(stderr, "        filename       = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "                         where each line is: 'Class' 'Wavfile' 'Present_freq' 'Reinf_rate'\n");
  fprintf(stderr, "                         'Class'= 1 or 2 for perch assignment\n");
  fprintf(stderr, "                         'Wavfile' is the name of the stimulus soundfile ( must be 44.1KHz\n");
  fprintf(stderr, "                         'Presfreq' is the overall rate at which the stimulus is presented.\n"); 
  fprintf(stderr, "                           The actual rate for each stimulus is presfreq divded by the\n");
  fprintf(stderr, "                           sum for all stims. Set all prefreq values to 1 for equal probablility \n"); 
  exit(-1);
}

/**********************************************************************
 ** parse the command line
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour,int *startmin, int *stopmin, int *swapval, int *initstate, float *stimdurLB, float *stimdurUB, char **stimfname)
{
  int i=0;
  if(DEBUG)printf("argc is %d\n", argc);
  
  if(argc>1){
    for (i = 1; i < argc; i++){
      if(DEBUG){fprintf(stderr, "argv[%d] was: %s\n", i, argv[i]);}
      if (*argv[i] == '-'){
	if (strncmp(argv[i], "-B", 2) == 0) 
	  sscanf(argv[++i], "%i", box_id);
	else if (strncmp(argv[i], "-S", 2) == 0)
	  sscanf(argv[++i], "%i", subjectid);
	else if (strncmp(argv[i], "-lb", 3) == 0)
	  sscanf(argv[++i], "%f", stimdurLB);
	else if (strncmp(argv[i], "-ub", 3) == 0)
	  sscanf(argv[++i], "%f", stimdurUB);
	else if (strncmp(argv[i], "-on", 3) == 0){
	  sscanf(argv[++i], "%i:%i", starthour,startmin);}
	else if (strncmp(argv[i], "-off", 4) == 0){
	  sscanf(argv[++i], "%i:%i", stophour, stopmin);}
	else if (strncmp(argv[i], "-swap", 5) == 0)
	  sscanf(argv[++i], "%i", swapval);
	else if (strncmp(argv[i], "-state", 6) == 0)
	  sscanf(argv[++i], "%i", initstate);
	else if (strncmp(argv[i], "-help", 5) == 0){
	  do_usage();
	}
	else{
	  fprintf(stderr, "Unknown option: %s\t", argv[i]);
	  fprintf(stderr, "Try 'femxpeck -help'\n");
	}
      }
      else
	*stimfname = argv[i];
    }
  }
  else
    do_usage();
  return 1;
}

/*********************************************************************
 ** do_mappings
 *********************************************************************/
void do_mappings(int state)
{
  switch(state){
  case 1:
    perch1 = C_PERCH;
    perch2 = R_PERCH;
    xperch = L_PERCH;
    aud1 = pcm_center;
    aud2 = pcm_right;
    xaud = pcm_left;
    break;
  case 2:
    perch1 = R_PERCH;
    perch2 = C_PERCH;
    xperch = L_PERCH;
    aud1 = pcm_right;
    aud2 = pcm_center;
    xaud = pcm_left;
    break;
  case 3:
    perch1 = L_PERCH;
    perch2 = R_PERCH;
    xperch = C_PERCH;
    aud1 = pcm_left;
    aud2 = pcm_right;
    xaud = pcm_center;
    break;
  case 4:
    perch1 = R_PERCH;
    perch2 = L_PERCH;
    xperch = C_PERCH;
    aud1 = pcm_right;
    aud2 = pcm_left;
    xaud = pcm_center;
    break;
  case 5:
    perch1 = L_PERCH;
    perch2 = C_PERCH;
    xperch = R_PERCH;
    aud1 = pcm_left;
    aud2 = pcm_center;
    xaud = pcm_right;
    break;
  case 6:
    perch1 = C_PERCH;
    perch2 = L_PERCH;
    xperch = R_PERCH;
    aud1 = pcm_center;
    aud2 = pcm_left;
    xaud = pcm_right;
    break;
  }
  if(DEBUG){fprintf(stderr,"perch1:%d, perch2:%d, xperch:%d\n", perch1, perch2, xperch);}
}

/**********************************************************************
 **  main
**********************************************************************/
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
  int nclasses, nstims, stim_class, C2_stim_number,C1_stim_number, stim_reinf,offstep1,offstep2,
    stepval,subjectid, num_c1stims, num_c2stims,stimdurtest,swapval=-1,rswap,swapcnt,
    initstate=-1,trial_num,session_num,C2_pval,C1_pval,i,j,k,*C2_plist=NULL,*C1_plist=NULL,mapval=-1,
    tot_c1stims=0, tot_c2stims=0,dosunrise=0,dosunset=0,starttime,stoptime,currtime,stim_num, doPB;
  
  long tot_trial_num;
  float stimdurLB=0.0, stimdurUB=0.0, stimdur_range = 0.0, trialdur = 0.0;
  unsigned long int temp_dur,stimulus_duration,offset1,offset2,offset,sd;
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval perchtime, good_perch, perchstart, tstart, tstop, tdur;
  struct tm *loctime;
  int target = 0, perchcheck = 0, do_trial = 0;
  int no_stim_trial = 0, tot_no_stim_trial = 0;

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
    float dur;
    char name[128];
  } class[MAXCLASS], stim[MAXSTIM];
  
  snd_pcm_t *activepcm;
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
        command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &swapval, &initstate, &stimdurLB, &stimdurUB, &stimfname);
       	if(DEBUG){
	  fprintf(stderr,"command_line_parse(): box_id=%d, subjectid=%d, starthour=%d, stophour=%d, startmin=%d, stopmin=%d, swapval=%d, initstate=%d, LB=%f, UB=%f stimfile: %s\n",
		  box_id, subjectid, starthour, stophour, startmin, stopmin, swapval, initstate, stimdurLB, stimdurUB, stimfname);
	}
	if(box_id!=3){
	  fprintf(stderr,"\n\n ***************************************************************\n");
	  fprintf(stderr,    " **  FATAL ERROR!: femxperch only runs in box 3 now!!         **\n");
	  fprintf(stderr,    " **   (really, it can run in any box that has three DAC lines **\n");
	  fprintf(stderr,    " **    and the femxperch apparatus)                           **\n"); 
	  fprintf(stderr,  "\n ***************************************************************\n\n");
	  exit(-1);
	}

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
	  fprintf(stderr, "\tERROR: try 'femxpeck -help' for available options\n");
	  exit(-1);
	}
	/*make sure the stimdurs are valid*/
	if(DEBUG){printf("checking stimdurs for errors\n");}
	if((stimdurUB-stimdurLB)<=0.0){
	  fprintf(stderr, "\tERROR:You must give upper and lower bounds for the stimulus presentation duration.\n");
	  fprintf(stderr, "\t\tand the upper bound must be greater than the lower bound"); 
	  fprintf(stderr, "\tTry 'femxpeck -help' for available options\n");
	  exit(-1);
	}
	stimdur_range = stimdurUB-stimdurLB;
	if(DEBUG){printf("stimdur range: %f\n",stimdur_range);}
	stimdurtest=((int)(stimdur_range*100))%25;
	if(DEBUG){printf("stimdur test value: %d\n",stimdurtest);}
	if(stimdurtest !=0){ 
	  fprintf(stderr, "\tERROR:Upper and lower stim durations must be multiples of .25 sec.\n");
	  exit(-1);
	}
	
	/*set some variables as needed*/
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
	  printf("Initializing box %d ......", box_id);
	}
	if((pcm_left = do_setup(L_PCM))<0){
	  fprintf(stderr,"FAILED to set up the pcm device %s\n", L_PCM);
	  exit (-1);}
	if((pcm_center = do_setup(C_PCM))<0){
	  fprintf(stderr,"FAILED to set up the pcm device %s\n", C_PCM);
	  exit (-1);}
	if((pcm_right=do_setup(R_PCM))<0){
	  fprintf(stderr,"FAILED to set up the pcm device %s\n", R_PCM);
	  exit (-1);}
	if (operant_open()!=0){
	  fprintf(stderr, "Problem opening IO interface\n");
	  snd_pcm_close(pcm_left); snd_pcm_close(pcm_center); snd_pcm_close(pcm_right);
	  exit (-1);
	}
	if(DEBUG){fprintf(stderr, "pcm_left handle: %d,\t pcm_center handle: %d,\t  pcm_right handle:%d\n", 
			  (int)pcm_left,(int)pcm_center, (int)pcm_right);}
	/* figure out the start state */
	do_mappings(initstate);
	mapval=fmod(initstate,2);
	if(DEBUG){fprintf(stderr, "mapval; %d\n", mapval);}

	operant_clear(box_id);
	if(DEBUG){fprintf(stderr, "box intialized!\n");}
	if(DEBUG){fprintf(stderr, "aud1 handle: %d,\t aud2 handle: %d,\t  xaud handle:%d\n", 
			  (int)aud1,(int)aud2, (int)xaud);}
	/* give user some feedback*/
	fprintf(stderr, "Loading stimuli from file '%s' for session in box '%d' \n", stimfname, box_id); 
	fprintf(stderr, "Subject ID number: %i\n", subjectid);

	/* Read in the list of exemplars */
	nstims = num_c1stims = num_c2stims = 0;
	nclasses=0;
	if ((stimfp = fopen(stimfname, "r")) != NULL){
	  while (fgets(buf, sizeof(buf), stimfp))
	    nstims++;
	  fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
	  rewind(stimfp);
	  
	  for (i = 0; i < nstims; i++){
	    fgets(buf, 128, stimfp);
	    temp_dur=0;
	    sscanf(buf, "\n\n%d\%s\%d", &tmp.class, tmp.exemplar, &tmp.freq);
	    if(DEBUG){fprintf(stderr, "%d %s %d\n", tmp.class, tmp.exemplar, tmp.freq);}
	    if(tmp.freq==0){
	      printf("ERROR: insufficnet data or bad format in '.stim' file. Try 'femxperch -help'\n");
	      exit(0);} 
	    /* count stimulus classes*/
	    if (nclasses<tmp.class){nclasses=tmp.class;}
	    if (DEBUG){printf("nclasses set to: %d\n", nclasses);}
	    if(tmp.class>2)
	      fprintf(stderr, "stimulus '%s' has an invalid class. Must be 1 or 2.\n", tmp.exemplar);	    
	    sprintf(fullsfname,"%s%s", STIMPATH, tmp.exemplar);                                /* add full path to file name */
	    
	    /*verify the soundfile*/
	    if(DEBUG){printf("trying to verify %s\n",fullsfname);}
	    if((temp_dur = verify_soundfile(fullsfname))<1){
	      fprintf(stderr, "Unable to verify %s!\n",fullsfname );  
	      snd_pcm_close(pcm_left); snd_pcm_close(pcm_center); snd_pcm_close(pcm_right);
	      exit(0); 
	    }
	    if(DEBUG){printf("soundfile %s verified, duration: %lu\n", tmp.exemplar, temp_dur);}
	    strcpy (stim[i].name, tmp.exemplar);
	    
	    switch(tmp.class){
	    case 1:
	      if (DEBUG){printf("adding class 1 soundfile to stimlist\n");}
	      num_c1stims++;
	      C1stim[num_c1stims-1].class = tmp.class;
	      strcpy (C1stim[num_c1stims-1].exemplar, tmp.exemplar);
	      C1stim[num_c1stims-1].freq = tmp.freq;
	      C1stim[num_c1stims-1].dur = temp_dur;
	      C1stim[num_c1stims-1].num = i;
	      tot_c1stims += C1stim[num_c1stims-1].freq;
	      break;
	    case 2:
	      if (DEBUG){printf("adding class 2 soundfile to stimlist\n");}
	      num_c2stims++;
	      C2stim[num_c2stims-1].class = tmp.class;
	      strcpy (C2stim[num_c2stims-1].exemplar, tmp.exemplar);
	      C2stim[num_c2stims-1].freq = tmp.freq;
	      C2stim[num_c2stims-1].dur = temp_dur;
	      C2stim[num_c2stims-1].num = i;
	      tot_c2stims += C2stim[num_c2stims-1].freq;
	      break;
	    }
	    if(DEBUG){printf("class1 stims: %d \ttotal c1stims to play: %d\n", num_c1stims, tot_c1stims);}
	    if(DEBUG){printf("class2 stims: %d \ttotal c2stims to play: %d\n", num_c2stims, tot_c2stims);}
	  }
	}
	else{ 
	  printf("Error opening stimulus input file! Try 'femxperch -help' for proper file formatting.\n");  
	   snd_pcm_close(pcm_left); snd_pcm_close(pcm_center); snd_pcm_close(pcm_right);
	  exit(0);	  
	}
	fclose(stimfp);
	if(DEBUG){printf("Done reading in stims; %d stims in %d classes found\n", nstims, nclasses);}

	if(DEBUG){
	  for(i=0;i<num_c1stims;i++){
	    printf("c1:%d\tclass:%d\tname:%s\tfreq:%d\tdur:%lu\n", i, C1stim[i].class, C1stim[i].exemplar, 
		   C1stim[i].freq, C1stim[i].dur);}
	  for(i=0;i<num_c2stims;i++){
	    printf("c2:%d\tclass:%d\tname:%s\tfreq:%d\tdur:%lu\n", i, C2stim[i].class, C2stim[i].exemplar, 
		   C2stim[i].freq, C2stim[i].dur);}
	}

	/* build the stimulus playlists */
	if(DEBUG){printf("Making the playlist for stimclass 1\n");}
	free(C1_plist);
	C1_plist = malloc( (tot_c1stims+1)*sizeof(int) );
	i=j=0;
	for (i=0;i<num_c1stims; i++){
	  k=0;
	  for(k=0;k<C1stim[i].freq;k++){
	    C1_plist[j]=i;
	    if(DEBUG){printf("value for class 1 playlist entry '%d' is '%d'\n", j, i);}
	    j++;
	  }
	}

	if(DEBUG){printf("Making the playlist for stimclass 2\n");}
	free(C2_plist);
	C2_plist = malloc( (tot_c2stims+1)*sizeof(int) );
	i=j=0;
	for (i=0;i<num_c2stims; i++){
	  k=0;
	  for(k=0;k<C2stim[i].freq;k++){
	    C2_plist[j]=i;
	    if(DEBUG){printf("value for class 2 playlist entry '%d' is '%d'\n", j, i);}
	    j++;
	  }
	}

	/*zero out the stimulus and class data counters*/
	for(i = 0; i<nstims;++i)
	  stim[i].trials = stim[i].dur = 0;
	if (DEBUG){printf("stimulus counters zeroed!\n");} 
	for(i=0;i<nclasses;i++)
	  class[i].trials = class[i].dur = 0;
	if (DEBUG){printf("class counters zeroed!\n");} 
	strcpy(class[0].name, "silence");
	strcpy(class[1].name, "class1"); 
	strcpy(class[2].name, "class2");	
	
	/*  Open & setup data logging files */
	curr_tt = time (NULL);
	loctime = localtime (&curr_tt);
	strftime (timebuff, 64, "%d%b%y", loctime);
	sprintf (stimftemp, "%s", stimfname);
	stimfroot = strtok (stimftemp, delimiters); 
	sprintf(datafname, "%i_%s.femxperch_rDAT", subjectid, stimfroot);
	sprintf(dsumfname, "%i.summaryDAT", subjectid);
	datafp = fopen(datafname, "a");
        dsumfp = fopen(dsumfname, "w");
	
	if ( (datafp==NULL) || (dsumfp==NULL) ){
          fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", datafname);
	  snd_pcm_close(pcm_left); snd_pcm_close(pcm_center); snd_pcm_close(pcm_right);
	  fclose(datafp);
	  fclose(dsumfp);
	  exit(-1);
        }

	/* Write data file header info */
	printf ("Data output to '%s'\n", datafname);
	fprintf (datafp, "File name: %s\n", datafname);
	fprintf (datafp, "Procedure source: %s\n", exp_name);
	fprintf (datafp, "Start time: %s", asctime(loctime));
	fprintf (datafp, "Subject ID: %d\n", subjectid);
	fprintf (datafp, "Stimulus source: %s\n", stimfname);  
	fprintf (datafp, "Sess#\ttTrl#\tsTrl#\tSwap\tStimulus\tOffset\tDuration\tClass\tTrl_dur\tTOD\tDate\n");


   /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
   ********************************************/
	session_num = 1;
	trial_num = 0;
	rswap = 0; swapcnt = 1;
	tot_trial_num = 0;
	tot_no_stim_trial=0;
	no_stim_trial=0;
	
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
	    if(DEBUG){printf("\n\n ***********\n TRIAL START\n");}
	    if (DEBUG==2){printf("minutes since midnight at loop start: %d\t starttime: %d\tstoptime: %d\n",
                              currtime,starttime,stoptime);}
	    
	    /*cue two randomly chosen stimulus source files, one from each playlist */
	    srand(time(0));
	    C1_pval = (int) ((tot_c1stims+0.0)*rand()/(RAND_MAX+0.0));       /* select playlist1 entry at random */ 
	    C2_pval = (int) ((tot_c2stims+0.0)*rand()/(RAND_MAX+0.0));       /* select playlist2 entry at random */ 
	    C1_stim_number = C1_plist[C1_pval];
	    C2_stim_number = C2_plist[C2_pval];
	    if (DEBUG){printf("cued stim for c1: %s\t c2: %s\n", C1stim[C1_stim_number].exemplar, C2stim[C2_stim_number].exemplar);} 
	    
	    
	    /* Wait for landing on one of the active perches */
	    if (DEBUG){printf("Waiting for the bird to land on a perch\n");}
	    operant_write (box_id, HOUSELT, 1);        /* house light on */
	    trial.perch1= trial.perch2 = trial.xperch = target = 0;
	    do{                                         
	      nanosleep(&rsi, NULL);	               	       
	      trial.perch1 = operant_read(box_id, perch1);   /*check perchs*/	
	      trial.perch2 = operant_read(box_id, perch2);   
	      trial.xperch = operant_read(box_id, xperch);   
	    }while ((trial.perch1==0) && (trial.perch2==0) && (trial.xperch==0));

	    /*check to be sure the bird stays on the same perch for around 1 sec before starting the trial*/
	    if(trial.perch1) target=perch1; else if(trial.perch2) target=perch2; else target=xperch;
	    gettimeofday(&perchstart, NULL);
	    timeradd (&perchstart, &min_perch, &good_perch);
	    do{
	      nanosleep(&rsi, NULL);
	      perchcheck = operant_read(box_id, target);
	      gettimeofday(&perchtime, NULL);
	    }while ( (perchcheck==1) && (timercmp(&perchtime, &good_perch, <) ) ); 
	  
	    if (perchcheck){ 
	      do_trial=1;    
	      if (DEBUG){printf("VAILD TRIAL --- perch is %d\n", target);}
	    }
	    else {
	      do_trial=0;
	      if (DEBUG){printf("FALSE START TRIAL, bird left perch '%d' too early\n", target);}
	    }
	    
	    if(do_trial){
	      ++trial_num;++tot_trial_num;
	      /*set your trial variables*/
	      if(rswap){
		if(trial.perch1){ /*perch 1 trial */
		  if (DEBUG){printf("***ON PERCH 1***(SWAP)\n");}
		  stim_class = C1stim[C1_stim_number].class;                              
		  strcpy (stimexm, C1stim[C1_stim_number].exemplar);  
		  stimulus_duration = C1stim[C1_stim_number].dur;                    
		  stim_reinf = C1stim[C1_stim_number].reinf;
		  stim_num = C1stim[C1_stim_number].num;
		  sprintf(fstim,"%s%s", STIMPATH, stimexm); 
		  activepcm = aud1;
		  doPB=1;
		}
		else if(trial.perch2){ /* perch 2 trial */
		  if (DEBUG){printf("***ON PERCH 2***(SWAP)\n");}
		  stim_class = C2stim[C2_stim_number].class;                              
		  strcpy (stimexm, C2stim[C2_stim_number].exemplar);                      
	          stimulus_duration = C2stim[C2_stim_number].dur;                    
		  stim_reinf = C2stim[C2_stim_number].reinf;
		  stim_num = C2stim[C2_stim_number].num;
		  sprintf(fstim,"%s%s", STIMPATH, stimexm);
		  activepcm = aud2;
		  doPB=1;
		}
		else{/*xperch trial*/
		  activepcm = xaud;
		  doPB=0;
		  stim_class =0;
		  stimulus_duration=0.0;
		  offset=0;
		  strcpy(stimexm, "no_stim");
		  if (DEBUG){printf("***ON INACTIVE PERCH ***(SWAP)\n");}
		}
	      }
	      else{
		if(trial.perch2){
		  if (DEBUG){printf("***ON PERCH 2***(NO SWAP)\n");}
		  stim_class = C1stim[C1_stim_number].class;                              
		  strcpy (stimexm, C1stim[C1_stim_number].exemplar);                      
		  stimulus_duration = C1stim[C1_stim_number].dur;                    
		  stim_reinf = C1stim[C1_stim_number].reinf;
		  stim_num = C1stim[C1_stim_number].num;
		  sprintf(fstim,"%s%s", STIMPATH, stimexm);                               
		  activepcm= aud2;
		  doPB=1;
		}
		else if (trial.perch1){
		  if (DEBUG){printf("***ON PERCH 1***(NO SWAP)\n");}
		  stim_class = C2stim[C2_stim_number].class;                              
		  strcpy (stimexm, C2stim[C2_stim_number].exemplar);                      
	          stimulus_duration = C2stim[C2_stim_number].dur;                    
		  stim_reinf = C2stim[C2_stim_number].reinf;
		  stim_num = C2stim[C2_stim_number].num;
		  sprintf(fstim,"%s%s", STIMPATH, stimexm);                               
		  activepcm = aud1;
		  doPB=1;
		}
		else{ /* xperch trial */
		  activepcm = xaud;
		  doPB=0;
		  stim_class =0;
		  stimulus_duration=0.0;
		  offset=0;
		  strcpy(stimexm, "no_stim");
		  if (DEBUG){printf("***ON INACTIVE PERCH ***(NO SWAP)\n");}
		}
	      }
	      if(DEBUG){
		printf("stim class: %d\t", stim_class);
		printf("stimulus file name: %s\n", stimexm);
        printf("doPB: %d\n", doPB);
	      }
	      
          sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	      
	      /* Play stimulus file */

	      gettimeofday(&tstart, NULL);
	      if(DEBUG){printf("****** STARTING %s PLAYBACK ....\t", stimexm);}
	      if(doPB){
		if (playall_and_watch(activepcm, fstim, box_id, target)!=1){
		  fprintf(stderr, "play_and_watch error on stimfile:%s. Program aborted %s\n", 
			  stimexm, asctime(localtime (&curr_tt)) );
		  fprintf(datafp, "play_and_watch error on stimfile:%s. Program aborted %s\n", 
			  stimexm, asctime(localtime (&curr_tt)) );
		  fclose(datafp);
		  fclose(dsumfp); 
		  snd_pcm_close(pcm_left); snd_pcm_close(pcm_center); snd_pcm_close(pcm_right);
		  exit(-1);
		}
	      }

	      if (DEBUG){printf("PLAYBACK COMPLETE\n");}

	      /* wait til bird leaves the perch*/
	      while(operant_read(box_id, target));{
		nanosleep(&rsi, NULL);
	      }
	      gettimeofday(&tstop, NULL);
	      timersub(&tstop, &tstart, &tdur);
	      trialdur = (float)tdur.tv_sec + (float)(tdur.tv_usec/1000000.0);
	      if(DEBUG){printf("BIRD JUST LEFT THE PERCH\t trialdur is: %.4f\n", trialdur);}

	     

	      /* note time that trial ends */
	      curr_tt = time (NULL); 
	      loctime = localtime (&curr_tt);                     /* date and wall clock time of trial*/
	      strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
	      strftime (min, 16, "%M", loctime);
	      strftime (month, 16, "%m", loctime);
	      strftime (day, 16, "%d", loctime);
	      currtime=(atoi(hour)*60)+atoi(min);
	      
	      /*update the data counters*/
	      ++class[stim_class].trials; 
	      class[stim_class].dur += trialdur;
	      if(doPB){
		++stim[stim_num].trials; 
		stim[stim_num].dur += trialdur; 
		++swapcnt;
	      }
	      else{
		++no_stim_trial;
		++tot_no_stim_trial;
	      }
	      
	      /* Pause for ITI */
	      operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	      nanosleep(&iti, NULL);                     /* wait intertrial interval */
	      if (DEBUG){printf("ITI passed\n");}
	      
	      /* Write trial data to output file */
	      strftime (tod, 256, "%H%M", loctime);
	      strftime (date_out, 256, "%m%d", loctime);
	      fprintf(datafp, "%d\t%lu\t%d\t%d\t%s\t%lu\t%lu\t%d\t%.4f\t%s\t%s\n",
		      session_num,tot_trial_num,trial_num,rswap,stimexm,offset,stimulus_duration,stim_class,trialdur,tod,date_out );
	      fflush (datafp);
	      if (DEBUG){
		printf("%d\t%lu\t%d\t%d\t%s\t%lu\t%lu\t%d\t%.4f\t%s\t%s\n", 
		       session_num,tot_trial_num,trial_num,rswap,stimexm,offset,stimulus_duration,stim_class,trialdur,tod,date_out );
	      }

	      
	      /* Update summary data */ 
	      if(freopen(dsumfname,"w",dsumfp)!= NULL){
		fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
		fprintf (dsumfp, "SESSION TOTALS BY STIMULUS CLASS\n\n");
		fprintf (dsumfp, "\t\tClass \tTrials \tDuration\n");
		for (i = 0; i<=nclasses;++i){
		  fprintf (dsumfp, "\t\t%s  \t %d    \t %.4f\n", 
			   class[i].name, class[i].trials, class[i].dur);
		}
	      
		fprintf (dsumfp, "\n\n\n\tSESSION TOTALS BY SOURCE SOUNDFILE\n");
		fprintf (dsumfp, "\t\tSoundfile \tTrials \tDuration\n");
		for (i = 0; i<nstims;++i){
		  fprintf (dsumfp, "\t\t%s  \t %d    \t %.4f\n", 
			   stim[i].name, stim[i].trials, stim[i].dur);
		}
		
		fprintf (dsumfp, "\n\n\nLast trial run @: %s\n", asctime(loctime) );
		fprintf (dsumfp, "Trials this session: %d\n",trial_num);
		fflush (dsumfp);
		
	      }
	      else
		fprintf(stderr, "ERROR!: problem re-opening summary output file!: %s\n", dsumfname);
	      
	      if (DEBUG){printf("trial summaries updated\n");}
	      
	      
	      /* End of trial chores */
	      sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);        /* unblock termination signals */ 
	      C1_stim_number = C2_stim_number = -1;                /* reset the stim number for correct trial*/
	      if (DEBUG){printf("****end of trial\n\t swapcount:%d\tswapval:%d\trswap:%d\n",swapcnt,swapval,rswap);}
	      if(swapcnt==swapval){                                /*check if you should swap the stim-resp pairings */ 
		if (DEBUG){printf("swapped the contingencies\n");}
		if(mapval){
		  do_mappings(initstate-1); 
		  mapval=0;
		}
		else{ 
		  do_mappings(initstate+1);
		  mapval=1;
		}
		if(rswap) rswap=0; else rswap=1;
		swapcnt=1;
	      }
	      if(DEBUG){printf("minutes since midnight at end of trial: %d\n", currtime);}
	    }
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
	  trial_num = 0;
	  no_stim_trial=0;

	  if (DEBUG){printf("swapcount:%d\tswapval:%d\trswap:%d\n",swapcnt,swapval,rswap);}
	  if(swapval==0){                            /*check if we need to swap the stim-perch pairings for the new session */	
	    if(mapval){
	      do_mappings(initstate-1); 
	      mapval=0;
	    }
	    else{
	      do_mappings(initstate+1);
	      mapval=1;
	    }
	    if(rswap) rswap=0; else rswap=1;
	    swapcnt=0; 
	  }
	  

	}while (1);// main loop
	
	curr_tt = time(NULL);
	
	
	/*  Cleanup */
	fclose(datafp);
	fclose(dsumfp);
	snd_pcm_close(pcm_left); snd_pcm_close(pcm_center); snd_pcm_close(pcm_right);
	return 0;
}                         

