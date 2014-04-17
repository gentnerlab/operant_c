/* 
7-26-06:TQG
perch-hop operant preference procedure based on gentner and hulse (2000)   
bird lands on one of three perches to trigger sound playback at that location, 
only two of the three perches are active for a given bird, and the stimulus-to-perch 
mapping switches after a set number of trials.

9-2-09: MRB
Modifying to set up operant discrimination task, measuring time that perch is 
left as a dependent measure of discriminability.

10-26-09 MRB
Modifying to restrict response number of seconds.
NOTE:requires three audio channels in a single box, and perch hop apparatus 
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
#define PERCH 2
#define HOUSELT	4   



/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             1             /* seconds that bird must stay on the same perch to start playback (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  1024          /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 256            /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     15.0           /* iti in seconds */
#define LONG_ITI		 60.0 		/*  longer it if no response. */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define MAXFILESIZE         26460000           /*maximum nuber of samples in any soundfile: 10 min at 44.1kHz */
#define	CHANGE_REPS		15               /* Number of repetitions of changed stimulus before trial end. */
#define HOPPEROP        4              
#define	FEEDLIGHT		6			/* Channel for hopper light */
#define FEEDER          5           
#define HOPPER_DROP_MS      300
#define FEED_DURATION       5000000
#define TIMEOUT_DURATION    10000000
#define RESPONSE_WINDOW	    3000000

int  trial_max = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME; 
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
float intertrial = INTER_TRIAL_INTERVAL;
int  sleep_interval = SLEEP_TIME;
const char exp_name[] = "PERCHDISCRIM";
int box_id = -1;
int perch1=0, perch2=0, xperch=0;
short *changesoundvec=NULL, *backsoundvec=NULL;
long feed_duration = FEED_DURATION;
long timeout_duration = TIMEOUT_DURATION;
long pause_samples=2646;
long pause_milliseconds = 60;

struct timespec iti = {INTER_TRIAL_INTERVAL, 0};
struct timespec longiti = {LONG_ITI, 0};
struct timespec rsi = {0, RESPONSE_SAMPLE_INTERVAL};
struct timeval min_perch = {RESP_INT_SEC, RESP_INT_USEC};

snd_pcm_t *pcm, *aud1, *aud2, *xaud;
unsigned int channels = 1;                      /* count of channels on a single DAC*/
unsigned int rate = 44100;                      /* stream rate */
snd_pcm_format_t format = SND_PCM_FORMAT_S16;   /* sample format */
unsigned int buffer_time = 500000;              /* ring buffer length in us */
unsigned int period_time = 100000;              /* period time in us */
int resample = 1;                               /* enable alsa-lib resampling */
int period =0;
long unsigned int back_frames, change_frames;

typedef struct {
  int hopper_failures;
  int hopper_wont_go_down_failures;
  int hopper_already_up_failures;
  int response_failures;
} Failures;


snd_pcm_sframes_t buffer_size;
snd_pcm_sframes_t period_size;
snd_output_t *output = NULL;

