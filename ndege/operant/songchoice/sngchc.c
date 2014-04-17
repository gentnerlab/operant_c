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

#define DEBUG 3

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
#define MAXBP                    64            /* maximum number of breakpoints per stimulus */ 
#define MAXCLASS                 256           /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define CUE_STIM_INTERVAL        0.0           /* pause length between cue and stim in seconds */
#define TARGET_DURATION          2000000       /* duration of cue in microseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define TIMEOUT_DURATION         2000000       /* default duration of timeout in microseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define HOPPER_DROP_MS           300           /*time for hopper to fall before checking that it did */
#define MAXFILESIZE              5292000       /* max samples allowed in soundfile */


long feed_duration = FEED_DURATION;
long timeout_duration = TIMEOUT_DURATION;
long target_duration = TARGET_DURATION;
float cue_stim_interval = CUE_STIM_INTERVAL;
int  trial_max = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME; 
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval = SLEEP_TIME;
const char exp_name[] = "SONGCHOICE";
int box_id = -1;
int flash = 0;
int nchan = 2;

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timespec isi = { 0, CUE_STIM_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};

typedef struct {
  int hopper_failures;
  int hopper_wont_go_down_failures;
  int hopper_already_up_failures;
  int response_failures;
} Failures; 

snd_pcm_t *handle;

/* -------- Signal handling --------- */
static void termination_handler (int signum){
  snd_pcm_close(handle);
  fprintf(stdout,"closed pcm device: term signal caught: exiting\n");
  exit(-1);
}


/*pcm setup*/
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



/********************************************************
 *  playstereo                                          *
 *                                                      *
 * returns: 1 on successful play                        *
 *          -1 when soundfile does not play             *
 *          0 when soundfile plays with under.over run  *
 *******************************************************/
