/*****************************************************************************
** femxpeck.c - operant key peck preference procedure
**              pecks to L and R key generate song playbacks
**              center key is not active
**              femxpeck counts the number of pecks (to the start key) 
**                during subsequent song playback 
**              femxpeck also will excise a sub stimulus (with defined 
**                duration and offset) from each soundfile in the .stim list
******************************************************************************
**
**
** 11-23-05 TQG Adapted from gng.c.090905
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>
#include <assert.h>
#include <sndfile.h>

#define ALSA_PCM_NEW_SW_PARAMS_API
#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include "/usr/local/src/operantio/operantio.c"

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
#define DEF_REF                  5             /* default reinforcement for corr. resp. set to 100% */
#define STIMPATH       "/usr/local/stimuli/"
//#define MAXFILESIZE 5292000                    /* max samples allowed in soundfile */
#define HOPPER_DROP_MS           300           /*time for hopper to fall before checking if it fell after command to do so*/

long feed_duration = FEED_DURATION;
int  trial_max = MAX_NO_OF_TRIALS;
int  startH = EXP_START_TIME; 
int  stopH = EXP_END_TIME;
int  sleep_interval = SLEEP_TIME;
int  reinf_val = DEF_REF;

snd_pcm_t *handle;
unsigned int channels = 1;                      /* count of channels */
unsigned int rate = 44100;                      /* stream rate */
snd_pcm_format_t format = SND_PCM_FORMAT_S16;   /* sample format */
unsigned int buffer_time = 500000;              /* ring buffer length in us */
unsigned int period_time = 100000;              /* period time in us */
snd_pcm_sframes_t buffer_size;
snd_pcm_sframes_t period_size;
snd_output_t *output = NULL;

typedef struct{
  int left;
  int center;
  int right;
}count;

typedef struct {
  int hopper_failures;
  int hopper_wont_go_down_failures;
  int hopper_already_up_failures;
  int response_failures;
} Failures;

int feed(int rval, Failures *f);

const char exp_name[] = "FEMXPECK";
int box_id = -1;

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};


/* -------- Signal handling --------- */
int client_fd = -1;

static void sig_pipe(int signum)
{ fprintf(stdout,"SIGPIPE caught\n"); client_fd = -1;}

static void termination_handler (int signum)
{
  snd_pcm_close(handle);
  fprintf(stdout,"closed pcm device: term signal caught: exiting\n");
  exit(-1);
}


int do_setup(char *pcm_name)
{
  snd_pcm_hw_params_t *params;
  snd_pcm_access_t access = SND_PCM_ACCESS_RW_INTERLEAVED;
  snd_pcm_sw_params_t *swparams;
  unsigned int rrate;
  int err, dir;
  snd_pcm_uframes_t persize;
  snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;

  printf("trying to alloca hw\n");
  snd_pcm_hw_params_alloca(&params);
  printf("trying to alloca sw\n");
  snd_pcm_sw_params_alloca(&swparams);
  printf("finished alloca \n");

  printf("trying to open %s\n", pcm_name);
    /*open the pcm device*/
  if ((err = snd_pcm_open(&handle, pcm_name, stream, 0)) < 0) {
    printf("Playback open error: %s\n", snd_strerror(err));
    return 0;
  }
  printf("opened %s\n", pcm_name);

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
    printf("Unable to set buffer time %i for playback: %s\n", buffer_time, snd_strerror(err))
;
    return err;
  }
  err = snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
  if (err < 0) {
    printf("Unable to get buffer size for playback: %s\n", snd_strerror(err));
    return err;
  }
  /* set the period time */
  err = snd_pcm_hw_params_set_period_time_near(handle, params, &period_time, &dir);
  if (err < 0) {
    printf("Unable to set period time %i for playback: %s\n", period_time, snd_strerror(err))
;
    return err;
  }
  err = snd_pcm_hw_params_get_period_size(params, &period_size, &dir);
  if (err < 0) {
    printf("Unable to get period size for playback: %s\n", snd_strerror(err));
    return err;
  }
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
  err = snd_pcm_sw_params_set_start_threshold(handle, swparams, (buffer_size / period_size) *
 period_size);
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
  printf("done with setup\n");
  return (double) persize;
}
 







