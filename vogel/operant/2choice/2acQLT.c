// cue light cues bird to report presence of a song class in two songs played simultaneously. ec 11/24/07 // 
#include <stdio.h>
#include <stdlib.h>
#include </usr/local/src/sndfile.h>
#include <math.h>
#include <signal.h>
#include <sys/time.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include "/usr/local/src/operantio/operantio_6509.c"
#include </usr/local/src/sunrise.h>

#define DEBUG 0
 
#ifndef SQR
#define SQR(a)  ((a) * (a))
#endif

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
#define FEEDER    5
#define GREENLT   6
#define BLUELT    7
#define REDLT     8


/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             2             /* seconds from simulus end until NORESP is registered  (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  1024          /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 256            /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define TIMEOUT_DURATION         2000000       /* default duration of timeout in microseconds */
#define CUE_DURATION             2000000       /* cue duration in microseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define HOPPER_DROP_MS           300           /*time for hopper to fall before checking that it did */
#define OUTSR                    44100         /* sample rate for output soundfiles */


long feed_duration = FEED_DURATION;
long timeout_duration = TIMEOUT_DURATION;
long cue_duration = CUE_DURATION;
int  trial_max = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME; 
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval = SLEEP_TIME;
const char exp_name[] = "2ACQLT";
int box_id = -1;
int flash = 0;
int xresp = 0;
int nchan = 1;
int interleaved = 0;
int cueflag = 0;

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
 *  playstereoclass                                     *
 *                                                      *
 * returns: 1 on successful play                        *
 *          -1 when soundfile does not play             *
 *          0 when soundfile plays with under.over run  *
 *******************************************************/