int playstereo(char *sfname1, char *sfname2, double breakpoint, double targlength, int targloc, int cueinterval, double period)
{
  
  SNDFILE *sf1, *sf2;
  SF_INFO *sfinfo1, *sfinfo2,*sfout_info;
  short *obuff, *obuff1, *obuff2;
  sf_count_t incount1, incount2;
  double padded;
  long pad = 0;
  int j= 0, loops = 0, cueframes, targframes, pauseframes, inframes, nspeak = 2, err, init;
  snd_pcm_uframes_t outframes, totframesout;
  unsigned short *ptr;
  int outsamprate = 44100;

  /* memory for SF_INFO structures */
  sfinfo1 = (SF_INFO *) malloc(sizeof(SF_INFO));
  sfinfo2 = (SF_INFO *) malloc(sizeof(SF_INFO));

  //fprintf(stderr, "trying to open '%s'\n", sfname); 
  /* open input files*/
  if(!(sf1 = sf_open(sfname1,SFM_READ,sfinfo1))){
    fprintf(stderr,"error opening input file %s\n",sfname1);
    return -1;
  }
  
  if(!(sf2 = sf_open(sfname2,SFM_READ,sfinfo2))){
    fprintf(stderr,"error opening input file %s\n",sfname2);
    return -1;
  }

  /* determine length inframes*/
  cueframes = (int) (breakpoint*outsamprate);
  pauseframes = (int) (cueinterval*outsamprate);
  targframes = (int) (targlength*outsamprate);
  inframes=(int)(cueframes+targframes);
  if(DEBUG){fprintf(stderr,"breakpoint:%g, cueframes:%i, pauseframes:%i targframes:%i, inframes:%i\n", 
		    breakpoint, cueframes, pauseframes, targframes, inframes);}
  /* allocate buffer memory */
  obuff1 = (short *) malloc(sizeof(int)*inframes);
  obuff2 = (short *) malloc(sizeof(int)*inframes);
  /* read the data */
  if(DEBUG){fprintf(stderr,"trying to sf_readf %d frames\n",(int)inframes);}
  incount1 = sf_readf_short(sf1, obuff1, inframes);
  if(DEBUG){fprintf(stderr,"got %d samples when I tried for %d from sf_readf_short()\n",(int)incount1, (int)inframes);}
  if(DEBUG){fprintf(stderr,"trying to sf_readf %d frames\n",(int)inframes);}
  incount2 = sf_readf_short(sf2, obuff2, inframes);
  if(DEBUG){fprintf(stderr,"got %d samples when I tried for %d from sf_readf_short()\n",(int)incount1, (int)inframes);}
  
  
  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use\n");
    sf_close(sf1);
    free(sfinfo1);
    free(obuff1);
    sf_close(sf2);
    free(sfinfo2);
    free(obuff2);
    return -1;
  }
  if (snd_pcm_reset(handle)<0){
    fprintf (stderr, "cannot reset audio interface for use\n");
    return -1;
  }
  /* pad the file size up to next highest period count*/    
  pad = (period * ceil((nspeak*(inframes+pauseframes))/ period))-(nspeak*(inframes+pauseframes));
  padded = (nspeak*inframes)+pad;
  outframes = padded;
  
  obuff = (short *) malloc(sizeof(int)*outframes);
  if(DEBUG){fprintf(stderr,"outframes:%d, nspeak:%d, padded:%g, targloc:%i, cut: %i\n", (int)outframes, nspeak, padded, targloc,(int)(nspeak*cueframes));}
  /* combine individual buffers into obuff */
  loops=0;
  for (j=0; j<outframes; j=j+nspeak){
    loops++;
    if (j<(nspeak*cueframes)){
      obuff[j] = obuff1[j/nspeak];
      obuff[j+1] = obuff1[j/nspeak];
    }
    else if ((j >= (nspeak*cueframes)) & (j < (nspeak*(cueframes+pauseframes)))){
      obuff[j] = 0;
      obuff[j+1] = 0;
    }
    else if ((j >= (nspeak*(cueframes+pauseframes)))&(targloc == 1)){  /*MAKE SURE THIS CORRESPONDS TO RIGHT SPEAKER TARGET - should work if rt spk is set to channel 1*/
      obuff[j] = obuff1[j/nspeak-pauseframes];
      obuff[j+1] = obuff2[j/nspeak-pauseframes];
    }
    else if ((j>= (nspeak*(cueframes+pauseframes)))&(targloc == 2)){  /*MAKE SURE THIS CORRESPONDS TO LEFT  SPEAKER TARGET*/
      obuff[j] = obuff2[j/nspeak-pauseframes];
      obuff[j+1] = obuff1[j/nspeak-pauseframes];
    }
    else{
      fprintf(stderr,"ERROR: incompatable target location %i\n",targloc);
      return -1;
    }
  }
  sfout_info = (SF_INFO *) malloc(sizeof(SF_INFO));
  sfout_info->channels = nspeak;
  sfout_info->samplerate = 44100;
  sfout_info->format = sfinfo1->format;
  if(DEBUG){fprintf(stderr,"output file format:%x \tchannels: %d \tsamplerate: %d\n",sfout_info->format, sfout_info->channels, sfout_info->samplerate);}

  ptr = obuff;
  totframesout = 0;
  if(DEBUG){printf("outframes is now %d\n", (int) outframes);}
  /*start the actual playback*/
  while (outframes > 0) {
    err = snd_pcm_writei(handle,ptr, outframes);
    if (err < 0) {
      init = 1;
      break;  /* skip one period */
    }
    if (snd_pcm_state(handle) == SND_PCM_STATE_RUNNING)
      init = 0;

    totframesout += err;
    ptr += err * nspeak;
    outframes -= err;
    if(DEBUG){printf("outframes is now %d\n", (int) outframes);}
    if (outframes == 0){
      if(DEBUG){printf("outframes is zero so I'll break\n");}
      break;
    }
    /* it is possible, that the initial buffer cannot store */
    /* all data from the last period, so wait a while */
  }
  if(DEBUG==3){
    printf("exited while writei loop, so what am I waiting for?\n");
    printf("frames not played: %d \n", (int)outframes);
    printf("frames played: %d \n", (int) totframesout);
  }

  /*free up resources*/
  free(sfinfo1);
  free(sfinfo2);
  free(sfout_info);
  free(obuff);
  free(obuff1);
  free(obuff2);
  return 1;
}