/* Underrun and suspend recovery */
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


unsigned long int verify_soundfile(char *sfname) /* verify that soundfile meets your assumptions */
{
  SNDFILE *sf;
  SF_INFO *sfinfo; 

  sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));
  if(!(sf = sf_open(sfname,SFM_READ,sfinfo))){
    fprintf(stderr,"error opening input file %s\n",sfname);
    free(sfinfo);
    return -1;
  }
  if (sfinfo->frames > sizeof(unsigned long int)){
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
  if((sfinfo->format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV){
    printf("Not a WAV file\n");
    sf_close(sf);
    free(sfinfo);
    return -1;
  } 
  free(sfinfo);
  return (sfinfo->frames/sfinfo->samplerate*1000); /*return the msec duration of the soundfile */
}

int play_and_count(char *sfname, double period, int stimdur, int stimoffset, int box_id)
{
  SNDFILE *sf;
  SF_INFO *sfinfo;
  short *obuff;
  sf_count_t incount;
  double inframes, padded, maxframes;
  long pad = 0;
  int outcount, tmp, i, err, totframesout, peck, ramp_dur=2205; /*default ramp duration is 50 msec*/
  snd_pcm_uframes_t outframes;
  sf_count_t offset;
  
  struct pecks {
    int left;
    int center;
    int right;
  }peckcount;

  sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));
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
  //printf("offset  is %d msec, which is %d samples\n", stimoffset, (int) offset );
  tmp = sf_seek(sf, offset, SEEK_SET);
  //printf("trying to seek by %d samples; sf_seek returned %d \n", (int)offset, tmp);

  maxframes = (int)sfinfo->frames;
  if (stimdur==-1) /* play the whole soundfile (less any offset)*/
    inframes = maxframes - (int)offset;
  else if(stimdur>0)/* play the define stimulus duration */
    inframes = (int) rint((float)sfinfo->samplerate*(float)stimdur/1000.0);
  else{
    fprintf (stderr, "Invalid stimulus duration '%d'\n",stimdur);
    sf_close(sf);free(sfinfo);return -1;}
  //printf("stimdur  is %d msec, so I want %d samples after the offset\n", stimdur, (int) inframes );
  
  /* pad the file size up to next highest period count*/
  pad = (period * ceil( inframes/ period))-inframes;
  padded=inframes+pad;
  outframes = padded;
  obuff = (short *) malloc(sizeof(int)*padded);
  
  /* read the data */
  if( ( maxframes - (offset + inframes) ) <0){
    fprintf (stderr, "The sum of the stimulus duration and offset exceeds the soundfile length\n");
    sf_close(sf);free(sfinfo);free(obuff);return -1;}
  //printf("trying to sf_readf %d frames\n",(int)inframes); 
  incount = sf_readf_short(sf, obuff, inframes);
  //printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount, (int)inframes);  
  
  /* ramp the first (and last) 50 ms from zero to normal amplitude*/
  ramp_dur=.05 * sfinfo->samplerate;
  for (i = 0; i<ramp_dur; i++)
    obuff[i] = obuff[i]*((float)i/(float)ramp_dur);
  for (i = (incount-ramp_dur); i<=incount; i++) 
    obuff[i] = obuff[i]* ((float)(incount-i)/(float)ramp_dur);
  //printf("outframes %d \n", (int)outframes);
 
  snd_pcm_nonblock(handle,1); /*make sure we're in non-blocking mode*/
  
  while (outframes > 0) { /*playback loop*/
    err = snd_pcm_writei(handle, obuff, outframes);
    if (err == -EAGAIN){
      continue;}
    if (err < 0) {
      if (xrun_recovery(handle, err) < 0) {
	printf("Write error: %s\n", snd_strerror(err));
	return 0;}
      break;  // skip one period 
    }
    //printf("wrote %d frames\n", err);

    /**************************/
    /*check for keypecks here */
    /**************************/

    peckcount.right += operant_read(box_id, RIGHTPECK);   /*get value at right peck position*/	
    peckcount.left += operant_read(box_id, LEFTPECK);   /*get value at left peck position*/		 	       
    peckcount.center += operant_read(box_id, CENTERPECK);   /*get value at left peck position*/		 	       
       
    totframesout += err;
    obuff += err * channels;
    outframes -= err;
  }
  //printf("frames not played: %d \n", (int)outframes);
  //printf("frames played: %d \n", totframesout);
  sf_close(sf);
  free(sfinfo);
  //  return peckcount;
  return 1;
}

