/* 
8/10/08:EC
adapted from femx
plays 3-channel stereo sound stimuli
NOTE:requires three audio channels in a SINGLE DAC (see ALSA file asound.conf), cue lights
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

#define DEBUG 0



/*----- AUDIO OUTPUT CHANNELS -----*/
#define TEST_PCM "test"
/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             1             /* seconds that bird must stay on the same perch to start playback (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  1024          /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 256            /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define TDOFFSET                 0.5           /* offset between masker playback and target/distractor onset */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define MAXFILESIZE         26460000           /*maximum nuber of samples in any soundfile: 10 min at 44.1kHz */

snd_pcm_t *pcm_test,*aud1, *aud2, *xaud;
unsigned int channels = 3;                      /* count of channels on a single DAC*/
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
  snd_pcm_close(pcm_test);
  fprintf(stdout,"closed pcm devices: term signal caught: exiting\n");
  exit(-1);
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
/**********************************************************************
 ** parse the command line
 **********************************************************************/
int command_line_parse(int argc, char **argv, char **handle, char **wav1, char **wav2, char **wav3)
{
  int i=0;
  if(DEBUG)printf("argc is %d\n", argc);
  
  if(argc>1){
    for (i = 1; i < argc; i++){
      if(DEBUG){fprintf(stderr, "argv[%d] was: %s\n", i, argv[i]);}
      *handle=argv[i];
    }
  }


/**********************************************************************
 **  main
**********************************************************************/
int main(int argc, char *argv[])
{
  float ramp_time = 0.05;  /*onset offset ramp duration in secs */
  int n_speak = 3;
  SNDFILE *sf;
  SF_INFO *sfinfo;
  short *obuff, *obuffR, *obuffC, *obuffL;
  unsigned short *ptr;
  sf_count_t incount;
  double inframes, inframesm, inframesd, inframest, maxframes;
  int tmp;
  snd_pcm_uframes_t outframes, totframesout;
  int i, err, count, init;
  int ramp_dur=0;
  int offset = TDOFFSET;
  int masksr, targsr, distsr;
  int paddur;
  int fs = 44100;
  
  struct pollfd *ufds;

   /* memory for SF_INFO structures */
  sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));
  
  /* open input files*/
  /* open wav1 */
  
  if(!(sf = sf_open(wav1,SFM_READ,sfinfo))){
    fprintf(stderr,"error opening input file %s\n",wav1);
    free(sfinfo);
    return -1;
  }
  wav1sr= sfinfo ->samplerate;
  inframesm = (int)sfinfo->frames;
  obuff1 = (short *) malloc(sizeof(int)*inframesm);
  if(DEBUG==3){printf("trying to sf_readf %d frames\n",(int)inframesm);}
  incount = sf_readf_short(sf, obuff1, inframesm);
  if(DEBUG==3){printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount, (int)inframesm);}
  
  
  /* open target */
  inframes=incount=ramp_dur=ramp_time=0;
  sf=[];
  if(!(sf = sf_open(targname,SFM_READ,sfinfo))){
    fprintf(stderr,"error opening input file %s\n",targname);
    free(sfinfo);
    return -1;
  }

  obuff2 = (short *) malloc(sizeof(int)*inframes);
  if(DEBUG==3){printf("trying to sf_readf %d frames\n",(int)inframest);}
  incount = sf_readf_short(sf, obufft, inframest);
  if(DEBUG==3){printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount, (int)inframest);}
  
 
  /* open distractor */
  incount=ramp_dur=ramp_time=0;
  sf=[];
  if(!(sf = sf_open(distname,SFM_READ,sfinfo))){
    fprintf(stderr,"error opening input file %s\n",distname);
    free(sfinfo);
    return -1;
  }
 
  obuffd = (short *) malloc(sizeof(int)*inframesd);
  if(DEBUG==3){printf("trying to sf_readf %d frames\n",(int)inframesd);}
  incount = sf_readf_short(sf, obuffd, inframesd);
  if(DEBUG==3){printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount, (int)inframesd);}
  
 
  /* combine individual buffers into obuff */
   

  for (i=0; i<(n_speak*inframesm); n_speak){ /* not sure if i can do this in C */
    for (j=0; j<inframesm; j++){
    obuff[i] = obuff1[j];
    obuff[i+1] = obuff2 [j];
    obuff[i+2] = obuff3 [j];
    }
  }
  
  outframes = inframesm * n_speak;
  if(DEBUG==3){printf("I'll try to write %d frames\n", (int)outframes);}

  /* prepare audio interface */
    if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use\n");
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  
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
  while ( (outframes > 0) 
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
  free(obuffm);
  obuff1=NULL;
  free(obuff1);
  obuff2=NULL;  
  free(obuff2);
  obufft=NULL;
  free(obuff3);
  obuff3=NULL;
  free(ufds);
  ufds=NULL;
}