int playclass1spk(char *sfname1, char *sfname2, float targlength, float lessdb, double period)
{
  SNDFILE *sf1, *sf2;
  SF_INFO *sfinfo1, *sfinfo2,*sfout_info;
  short *obuff, *obuff1, *obuff2;
  sf_count_t incount1, incount2;
  double LLrms2, LLrms1;
  long pad = 0;
  int j= 0, loops = 0, inframes, outsamps, nspeak = nchan, err, init;
  snd_pcm_uframes_t outframes, totframesout;
  unsigned short *ptr;
  int outsamprate = OUTSR;
  float foo, dist_db;
  int nbits, bw, i;
  signed int total1, total2;
  float dcoff2,dcoff1, peakrms, rms2,rms1, peak_db,db1,db2, newrms, scale, max,scale1,scale2;

  /* memory for SF_INFO structures */
  sfinfo1 = (SF_INFO *) malloc(sizeof(SF_INFO));
  sfinfo2 = (SF_INFO *) malloc(sizeof(SF_INFO));
  
  //fprintf(stderr, "trying to open '%s'\n", sfname); 
  /* open input files*/
  if(!(sf1 = sf_open(sfname1,SFM_READ,sfinfo1))){
    fprintf(stderr,"error opening input file %s\n",sfname1);
    free(sfinfo1);
    free(sfinfo2);
    return -1;
  }
  
  if(!(sf2 = sf_open(sfname2,SFM_READ,sfinfo2))){
    fprintf(stderr,"error opening input file %s\n",sfname2);
    free(sfinfo1);
    free(sfinfo2);
    sf_close(sf1);
    return -1;
  }

  /* determine length inframes*/
  inframes = (int) (targlength*outsamprate);
  if(DEBUG){fprintf(stderr,"inframes:%i\n",inframes);}
  /* allocate buffer memory */
  obuff1 = (short *) malloc(sizeof(int)*inframes);
  obuff2 = (short *) malloc(sizeof(int)*inframes);

  /* read the data */
  if(DEBUG){fprintf(stderr,"file one: trying to sf_readf %d frames,\t",(int)inframes);}
  incount1 = sf_readf_short(sf1, obuff1, inframes);
  if(DEBUG){fprintf(stderr,"got %d frames from sf_readf_short()\n",(int)incount1);}
  if(DEBUG){fprintf(stderr,"file two: trying to sf_readf %d frames,\t",(int)inframes);}
  incount2 = sf_readf_short(sf2, obuff2, inframes);
  if(DEBUG){fprintf(stderr,"got %d frames from sf_readf_short()\n",(int)incount2);}

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
  pad = (period * ceil((inframes)/ period)-(inframes));
  outframes = inframes+pad;
  outsamps =(int) nspeak*outframes;
  
  
  /*get the levels of the target soundfile*/
  nbits = 16;
  bw=6.0206*nbits;  /*we assume 16bit soundfiles for now*/
  
  total1 = 0;
  total2 = 0;
  for (j=0; j < inframes; j++){
    total1 += obuff1[j];
    total2 += obuff2[j];
  }
  dcoff1 = (float) (total1 * 1.0 /inframes);
  dcoff2 = (float) (total2 * 1.0 /inframes);
  //if(DEBUG){printf("DC offset for target/distractor2 is %f\n", dcoff1);}
  //if(DEBUG){printf("DC offset for the distractor is %f\n",dcoff2);}
  
  LLrms1 = 0.0;
  LLrms2 = 0.0;
  for (j=0; j<inframes; j++){
    LLrms1 += SQR(obuff1[j] - dcoff1);
    LLrms2 += SQR(obuff2[j] - dcoff2);
  }
  rms1 = sqrt(LLrms1 / (double)inframes) / (double) pow(2,15);
  rms2 = sqrt(LLrms2 / (double)inframes) / (double) pow(2,15);
  db1 = bw + (20.0 * log10(rms1) );
  db2 = bw + (20.0 * log10(rms2) );
  if(DEBUG){printf("target/distractor2 rms is %f dB SPL  ", db1);}
  if(DEBUG){printf("distractor rms is %f dB SPL\n", db2);}
  //if(DEBUG){printf("difference in sound levels (target - distractor): %f\n", (db1-db2));}
  
  
  /*if training, scale distractor sfs to the new levels */
  if (lessdb == -100.0){
    dist_db = 0.0;
    for (j=0; j<inframes; j++)
      obuff2[j] = 0;
    if(DEBUG){fprintf(stderr,"silencing distractor(s)\n");}
  }
  else if (lessdb < 0.0){
    dist_db=db1+lessdb;
    scale2=scale1=0.0;
    foo=(dist_db-bw)/20.0;
    newrms = pow(10,foo);
    scale2 = newrms/rms2;
    //scale1 = newrms/rms1;
    if(DEBUG){fprintf(stderr,"distractor db:%f newrms:%g,scaledist1 = %g; scaledist2: %g\n", dist_db, newrms, scale2, scale1);}
    for (j=0; j<inframes; j++)
      obuff2[j] = scale2 * obuff2[j];
  }

  obuff = (short *) malloc(sizeof(int)*(outsamps));
  if(DEBUG){fprintf(stderr,"outframes:%d, nspeak:%d, pad:%i, outsamps:%i\n", (int)outframes, nspeak, (int) pad, outsamps);}
  /* combine individual buffers into obuff */
  loops=0;
  for (j=0; j<(outsamps); j=j+nspeak){
    loops++;
    obuff[j] = obuff1[j]+obuff2[j];   /* 2nd dist or target */
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
    //fprintf(stderr, "PLAYSTEREO2: SPL in the combined soundfile exceeds 90 dB, SETTING THE MAX TO 90 dB \n");
    scale = 90/peakrms;
    for(i=0; i<outsamps;i++)
      obuff[j] = scale * obuff[j];
  }

   /* output info */
  sfout_info = (SF_INFO *) malloc(sizeof(SF_INFO));
  sfout_info->channels = nspeak;
  sfout_info->samplerate = outsamprate;
  sfout_info->format = sfinfo1->format;
  //if(DEBUG){fprintf(stderr,"output file format:%x \tchannels: %d \tsamplerate: %d\n",sfout_info->format, sfout_info->channels, sfout_info->samplerate);}
  
  ptr = obuff;
  totframesout = 0;
  if(DEBUG){fprintf(stderr,"outframes is now %d\n", (int) outframes);}
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
    //   if(DEBUG){printf("outframes is now %d\n", (int) outframes);}
    if (outframes == 0){
      if(DEBUG){fprintf(stderr,"outframes is zero so I'll break\n");}
      break;
    }
    /* it is possible, that the initial buffer cannot store */
    /* all data from the last period, so wait a while */
  }
  
  if(DEBUG==3){
    fprintf(stderr,"frames not played: %d \t", (int)outframes);
    fprintf(stderr,"frames played: %d \n", (int) totframesout);
  }
  
  /*free up resources*/
  sf_close(sf1);
  free(sfinfo1);
  sf_close(sf2);
  free(sfinfo2);
  free(sfout_info);
  free(obuff);
  free(obuff1);
  free(obuff2);

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
  fprintf(stderr, "2acQLT usage:\n");
  fprintf(stderr, "     [-help] [-B int] [-R int] [-fx] [-t int] [-w int] [-on int] [-off int] [-S int] <filename>\n\n");
  fprintf(stderr, "        -help        = show this help message\n");
  fprintf(stderr, "        -B int       = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "        -f           = flash left & right pokelights during response window\n");
  fprintf(stderr, "        -t 'x'       = set the timeout duration to 'x' secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -w 'x'       = set the response window duration to 'x' secs (use an integer)\n");
  fprintf(stderr, "        -x           = use this flag to enable correction trials\n");
  fprintf(stderr, "        -p real:real:real:real         = manually set trial probabilities: sound only: cue only: sound+cue correct: sound+cue catch.\n");
  fprintf(stderr, "                                        default = 0.50:0.50:0.00:0.00. Class 1 = red, class 2 = blue\n");
  fprintf(stderr, "        -on int:int      = set hour:min for exp to start eg: '-on 7:35' (default is 7AM, use 99 for sunrise)\n");
  fprintf(stderr, "        -off int:int     = set hour:min for exp to stop eg: '-off 19:01' (default is 7PM, use 99 for sunset)\n");
  fprintf(stderr, "        -db negative real   = change sound level of distractor: negative value REDUCES sound level relative to target\n");
  fprintf(stderr, "                               -db -100.0 silences distractor\n");
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
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *resp_wind, int *soundp_man, int *cuelightp_man, int *cuesoundp_man, int *catchp_man, float *hide_dist, float *timeout_val, char **stimfname)
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
      else if (strncmp(argv[i], "-db", 3) == 0) 
	sscanf(argv[++i], "%f", hide_dist);
      else if (strncmp(argv[i], "-p", 2) == 0){
	sscanf(argv[++i], "%i:%i:%i:%i", soundp_man, cuelightp_man, cuesoundp_man, catchp_man);
      }
      else if (strncmp(argv[i], "-on", 3) == 0)
        sscanf(argv[++i], "%i:%i", starthour, startmin);
      else if (strncmp(argv[i], "-off", 4) == 0)
        sscanf(argv[++i], "%i:%i", stophour, stopmin);
      else if (strncmp(argv[i], "-help", 5) == 0){
        do_usage();
      }
      else{
        fprintf(stderr, "Unknown option: %s\t", argv[i]);
        fprintf(stderr, "Try '2acQLT -help'\n");
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
	char  buf[128], stimexm[128], distexm[128],fstim[256], fdist[256],timebuff[64], tod[256], date_out[256], buffer[30],temphour[16],tempmin[16];
	int nclasses, nstims, stim_class, stim_reinf, stim_punish, resp_sel, resp_acc, subjectid, period, tot_trial_num, 
	  played, resp_wind=0,trial_num, session_num, i,j,k, correction, playval, loop, stim_number, cue[3], catch,
	  dist_class, distval, dist_number, dist2_class,distval2,dist_number2,trialtype,
	  *playlist=NULL, totnstims=0, mirror=0, dosunrise=0,dosunset=0,starttime,stoptime,currtime,playflag,cue_class,
	  cuetrialsR,cuetrialsCR,playtrialsR,playtrialsCR, cuetrialsT, cuetrialsCT, playtrialsT, playtrialsCT;
	float timeout_val=0.0, resp_rxt=0.0, cuelight_p = 0.50, sound_p=0.50, cuesound_p = 0.0,catch_p = 0.0, 
	  hide_dist=0.0, trialrand = 0.0, less_db=0.0, target_duration = 8.0, lessdb_trial;
	int cuelightp_man = -1, cuesoundp_man, soundp_man, catchp_man;
	int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
	float latitude = 32.82, longitude = 117.14;
	time_t curr_tt, rise_tt, set_tt;
	struct timeval stimoff, resp_window, resp_lag, resp_rt;
	struct tm *loctime;
	Failures f = {0,0,0,0};
	int left = 0, right= 0, center = 0, fed = 0;
	int reinfor_sum = 0, reinfor = 0;
	struct stim {
	  char exemplar[128];
	  int class;
	  int reinf;
	  int punish;
	  int freq;
	  unsigned long int dur; 
	  int num;
	}stimulus[MAXSTIM];
	struct resp{
	  int C;
	  int X;
	  int N;
	  int count;
	  int CX;
	  int q;
	  int catch;
	  int stim;
	}Rstim[MAXSTIM], Tstim[MAXSTIM], Rclass[MAXCLASS], Tclass[MAXCLASS];
	sigset_t trial_mask;
        cue[0] = GREENLT; cue[1]=REDLT; cue[2]=BLUELT; cue[3]=GREENLT;        

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
        command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &resp_wind, &soundp_man, &cuelightp_man, &cuesoundp_man, &catchp_man, &hide_dist, &timeout_val, &stimfname); 
       	if(DEBUG){
	  fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, xresp=%d, cue=%d, resp_wind=%d cuelightp_man=%d, timeout_val=%f flash=%d stimfile: %s\n",
		box_id, subjectid, starthour, stophour, startmin, stopmin, xresp, cueflag, resp_wind, cuelightp_man,timeout_val, flash, stimfname);
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
	  fprintf(stderr, "\tERROR: try '2acQLT -help' for available options\n");
	  exit(-1);
	}

	/*set some variables as needed*/
	if (resp_wind>0)
	  respoff.tv_sec = resp_wind;
	fprintf(stderr, "response window duration set to %d secs\n", (int) respoff.tv_sec);	
	if(timeout_val>0.0)
	  timeout_duration = (int) (timeout_val*1000000);
	fprintf(stderr, "timeout duration set to %d microsecs\n", (int) timeout_duration);
	if(hide_dist!=0.0){
	  less_db = hide_dist;
          fprintf(stderr, "distractor sound level manually set to %f relative to target\n", less_db);
	  if (hide_dist > 0.0){fprintf(stderr, "WARNING: distractor db input positive (%f), sound levels will not be altered", less_db);}
	}
        if(cuelightp_man > -1.0){
	  cuelight_p = (float)cuelightp_man/100.0;
	  sound_p = (float)soundp_man/100.0;
	  cuesound_p = (float)cuesoundp_man/100.0;
          catch_p = (float)catchp_man/100.0;        
	  if ((soundp_man+cuelightp_man+cuesoundp_man+catchp_man) != 100){
	    fprintf(stderr, "error:trial probabilities do not equal 1: sound_p = %f, cuelight_p = %f, cuesound_p = %f, catch_p = %f", sound_p, cuelight_p, cuesound_p, catch_p);
	    exit(-1);
	  }
	}
	fprintf(stderr, "trial percentages manually set to sound only:%f, cue only:%f, cue+sound correct:%f, cue+sound catch:%f \n", sound_p, cuelight_p, cuesound_p, catch_p);
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
	if((period= setup_pcmdev(pcm_name))<0){
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
	if(flash){fprintf(stderr, "!!WARNING: Flashing keylights during response window!!\n");}
	if(xresp){fprintf(stderr, "!!WARNING: Enabling correction trials for 'No' responses !!\n");}
	
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
	    sscanf(buf, "%d\%s\%d\%d\%d", &stimulus[i].class, stimulus[i].exemplar, &stimulus[i].freq, &stimulus[i].reinf, &stimulus[i].punish);
	    if((stimulus[i].freq==0) || (stimulus[i].reinf==0)|| (stimulus[i].punish==0)){
	      printf("ERROR: insufficnet data or bad format in '.stim' file. Try '2acQLT -help'\n");
	      exit(0);} 
	    totnstims += stimulus[i].freq;
	    if(DEBUG){printf("totnstims: %d\n", totnstims);}
	    
	    /* count stimulus classes*/
	    if (nclasses<stimulus[i].class){nclasses=stimulus[i].class;}
	    if (DEBUG){printf("nclasses: %d\n", nclasses);}
	    
	    /*check the reinforcement rates */
	    if (stimulus[i].class==1){
	      fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct LEFT responses\n", 
		      stimulus[i].exemplar, stimulus[i].reinf);
	      fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect RIGHT responses\n", 
		      stimulus[i].exemplar, stimulus[i].punish);
	    }
	    else if (stimulus[i].class==2){
	      fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct RIGHT responses\n", 
		      stimulus[i].exemplar, stimulus[i].reinf);
	      fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect LEFT responses\n", 
		      stimulus[i].exemplar, stimulus[i].punish);
	    }
	    else{
	      if((mirror==1)){
		if (stimulus[i].reinf>50){
		  fprintf(stderr, "ERROR!: To mirror food and timeout reinforcement values you must use a base rate less than or equal to 50%%\n");
		  snd_pcm_close(handle);
		  exit(0);
		}
		else{
		  fprintf(stderr, "p(food) reward and p(timeout) on probe trials with %s is set to %d%%\n", stimulus[i].exemplar, stimulus[i].reinf );
		}
	      }
	      else{fprintf(stderr, "Reinforcement rate on probe trials is set to %d%% pct for correct GO responses, 100%% for incorrect GO responses\n", 
			   stimulus[i].reinf);
	      }
	    }
	  }
	}
        else 
          {
            fprintf(stderr,"Error opening stimulus input file! Try 'gng_probe -help' for proper file formatting.\n");  
            snd_pcm_close(handle);
            exit(0);     
	  }
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
	sprintf(datafname, "%i_%s.2acQLT_rDAT2", subjectid, stimfroot);
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
	fprintf (datafp, "Sess#\tTrl#\tCrxn\tStimulus\tDistractor\tClass\tDist\tTrial\tDist dB\tR_sel\tR_acc\tRT\tReinf\tTOD\tDate\n");
	 
   /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
   ********************************************/
	session_num = 1;
	trial_num = 0;
	tot_trial_num = 0;
	correction = 1;

	/*zero out the response tallies */
	for(i = 0; i<nstims;++i){
          Rstim[i].C = Rstim[i].X = Rstim[i].N = Rstim[i].count =0;
	  Tstim[i].C = Tstim[i].X = Tstim[i].N = Tstim[i].count = 0;
        }
	for(i=1;i<=nclasses;i++){
	  Rclass[i].C = Rclass[i].X = Rclass[i].N = Rclass[i].count = Rstim[i].catch = Rstim[i].q = Rstim[i].stim = Rstim[i].CX =0;
	  Tclass[i].C = Tclass[i].X = Tclass[i].N = Tclass[i].count = Tstim[i].catch = Tstim[i].q = Tstim[i].stim = Tstim[i].CX =0;
        }	 	  
	cuetrialsR = cuetrialsCR = playtrialsR = playtrialsCR = 0;
	cuetrialsT = cuetrialsCT = playtrialsT = playtrialsCT = 0;
  
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
	    stim_class = 10;
	    while (stim_class > 2){
	      playval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));            /* select stim exemplar at random */ 
	      if (DEBUG){printf(" %d\t", playval);}
	      stim_number = playlist[playval];
	      stim_class = stimulus[stim_number].class;                               /* set stimtype variable */
	    }
            strcpy (stimexm, stimulus[stim_number].exemplar);                       /* get exemplar filename */
            stim_reinf = stimulus[stim_number].reinf;
            stim_punish = stimulus[stim_number].punish;
	    sprintf(fstim,"%s%s", STIMPATH, stimexm);                                /* add full path to file name */

	    /* select stim distractor at random */
	    dist_class = stim_class;
	    while (dist_class==stim_class){
	      distval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));
	      dist_number = playlist[distval];
	      dist_class = stimulus[dist_number].class;                            
	    }
	    strcpy(distexm, stimulus[dist_number].exemplar);
	    sprintf(fdist,"%s%s", STIMPATH, distexm);                                /* add full path to file name */

	    /* determine trial type */
	    cueflag = trialtype = cue_class = playflag = catch = 0;
	    lessdb_trial = less_db; 
	    trialrand = (float)(rand()/(RAND_MAX+0.0));
	    if (trialrand < cuelight_p){ /* cue only trial */
	      trialtype = 1;
	      cueflag = 1;
              cue_class = stim_class;
	      playflag = 0;
	    }
	    else if (trialrand < (cuelight_p + cuesound_p)){ /* correct cue+sound trial */
	      trialtype = 2;
	      cueflag = 1;
	      cue_class = stim_class;
	      playflag = 1;
	    }
	    else if (trialrand < (cuelight_p + cuesound_p + catch_p)){ /* catch trial */
	      trialtype = 3;
	      cueflag = 1;
	      catch = 1;
	      playflag = 1;
	      cue_class = stim_class;
	      dist2_class = stim_class;
	      while ((dist2_class == stim_class)||(distval == distval2)){
		distval2 = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));
		dist_number2 = playlist[distval2];
		dist2_class = stimulus[dist_number2].class;                            
	      }
	      strcpy(stimexm, stimulus[dist_number2].exemplar);
	      sprintf(fstim,"%s%s", STIMPATH, stimexm);                                /* add full path to file name */
	    }
	    else{      /* sound only trial */
	      trialtype = 0;
	      lessdb_trial = -100.0;
	      playflag = 1;
	    }

	    do{                                             /* start correction trial loop */
	      left = right = center = 0;        /* zero trial peckcounts */
	      resp_sel = resp_acc = resp_rxt = 0;                 /* zero trial variables        */
	      ++trial_num;++tot_trial_num;
	    

	      /* Wait for center key press */
	      if (DEBUG){printf("\n\nTrial Type %d\nWaiting for center key press\n", trialtype);}
	      operant_write (box_id, HOUSELT, 1);        /* house light on */
	      right=left=center=0;
	      do{                                         
		nanosleep(&rsi, NULL);	               	       
		center = operant_read(box_id, CENTERPECK);   /*get value at center response port*/	
	      }while (center==0);  

	      sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	      /* if cueflag, turn on cue light */
	      if(cueflag){
		operant_write(box_id, cue[cue_class], 1);
		if (DEBUG){fprintf(stderr,"cue_class %d, cue[cue_class] %d\n", cue_class, cue[cue_class]);}
		usleep(cue_duration);
		  if (DEBUG){fprintf(stderr,"displaying cue light for class %d\n", stim_class);}	 
		//operant_write(box_id, cue[cue_class], 0);
	      }
	      
	      /* if playflag, play the stimulus*/
	      if(playflag){
		if (DEBUG){printf("START '%s' with dist '%s'\n", stimexm,distexm);}
		if (DEBUG){fprintf(stderr, "target duration :'%1.2f'\n, altering distractor by %1.2f dB SPL\n",(float)target_duration, lessdb_trial); }
		if ((played = playclass1spk(fstim, fdist, target_duration, lessdb_trial,period))!=1){;
		fprintf(stderr, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
			pcm_name, stimexm, asctime(localtime (&curr_tt)) );
		fclose(datafp);
		fclose(dsumfp);
		exit(-1);
		}
	      }

              /* turn off cue - note: may want to change this so cue shuts off before song play */
	      operant_write(box_id, cue[cue_class], 0);
	      gettimeofday(&stimoff, NULL);
	      if (DEBUG){
		stimoff_sec = stimoff.tv_sec;
		stimoff_usec = stimoff.tv_usec;
		printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);}
	    
	      /* Wait for response */
	      if (DEBUG){printf("flag: waiting for response\n");}
	      timeradd (&stimoff, &respoff, &resp_window);
	      if (DEBUG){respwin_sec = resp_window.tv_sec;}
	      if (DEBUG){respwin_usec = resp_window.tv_usec;}
	      if (DEBUG){printf("resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);}
                    
	      loop=left=right=center=0;
	      do{
		nanosleep(&rsi, NULL);
		left = operant_read(box_id, LEFTPECK);
		right = operant_read(box_id, RIGHTPECK );
                center = operant_read(box_id, CENTERPECK);
		if((left==0) && (center ==0) && (right==0) && flash){
		  ++loop;
		  if (catch == 0){
		    if(loop%80==0){
		      if(loop%160==0){ 
			operant_write (box_id, LFTKEYLT, 1);
			operant_write (box_id, RGTKEYLT, 1);
		      }
		      else{
			operant_write (box_id, LFTKEYLT, 0);
			operant_write (box_id, RGTKEYLT, 0);
		      }
		    }
		  }
		  else if (catch ==1){
		    if(loop%80==0){
		      if(loop%160==0){ 
			operant_write (box_id, CTRKEYLT, 1);
		      }
		      else{
			operant_write (box_id, CTRKEYLT, 0);
		      }
		    }
		  }
		}
	       gettimeofday(&resp_lag, NULL);
	      }while ( (left==0) && (right==0) && (center==0) && (timercmp(&resp_lag, &resp_window, <)) );
                   
	      operant_write (box_id, LFTKEYLT, 0);    /*make sure the key lights are off after resp interval*/
	      operant_write (box_id, RGTKEYLT, 0);
	      operant_write (box_id, CTRKEYLT, 0);

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
              if (catch){
		Rclass[stim_class].catch++; Tclass[stim_class].catch++;
		if (center == 1){ /* ok response to catch trial */
		  resp_sel = 0;
		  resp_acc = 3;
		  reinfor=feed(stim_reinf, &f);
		  if (reinfor == 1) { ++fed;}
		}
		else if (left==1){
		  resp_sel = 1;
		  resp_acc = 0;
		  ++Rclass[stim_class].CX;++Tclass[stim_class].CX; 
		  reinfor =timeout(stim_punish);
		  if (cue_class == 1){
		    ++Rclass[stim_class].q; ++Tclass[stim_class].q;
		  }
		  else {
		    ++Rclass[stim_class].stim; ++Tclass[stim_class].stim;
		  }
		}
		else if (right==1){
		  resp_sel = 2;
		  resp_acc = 0;
		  ++Rclass[stim_class].CX;++Tclass[stim_class].CX; 
		  reinfor =timeout(stim_punish);
		  if (cue_class == 2){
		    ++Rclass[stim_class].q; ++Tclass[stim_class].q;
		  }
		  else {
		    ++Rclass[stim_class].stim; ++Tclass[stim_class].stim;
		  }
		}
		else {  
		  resp_sel = -1;
		  resp_acc = 2;
		  ++Rstim[stim_number].N;++Tstim[stim_number].N;
		  ++Rclass[stim_class].N;++Tclass[stim_class].N; 
		  reinfor = 0;
		  if (DEBUG){ printf("flag: no response to stimtype 1\n");}
		}
	      }
	      else if (catch ==0){
		if (DEBUG){printf("flag: stim_class = %d\n", stim_class);}
		if (DEBUG){printf("flag: exit value left = %d, right = %d\n", left, right);}
		if (stim_class == 1){                                 /* GO LEFT */                          
		  if ( (left==0 ) && (center == 0) && (right==0) ){
		    resp_sel = -1;
		    resp_acc = 2;
		    ++Rstim[stim_number].N;++Tstim[stim_number].N;
		    ++Rclass[stim_class].N;++Tclass[stim_class].N; 
		    reinfor = 0;
		    if (DEBUG){ printf("flag: no response to stimtype 1\n");}
		  }
		  else if (left != 0){
		    resp_sel = 1;
		    resp_acc = 1;	
		    ++Rstim[stim_number].C;++Tstim[stim_number].C;
		    ++Rclass[stim_class].C; ++Tclass[stim_class].C; 
		    reinfor=feed(stim_reinf, &f);
		    if (reinfor == 1) { ++fed;}
		    if (DEBUG){printf("flag: correct response to stimtype 1\n");}
		  }
		  else if (right != 0){
		    resp_sel = 2;
		    resp_acc = 0;
		    ++Rstim[stim_number].X;++Tstim[stim_number].X;
		    ++Rclass[stim_class].X; ++Tclass[stim_class].X; 
		    reinfor =  timeout(stim_punish);
		    if (DEBUG){printf("flag: incorrect response to stimtype 1\n");}
		  } 
		  else if (center !=0){
		    resp_sel = 0;
		    resp_acc = 0;
		    reinfor = timeout(stim_punish);
		  }
		}
		else if (stim_class == 2){                           /* GO RIGHT */
		  if ( (left==0) && (right==0) && (center==0) ){
		    resp_sel = 0;
		    resp_acc = 2;
		    ++Rstim[stim_number].N;++Tstim[stim_number].N;
		    ++Rclass[stim_class].N;++Tclass[stim_class].N; 
		    reinfor = 0;
		    if (DEBUG){printf("flag: no response to stimtype 2\n");}
		  }
		  else if (left!=0){
		    resp_sel = 1;
		    resp_acc = 0;
		    ++Rstim[stim_number].X;++Tstim[stim_number].X;
		    ++Rclass[stim_class].X; ++Tclass[stim_class].X; 
		    reinfor =  timeout(stim_punish);
		    if (DEBUG){printf("flag: incorrect response to stimtype 2\n");}
		  }
		  else if (right!=0){
		    resp_sel = 2;
		    resp_acc = 1;
		    ++Rstim[stim_number].C;++Tstim[stim_number].C;
		    ++Rclass[stim_class].C; ++Tclass[stim_class].C; 
		    reinfor=feed(stim_reinf, &f);
		    if (reinfor == 1) { ++fed;}
		    if (DEBUG){printf("flag: correct response to stimtype 2\n");}
		  } 
		  else if (center !=0){
		    resp_sel = 0;
		    resp_acc = 0;
		    reinfor = timeout(stim_punish);
		  }
		}
	      }
	      else{
		fprintf(stderr, "Unrecognized stimulus class: Fatal Error");
		fclose(datafp);
		fclose(dsumfp);
		exit(-1);
	      }

	      if (cueflag && (playflag == 0)&& (resp_acc < 2)){
		cuetrialsR++;cuetrialsT++;
		if(resp_acc == 1)
		  cuetrialsCR++;cuetrialsCT++;
	      }
	      if (playflag && (cueflag == 0) && (resp_acc < 2)){
		playtrialsR++;playtrialsT++;
		if(resp_acc == 1)
		  playtrialsCR++;playtrialsCT++;	
	      }
	      

	

	      /* Pause for ITI */
	      reinfor_sum = reinfor + reinfor_sum;
	      operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	      nanosleep(&iti, NULL);                                   /* wait intertrial interval */
	      if (DEBUG){printf("flag: ITI passed\n");}
                                        
                   
	      /*write trial data to output file */
	      strftime (tod, 256, "%H%M", loctime);
	      strftime (date_out, 256, "%m%d", loctime);
	      fprintf(datafp, "%d\t%d\t%d\t%s\t%s\t%d\t%d\t%d\t%.2f\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, 
		      correction, stimexm, distexm, stim_class, dist_class, trialtype, lessdb_trial,resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
	      fflush (datafp);
	      if (DEBUG){printf("flag: trial data written\n");}

	      /*generate some output numbers*/
	      for (i = 0; i<nstims;++i){
		Rstim[i].count = Rstim[i].X + Rstim[i].C;
		Tstim[i].count = Tstim[i].X + Tstim[i].C;
	      }
	      for (i = 1; i<=nclasses;++i){
		Rclass[i].count = Rclass[i].X + Rclass[i].C;
		Tclass[i].count = Tclass[i].X + Tclass[i].C;
	      }
	    
	      /* Update summary data */
	      if(freopen(dsumfname,"w",dsumfp)!= NULL){
		fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
		fprintf (dsumfp, "\tPROPORTION CORRECT RESPONSES (by stimulus, including correction trials)\n");
		fprintf (dsumfp, "\tStim\t\tCount\tToday     \t\tCount\tTotals\t(excluding 'no' responses)\n");
		for (i = 0; i<nstims;++i){
		  fprintf (dsumfp, "\t%s\t\t%d\t%1.4f     \t\t%d\t%1.4f\n", 
			   stimulus[i].exemplar, Rstim[i].count, (float)Rstim[i].C/(float)Rstim[i].count, Tstim[i].count, (float)Tstim[i].C/(float)Tstim[i].count );
		}
		fprintf (dsumfp, "\n\nPROPORTION CORRECT RESPONSES (by stim class, including correction trials)\n");
		for (i = 1; i<=nclasses;++i){
		  fprintf (dsumfp, "\t%d\t\t%d\t%1.4f     \t\t%d\t%1.4f\n", 
			   i, Rclass[i].count, (float)Rclass[i].C/(float)Rclass[i].count, Tclass[i].count, (float)Tclass[i].C/(float)Tclass[i].count );
		}
		fprintf (dsumfp, "\n\nPROPORTION INCORRECT CATCH RESPONSES (by stim class, including correction trials)\n");
		fprintf (dsumfp, "\tStim\t\tCount\tToday\tw/cue\tw/sound     \t\tCount\tTotals\tw/cue\tw/sound\n");
		for (i = 1; i<=nclasses;++i){
		  fprintf (dsumfp, "\t%d\t\t%d\t%1.4f\t%d\t%d     \t\t%d\t%1.4f\t%d\t%d\n", 
			   i, Rclass[i].catch, (float)Rclass[i].CX/(float)Rclass[i].catch, Rclass[i].q, Rclass[i].stim, Tclass[i].catch, (float)Tclass[i].CX/(float)Tclass[i].catch, Tclass[i].q, Tclass[i].stim );
		}
		fprintf (dsumfp, "\n\nPROPORTION CORRECT RESPONSES (by CUE, including correction trials)\n");
		fprintf (dsumfp, "\t\t\t%d\t%1.4f     \t\t%d\t%1.4f\n", 
			  cuetrialsR, (float)cuetrialsCR/(float)cuetrialsR, cuetrialsT, (float)cuetrialsCT/(float)cuetrialsT );	
		fprintf (dsumfp, "\n\nPROPORTION CORRECT RESPONSES (by SOUND ONLY, including correction trials)\n");
		fprintf (dsumfp, "\t\t\t%d\t%1.4f     \t\t%d\t%1.4f\n", 
			  playtrialsR, (float)playtrialsCR/(float)playtrialsR, playtrialsT, (float)playtrialsCT/(float)playtrialsT );
		fprintf (dsumfp, "Last trial run @: %s\n", asctime(loctime) );
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
	      if (xresp && (resp_acc == 0))
		correction = 0;
	      else
		correction = 1;                                              /* set correction trial var */
	      // if ((xresp==1)&&(resp_acc == 2))
	      //	correction = 0;                                              /* set correction trial var for no-resp */
	    
	      curr_tt = time(NULL);
	      loctime = localtime (&curr_tt);
	      strftime (hour, 16, "%H", loctime);
	      strftime(min, 16, "%M", loctime);
	      currtime=(atoi(hour)*60)+atoi(min);
	      if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
	    
	    }while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); /* correction trial loop */
	    
	    stim_number = -1;                                                /* reset the stim number for correct trial*/
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
	  for(i = 0; i<nstims;++i){
	    Rstim[i].C = Rstim[i].X = Rstim[i].N = Rstim[i].count = Rstim[i].catch = Rstim[i].q = Rstim[i].stim = Rstim[i].CX =0;
	  }
	  for(i=1;i<=nclasses;i++){
	    Rclass[i].C = Rclass[i].X = Rclass[i].N = Rclass[i].count = Rclass[i].catch = Rclass[i].q = Rclass[i].stim = Rclass[i].CX  = 0;
	  }
	  cuetrialsR = cuetrialsCR = playtrialsR = playtrialsCR = 0;
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

