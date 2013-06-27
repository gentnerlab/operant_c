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
#define MAXSTIM                  512           /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 256           /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define TARGET_DURATION          2.0           /* duration of cue in seconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define TIMEOUT_DURATION         2000000       /* default duration of timeout in microseconds */
#define CUE_DURATION             1000000       /* default duration of cue in microseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define HOPPER_DROP_MS           300           /*time for hopper to fall before checking that it did */
#define MAXFILESIZE              5292000       /* max samples allowed in input soundfile */
#define OUTSR                    44100         /* sample rate for output soundfiles */
#define MIN_CUELENGTH            1.5           /* minimum cue length - no breakpoints can be less than this */


long feed_duration = FEED_DURATION;
long timeout_duration = TIMEOUT_DURATION;
long cue_duration = CUE_DURATION;
float target_duration = TARGET_DURATION;
int  trial_max = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME; 
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval = SLEEP_TIME;
const char exp_name[] = "CLASSCHOICE1SPK";
int box_id = -1;
int flash = 0;
int nchan = 1;
int interleaved = 0;

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
/* struct timespec isi = { 0, CUE_STIM_INTERVAL}; */
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
 *  playstereoclass                                     *
 *                                                      *
 * returns: 1 on successful play                        *
 *          -1 when soundfile does not play             *
 *          0 when soundfile plays with under.over run  *
 *******************************************************/
