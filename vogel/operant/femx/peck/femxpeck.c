
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
#define DEF_REF                  5             /* default reinforcement for corr. resp. set to 100% */
#define STIMPATH       "/usr/local/stimuli/"
#define HOPPER_DROP_MS           300           /*time for hopper to fall before checking that it did */
#define MAXFILESIZE         26460000           /*mximum nuber of samples in any soundfile: 10 min at 44.1kHz */

long feed_duration = FEED_DURATION;
int  trial_max = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME; 
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval = SLEEP_TIME;
int  reinf_val = DEF_REF;
const char exp_name[] = "FEMXPECK";
int box_id = -1;

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
 * extract a stimulus from the soundfile, output sound, and count pecks during playback
 **************************************************************************************/
int play_and_count(char *sfname, double period, int stimdur, int stimoffset, int box_id ,struct PECK * trl)
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
  struct PECK old, new;

  old.left=trl->left;
  old.right=trl->right;
  old.center=trl->center;

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
  if(DEBUG){printf("offset  is %d msec, which is %d samples\n", stimoffset, (int) offset );}
  tmp = sf_seek(sf, offset, SEEK_SET);
  if(DEBUG){printf("trying to seek by %d samples; sf_seek returned %d \n", (int)offset, tmp);}
  maxframes = (int)sfinfo->frames;
  if (stimdur==-1) /* play the whole soundfile (less any offset)*/
    inframes = maxframes - (int)offset;
  else if(stimdur>0)/* play the define stimulus duration */
    inframes = (int) rint((float)sfinfo->samplerate*(float)stimdur/1000.0);
  else{
    fprintf (stderr, "Invalid stimulus duration '%d'\n",stimdur);
    sf_close(sf);free(sfinfo);return -1;
  }
  if(DEBUG){printf("stimdur  is %d msec, so I want %d samples after the offset\n", stimdur, (int) inframes );}
  
  obuff = (short *) malloc(sizeof(int)*inframes);
  if( ( maxframes - (offset + inframes) ) <0){/* read the data */
    fprintf (stderr, "The sum of the stimulus duration and offset exceeds the soundfile length\n");
    sf_close(sf);free(sfinfo);free(obuff);return -1;
  }
  if(DEBUG){printf("trying to sf_readf %d frames\n",(int)inframes);}
  incount = sf_readf_short(sf, obuff, inframes);
  if(DEBUG){printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount, (int)inframes);}
  
  /* ramp the first (and last) 50 ms from zero to normal amplitude*/
  ramp_dur= ramp_time * sfinfo->samplerate; //get number of samples for ramp
  for (i = 0; i<ramp_dur; i++)
    obuff[i] = obuff[i]*((float)i/(float)ramp_dur);
  for (i = (incount-ramp_dur); i<=incount; i++) 
    obuff[i] = obuff[i]* ((float)(incount-i)/(float)ramp_dur);
  
  outframes = inframes;
  if(DEBUG){printf("I'll try to write %d frames\n", (int)outframes);}
  
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
    if (snd_pcm_state(handle) == SND_PCM_STATE_RUNNING)
      init = 0;
    /*get key values now*/  
    new.right = operant_read(box_id, RIGHTPECK);    /*get value at right peck position*/
    new.left = operant_read(box_id, LEFTPECK);      /*get value at left peck position*/
    new.center = operant_read(box_id, CENTERPECK);  /*get value at left peck position*/
    /*find zero-to-one key transition: this means you saw a response start*/  
    if(new.right > old.right)
      trl->right++;
    else if(new.left > old.left)
      trl->left++;
    else if (new.center > old.center)
      trl->center++;
    old=new;
    /* if(DEBUG){printf("in play_and_count L-C-R peckcounts: %d-%d-%d\n",
       trl->left, trl->center, trl->right);}*/
    totframesout += err; 
    ptr += err * channels;
    outframes -= err;
    /* if(DEBUG){printf("outframes is now %d\n", (int) outframes);}*/
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
  sf_close(sf);
  free(sfinfo);
  sfinfo=NULL;
  free(obuff);
  obuff=NULL;
  free(ufds);
  ufds=NULL;
  return (1); /*successful playback*/
}