int main(int ac, char *av[])
{
	FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
	char *stimfname = NULL, *tmpexemp=NULL;
	char *stimfroot, *pcm_name, *fullsfname;
	const char delimiters[] = " .,;:!-";
	char datafname[128], hour [16], min[16], month[16], 
	  day[16], dsumfname[128], stimftemp[128];
	char  buf[128], stimexm[128],fstim[256],
	  timebuff[64], tod[256], date_out[256];
	int dinfd=0, doutfd=0, nclasses, nstims, stim_class, C2_stim_number,C1_stim_number, stim_reinf,off_steps,stepval, 
	  subjectid, period, resps, num_c1stims, num_c2stims,stimdurtest,
	  trial_num, session_num, C2_pval,C1_pval, i,j,k, *C2_plist=NULL, *C1_plist=NULL, 
	  tot_c1stims=0, tot_c2stims=0,tmpclass,tmpfreq,tmpreinf;
	float stimdurLB=0.0, stimdurUB=0.0, stimdur_range = 0.0;
	unsigned long int temp_dur,stimulus_duration,offset;
	time_t curr_tt;
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
	  int playnum;
	}C1stim[MAXSTIM], C2stim[MAXSTIM];
	struct response {
	  int count;
	  int go;
	  int no;
	  float ratio;
	} stimRses[MAXSTIM], stimRtot[MAXSTIM], classRses[MAXCLASS], classRtot[MAXCLASS];
	
	struct PECK trial;

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

	/* snd_pcm_hw_params_t *params;
  snd_pcm_sw_params_t *swparams;
  printf("try hw alloca\n");
  snd_pcm_hw_params_alloca(&params);
  printf("try sw alloca\n"); 
  snd_pcm_sw_params_alloca(&swparams);
	*/
	/* Parse the command line */
	if(DEBUG){printf("parse command line\n");}
	for (i = 1; i < ac; i++){
	  if (*av[i] == '-'){
	    if (strncmp(av[i], "-B", 2) == 0){ 
	      sscanf(av[++i], "%d", &box_id);
	      sprintf(pcm_name, "dac%d", box_id);
	      if(DEBUG){
		printf("box number = %d\n", box_id); 
		printf("dac = %s\n", pcm_name);}
	    }
	    else if (strncmp(av[i], "-S", 2) == 0){
	      sscanf(av[++i], "%i", &subjectid);
	      if(DEBUG){printf("subj = %d\n", subjectid);}
	    }
	    else if (strncmp(av[i], "-lb", 3) == 0){
	      sscanf(av[++i], "%f", &stimdurLB);
	    if(DEBUG){printf("dac = %f\n", stimdurLB);}
	    }
	    else if (strncmp(av[i], "-ub", 3) == 0){
	      sscanf(av[++i], "%f", &stimdurUB);
	      if(DEBUG){printf("dac = %f\n", stimdurUB);}
	    }
	    else if (strncmp(av[i], "-on", 3) == 0){
	      sscanf(av[++i], "%i", &startH);
	      if(DEBUG){printf("dac = %d\n", startH);}
	    }
	    else if (strncmp(av[i], "-off", 4) == 0){
	      sscanf(av[++i], "%i", &stopH);
	      if(DEBUG){printf("dac = %d\n", stopH);}
	    }
	    else if (strncmp(av[i], "-help", 5) == 0){
	      fprintf(stderr, "femxpeck usage:\n");
	      fprintf(stderr, "    femxpeck [-help] [-B int] [-lb float] [-ub float] [-on int] [-off int] [-S <subject number>] <filename>\n\n");
	      fprintf(stderr, "        -help          = show this help message\n");
	      fprintf(stderr, "        -B int         = use '-B 1' '-B 2' ... '-B 12' \n");
	      fprintf(stderr, "        -lb float      = lower bound (in secs) for stimulus duration. eg. 7.25\n");
	      fprintf(stderr, "        -ub float      = upper bound (in secs) for stimulus duration. eg. 15.0 \n");
	      fprintf(stderr, "                         NOTE: LB and UB should use .25 sec resolution because the\n");
	      fprintf(stderr, "                           stimulus durations are uniformly distributed between LB and UB\n");
	      fprintf(stderr, "                           in 250 msec steps\n");
	      fprintf(stderr, "        -on int        = set hour for exp to start eg: '-on 7' (default is 7AM)\n");
	      fprintf(stderr, "        -off int       = set hour for exp to stop eg: '-off 19' (default is 7PM)\n");
	      fprintf(stderr, "        -S int         = specify the subject ID number (required)\n");
	      fprintf(stderr, "        filename       = specify the name of the text file containing the stimuli (required)\n");
	      fprintf(stderr, "                         where each line is: 'Class' 'Wavfile' 'Present_freq' 'Reinf_rate'\n");
	      fprintf(stderr, "                         'Class'= 1 for LEFT-, 2 for RIGHT-key assignment \n");
	      fprintf(stderr, "                         'Wavfile' is the name of the stimulus soundfile (must be 44100 Hz sample rate\n");
	      fprintf(stderr, "                         'Presfreq' is the overall rate (compared to the other stimuli) at which the stimulus is presented. \n"); 
	      fprintf(stderr, "                              The actual rate for each stimulus (expressed as an integer) is that value divded by the\n");
	      fprintf(stderr, "                              sum for all stimuli. Set all prefreq values to 1 for equal probablility \n"); 
	      fprintf(stderr, "                         'Reinf_rate' is the percentage of time that food is made available following presentation of this stimulus.\n");
	      exit(-1);
	    }
	    else{
	      fprintf(stderr, "Unknown option: %s\t", av[i]);
	      fprintf(stderr, "Try 'femxpeck -help' for help\n");
	    }
	  }
	  else{
	    stimfname = av[i];
	  }
	}
	if(DEBUG){printf("commandline done, check for errors\n");}
	/* watch for terminal errors*/
	if (stopH <= startH){
	  fprintf(stderr, "\tTERMINAL ERROR: exp start-time must be greater than stop-time\n");
	  exit(-1);
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
	

	/* Initialize box */
	if(DEBUG){printf("Initializing box %d ...\n", box_id);}
	printf("trying to execute setup(%s)\n", pcm_name);
	period=do_setup(pcm_name);


	//	if((period=do_setup(pcm_name))<0){
	//fprintf(stderr,"FAILED to set up the pcm device %s\n", pcm_name);
	// exit (-1);}
	printf("WTF!!\n");
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
	
	/* Read in the list of exmplars from stimulus file */
	nstims = num_c1stims = num_c2stims = 0;
	nclasses=0;
	if ((stimfp = fopen(stimfname, "r")) != NULL){
	  while (fgets(buf, sizeof(buf), stimfp))
	    nstims++;
	  fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
	  rewind(stimfp);
	  
	  for (i = 0; i < nstims; i++){
	    fgets(buf, 128, stimfp);
	    tmpclass=tmpfreq=tmpreinf=temp_dur=0;
	    tmpexemp=NULL;
	    sscanf(buf, "%d\%s\%d\%d", &tmpclass, tmpexemp, &tmpfreq, &tmpreinf);

	    if((tmpfreq==0) || (tmpreinf==0)){
	      printf("ERROR: insufficnet data or bad format in '.stim' file. Try 'femxpeck -help'\n");
	      exit(0);} 
	    /* count stimulus classes*/
	    if (nclasses<tmpclass){nclasses=tmpclass;}
	    if (DEBUG){printf("nclasses: %d\n", nclasses);}
	    if(tmpclass>2)
	      fprintf(stderr, "stimulus '%s' has an invalid class. Must be 1 or 2.\n", tmpexemp);
	    /*check the reinforcement rates */
	    if (tmpreinf >100)
	      fprintf(stderr, "Food reinforcement rate of %d%% for %s is invalid. Must be less than or equal to 100%%\n", 
		      tmpreinf, tmpexemp);
	    else 
	      fprintf(stderr, "Food access is available %d%% of the time following %s\n",tmpreinf, tmpexemp);
	    
	    sprintf(fullsfname,"%s%s", STIMPATH, tmpexemp);                                /* add full path to file name */

	    if((temp_dur = verify_soundfile(fullsfname))<1){
	      fprintf(stderr, "Unable to verify %s!\n",fullsfname );  
	      snd_pcm_close(handle);
	      exit(0); 
	    }

	    if(DEBUG){printf("soundfile %s verified\n", tmpexemp);}
	    
	    switch(tmpclass){
	    case 1:
	      num_c1stims++;
	      C1stim[i].class = tmpclass;
	      strcpy (C1stim[i].exemplar, tmpexemp);
	      C1stim[i].freq = tmpfreq;
	      C1stim[i].reinf = tmpreinf;
	      C1stim[i].dur = temp_dur;
	      tot_c1stims += C1stim[i].freq;
	      break;
	    case 2:
	      num_c2stims++;
	      C2stim[i].class = tmpclass;
	      strcpy (C2stim[i].exemplar, tmpexemp);
	      C2stim[i].freq = tmpfreq;
	      C2stim[i].reinf = tmpreinf;
	      C2stim[i].dur = temp_dur;
	      tot_c2stims += C2stim[i].freq;
	      break;
	    }
	    if(DEBUG){printf("class1 stims: %d \ttot_c1stims: %d\n", num_c1stims, tot_c1stims);}
	    if(DEBUG){printf("class2 stims: %d \ttot_c2stims: %d\n", num_c2stims, tot_c2stims);}
	  }
	}
	else{ 
	  printf("Error opening stimulus input file! Try 'femxpeck -help' for proper file formatting.\n");  
	  snd_pcm_close(handle);
	  exit(0);	  
	}
        
	fclose(stimfp);
	if(DEBUG){printf("Done reading in stims; %d stims in %d classes found\n", nstims, nclasses);}

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

	/*  Open & setup data logging files */
	curr_tt = time (NULL);
	loctime = localtime (&curr_tt);
	if (DEBUG){printf("time: %s\n" , asctime (loctime));}
	strftime (timebuff, 64, "%d%b%y", loctime);
	if (DEBUG){printf ("stimfname: %s\n", stimfname);}
	sprintf (stimftemp, "%s", stimfname);
	if (DEBUG){printf ("stimftemp: %s\n", stimftemp);}
	stimfroot = strtok (stimftemp, delimiters); 
	if (DEBUG){printf ("stimftemp: %s\n", stimftemp);}
	if (DEBUG){printf ("stimfname: %s\n", stimfname);}
	sprintf(datafname, "%i_%s.fempeck_rDAT", subjectid, stimfroot);
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
	  exit(-1);
        }

	/* Write data file header info */
	printf ("Data output to '%s'\n", datafname);
	fprintf (datafp, "File name: %s\n", datafname);
	fprintf (datafp, "Procedure source: %s\n", exp_name);
	fprintf (datafp, "Start time: %s", asctime(loctime));
	fprintf (datafp, "Subject ID: %d\n", subjectid);
	fprintf (datafp, "Stimulus source: %s\n", stimfname);  
	fprintf (datafp, "Sess#\tTrl#\tStimulus\t\t\tClass\tResps\tReinf\tTOD\tDate\n");


   /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
   ********************************************/
	session_num = 1;
	trial_num = 0;

	///
	/// CHANGE THESE
	///
	for(i = 0; i<nstims;++i){   /*zero out the response tallies */
	  stimRses[i].go = stimRses[i].no = stimRtot[i].go = stimRtot[i].no = 0;
	  stimRses[i].ratio = stimRtot[i].ratio = 0.0;
	}
 	if (DEBUG){printf("stimulus counters zeroed!\n");} 
	for(i=0;i<nclasses;i++){
	  classRses[i].go = classRses[i].no = classRtot[i].go = classRtot[i].no = 0;
	  classRses[i].ratio = classRtot[i].ratio = 0.0;;
	}
	///
	/// DOWN TO HERE

	if (DEBUG){printf("class counters zeroed!\n");} 
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	if (DEBUG){printf("atoi(hour) at loop start: %d \n", atoi(hour));}

	operant_write (box_id, HOUSELT, 1);        /* make sure houselight is on */

	do{                                                                               /* start the block loop */
	  while ((atoi(hour) >= startH) && (atoi(hour) < stopH)){
	    
	    /*cue two randomly chosen stimulus source files, one from each playlist */
	    srand(time(0));
	    C1_pval = (int) ((tot_c1stims+0.0)*rand()/(RAND_MAX+0.0));              /* select playlist1 entry at random */ 
	    C2_pval = (int) ((tot_c2stims+0.0)*rand()/(RAND_MAX+0.0));              /* select playlist2 entry at random */ 
	    C1_stim_number = C1_plist[C1_pval];
	    C2_stim_number = C2_plist[C2_pval];
	    if (DEBUG){printf("playval for class 1: %d\t class 2: %d\n", C1_pval, C2_pval);} 
	    
	    /* randomly select stim duration (in 250 ms increments) */
	    stepval = (int) (((stimdur_range/0.25)+1)*rand()/(RAND_MAX+0.0)); 
	    stimulus_duration = (((float)stepval*0.25)+stimdurLB)*1000;
	    if(DEBUG){printf("stepval:  %d\tstimdur: %lu\n", stepval, stimulus_duration);}
	    /* randomly select stim offset duration (in 500 ms increments) */
	    off_steps = (500*(rint((C1stim[C1_stim_number].dur - stimulus_duration)/500)))/500;
	    offset = (int)((off_steps+1)*rand()/(RAND_MAX+0.0)) * 500;
 
	    //resp_sel = resp_acc = resp_rxt = 0;        /* zero trial variables        */
	    ++trial_num;
	    
	    /* Wait for left or right key press */
	    if (DEBUG){printf("Waiting for left or right key press\n");}
	    operant_write (box_id, HOUSELT, 1);        /* house light on */
	    trial.right= trial.left = trial.center = 0;
	    do{                                         
	      nanosleep(&rsi, NULL);	               	       
	      trial.right = operant_read(box_id, RIGHTPECK);   /*get value at right peck position*/	
	      trial.left = operant_read(box_id, LEFTPECK);   /*get value at left peck position*/		 	       
	    }while ((trial.right==0) && (trial.left==0));  
	    
	    /*set your trial variables*/
	    if(trial.right){
	      // targetpeck = RIGHTPECK;
	      stim_class = C1stim[C1_stim_number].class;                              
	      strcpy (stimexm, C1stim[C1_stim_number].exemplar);                      
	      stim_reinf = C1stim[C1_stim_number].reinf;
	      sprintf(fstim,"%s%s", STIMPATH, stimexm);                               
	    }
	    else{
	      //targetpeck = LEFTPECK;
	      stim_class = C2stim[C2_stim_number].class;                              
	      strcpy (stimexm, C2stim[C2_stim_number].exemplar);                      
	      stim_reinf = C2stim[C2_stim_number].reinf;
	      sprintf(fstim,"%s%s", STIMPATH, stimexm);                               
	    }
	    if(DEBUG){
	      printf("class: %d\t", stim_class);
	      printf("reinf: %d\t", stim_reinf);
	      printf("name: %s\n", stimexm);
	      printf("full stim path: %s\n", fstim);
	    }
  
	    sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	      	      
	    /* Play stimulus file */
	    if (DEBUG){printf("STARTING PLAYBACK '%s'\n", stimexm);}
	    play_and_count(fstim, period,stimulus_duration, offset, box_id);
	    
	    /*catch play errors
	      fprintf(stderr, "play_and_count failed on pcm:%s stimfile:%s. Program aborted %s\n", 
		      pcm_name, stimexm, asctime(localtime (&curr_tt)) );
	      fprintf(datafp, "play_and_countfailed on pcm:%s stimfile:%s. Program aborted %s\n", 
		      pcm_name, stimexm, asctime(localtime (&curr_tt)) );
	      fclose(datafp);
	      fclose(dsumfp);
	      exit(-1);
	      }*/
	    if (DEBUG){printf("PLAYBACK COMPLETE  '%s'\n", stimexm);}
	    if (DEBUG){printf("L-C-R pecks: %d-%d-%d\n", trial.left, trial.center,trial.right);}
	    
	    /* note time that trial ends */
	    curr_tt = time (NULL); 
	    loctime = localtime (&curr_tt);                     /* date and wall clock time of trial*/
	    strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
	    strftime (min, 16, "%M", loctime);
	    strftime (month, 16, "%m", loctime);
	    strftime (day, 16, "%d", loctime);
	    
	    /*deliver some food */
	    if((reinfor = feed(stim_reinf, &f)) == 1)
	      ++fed;

	    /* TODO: *********************************************************	    
	    * tabulate the number of response to the active key (can we do all keys? YES) on this trial
	    * keep track for each stimulus and each class,over session clock (pecks/hour)
	    ******************************************************************
	     ++stimRses[stim_number].count; ++stimRtot[stim_number].count; ++classRses[stim_class].count; ++classRtot[stim_class].count;	   
	    */
	    
	    /* Pause for ITI */
	    reinfor_sum = reinfor + reinfor_sum;
	    operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	    nanosleep(&iti, NULL);                     /* wait intertrial interval */
	    if (DEBUG){printf("ITI passed\n");}
					
	    /* Write trial data to output file */
	    strftime (tod, 256, "%H%M", loctime);
	    strftime (date_out, 256, "%m%d", loctime);
	    fprintf(datafp, "%d\t%d\t%s\t\t%d\t%d\t%d\t%s\t%s\n", session_num, trial_num, 
		      stimexm, stim_class, resps, reinfor, tod, date_out );
	    fflush (datafp);
	    if (DEBUG){printf("Trial data written\n");}
	    /*
	    //generate some output numbers
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


	      if (DEBUG){printf("flag: ouput numbers done\n");}
	      // Update summary data 
	     
	      if(freopen(dsumfname,"w",dsumfp)!= NULL){
		fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
		fprintf (dsumfp, "\tRESPONSE RATIOS (by stimulus)\n");
		fprintf (dsumfp, "\tStim     \t\tSession     \t\tTotals\n");
		for (i = 0; i<nstims;++i){
		  fprintf (dsumfp, "\t%s     \t\t%1.4f     \t\t%1.4f\n", 
			   stimulus[i].exemplar, stimRses[i].ratio, stimRtot[i].ratio);
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
	    */

	      /* End of trial chores */
	      
	      sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                   /* unblock termination signals */ 
	      //if(xresp)
	      //	if (stim_class==2 && resp_acc == 0){correction = 0;}else{correction = 1;}        /* set correction trial var */
	      //else 
	      //	correction = 1; /* make sure you don't invoke a correction trial by accident */
	      
	      //}while ( (atoi(hour) >= startH) && (atoi(hour) < stopH) ); /* correction trial loop */
		
	      C1_stim_number = C2_stim_number = -1;                                          /* reset the stim number for correct trial*/
	  }                                                        /*  trial loop */
	  
	  curr_tt = time (NULL);
	  
	  
	  /* Loop while lights out */

	  while ( (atoi(hour) < startH) || (atoi(hour) >= stopH) ){  
	    operant_write(box_id, HOUSELT, 0);
	    operant_write(box_id, FEEDER, 0);
	    operant_write(box_id, LFTKEYLT, 0);
	    operant_write(box_id, CTRKEYLT, 0);
	    operant_write(box_id, RGTKEYLT, 0);
	    sleep (sleep_interval);
	    curr_tt = time(NULL);
	    loctime = localtime (&curr_tt);
	    strftime (hour, 16, "%H", loctime);
	  }
	  operant_write(box_id, HOUSELT, 1);
	  curr_tt = time(NULL);
	  ++session_num;                                                                     
	
	  /* for (i = 0; i<nstims;++i){
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
	  */
	  f.hopper_wont_go_down_failures = f.hopper_already_up_failures = f.hopper_failures =f.response_failures = fed = reinfor_sum = 0;

	}while (1);// main loop
	
	curr_tt = time(NULL);
	
	
	/*  Cleanup */
	fclose(datafp);
	fclose(dsumfp);
	return 0;
}                         

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