int playclass1spk(char *sfname1, char *sfname2,  float targlength, float lessdb, int catch, double period)
{
  SNDFILE *sf1, *sf2;
  SF_INFO *sfinfo1, *sfinfo2,*sfout_info;
  short *obuff, *obuff1, *obuff2;
  sf_count_t incount1, incount2;
  double LLrms2, LLrms1;
  long pad = 0;
  int j= 0, loops = 0, inframes, outsamps, nspeak = 1, err, init;
  snd_pcm_uframes_t outframes, totframesout;
  unsigned short *ptr;
  int outsamprate = OUTSR;
  float foo, dist_db;
  int nbits, bw;
  signed int total1, total2;
  float dcoff2,dcoff1, rms2,rms1, db1,db2, newrms, scale1,scale2;
  
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
  else if (lessdb != 0.0){
    dist_db=db1+lessdb;
    scale2=scale1=0.0;
    foo=(dist_db-bw)/20.0;
    newrms = pow(10,foo);
    scale2 = newrms/rms2;
    scale1 = newrms/rms1;
    if(DEBUG){fprintf(stderr,"distractor db:%f newrms:%g,scaledist1 = %g; scaledist2: %g\n", dist_db, newrms, scale2, scale1);}
    for (j=0; j<inframes; j++)
      obuff2[j] = scale2 * obuff2[j];
  }
  /* catch trial edits: pick up here */
  obuff = (short *) malloc(sizeof(int)*(outsamps));
  if(DEBUG){fprintf(stderr,"outframes:%d, nspeak:%d, pad:%i, outsamps:%i\n", (int)outframes, nspeak, (int) pad, outsamps);}
  /* combine individual buffers into obuff */
  loops=0;
  for (j=0; j<(outsamps); j=j+nspeak){
    loops++;
    obuff[j] = obuff1[j]+obuff2[j];   /* 2nd dist or target */
  }
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
  fprintf(stderr, "CLASSCHOICE usage:\n");
  fprintf(stderr, "NOTE: CLASSCHOICE MUST run in a box equipped with two speakers\n");
  fprintf(stderr, "     [-help] [-B int] [-R int] [-fx] [-t int] [-w int] [-on int] [-off int] [-S int] <filename>\n\n");
  fprintf(stderr, "        -help        = show this help message\n");
  fprintf(stderr, "        -B int       = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "        -trg real    = set the target and distractor duration to 'x' secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -t real      = set the timeout duration to 'x' secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -w int       = set the response window duration to 'x' secs (use an integer)\n");
  fprintf(stderr, "        -on int:int      = set hour:min for exp to start eg: '-on 7:35' (default is 7AM, use 99 for sunrise)\n");
  fprintf(stderr, "        -off int:int     = set hour:min for exp to stop eg: '-off 19:01' (default is 7PM, use 99 for sunset)\n");
  fprintf(stderr, "        -S int           = specify the subject ID number (required)\n");
  fprintf(stderr, "        -db real         = change sound level of distractor: negative value REDUCES sound level relative to target\n");
  fprintf(stderr, "        -c real          = change catch trial percentage. default is 0.15 \n");
  fprintf(stderr, "	                   by given db value, use -100.0 for silence (use for training)\n");
  fprintf(stderr, "        -blk  int       = manually specify block size for trial type. Default = 100 trials.\n");
  fprintf(stderr, "        -qint int       = manually specify block size for trial type. Default = 100 trials.\n");
  fprintf(stderr, "        -I           = interleave trials.\n");
  fprintf(stderr, "        filename     = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "                       where each line is: 'Class' 'Wavfile' 'Present_freq' 'Rnf_rate 'Pun_rate'\n");
  fprintf(stderr, "                       'Wavfile' is the name of the stimulus soundfile (must be 44100 Hz sample rate\n");
  fprintf(stderr, "                       'Presfreq' is the overall rate (compared to the other stimuli) at which the stimulus is presented. \n"); 
  fprintf(stderr, "                         The actual integer rate for each stimulus is that value divided by the sum for all stimuli.\n");
  fprintf(stderr, "                         Use '1' for equal probablility \n");       
  fprintf(stderr, "                       'Rnf_rate' percentage of time that food is available following correct responses to this stimulus.\n");
  fprintf(stderr, "                       'Pun_rate' percentage of time that a timeout follows incorrect responses to this stimulus.\n"); 
  fprintf(stderr, "NOTE: Reinforcement and Punishment rates set individually for each stimulus in stimfile.\n");
  fprintf(stderr, "                       \n");
  exit(-1);
}

/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *resp_wind, float *timeout_val, float *targlength, int *blocksize_man, float *hide_dist, float *catchpercent_man, char **stimfname)
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
      else if (strncmp(argv[i], "-I", 2) == 0){
        interleaved=1;
      }
      else if (strncmp(argv[i], "-rf", 3) == 0){
        interleaved=1;
      }
      else if (strncmp(argv[i], "-blk", 4) == 0){
        sscanf(argv[++i], "%i", blocksize_man);
      }
      else if (strncmp(argv[i], "-c", 2) == 0){
	sscanf(argv[++i], "%f", catchpercent_man);
      }
      else if (strncmp(argv[i], "-trg", 4) == 0){ 
	sscanf(argv[++i], "%f", targlength);
      }      
      else if (strncmp(argv[i], "-t", 2) == 0){ 
	sscanf(argv[++i], "%f", timeout_val);
      }
      //    else if (strncmp(argv[i], "-f", 2) == 0)
      //	flash = 1;
      else if (strncmp(argv[i], "-db", 3) == 0) 
	sscanf(argv[++i], "%f", hide_dist);
      else if (strncmp(argv[i], "-on", 3) == 0)
        sscanf(argv[++i], "%i:%i", starthour, startmin);
      else if (strncmp(argv[i], "-off", 4) == 0)
        sscanf(argv[++i], "%i:%i", stophour, stopmin);
      else if (strncmp(argv[i], "-help", 5) == 0){
        do_usage();
      }
      else{
        fprintf(stderr, "Unknown option: %s\t", argv[i]);
        fprintf(stderr, "Try 'CLASSCHOICE -help'\n");
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
  FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
  char *stimfname = NULL;
  char *stimfroot;
  const char delimiters[] = " .,;:!-";
  const char stereopcm [] = "doubledac";
  char datafname[128], hour[16], min[16], month[16],day[16], year[16], dsumfname[128], stimftemp[128], pcm_name[128];
  char  buf[256],distexm[128],targetloc[128],stimexm[128],fstim[256], fdist [256],timebuff[64],buffer[30],
    temphour[16],tempmin[16],tod[256], date_out[256];
  int nstims, stim_reinf, stim_punish, resp_sel, resp_acc, subjectid, period, tot_trial_num, catchtrial,cue[4],targclass=0,
    played, resp_wind=0,trial_num, session_num, i,j,k, correction, playval, distval, loop,stim_number,targloc, minlength,distval2,targframes,
    *playlist=NULL, totnstims=0,dosunrise=0,dosunset=0,starttime,stoptime,currtime,dist_number,dist_number2,blocksize=100,blocksize_man=0,
    todaycount, totalcount, todaycorrect, totalcorrect, nclasses,stim_class,dist_class,target, ncatchR, ncatchT, xcatchR, xcatchT;
  float timeout_val=0.0, resp_rxt=0.0, cueint = 0.0, targlength = 0.0, lessdb = 0.0, hide_dist, 
    catchpercent = 0.15, catchpercent_man =0.0, catchrand, cue_stim_interval=0.0;
  int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
  int righttotR = 0, righttotT = 0, centtotR = 0, centtotT = 0,lefttotR = 0, lefttotT = 0;
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval stimoff, resp_window, resp_lag, resp_rt;
  struct tm *loctime;
  Failures f = {0,0,0,0};
  int left = 0, right= 0, center = 0, fed = 0;
  int reinfor_sum = 0, reinfor = 0, outsr = OUTSR;
  float thisrand= -1.0;
  SNDFILE *sf;
  SF_INFO *sfinfo;
  struct stim {
    char exemplar[128];
    float breakpt[MAXBP];
    int reinf;
    int class;
    int punish;
    int freq;
    unsigned long int dur; 
    int num;
  }stimulus[MAXSTIM];
  struct resp{
    int C;
    int catchX;
    int X;
    int no;
    int left;
    int center;
    int right;
    int count;
  }Rstim[MAXSTIM], Tstim[MAXSTIM];
  
  sigset_t trial_mask;
  cue[1]=REDLT; cue[2]=GREENLT; cue[3]=BLUELT;
  srand (time (0) );
  if(DEBUG){fprintf(stderr, "start classchoice");}
  
  /* set up termination handler*/
  sigemptyset (&trial_mask);
  sigaddset (&trial_mask, SIGINT);
  sigaddset (&trial_mask, SIGTERM);
  signal(SIGTERM, termination_handler);
  signal(SIGINT, termination_handler);
  
  
  /* Parse the command line */
  command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &resp_wind, &timeout_val, &targlength, &blocksize_man, &hide_dist, &catchpercent_man, &stimfname); 
  if(DEBUG){
    fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, resp_wind=%d, timeout_val=%f, targlength=%f, blocksize_man=%i,hide_dist=%f, catchpercent_man=%f, stimfile:%s",    box_id, subjectid, starthour, stophour, startmin, stopmin, resp_wind, timeout_val, targlength, blocksize_man, hide_dist, catchpercent_man, stimfname);
  }
  //  sprintf(pcm_name, "%s%d", stereopcm, box_id); /* for stereo */
  sprintf(pcm_name, "dac%i", box_id); /* for mono */
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
    fprintf(stderr, "\tERROR: try 'CC1SPK -help' for available options\n");
    exit(-1);
  }

  
  /* set some variables as needed*/
  if (resp_wind>0)
    respoff.tv_sec = resp_wind;
  fprintf(stderr, "response window duration set to %d secs\n", (int) respoff.tv_sec);	
  if(timeout_val>0.0)
    timeout_duration = (int) (timeout_val*1000000);
  fprintf(stderr, "timeout duration set to %1.2f seconds\n", (float) (timeout_duration/1000000));
  if(targlength>0.0)
    target_duration = (float) (targlength);
  fprintf(stderr, "target duration set to %1.2f seconds\n", (float) (target_duration));
 if(blocksize_man > 0)
    blocksize = blocksize_man;
  fprintf(stderr, "block size  manually set to %d trials\n", (blocksize));
  if(catchpercent_man>0.0)
    catchpercent = (float) (catchpercent_man);
  fprintf(stderr, "catch trial percentage manually set to %1.0f\n", 100.0*catchpercent);
  if(hide_dist!=0.0){
    lessdb = hide_dist;
    if (hide_dist > 0.0){fprintf(stderr, "WARNING: distractor will be INCREASED to %f dB SPL above target", lessdb);}
  }
  if (DEBUG){fprintf(stderr, "reduce distractor stimulus by %1.2f db SP\n", lessdb);}
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
    fprintf(stderr,"Sessions start at sunrise (Today: '%s')\n",buffer);
  }
  if(stophour==99){
    dosunset=1;
    set_tt = sunset(atoi(year), atoi(month), atoi(day), latitude, longitude);
    strftime(temphour, 16, "%H", localtime(&set_tt));
    strftime(tempmin, 16, "%M", localtime(&set_tt));
    stophour=atoi(temphour);
    stopmin=atoi(tempmin);
    strftime(buffer,30,"%m-%d-%Y  %T",localtime(&set_tt));
    fprintf(stderr,"Sessions stop at sunset (Today: '%s')\n",buffer);
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
    fprintf(stderr,"verify that box has two functional speakers\n");
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
  
  /* Read in the list of exemplars */
  nstims = nclasses = 0;  
  minlength = MAXFILESIZE; 
  if ((stimfp = fopen(stimfname, "r")) != NULL){
    while (fgets(buf, sizeof(buf), stimfp))
      nstims++;
    fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
    rewind(stimfp);
    for (i = 0; i < nstims; i++){
      fgets(buf, 128, stimfp);
      stimulus[i].freq = stimulus[i].reinf=0;
      sscanf(buf, "%d\%s\%d\%d\%d", 
	     &stimulus[i].class, stimulus[i].exemplar, &stimulus[i].freq, &stimulus[i].reinf, &stimulus[i].punish);
      strcpy (stimexm, stimulus[i].exemplar);                       /* get exemplar filename */ 
      totnstims += stimulus[i].freq;
      if(DEBUG){printf("totnstims: %d\n", totnstims);}
      sprintf(fstim,"%s%s", STIMPATH, stimexm);
      /* memory for SF_INFO structures */
      sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));
      /* open input files*/
      if(!(sf = sf_open(fstim,SFM_READ,sfinfo))){
	fprintf(stderr,"error opening input file %s\n", stimexm);
	return -1;
      }
      /*       if (DEBUG){ */
      /* 	/\*  print out some info about the file one *\/ */
      /* 	fprintf (stderr,"\n ---------- Stimulus parameters for %s ------------ \n", stimulus[i].exemplar); */
      /* 	  fprintf (stderr, "    Samples: %d\n", (int)sfinfo->frames) ; */
      /* 	  fprintf (stderr, "Sample Rate: %d\n", sfinfo->samplerate) ; */
      /* 	  fprintf (stderr, "   Channels: %d\n", sfinfo->channels) ; */
      /*       } */
		    
      /* count stimulus classes*/
      if (nclasses<stimulus[i].class){nclasses=stimulus[i].class;}
      if (DEBUG){printf("nclasses: %d\n", nclasses);}
	
      /*check the reinforcement rates */
      if(DEBUG){
	fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct responses\n", 
		stimulus[i].exemplar, stimulus[i].reinf);
	fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect responses\n", 
		stimulus[i].exemplar, stimulus[i].punish); 
      }
	
      /* check that some assumptions are met */
      if (sfinfo->frames > MAXFILESIZE){
	fprintf(stderr,"%s is too large!\n", stimexm);
	sf_close(sf);
	return -1;
      }
      else if (sfinfo->frames < minlength)
	minlength = sfinfo->frames;
      if (sfinfo->samplerate != 44100){
	fprintf(stderr, "Sample rate for %s is not equal to 44.1 KHz!\n", stimexm);
	sf_close(sf);
	return -1;
      }
      if (sfinfo->channels != 1){
	fprintf(stderr, "Sound file %s is not mono!\n", stimexm);
	sf_close(sf);
	return -1;
      }
      if ((sfinfo->format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV ){
	fprintf(stderr, "Sound file %s is not in wav format!\n", stimexm);
	sf_close(sf);
	return -1;
      }
      sf_close(sf);
      free(sfinfo);
    }
  }
  else{
    fprintf(stderr,"Error opening stimulus input file! Try 'SONGCHOICE -help' for proper file formatting.\n");  
    snd_pcm_close(handle);
    exit(0);     
  }
  fclose(stimfp);
  if(DEBUG){fprintf(stderr,"\nDone reading in stims; %d stims found\n", nstims);}
        
  /* Don't allow correction trials when probe stimuli are presented */
  /*   if(xresp==1 && doprobes==1){ */
  /*     fprintf(stderr, "ERROR!: You cannot use correction trials and probe stimuli in the same session.\n  Exiting now\n"); */
  /*     snd_pcm_close(handle); */
  /*     exit(-1); */
  /*   } */
  

  targframes = (int)targlength*outsr;
  if(targframes>minlength){
    fprintf(stderr,"WARNING: minumum target length shorter than target duration. changing target duration from %1.4f to %1.4f\n", targlength, minlength/(float)outsr);
    targlength = (float)minlength/(float)outsr;
  }
  /* build the stimulus playlist */
  if(DEBUG){fprintf(stderr,"flag: making the playlist\n");}
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
  if(DEBUG){fprintf(stderr,"there are %d stims in the playlist\n", totnstims);}
  
  /*  Open & setup data logging files */
  curr_tt = time (NULL);
  loctime = localtime (&curr_tt);
  strftime (timebuff, 64, "%d%b%y", loctime);
  sprintf (stimftemp, "%s", stimfname);
  stimfroot = strtok (stimftemp, delimiters);
  sprintf(datafname, "%i_%s_CC.rDAT", subjectid,stimfroot);
  sprintf(dsumfname, "%i.CCsummaryDAT", subjectid);
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
  fprintf (datafp, "Sess#\tTrl#\tCrxn\tClass\tTargstim\tDiststim\tTarglen\tCueSrc\tR_sel\tR_acc\tRT\tReinf\tTOD\tDate\n");
  
  /********************************************
    +++++++++++ Trial sequence ++++++++++++++
  ********************************************/
  session_num = 1;
  trial_num = 0;
  tot_trial_num = 0;
  correction = 1;
  
  /*zero out the response tallies */
  for(i = 0; i<nstims;++i){
    Rstim[i].C =  Rstim[i].no = Rstim[i].X = Rstim[i].count = 0;
    Tstim[i].C = Tstim[i].no = Tstim[i].X = Tstim[i].count = 0;
    ncatchR = ncatchT = xcatchR = xcatchT = 0;
  }
  curr_tt = time(NULL);
  loctime = localtime (&curr_tt);
  strftime (hour, 16, "%H", loctime);
  strftime(min, 16, "%M", loctime);
  if (DEBUG){printf("hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
  currtime=(atoi(hour)*60)+atoi(min);
	
  operant_write (box_id, HOUSELT, 1);        /* make sure houselight is on */

  do{                                                                               /* start the main loop */
    while ((currtime>=starttime) && (currtime<stoptime)){                          /* start main trial loop */
      if (DEBUG){fprintf(stderr,"\n\nminutes since midnight at loop start: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
      srand(time(0));
      /* determine trial type */
      if (interleaved)
	targclass = 1 + (int) ((nclasses+0.0)*rand()/(RAND_MAX+0.0));      /* if interleaved, select trial type at random */
      else if (totalcount%blocksize==0){
	if(blocksize%(2*blocksize)==0)
	  targclass = 1;
	else
	  targclass = 2;
      }

      /* select stim exemplar at random */
      stim_class = 0;
      while(stim_class != targclass){
	playval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));
        stim_number = playlist[playval]; 
	stim_class = stimulus[stim_number].class;
      }
      if (DEBUG){printf(" %d\t", playval);}
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

      /* if catch trial, select a second distractor from random */
      catchrand = (float)(rand()/(RAND_MAX+0.0));
      if (catchrand < catchpercent){
        dist_class = stim_class;
	while ((dist_class == stim_class)||(distval == distval2)){
	  distval2 = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));
	  dist_number2 = playlist[distval2];
	  dist_class = stimulus[dist_number2].class;                            
	}
	strcpy(stimexm, stimulus[dist_number2].exemplar);
	sprintf(fstim,"%s%s", STIMPATH, stimexm);                                /* add full path to file name */
	catchtrial = 1;
      }
      else{
	catchtrial = 0;
      }

      /* 	/\* select a target source at random *\/ */
      /*       thisrand = (rand()/(RAND_MAX+1.0)); */
      /*       if (DEBUG){fprintf(stderr, "loc rand: %1.2f: ", thisrand);} */
      /*       if (thisrand > 0.5){ */
      /* 	/\* target source is right *\/ */
      /* 	targloc = 1; */
      /* 	sprintf(targetloc, "right");} */
      /*       else if (thisrand <= 0.5) { */
      /* 	/\* target source is left *\/ */
      /* 	targloc = 2; */
      /* 	sprintf(targetloc, "left");} */
      /*       else{ */
      /* 	fprintf(stderr,"ERROR: incorrect cue choice\n"); */
      /* 	exit(0);} */
      /*       if(DEBUG){fprintf(stderr, "target is on %s\n", targetloc);} */
      

      /* create a fxn that will combine distractor and target at appropriate db */
      
      do{                                             /* start correction trial loop */
	left = right = center = 0;                    /* zero trial peckcounts       */
	resp_sel = resp_acc = resp_rxt = 0;           /* zero trial variables        */
	++trial_num;++tot_trial_num;
	if (DEBUG){fprintf(stderr, "catch:%d, crxn:%d ", catchtrial, correction);}

	/* Wait for center key press */
	if (DEBUG){fprintf(stderr,"\n\nWaiting for center  key press\n");}
	operant_write (box_id, HOUSELT, 1);        /* house light on */
	right=left=center=0;
	if (DEBUG){fprintf(stderr,"center key set to zero\n");}
	do{
	  nanosleep(&rsi, NULL);
	  center = operant_read(box_id, CENTERPECK);   /*get value at center response port*/
	}while (center==0);
	if (DEBUG){fprintf(stderr,"center key pressed\n");}
	sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/

	/* show cue light */
	operant_write(box_id, cue[stim_class], 1);
	usleep(cue_duration);	  
	if (DEBUG){fprintf(stderr,"displaying cue light for class %d\n", stim_class);}	 
	/* 	operant_write(box_id, cue[stim_class], 0); */ /* uncomment if you want cue only before trial */

	/* play the stimulus*/
	if (DEBUG){fprintf(stderr,"start sound file\n");}

	if (DEBUG){fprintf(stderr, "trying to playstereo with target: %s, distractor1 : %s, target location:'%s' target duration :'%1.2f'\n, altering distractor by %1.2f dB SPL", stimexm, distexm, targetloc, (float)target_duration, lessdb); }
	played = playclass1spk(fstim, fdist, (float)target_duration, lessdb, catchtrial,period);
	if (DEBUG){fprintf(stderr, "\nplayed: %d\n", played);}
	if(played != 1){
	  fprintf(stderr, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n",
		  pcm_name, stimexm, asctime(localtime (&curr_tt)) );
	  fclose(datafp);
	  fclose(dsumfp);
	  exit(-1);
	}
	
        operant_write(box_id, cue[stim_class], 0); /* turn off cue light. note - may want to change this later. */

	if (DEBUG){fprintf(stderr,"stop sound file\n");}
	gettimeofday(&stimoff, NULL);
	if (DEBUG){
	  stimoff_sec = stimoff.tv_sec;
	  stimoff_usec = stimoff.tv_usec;
	  fprintf(stderr,"stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);}
	
	/* Wait for response */
	if (DEBUG){fprintf(stderr, "flag: waiting for center response\n");}
	timeradd (&stimoff, &respoff, &resp_window);
	if (DEBUG){respwin_sec = resp_window.tv_sec;}
	if (DEBUG){respwin_usec = resp_window.tv_usec;}
	if (DEBUG){fprintf(stderr,"resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);}
	flash = 1;
	loop=left=right=center=0;
	do{
	  nanosleep(&rsi, NULL);
          center = operant_read(box_id, CENTERPECK);
	  if((center==0) && flash){
	    ++loop;
	    if(loop%80==0){
	      if(loop%160==0){
		operant_write (box_id, CTRKEYLT, 1);
	      }
	      else{
	       	operant_write (box_id, CTRKEYLT, 0);
	      }
	    }
	  }
	  gettimeofday(&resp_lag, NULL);
	}while ((center==0) && (timercmp(&resp_lag, &resp_window, <)) );/* note - don't care about right or left at this time, may way to change */
                   
	operant_write (box_id, LFTKEYLT, 0);    /*make sure the key lights are off after resp interval*/
	operant_write (box_id, RGTKEYLT, 0);
	operant_write (box_id, CTRKEYLT, 0);
	flash = 0;

	/* Calculate response time */
	curr_tt = time (NULL);
	loctime = localtime (&curr_tt);                     /* date and wall clock time of resp */
	timersub (&resp_lag, &stimoff, &resp_rt);           /* reaction time */
	if (DEBUG){resp_sec = resp_rt.tv_sec;}
	if (DEBUG){ resp_usec = resp_rt.tv_usec;}
	if (DEBUG){fprintf(stderr,"resp rt sec: %d \t usec: %d\n", resp_sec, resp_usec);}
	resp_rxt = (float) resp_rt.tv_sec + ( (float) resp_rt.tv_usec/1000000);  /* format reaction time */
	if (DEBUG){fprintf(stderr,"flag: resp_rxt = %.4f\n", resp_rxt);}
	
	strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
	strftime (min, 16, "%M", loctime);
	strftime (month, 16, "%m", loctime);
	strftime (day, 16, "%d", loctime);
	
	
	/* Consequate responses */
	if (DEBUG){fprintf(stderr,"flag: exit value left = %d, right = %d\n", left, right);}
        if (catchtrial){
          ncatchR++; ncatchT++;
	  /*         if  (left){ /\* incorrect, went on catch trial *\/ */
	  /*             resp_sel = 2; */
	  /* 	    resp_acc = 0; */
	  /*             xcatchR++; xcatchT++; */
	  /* 	    ++Rstim[stim_number].catchX;++Tstim[stim_number].catchX; */
	  /* 	    ++Rstim[stim_number].left;++Tstim[stim_number].left; */
	  /* 	    reinfor =  timeout(stim_punish); */
	  /* 	    if (DEBUG){fprintf(stderr,"flag: left response to catch trial, lights out\n");} */
	  /* 	  }           */
	  /*           if  (right){ /\* incorrect, went on catch trial *\/ */
	  /*             resp_sel = 1; */
	  /* 	    resp_acc = 0; */
	  /*             xcatchR++; xcatchT++; */
	  /* 	    ++Rstim[stim_number].catchX;++Tstim[stim_number].catchX; */
	  /* 	    ++Rstim[stim_number].right;++Tstim[stim_number].right; */
	  /* 	    reinfor =  timeout(stim_punish); */
	  /* 	    if (DEBUG){fprintf(stderr,"flag: right response to catch trial, lights out\n");} */
	  /* 	  } */
          if  (center){ /* incorrect, went on catch trial */
            resp_sel = 0;
	    resp_acc = 0;
	    xcatchR++; xcatchT++;
	    ++Rstim[stim_number].center;++Tstim[stim_number].center;
	    ++Rstim[stim_number].catchX;++Tstim[stim_number].catchX; 
	    reinfor =  timeout(stim_punish);
	    if (DEBUG){fprintf(stderr,"flag: center response to catch trial, no reinforcement\n");}
	  }
	  else {
	    resp_sel = -1;
	    resp_acc = 2;
	    ++Rstim[stim_number].no;++Tstim[stim_number].no;
	    reinfor = 0;
	    if (DEBUG){fprintf(stderr,"flag: no response to catch trial\n");}
	  }
	}
	else if(center){ 
	  resp_sel = 0;
	  resp_acc = 1;
	  ++Rstim[stim_number].C; ++Tstim[stim_number].C;
	  ++Rstim[stim_number].center;++Tstim[stim_number].center;
	  reinfor=feed(stim_reinf, &f);
	  if (reinfor == 1) { ++fed;}
	  if (DEBUG){fprintf(stderr,"flag: correct response\n");}
	}
	else if(center==0){
	  resp_sel = -1;
	  resp_acc = 2;
	  ++Rstim[stim_number].no;++Tstim[stim_number].no;
	  reinfor = 0;
	  if (DEBUG){fprintf(stderr,"flag: no response to target\n");}
	}
	else{
	  fprintf(stderr, "Unrecognized stimulus class: Fatal Error");
	  fclose(datafp);
	  fclose(dsumfp);
	  exit(-1);
	}	
	
	/* Pause for ITI */
	reinfor_sum = reinfor + reinfor_sum;
	operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	nanosleep(&iti, NULL);                                   /* wait intertrial interval */
	if (DEBUG){fprintf(stderr,"flag: ITI passed\n");}
	
	/* Write trial data to output file */
	strftime (tod, 256, "%H%M", loctime);
	strftime (date_out, 256, "%m%d", loctime);
	fprintf(datafp, "%d\t%d\t%d\t%d\t%s\t%s\t%1.2f\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, 
		correction, stim_class,stimexm, distexm, target_duration, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
	fflush (datafp);
	
	if (DEBUG){fprintf(stderr,"flag: trial data written\n");}
	/*generate some output numbers*/

        todaycount = totalcount = todaycorrect = totalcorrect = 0; 
        righttotR = lefttotR = centtotR = righttotT = lefttotT = centtotT = 0;
	for (i = 0; i<nstims; i++){
	  Rstim[i].count = Rstim[i].X + Rstim[i].C;
          todaycount = todaycount + Rstim[i].count; todaycorrect = todaycorrect + Rstim[i].C;
          righttotR = righttotR + Rstim[i].right; centtotR = centtotR + Rstim[i].center; lefttotR = lefttotR + Rstim[i].left;
	  Tstim[i].count = Tstim[i].X + Tstim[i].C;
          totalcount = totalcount + Tstim[i].count; totalcorrect = totalcorrect + Tstim[i].C;
          righttotT = righttotT + Tstim[i].right; centtotT = centtotT + Tstim[i].center; lefttotT = lefttotT + Tstim[i].left;
	}
	if(freopen(dsumfname,"w",dsumfp)!= NULL){
	  fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name);
	  fprintf (dsumfp, "\tPERCENT CORRECT RESPONSES (by stim, including crxn trials, not including no response trials)\n");
	  fprintf (dsumfp, "\tStim\t\tCount\tToday     \t\tCount\tTotals\n");
          fprintf (dsumfp, "\tallstims\t%d\t%1.4f     \t\t%d\t%1.4f\n\n",todaycount, (float)todaycorrect/(float)todaycount, totalcount, (float)totalcorrect/(float)totalcount );
	  for (i = 0; i<nstims;++i){
	    fprintf (dsumfp, "\t%s\t%d\t%1.4f     \t\t%d\t%1.4f\n",
		     stimulus[i].exemplar, Rstim[i].count, (float)Rstim[i].C/(float)Rstim[i].count, Tstim[i].count, (float)Tstim[i].C/(float)Tstim[i].count );
	  }
	  fprintf (dsumfp, "\n\tResponse by  Location\n");
	  fprintf (dsumfp, "\t\tRight\tCenter\tLeft   \t\tRight\tCenter\tLeft\n");
	  fprintf (dsumfp, "\tToday:\t%i\t%i\t%i     \tTotal:\t%i\t%i\t%i\n", righttotR, centtotR, lefttotR, righttotT, centtotT, lefttotT);
          fprintf (dsumfp, "\n\tIncorrect responses to Catch Trials\n");
	  fprintf (dsumfp, "\tCount\tToday   \tCount\tTotal\n");
	  fprintf (dsumfp, "\t%i\t%1.4f     \t%i\t%1.4f\n", ncatchR, (float) xcatchR/(float)ncatchR, ncatchT, (float)xcatchT/(float)ncatchT);
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
	
	
	if (DEBUG){fprintf(stderr,"flag: summaries updated\n");}
	
	
	
	/* End of trial chores */
	sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                  /* unblock termination signals */
        correction = 1; 

	if (resp_acc == 0){
	 correction = 0;}
	else
	 correction = 1;                                              /* set correction trial var */
	//if ((xresp==1)&&(resp_acc == 2)){
	//if(resp_acc ==2){
	//  correction = 0;}                                             /* set correction trial var for no-resp */
	
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
	currtime=(atoi(hour)*60)+atoi(min);
	if (DEBUG){fprintf(stderr,"minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
	
      }while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); /* correction trial loop */
      
      stim_number = -1;                                                /* reset the stim number for correct trial*/
    }                                                                  /* trial loop */
    
    /* You'll end up here if its night time or if you skip a day
       The latter only happens if the night window elapses between the time a stimulus is chosen
       and the time its actually played--i.e. if the subject doesn't initiates a cued trial during
       the night window, which is rare
    */

    
  /*   /\* Loop while lights out *\/ */
    if (DEBUG){fprintf(stderr,"minutes since midnight: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
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
      if (DEBUG){fprintf(stderr,"minutes since midnight: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
    }
    operant_write(box_id, HOUSELT, 1);
    
    /*reset some vars for the new day */
    ++session_num;
    trial_num = 0;
    for(i = 0; i<nstims;++i){
      Rstim[i].C = Rstim[i].X = Rstim[i].count = Rstim[i].left = Rstim[i].center = Rstim[i].right = 0;
      ncatchR = xcatchR = 0;
      righttotR = 0; righttotT = 0;
      centtotR = 0; centtotT = 0;
      lefttotR = 0; lefttotT = 0;
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
    
    
    
  }while (1);/*main trial loop*/
  
  
  /*  Cleanup */
  fclose(datafp);
  fclose(dsumfp);
  free(playlist);
  return 1;
}