/**********************************************************************
 **********************************************************************/
void do_usage()
{
  fprintf(stderr, "femxpeck usage:\n");
  fprintf(stderr, "    femxpeck [-help] [-B int] [-lb float] [-ub float] [-on int] [-off int] [-S <subject number>] <filename>\n\n");
  fprintf(stderr, "        -help          = show this help message\n");
  fprintf(stderr, "        -B int         = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "        -lb float      = lower bound (in secs) for stimulus duration. eg. 7.25\n");
  fprintf(stderr, "        -ub float      = upper bound (in secs) for stimulus duration. eg. 15.0 \n");
  fprintf(stderr, "                         NOTE: LB and UB should use .25 sec resolution because the\n");
  fprintf(stderr, "                           stimulus durations are uniformly distributed between LB and UB\n");
  fprintf(stderr, "                           in 250 msec steps\n");
  fprintf(stderr, "        -on int:int    = set hour:min for exp to start eg: '-on 7:30' (default is 7:00 AM)\n");
  fprintf(stderr, "        -off int:int   = set hour for exp to stop eg: '-off 19:45' (default is 7:00 PM)\n");
  fprintf(stderr, "                         To use daily sunset or sunrise times set 'on' or 'off' to '99'\n");
  fprintf(stderr, "        -swap int      = set the number of trials between stim-resp swap\n");
  fprintf(stderr, "                         use '-1' to never swap(default), '0' to swap each session\n");
  fprintf(stderr, "                         \n");
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
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour,int *startmin, int *stopmin, int *swapval, float *stimdurLB, float *stimdurUB, char **stimfname)
{
  int i=0;
  
  for (i = 1; i < argc; i++){
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
 **  main
**********************************************************************/
int main(int argc, char *argv[])
{
	FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
	char *stimfname = NULL;
	char *stimfroot, fullsfname[256];
	const char delimiters[] = " .,;:!-";
	char datafname[128], hour [16], min[16], month[16], year[16], 
	  day[16], dsumfname[128], stimftemp[128], pcm_name[128];
	char  buf[128], stimexm[128],fstim[256],temphour[16],tempmin[16],
	  timebuff[64], tod[256], date_out[256], buffer[30];
	int nclasses, nstims, stim_class, C2_stim_number,C1_stim_number, stim_reinf,offstep1,offstep2,stepval, 
	  subjectid, period, num_c1stims, num_c2stims,stimdurtest,swapval=-1,rswap,swapcnt,
	  trial_num, session_num, C2_pval,C1_pval, i,j,k, *C2_plist=NULL, *C1_plist=NULL, 
	  tot_c1stims=0, tot_c2stims=0,dosunrise=0,dosunset=0,starttime,stoptime,currtime,stim_num;
	long tot_trial_num;
	float stimdurLB=0.0, stimdurUB=0.0, stimdur_range = 0.0;
	unsigned long int temp_dur,stimulus_duration,offset1,offset2,offset,sd;
	float latitude = 32.82, longitude = 117.14;
	time_t curr_tt, rise_tt, set_tt;
	struct tm *loctime;
	int fed = 0;
	Failures f = {0,0,0,0};
	int reinfor_sum = 0, reinfor = 0;
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
        command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &swapval, &stimdurLB, &stimdurUB, &stimfname);
       	if(DEBUG){
	  fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, starthour=%d, stophour=%d, startmin=%d, stopmin=%d, swapval=%d, LB=%f, UB=%f stimfile: %s\n",
		  box_id, subjectid, starthour, stophour, startmin, stopmin, swapval, stimdurLB, stimdurUB, stimfname);
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
	  printf("Initializing box %d ...\n", box_id);
	  printf("trying to execute setup(%s)\n", pcm_name);
	}
	if((period=do_setup(pcm_name))<0){
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
	    sscanf(buf, "%d\%s\%d\%d", &tmp.class, tmp.exemplar, &tmp.freq, &tmp.reinf);
	    if(DEBUG){fprintf(stderr, "%d %s %d %d\n", tmp.class, tmp.exemplar, tmp.freq, tmp.reinf);}
	    if((tmp.freq==0) || (tmp.reinf==0)){
	      printf("ERROR: insufficnet data or bad format in '.stim' file. Try 'femxpeck -help'\n");
	      exit(0);} 
	    /* count stimulus classes*/
	    if (nclasses<tmp.class){nclasses=tmp.class;}
	    if (DEBUG){printf("nclasses set to: %d\n", nclasses);}
	    if(tmp.class>2)
	      fprintf(stderr, "stimulus '%s' has an invalid class. Must be 1 or 2.\n", tmp.exemplar);
	    /*check the reinforcement rates */
	    if (tmp.reinf >100)
	      fprintf(stderr, "Food reinforcement rate of %d%% for %s is invalid. Must be less than or equal to 100%%\n", 
		      tmp.reinf, tmp.exemplar);
	    else 
	      fprintf(stderr, "Food access is available %d%% of the time following %s\n",tmp.reinf, tmp.exemplar);
	    
	    sprintf(fullsfname,"%s%s", STIMPATH, tmp.exemplar);                                /* add full path to file name */
	    
	    if(DEBUG){printf("\n\ntrying to verify %s\n",fullsfname);}

	    if((temp_dur = verify_soundfile(fullsfname))<1){
	      fprintf(stderr, "Unable to verify %s!\n",fullsfname );  
	      snd_pcm_close(handle);
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
	      C1stim[num_c1stims-1].reinf = tmp.reinf;
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
	      C2stim[num_c2stims-1].reinf = tmp.reinf;
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
	  printf("Error opening stimulus input file! Try 'femxpeck -help' for proper file formatting.\n");  
	  snd_pcm_close(handle);
	  exit(0);	  
	}
        
	fclose(stimfp);
	if(DEBUG){printf("Done reading in stims; %d stims in %d classes found\n", nstims, nclasses);}


	if(DEBUG){
	  for(i=0;i<num_c1stims;i++){
	    printf("c1:%d\tclass:%d\tname:%s\tfreq:%d\treinf:%d\tdur:%lu\n", i, C1stim[i].class, C1stim[i].exemplar, 
		   C1stim[i].freq,C1stim[i].reinf, C1stim[i].dur);}
	  for(i=0;i<num_c2stims;i++){
	    printf("c2:%d\tclass:%d\tname:%s\tfreq:%d\treinf:%d\tdur:%lu\n", i, C2stim[i].class, C2stim[i].exemplar, 
		   C2stim[i].freq,C2stim[i].reinf, C2stim[i].dur);}
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
	  stim[i].trials = stim[i].rLFT = stim[i].rCTR = stim[i].rRGT = 0;
	if (DEBUG){printf("stimulus counters zeroed!\n");} 
	for(i=0;i<nclasses;i++)
	  class[i].trials = class[i].rLFT = class[i].rCTR = class[i].rRGT = 0;
	if (DEBUG){printf("class counters zeroed!\n");} 
	strcpy(class[1].name, "LEFT"); 
	strcpy(class[2].name, "RIGHT");	
	
	/*  Open & setup data logging files */
	curr_tt = time (NULL);
	loctime = localtime (&curr_tt);
	strftime (timebuff, 64, "%d%b%y", loctime);
	sprintf (stimftemp, "%s", stimfname);
	stimfroot = strtok (stimftemp, delimiters); 
	sprintf(datafname, "%i_%s.femxpeck_rDAT", subjectid, stimfroot);
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
	printf ("Data output to '%s'\n", datafname);
	fprintf (datafp, "File name: %s\n", datafname);
	fprintf (datafp, "Procedure source: %s\n", exp_name);
	fprintf (datafp, "Start time: %s", asctime(loctime));
	fprintf (datafp, "Subject ID: %d\n", subjectid);
	fprintf (datafp, "Stimulus source: %s\n", stimfname);  
	fprintf (datafp, "Sess#\ttTrl#\tsTrl#\tSwap\tStimulus\tOffset\tDuration\tClass\tLeft\tCenter\tRight\tReinf\tTOD\tDate\n");


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