/***********************************************(box_id,HOPPEROP**********************************
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
  fprintf(stderr, "SONGCHOICE usage:\n");
  fprintf(stderr, "     [-help] [-B int] [-R int] [-fx] [-t int] [-w int] [-on int] [-off int] [-S int] <filename>\n\n");
  fprintf(stderr, "        -help        = show this help message\n");
  fprintf(stderr, "        -B int       = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "        -trg real    = set the target and distractor duration to 'x' secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -t real      = set the timeout duration to 'x' secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -w int       = set the response window duration to 'x' secs (use an integer)\n");
  fprintf(stderr, "        -on int:int      = set hour:min for exp to start eg: '-on 7:35' (default is 7AM, use 99 for sunrise)\n");
  fprintf(stderr, "        -off int:int     = set hour:min for exp to stop eg: '-off 19:01' (default is 7PM, use 99 for sunset)\n");
  fprintf(stderr, "        -S int       = specify the subject ID number (required)\n");
  fprintf(stderr, "        filename     = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "                       where each line is: 'Wavfile' 'Present_freq' 'Rnf_rate 'Pun_rate''\n");
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
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *resp_wind, float *timeout_val, float *targlength, float *cueint, char **stimfname)
{
  int i=0;
  
  for (i = 1; i < argc; i++){
    if (*argv[i] == '-'){
      if (strncmp(argv[i], "-B", 2) == 0) 
        sscanf(argv[++i], "%i", box_id);
      else if (strncmp(argv[i], "-S", 2) == 0)
        sscanf(argv[++i], "%i", subjectid);
      // else if (strncmp(argv[i], "-x", 2) == 0)
      //	xresp = 1;
      else if (strncmp(argv[i], "-w", 2) == 0){
	sscanf(argv[++i], "%i", resp_wind);
      }
      else if (strncmp(argv[i], "-trg", 4) == 0){ 
	sscanf(argv[++i], "%f", targlength);
      }      
      else if (strncmp(argv[i], "-t", 2) == 0){ 
	sscanf(argv[++i], "%f", timeout_val);
      }
      else if (strncmp(argv[i], "-qint", 5) == 0){ 
	sscanf(argv[++i], "%f", cueint);
      }
      //    else if (strncmp(argv[i], "-f", 2) == 0)
      //	flash = 1;
      else if (strncmp(argv[i], "-on", 3) == 0)
        sscanf(argv[++i], "%i:%i", starthour, startmin);
      else if (strncmp(argv[i], "-off", 4) == 0)
        sscanf(argv[++i], "%i:%i", stophour, stopmin);
      else if (strncmp(argv[i], "-help", 5) == 0){
        do_usage();
      }
      else{
        fprintf(stderr, "Unknown option: %s\t", argv[i]);
        fprintf(stderr, "Try 'SONGCHOICE -help'\n");
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
int main(int argc, char *argv[]){
  printf("here");
  FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
  char *stimfname = NULL;
  char *stimfroot, *tmpstr;
  const char delimiters[] = " .,;:!-";
  const char stereopcm [] = "doubledac";
  char datafname[128], hour[16], min[16], month[16],day[16], year[16], dsumfname[128], stimftemp[128], pcm_name[128];
  char  buf[256],distexm[128],targetloc[128],stimexm[128],fstim[256], fdist [256],timebuff[64],buffer[30],*bplist= NULL,
    temphour[16],tempmin[16],tod[256], date_out[256];
  int nstims, stim_class, stim_reinf, stim_punish, stim_nbps, resp_sel, resp_acc, subjectid, period, tot_trial_num,
    played, resp_wind=0,trial_num, session_num, i,j,k, correction, playval, distval, loop,stim_number,targloc, nbps,minlength,
    *playlist=NULL, totnstims=0,dosunrise=0,dosunset=0,starttime,stoptime,currtime,dist_number,breakval;
  float timeout_val=0.0, resp_rxt=0.0, breakpoint = 0.0, cueint = 0.0, targlength = 0.0;
  int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval stimoff, resp_window, resp_lag, resp_rt;
  struct tm *loctime;
  Failures f = {0,0,0,0};
  int left = 0, right= 0, center = 0, fed = 0;
  int reinfor_sum = 0, reinfor = 0;
  float probeCO=68.0;
  
  SNDFILE *sf;
  SF_INFO *sfinfo;
  struct stim {
    char exemplar[128];
    float breakpt[MAXBP];
    int reinf;
    int punish;
    int freq;
    int nbps;
    unsigned long int dur; 
    int num;
  }stimulus[MAXSTIM];
    
  struct resp{
    int C;
    int X;
    int no;
    int CbyBP[MAXBP];
    int XbyBP[MAXBP];
    int nobyBP[MAXBP];
    int countbyBP[MAXBP];
    int foo[4];
    int left;
    int center;
    int right;
    int count;
  }Rstim[MAXSTIM], Tstim[MAXSTIM];
  
  sigset_t trial_mask;
  
  return 1;
}