/* -------- Signal handling --------- */
int client_fd = -1;
static void sig_pipe(int signum)
{ fprintf(stdout,"SIGPIPE caught\n"); client_fd = -1;}
static void termination_handler (int signum){
  snd_pcm_close(pcm);
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
 * take a pointer to a sound buffer, output sound to a pcm(handle). 
 * this play will watch the perch during playback and abort if bird leaves the target perch
 * returns -1 on playback error, 1 for success, 2 if bird left perch
 * during playback.
 * Also returns the time that the bird left the perch.
 **************************************************************************************/
int play_and_watch(snd_pcm_t *handle, short *obuff, int frames, int box_id, struct timeval *left_perch)
{
  if(DEBUG){printf("starting playback...\n");}
	short *ptr;
	int perch_flag;
	snd_pcm_uframes_t outframes, totframesout;
	int err, count, init;

	struct pollfd *ufds;

	outframes = frames;                     
	if (snd_pcm_prepare (handle) < 0) {
		fprintf (stderr, "cannot prepare audio interface for use\n");
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
    perch_flag=1;

	/*start the actual playback*/
	while ((outframes > 0) && (perch_flag==1))  {
      if(DEBUG){printf("outframes is: %i\n", (int) outframes);}
		if (operant_read(box_id, PERCH)) {
			perch_flag=1;}
        else{
          perch_flag=0;
          gettimeofday(left_perch, NULL);
          return 2;
          break;}
        if(DEBUG){printf("perch_flag is: %i\n", perch_flag);}
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
			}}

		if(DEBUG){
			printf("exited while writei loop, so what am I waiting for?\n");
			printf("frames not played: %d \n", (int)outframes);
			printf("frames played: %d \n", (int) totframesout);
		}

		free(ufds);
		ufds=NULL;
		return 1; /*successful playback*/
	}

	/********************************************
	 ** BUILD TEMP BUFFER. This function extracts a sound vector from a stimulus file, adding silence to the end of it
	 ********************************************/
	int buildtempbuffs(char * backstim, char * changestim)
	{
		snd_pcm_uframes_t outframes;
		SNDFILE *sfinb=NULL, *sfinc=NULL;
		SF_INFO sfin_infob, sfin_infoc;
		sf_count_t incountb, incountc;
		short *inbuffb, *inbuffc;
		int samp=0, j;

		sfin_infob.format=0;
		sfin_infoc.format=0;
		if(!(sfinb = sf_open(backstim,SFM_READ,&sfin_infob))){
			fprintf(stderr,"error opening input file %s\n",backstim);
			return -1;}

		/*read in the file */
		inbuffb = (short *) malloc(sizeof(short) * sfin_infob.frames);
		incountb = sf_readf_short(sfinb, inbuffb, sfin_infob.frames);
		sf_close(sfinb);


		if(DEBUG==1){fprintf(stderr, "samples in: %lu \n", (long unsigned int)incountb);}
		outframes = (long unsigned int)incountb + pause_samples;
        if(DEBUG){printf("preallocation outframes is %i\n", (int) outframes);}
        samp=0;
		backsoundvec = (short *) malloc(sizeof(short)*outframes);
		for (j=0;j<incountb;j++){
			backsoundvec[samp++] = inbuffb[j];}
		for (j=0;j<(int) pause_samples;j++){
			backsoundvec[samp++] = 0;}

		back_frames = (long unsigned int) outframes;

		if(DEBUG){printf("Finished building back soundvector.\n");}
        if(DEBUG){printf("frames in out: %lu.\n", back_frames);}
		free(inbuffb);

		if(!(sfinc = sf_open(changestim,SFM_READ,&sfin_infoc))){
			fprintf(stderr,"error opening input file %s\n",changestim);
			return -1;}

		/*read in the file */
		inbuffc = (short *) malloc(sizeof(int) * sfin_infoc.frames);
		incountc = sf_readf_short(sfinc, inbuffc, sfin_infoc.frames);
		sf_close(sfinc);


		if(DEBUG==1){fprintf(stderr, "samples in: %lu \n", (long unsigned int)incountc);}
		outframes = (long unsigned int)incountc + pause_samples;
        samp=0;
		changesoundvec = (short *) malloc(sizeof(short)*outframes);
		for (j=0;j<incountc;j++){
			changesoundvec[samp++] = inbuffc[j];}
		for (j=0;j<pause_samples;j++){
			changesoundvec[samp++] = 0;}
		change_frames = outframes;

		if(DEBUG){printf("Finished building change soundvector.\n");}
        if(DEBUG){printf("frames in out: %lu.\n", change_frames);}
		free(inbuffc);
		return 1;
	}
	/**********************************************************************
	 **********************************************************************/
	void do_usage()
	{
		fprintf(stderr, "perchdiscrim usage:\n");
		fprintf(stderr, "  perchdiscrim [-help] [-B int] [-on int:int] [-off int:int] [-S int] <filename>\n\n");
		fprintf(stderr, "        -help          = show this help message\n");
		fprintf(stderr, "        -B int         = Box number\n");
		fprintf(stderr, "        -on int:int    = set hour:min for exp to start eg: '-on 7:30' (default is 7:00 AM)\n");
		fprintf(stderr, "        -off int:int   = set hour for exp to stop eg: '-off 19:45' (default is 7:00 PM)\n");
		fprintf(stderr, "                         To use daily sunset or sunrise times use '-on 99' and/or '-off 99'\n");
		fprintf(stderr, "        -S int         = specify the subject ID number (required)\n");
        fprintf(stderr, "        -I int         = specify inter-trial interval in seconds (optional, default=15s)\n");
		fprintf(stderr, "        filename       = specify the name of the text file containing the stimuli (required)\n");
		fprintf(stderr, "                         where each line is: 'BackWavfile' 'SwitchWavfile 'Present_freq'\n");
		fprintf(stderr, "                         'BackWavfile' is the name of the repeated stimulus soundfile ( must be 44.1KHz\n");
        fprintf(stderr, "                         'SwitchWavfile' is the name of the stimulus that will be switched to after a randomized number of repetitions (must be 44.1KHz)\n");
		fprintf(stderr, "                         'Presfreq' is the overall rate at which the stimulus is presented.\n"); 
		fprintf(stderr, "                           The actual rate for each stimulus is presfreq divided by the\n");
		fprintf(stderr, "                           sum for all stims. Set all prefreq values to 1 for equal probability \n"); 
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
  operant_write(box_id, FEEDLIGHT, 1);
  usleep(feed_duration/2);
  
  if(operant_read(box_id, HOPPEROP)!=1){
    printf("error -- hopper not raised when it should be -- box %d\n",box_id);
    f->hopper_failures++;
  }
  usleep(feed_duration/2);
  operant_write(box_id, FEEDER, 0);
  operant_write(box_id, FEEDLIGHT, 0);
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


	/**********************************************************************
	 ** parse the command line
	 **********************************************************************/
	int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour,int *startmin, int *stopmin, float *intertrial, long *pause_milliseconds, char **stimfname)
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
	else if (strncmp(argv[i], "-on", 3) == 0){
	  sscanf(argv[++i], "%i:%i", starthour,startmin);}
	else if (strncmp(argv[i], "-off", 4) == 0){
	  sscanf(argv[++i], "%i:%i", stophour, stopmin);}
    else if (strncmp(argv[i], "-I", 2) == 0){
      sscanf(argv[++i], "%f", intertrial);}
    else if (strncmp(argv[i], "-P", 2) == 0){
      sscanf(argv[++i], "%li", pause_milliseconds);}
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

/**********************************************************************
 **  main
**********************************************************************/
int main(int argc, char *argv[])
{
  FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
  char *stimfname = NULL;
  char *stimfroot;
  const char delimiters[] = " .,;:!-";
  char datafname[128]="", hour [16], min[16], month[16], year[16], 
    day[16], dsumfname[128]="", stimftemp[128], pcm_name[128];
  char  buf[128], stimexm[128],temphour[16],tempmin[16],
    timebuff[64], tod[256], date_out[256], buffer[30], fullsfnameback[256], fullsfnamechange[256], backstimname[256],
    changestimname[256];
  int nclasses, nstims, stim_reinf,
    subjectid, swapval=-1,rswap,swapcnt, tot_stims,
    trial_num,session_num,i,j,k,*plist=NULL,
    dosunrise=0,dosunset=0,starttime,stoptime,currtime, err, pval, stim_number,
    perch_status, perch_check, num_repeats, trial_flag=0, fed=0, reinfor=0;
  
  long tot_trial_num;
  float trialdur = 0.0, resp_latency=0.0;
  unsigned long int temp_dur;
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval perchtime, good_perch, perchstart, tstart, tstop, tdur, changestart, left_latency, time_left;
  struct tm *loctime;
  Failures f = {0,0,0,0};
  int target = 0, perchcheck = 0, do_trial = 0;
  int no_stim_trial = 0, tot_no_stim_trial = 0;

  struct stim {
    char back[128];
    char change[128];
    int reinf;
    int freq;
    unsigned long int dur; 
    int num;
    char shortback[128];
    char shortchange[128];
  } stimlist[MAXSTIM],tmp;

  struct data {
    int trials;
    float dur;
    char back[128];
    char change[128];
  } stim[MAXSTIM];
  
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
        command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &intertrial, &pause_milliseconds, &stimfname);
       	if(DEBUG){
	  fprintf(stderr,"command_line_parse(): box_id=%d, subjectid=%d, starthour=%d, stophour=%d, startmin=%d, stopmin=%d, iti=%f, stimfile: %s\n",
		  box_id, subjectid, starthour, stophour, startmin, stopmin, intertrial, stimfname);}
	
	if(DEBUG){fprintf(stderr,"commandline done, now checking for errors\n");}
    if(DEBUG){printf("iti=%d pause ms=%li\n", intertrial, (long int) pause_milliseconds);}
    iti.tv_sec = (time_t) intertrial;
    pause_samples = (long int) (pause_milliseconds/1000)*44100;

	/* watch for terminal errors*/
	if( (stophour!=99) && (starthour !=99) ){
          if ((stophour <= starthour) && (stopmin<=startmin)){
            fprintf(stderr, "\tTERMINAL ERROR: exp start-time must be greater than stop-time\n");
            exit(-1);
          }
        }
	if (box_id <= 0){
	  fprintf(stderr, "\tYou must enter a box ID!\n"); 
	  fprintf(stderr, "\tERROR: try 'perchdiscrim  -help' for available options\n");
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
	sprintf(pcm_name, "dac%i", box_id);   /* write pcm box name. Need to fix asprintf bug. printf should work... */
    if(DEBUG){printf("pcm name is %s\n", pcm_name);}

	if((pcm=do_setup(pcm_name))<0){
	  fprintf(stderr,"FAILED to set up the pcm device %s\n", pcm_name);
	  exit (-1);}
	if (operant_open()!=0){
	  fprintf(stderr, "Problem opening IO interface\n");
	  snd_pcm_close(pcm); 
	  exit (-1);
	}
	if(DEBUG){fprintf(stderr, "pcm handle: %d\n", (int)pcm);}
	operant_clear(box_id);
	if(DEBUG){fprintf(stderr, "box intialized!\n");}

	/* give user some feedback*/
	fprintf(stderr, "Loading stimuli from file '%s' for session in box '%d' \n", stimfname, box_id); 
	fprintf(stderr, "Subject ID number: %i\n", subjectid);

	/* Read in the list of exemplars */
	nstims = 0;
	nclasses=0;
    tot_stims=0;
	if ((stimfp = fopen(stimfname, "r")) != NULL){
	  while (fgets(buf, sizeof(buf), stimfp))
	    nstims++;
	  fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
	  rewind(stimfp);
	  
	  for (i = 0; i < nstims; i++){
	    fgets(buf, 128, stimfp);
	    temp_dur=0;
	    sscanf(buf, "\n\n\%s\%s\%d",  tmp.back, tmp.change, &tmp.freq);
	    if(DEBUG){fprintf(stderr, " %s %s %d\n", tmp.back, tmp.change, tmp.freq);}
	    if(tmp.freq==0){
	      printf("ERROR: insufficient data or bad format in '.stim' file. Try 'perchdiscrim -help'\n");
	      exit(0);} 
	    /* count stimulus classes*/
	    sprintf(fullsfnameback,"%s%s", STIMPATH, tmp.back);                                /* add full path to file name */
	    sprintf(fullsfnamechange,"%s%s", STIMPATH, tmp.change);                                /* add full path to file name */
	    
	    /*verify the soundfile*/
	    if(DEBUG){printf("trying to verify %s\n",fullsfnameback);}
	    if((temp_dur = verify_soundfile(fullsfnameback))<1){
	      fprintf(stderr, "Unable to verify %s!\n",fullsfnameback );  
	      snd_pcm_close(pcm);
	      exit(0); 
	    }
	    if(DEBUG){printf("soundfile %s verified, duration: %lu\n", tmp.back, temp_dur);}
	    
	    if(DEBUG){printf("trying to verify %s\n",fullsfnamechange);}
	    if((temp_dur = verify_soundfile(fullsfnamechange))<1){
	      fprintf(stderr, "Unable to verify %s!\n",fullsfnamechange );  
	      snd_pcm_close(pcm);
	      exit(0); 
	    }
	    if(DEBUG){printf("soundfile %s verified, duration: %lu\n", tmp.change, temp_dur);}

	   if (DEBUG){printf("adding stim to stimlist\n");}
	    strcpy(stimlist[i].back, fullsfnameback);
	    strcpy(stimlist[i].change, fullsfnamechange);
	      stimlist[i].freq = tmp.freq;
	      stimlist[i].dur = temp_dur;
	      stimlist[i].num = i;
         strcpy(stimlist[i].shortback, tmp.back);
         strcpy(stimlist[i].shortchange, tmp.change);
	      tot_stims += stimlist[i].freq;
	    }

	    if(DEBUG){printf("stims: %d \ttotal stims to play: %d\n", nstims, tot_stims);}
	  
	}
	else{ 
	  printf("Error opening stimulus input file! Try 'perchdiscrim -help' for proper file formatting.\n");  
	   snd_pcm_close(pcm); 
	  exit(0);	  
	}
	fclose(stimfp);
	if(DEBUG){printf("Done reading in stims; %d stims \n", nstims);}

	if(DEBUG){
	  for(i=0; i<nstims; i++){
	    printf("%d\tname:%s , %s \tfreq:%d\tdur:%lu\n", i, stimlist[i].back, stimlist[i].change, 
		   stimlist[i].freq, stimlist[i].dur);}
	}

	/* build the stimulus playlists */
	if(DEBUG){printf("Making the playlist\n");}
	free(plist);
	plist = malloc( (tot_stims+1)*sizeof(int) );
	i=j=0;
	for (i=0;i<nstims; i++){
	  k=0;
	  for(k=0;k<stimlist[i].freq;k++){
	    plist[j]=i;
	    if(DEBUG){printf("value for class 1 playlist entry '%d' is '%d'\n", j, i);}
	    j++;
	  }
	}


	/*zero out the stimulus and class data counters*/
	for(i = 0; i<nstims;++i)
	  stim[i].trials = stim[i].dur = 0;
	if (DEBUG){printf("stimulus counters zeroed!\n");} 

    /* for this task set punishment and reward rates to 100 */

	stim_reinf = 100;
	/*  Open & setup data logging files */
	curr_tt = time (NULL);
	loctime = localtime (&curr_tt);
	strftime (timebuff, 64, "%d%b%y", loctime);
	sprintf (stimftemp, "%s", stimfname);
	stimfroot = strtok (stimftemp, delimiters); 
	sprintf(datafname, "%i_%s.perchdiscrim_rDAT", subjectid, stimfroot);
	sprintf(dsumfname, "%i.summaryDAT", subjectid);
	datafp = fopen(datafname, "a");
        dsumfp = fopen(dsumfname, "w");
	
	if ( (datafp==NULL) || (dsumfp==NULL) ){
          fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", datafname);
	  snd_pcm_close(pcm);
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
do{
	while((currtime>=starttime) && (currtime<stoptime)){
		if(DEBUG){printf("\n\n ***********\n TRIAL START\n");}
		if (DEBUG==2){printf("minutes since midnight at loop start: %d\t starttime: %d\tstoptime: %d\n",
				currtime,starttime,stoptime);}

		srand(time(0));

        pval = (int) ((tot_stims+0.0)*rand()/(RAND_MAX+0.0)); /* randomly select a stimulus pair */
        stim_number = plist[pval];
        if (DEBUG){printf("cued stim: %s, %s\n", stimlist[stim_number].back, stimlist[stim_number].change);}
        strcpy(backstimname, stimlist[stim_number].back);
        strcpy(changestimname, stimlist[stim_number].change);
        err = buildtempbuffs(stimlist[stim_number].back, stimlist[stim_number].change);


        
        if(DEBUG){printf("after building buffs: backframes=%i, changeframes=%i\n", (int) back_frames, (int) change_frames);}	
        if ( err != 1 ) {
          printf("Error filling stimulus vectors");
          free(backsoundvec);
          free(changesoundvec);
        }

		/* Wait for landing on one of the active perches */
		perch_status = 0;

		if (DEBUG){printf("Waiting for the bird to land on a perch\n");}
		operant_write (box_id, HOUSELT, 1);        /* house light on */
		do{                                         
			nanosleep(&rsi, NULL);	               	       
			perch_check = operant_read(box_id, PERCH);   /*check perch*/	
		}while(perch_check==0); 

		/*check to be sure the bird stays on the same perch for around 1 sec before starting the trial*/
		gettimeofday(&perchstart, NULL);
		timeradd(&perchstart, &min_perch, &good_perch);

		do{
			nanosleep(&rsi, NULL);
			perchcheck = operant_read(box_id, PERCH);
			gettimeofday(&perchtime, NULL);
		}while ((perchcheck==1) && (timercmp(&perchtime, &good_perch, <))); 

		if (perchcheck){ 
			do_trial=1;    
			perch_status=1;
            stim[stim_number].trials++;
			if (DEBUG){printf("VALID TRIAL --- perch is %d\n", PERCH);}
		}
		else {
			do_trial=0;
			if (DEBUG){printf("FALSE START TRIAL, bird left perch '%d' too early\n", target);}
			perch_status=0;
		}

		sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
		num_repeats = (int) (rand() % 7) + 4;   /* select random number of repetitions of background */

		/* Play stimulus file checking each time if bird left
		 * perch */
		gettimeofday(&tstart, NULL);
		if(DEBUG){printf("****** STARTING %s PLAYBACK ....\n", stimexm);}
        
        trial_flag=0;
        for ( j = 0; j < num_repeats; j += 1 ){
          if ( perch_status == 1 ){
            err = play_and_watch(pcm, backsoundvec, back_frames, box_id, &time_left);
            if(err==1){perch_status = 1;}
            if(err==2){perch_status = 0;
            trial_flag = 2; /* left perch before change, timeout */
            break;
          } }
          else{ break;}
        }

        if(DEBUG){printf("Before switch, trial_flag=%i\n", trial_flag);}
		if (DEBUG){printf("SWITCHING TO CHANGE BUFFER\n");};
		gettimeofday(&changestart, NULL);
        if(trial_flag != 2){
          for ( j = 0; j < CHANGE_REPS; j += 1 ){
            if ( perch_status == 1 ) {
              err = play_and_watch(pcm, changesoundvec, change_frames, box_id, &time_left);

              if ( err==1 && j==(CHANGE_REPS - 1)) {
                trial_flag=0; /* never left perch */
              }
              if(err==2){perch_status = 0;
                trial_flag = 1;     /* left perch after change */
                break;
              }
            }
          }
        }

		if (DEBUG){printf("PLAYBACK COMPLETE\n");}

		gettimeofday(&tstop, NULL);
		timersub(&tstop, &tstart, &tdur);

		timersub(&time_left, &changestart, &left_latency); /* latency to leave perch after change */

		trialdur = (float)tdur.tv_sec + (float)(tdur.tv_usec/1000000.0);
		if(DEBUG){printf("BIRD JUST LEFT THE PERCH\t trialdur is: %.4f\n", trialdur);}


		if(trial_flag==1){resp_latency = (float) left_latency.tv_sec + ( (float) left_latency.tv_usec/1000000);}  /* format reaction time */
		/*  Do feed or timeout */

		switch ( trial_flag ) {
			case 1:	
				reinfor = feed(stim_reinf, &f);
				++fed;
				break;

			case 2:	
				reinfor = timeout(stim_reinf);
				break;

			default:	
				break;
		}	

		/* note time that trial ends */
		curr_tt = time(NULL); 
		loctime = localtime (&curr_tt);                     /* date and wall clock time of trial*/
		strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
		strftime (min, 16, "%M", loctime);
		strftime (month, 16, "%m", loctime);
		strftime (day, 16, "%d", loctime);
		currtime=(atoi(hour)*60)+atoi(min);

        if(trial_flag==0){resp_latency = 0.0;}
        if(trial_flag==2){resp_latency = trialdur;}

		++no_stim_trial;
		++tot_no_stim_trial;

		/* Pause for ITI */
		operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
			nanosleep(&iti, NULL);                     /* wait intertrial interval */
		if (DEBUG){printf("ITI passed\n");}

		/* Write trial data to output file */
		strftime (tod, 256, "%H%M", loctime);
		strftime (date_out, 256, "%m%d", loctime);
		fprintf(datafp, "%d\t%lu\t%d\t%s\t%s\t%d\t%.4f\t%s\t%s\n",
				session_num,tot_trial_num,trial_num,stimlist[stim_number].shortback,stimlist[stim_number].shortchange,trial_flag,resp_latency,tod,date_out );
		fflush (datafp);
		

		/* Update summary data */ 
		if(freopen(dsumfname,"w",dsumfp)!= NULL){
			fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
			fprintf (dsumfp, "\n\n\n\tSESSION TOTALS BY SOURCE SOUNDFILE\n");
			fprintf (dsumfp, "\t\tSoundfile \tTrials \tDuration\n");
			for (i = 0; i<nstims;++i){
				fprintf (dsumfp, "\t\t%s\t%s  \t %d    \t %.4f\n", 
						stim[i].back, stim[i].change,  stim[i].trials, stim[i].dur);
			}

        fprintf (dsumfp, "Last trial run @: %s\n", asctime(loctime) );
		fprintf (dsumfp, "Trials this session: %d\n",trial_num);
		fprintf (dsumfp, "Feeder ops today: %d\n", fed );
		fprintf (dsumfp, "Hopper failures today: %d\n", f.hopper_failures);
		fprintf (dsumfp, "Hopper won't go down failures today: %d\n",f.hopper_wont_go_down_failures);
		fprintf (dsumfp, "Hopper already up failures today: %d\n",f.hopper_already_up_failures);
		fprintf (dsumfp, "Responses during feed: %d\n", f.response_failures); 
        
        fflush(dsumfp);
		}
		else
			fprintf(stderr, "ERROR!: problem re-opening summary output file!: %s\n", dsumfname);

		if (DEBUG){printf("trial summaries updated\n");}


		/* End of trial chores */
        tot_trial_num++;
        trial_num++;

		sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);        /* unblock termination signals */ 
		if(DEBUG){printf("minutes since midnight at end of trial: %d\n", currtime);}
		free(backsoundvec);
		free(changesoundvec);
	}
	  
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
      curr_tt = time(NULL);
	  if (DEBUG){printf("swapcount:%d\tswapval:%d\trswap:%d\n",swapcnt,swapval,rswap);}

	}while (1);// main loop
	
	
	/*  Cleanup */
	fclose(datafp);
	fclose(dsumfp);
	snd_pcm_close(pcm);
	return 0;
}                         

