/* based on 2acmot */
/* This code will construct, for each trial, a stimulus that is sequence of wav files. */
/* The stim file should contain a list of motifs with the following organization:*/
/* 'class'  'motif.wav' 'percent select' */
/* 'class: 1=left, 2=right, 3 or greater = probe */
/* 'motif.wav':  name of a motif, 16 bit, 44.1kHz wav format */
/* 'percent select': percentage of time that motif.wav should be chosen for */
/* On each trial a new stimulus is contructed based on the transition probabily */
/* The stimulus will contain the same number of motifs as is given in the .stim file for each class*/
/* If the primary motif is not chosen for a given slot in the sequence */
/* one of the other motifs in the class will be chosen with equal probability p=(1-(pcnt/100))/nmots-1. */
/* NOTE: the above strategy permits the same motif to appear mulitple times in a sequence */
/**/
/**/
/*TQG:03/25/08  added capacity to randomly vary the length of the sequence on a given trial*/

#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <math.h>
#include <signal.h>
#include <sys/time.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include "/usr/local/src/operantio/operantio.c"
#include "/usr/local/src/audioio/audout.c"
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
#define MAXMOTS                  512           /* max number of motifs per probe class*/
#define MAXMOTSPERSEQ             32            /* max number of motifs per sequence */
#define MAXCLASS                 256            /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define TIMEOUT_DURATION         2000000       /* default duration of timeout in microseconds */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define HOPPER_DROP_MS           300           /*time for hopper to fall before checking that it did */


long feed_duration = FEED_DURATION;
long timeout_duration = TIMEOUT_DURATION;
int  trial_max = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME; 
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval = SLEEP_TIME;
const char exp_name[] = "2ACMOT";
int box_id = -1;
int flash = 0;
int do_rand_start = 0;
int do_rand_seq_len =0;
int xresp = 0;

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};

typedef struct {
  int hopper_failures;
  int hopper_wont_go_down_failures;
  int hopper_already_up_failures;
  int response_failures;
} Failures; 

typedef struct {
  char *exemplar[MAXMOTS];
  float hit[MAXMOTS];
  int count;
} Seqstim;

int feed(int rval, Failures *f);
int timeout(int rval);
int probeRx(int rval, int pval, Failures *f);


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
    if(DEBUG){fprintf(stderr,"feeder up\n");}
    return(1);
  }
  else{return (0);}
}

int probeRx(int dofeed, int dotimeout, Failures *f)
{
 int outcome=0;

 if(DEBUG){fprintf(stderr,"PROBE FEED: dofeed=%d, dotimeout=%d\n", dofeed, dotimeout);}

 outcome = 1+(int) (100.0*rand()/(RAND_MAX+1.0)); 
 if (outcome <= dofeed){
   /*do feed */
   doCheckedFeed(f); 
   if(DEBUG){fprintf(stderr,"FEED: from probRx\n");}
   return(1);
 }
 else if (outcome <= (dofeed+dotimeout)){
   /*do timeout*/
   operant_write(box_id, HOUSELT, 0);
   usleep(timeout_duration);
   operant_write(box_id, HOUSELT, 1);
   if(DEBUG){fprintf(stderr,"TIMEOUT: from probRx\n");}
   return(2);
 }
 else {
   /*do nothing*/
   if(DEBUG){fprintf(stderr,"NO RF: from probRx\n");}
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
  fprintf(stderr, "2ac_entropy usage:\n");
  fprintf(stderr, "  [-help] [-B int] [-f int] [-t int] [-w int] [-on int:int] [-off int:int] [-p int]\n"); 
  fprintf(stderr, "  [-Rf int:int:int] [-minmax int:int] [-Rn] [-S int] <filename>\n\n"); 
  
  fprintf(stderr, "     2ac_entropy will construct, for each trial, a stimulus that is a sequence of wav files.\n");
  fprintf(stderr, "     The stimulus will contain the same number of motifs as given in the .stim file for each class\n");
  fprintf(stderr, "      constiuent stimuli are chosen with thier associated probability in the order listed in the .stim file.\n");
  fprintf(stderr, "     If the primary motif is not chosen for a given slot in the sequence one of the other motifs in the class\n");
  fprintf(stderr, "      will be chosen with equal probability, i.e. p=(1-(percent select/100))/nmots-1.\n\n");
  fprintf(stderr, "      To have the sequence start at a random position (with equal probability) use the '-R' option \n");
  fprintf(stderr, "       -help                = show this help message\n");
  fprintf(stderr, "       -B int               = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "       -f                   = flash left & right pokelights during response window\n");
  fprintf(stderr, "       -t 'x'               = set the timeout duration to 'x' secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "       -w 'x'               = set the response window duration to 'x' secs (use an integer)\n");
  fprintf(stderr, "       -x                   = use this flag to enable correction trials for 'no-response' trials,\n");
  fprintf(stderr, "       -on int:int          = set hour:min for exp to start eg: '-on 7:30' (default is 7:00 AM)\n");
  fprintf(stderr, "       -off int:int         = set hour:min for exp to stop eg: '-off 19:45' (default is 7:00 PM)\n");
  fprintf(stderr, "                              To use daily sunset or sunrise times set 'on' or 'off' to '99'\n");
  fprintf(stderr, "       -Rn                  = randomize the start postion of the motif sequence\n");
  fprintf(stderr, "       -RandL               = choose between min:max motifs to appear in the stim sequence on a trial\n");
  fprintf(stderr, "       -p int               = set the presentation rate for the probe stimuli(e.g. 70=70%%; default is 0)\n");
  fprintf(stderr, "	                         class 1 & 2 bsl trials are presented with equal prob on 100-proberate%% of all trials.\n");
  fprintf(stderr, "       -Rf int:int:int:int  = set the reinforcement rate (%%) for bsl_corr:bsl_incorr:prb_food:prb_timeout\n");
  fprintf(stderr, "       -S int               = specify the subject ID number (required)\n");
  fprintf(stderr, "       filename             = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "                             where each line contains: 'Class' 'Wavfile' 'pcnt'\n");
  fprintf(stderr, "                             'Class'= 1 for LEFT-, 2 for RIGHT- baseline key assignment; 3 or higher for probe\n");
  fprintf(stderr, "                               NOTE: baseline and probe classes must be ordered consecutively w/o skipping any value\n");
  fprintf(stderr, "                             'Wavfile' is the name of the stimulus soundfile (must be 16bit, 44100 Hz sample rate\n");
  fprintf(stderr, "                             'pcnt' is the percentage of time that the given stim should be chosen for that slot in the sequence (use a real number w/ one decimal place)\n");
  fprintf(stderr, "\n");
  exit(-1);
}

/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin,
		       int *resp_wind, float *timeout_val, char **stimfname, int *prbrate,
		       int *CRf, int *XRf, int *prbRf, int *prbTO)
{
  int i=0;
  if (argc<3){
    do_usage();
    exit(-1);
  }
  
  for (i = 1; i < argc; i++){
    if (*argv[i] == '-'){
      if (strncmp(argv[i], "-B", 2) == 0) 
        sscanf(argv[++i], "%i", box_id);
      else if (strncmp(argv[i], "-S", 2) == 0)
        sscanf(argv[++i], "%i", subjectid);
      else if (strncmp(argv[i], "-x", 2) == 0)
	xresp = 1;
      else if (strncmp(argv[i], "-w", 2) == 0)
	sscanf(argv[++i], "%i", resp_wind);
      else if (strncmp(argv[i], "-t", 2) == 0)
	sscanf(argv[++i], "%f", timeout_val);
      else if (strncmp(argv[i], "-f", 2) == 0)
	flash = 1;
      else if (strncmp(argv[i], "-Rn", 3) == 0)
	do_rand_start = 1;
      else if (strncmp(argv[i], "-on", 3) == 0)
        sscanf(argv[++i], "%i:%i", starthour, startmin);
      else if (strncmp(argv[i], "-off", 4) == 0)
        sscanf(argv[++i], "%i:%i", stophour, stopmin);
      else if (strncmp(argv[i], "-p", 2) == 0)
	sscanf(argv[++i], "%i", prbrate);
      else if (strncmp(argv[i], "-Rf", 3) == 0)
	sscanf(argv[++i], "%i:%i:%i:%i", CRf, XRf, prbRf, prbTO);
      else if (strncmp(argv[i], "-RandL", 6) == 0){
	do_rand_seq_len = 1;
      }
      else if (strncmp(argv[i], "-help", 5) == 0){
        do_usage();
      }
      else{
        fprintf(stderr, "Unknown option: %s\t", argv[i]);
        fprintf(stderr, "Try '2ac_entropy -help'\n");
      }
    }
    else
      *stimfname = argv[i];
  }
  return 1;
}

/*****************************************************************************
 *   Get soundfile info and verify formats
 *****************************************************************************/
int verify_soundfile(char *sfname)
{
  SNDFILE *sfin=NULL;
  SF_INFO *finfo; 
  long unsigned int duration;

  finfo = (SF_INFO *) malloc(sizeof(SF_INFO));
  finfo->format=0;
  if(!(sfin = sf_open(sfname,SFM_READ,finfo))){
    fprintf(stderr,"error opening input file %s\n",sfname);
    return -1;
  }

 /*print out some info about the file you just openend */
 if(DEBUG){
   printf(" ---------- Stimulus parameters ------------ \n");
   printf ("Samples : %d\n", (int)finfo->frames) ;
   printf ("Sample Rate : %d\n", finfo->samplerate) ;
   printf ("Channels    : %d\n", finfo->channels) ;
 }
  
  /* check that some assumptions are met */
 if (finfo->frames > MAXFILESIZE){
   fprintf(stderr,"File is too large!\n");
   sf_close(sfin);
   return -1;
 }
 if (finfo->samplerate != 44100){
   fprintf(stderr, "Sample rate for %s is not equal to 44.1 KHz!\n", sfname);
   sf_close(sfin);
   return -1;
 } 

 if (finfo->channels != 1){
   fprintf(stderr, "Sound file %s is not mono!\n", sfname);
   sf_close(sfin);
   return -1;
 }
 /* make sure format is WAV */
 if((finfo->format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV){
   printf("Not a WAV file\n");
   sf_close(sfin);
   return -1;
 }
 duration = finfo->frames/44.1;
 free(finfo);
 sf_close(sfin);  
 return (duration);
}


/**********************************************************************************************
* make your sequence here                                                                     *
* make a sequence based on the motifs in *ps (in their order of entry) where the probability  *
* of mot-i at sequence position i is ps[i].hit, and the prob of another                       *
* moitf is (1-ps[i].hit)/(nmots-1)                                                            *
***********************************************************************************************/
int make_entseq(Seqstim *ps, int stimclass, char **tmpseq, char *outsfname)
{
  SNDFILE *sfin=NULL, *sfout=NULL;
  SF_INFO sfin_info, sfout_info; 
  
  int mot[MAXSTIM], i, newi, j, motnum=0, samp=0, seqlen=0, cindx=0, offset=0;
  float dohit=0.0;
  char sfname[256], seq[128];//*seq;
  short *inbuff[MAXMOTS], *obuff;
  sf_count_t incount[MAXMOTS], outframes=0, fcheck=0;
 
  *tmpseq = NULL;
  cindx = stimclass-1; 
  seqlen = ps[cindx].count;
 
   
  srand((unsigned int)time((time_t *)NULL));
 

  /*choose an offset to reorder the output array */ 
  if(do_rand_start){
    offset = (int)((seqlen+0.0)*rand()/(RAND_MAX+1.0));
    for (i=0;i<seqlen;i++){
      newi=i+offset;
      if(newi>seqlen-1)  /*wrap to start seq when offset runs past the end*/
	newi-=seqlen;
      dohit = (1+(int)(1000.0*rand()/(RAND_MAX+1.0)))/10.0;
      if(DEBUG==2){fprintf(stderr,"dohit:%g, hit:%g\n", dohit, ps[cindx].hit[newi]);}
      if(dohit<=ps[cindx].hit[newi])
	mot[i] = newi;
      else{ /* choose any motif other than ps[i]*/
	mot[i] = (int) ((seqlen+0.0)*rand()/(RAND_MAX+1.0) );
	while(mot[i] == newi){ /*choose antoher that does not match the primarly motif*/
	  if(DEBUG==2){fprintf(stderr, "found bad motif match mot[%d] can't be %d\n",i, newi);}
	  mot[i] = (int) ((seqlen+0.0)*rand()/(RAND_MAX+1.0));
	}
      }
      if(i==0)
	sprintf(seq,"%i", mot[i]);
      else
	sprintf(seq,"%s-%i", seq, mot[i]);
      if(DEBUG==2){fprintf(stderr, "mot %d is set to %d\n",i, mot[i]);}
    }
  }
  else{
    for (i=0;i<seqlen;i++){
      dohit = (1+(int)(1000.0*rand()/(RAND_MAX+1.0)))/10.0; 
      if(DEBUG==2){fprintf(stderr,"dohit:%g, hit:%g\n", dohit, ps[cindx].hit[i]);} 
      if(dohit<=ps[cindx].hit[i]){
	mot[i] = i;}
      else{ /* choose any motif other than ps[i]*/
	mot[i] = (int) ((seqlen+0.0)*rand()/(RAND_MAX+1.0) );
	while(mot[i] == i){ /*choose antoher that does not match the primarly motif*/
	  if(DEBUG==2){fprintf(stderr, "found motif match mot[%d]=%d\n",i, mot[i]);}
	  mot[i] = (int) ((seqlen+0.0)*rand()/(RAND_MAX+1.0));
	}
      } 

      if(i==0){
	sprintf(seq,"%d", mot[i]);
      }
      else{
	sprintf(seq,"%s-%i", seq, mot[i]); 
      }
      if(DEBUG==2){fprintf(stderr, "mot %d is set to %d\n",i, mot[i]);}
    }
   }

  *tmpseq = seq;
  if(DEBUG==2){fprintf(stderr, "STIMULUS SEQUENCE IS %s\n",*tmpseq);}

 if(DEBUG==2){
   for (i=0; i<seqlen; i++){
     motnum = mot[i];
     sprintf(sfname,"%s%s", STIMPATH, ps[cindx].exemplar[motnum]);
     fprintf(stderr, "motif %d is '%s'\n", i, sfname);
   }
 }

 if(do_rand_seq_len) /*choose a number between 2 and seqlen*/
   seqlen = 2+(int)((seqlen-2.0)*rand()/(RAND_MAX+1.0));
 //if(DEBUG==2){fprintf(stderr,"seqlen:%g\n", seqlen);}
 

 /*open each motif*/
 for (i=0; i<seqlen; i++){
   motnum=mot[i];
   sprintf(sfname,"%s%s", STIMPATH, ps[cindx].exemplar[motnum]);
   sfin_info.format=0;
   if(!(sfin = sf_open(sfname,SFM_READ,&sfin_info))){
     fprintf(stderr,"error opening input file %s\n",sfname);
     return -1;
   }
   
   /*read in the file */
   inbuff[i] = (short *) malloc(sizeof(int) * sfin_info.frames);
   incount[i] = sf_readf_short(sfin, inbuff[i], sfin_info.frames);
   sf_close(sfin);
   
   if(DEBUG==2){fprintf(stderr, "samples in: %lu \n", (long unsigned int)incount[i]);}
   outframes += incount[i];
   if(DEBUG==2){fprintf(stderr, "outframes is: %lu\n", (long unsigned int)outframes);}
 }

 obuff = (short *) malloc(sizeof(int)*outframes);
 for (i=0; i<seqlen; i++){
   for (j=0;j<incount[i];j++){
     obuff[samp++] = inbuff[i][j];
   }
   free(inbuff[i]); /*free the inbuffs*/
 }
 sfout_info.frames = outframes;
  
 /*this works as long as the files have been verified*/
 sfout_info.channels = sfin_info.channels;
 sfout_info.samplerate = sfin_info.samplerate;
 sfout_info.format = sfin_info.format;
 if(DEBUG==2){fprintf(stderr,"output file format:%x \tchannels: %d \tsamplerate: %d\n",sfout_info.format, sfout_info.channels, sfout_info.samplerate);}
 
 /*write the ouput file*/ 
 sprintf(outsfname,"%smotseq_tmp_box%d.wav", STIMPATH, box_id);
 if(!(sfout = sf_open(outsfname,SFM_WRITE,&sfout_info))){
   fprintf(stderr,"error opening output file '%s'\n",outsfname);
   return -1;
 }
 
 fcheck=sf_writef_short(sfout, obuff, outframes);
 if(fcheck!=outframes){
   fprintf(stderr,"UH OH!:I could only write %lu out of %lu frames!\n", (long unsigned int)fcheck, (long unsigned int)outframes);
   return -1;
 }
 else
   if(DEBUG==2){fprintf(stderr,"outframes: %lu \tfcheck: %lu \tduration: %g secs\n",
			(long unsigned int)outframes,(long unsigned int)fcheck,(double)outframes/sfout_info.samplerate);}
 sf_close(sfout);
 free(obuff);
 return 1;
}

/**********************************************************************
*                           MAIN
**********************************************************************/	
int main(int argc, char *argv[])
{
	FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
	char *stimfname = NULL;
	char *stimfroot, *pcm_name, *entseq=NULL, tmpExemplar[128];
	const char delimiters[] = " .,;:!-";
	char datafname[128], hour[16], min[16], month[16],day[16], year[16], dsumfname[128], stimftemp[128],outsf[128];
	char  buf[128], stimexm[128],fstim[256], timebuff[64], tod[256], date_out[256], buffer[30],temphour[16],tempmin[16];
	int stim_class, resp_sel, resp_acc, subjectid, period, foo; 
	int played, resp_wind=0,trial_num, session_num, i,j, correction, loop, stim_number; 
	int dosunrise=0,dosunset=0,starttime,stoptime,currtime, tmpClass=0, mindx=0,cindx=0;
	long unsigned int tmpDur=0;
	
	float timeout_val=0.0, resp_rxt=0.0, tmpHit=0.0;
	int respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
	int nstims=0, nclasses=0;
	int motmax =1, motmin=1, prbrate=0, CRf=0, XRf=0, prbRf=0, prbTO=0;
	int ret, do_prb, nprbclasses=0;


	float latitude = 32.82, longitude = 117.14;
	time_t curr_tt, rise_tt, set_tt;
	struct timeval stimoff,stimon, resp_window, resp_lag, resp_rt, stim_dur;
	struct tm *loctime;
	Failures f = {0,0,0,0};
	int left = 0, right= 0, center = 0, fed = 0;
	int reinfor_sum = 0, reinfor = 0;
	sigset_t trial_mask; 

	Seqstim seqstims[MAXCLASS], *pseqstims;

	struct resp{
	  int left;
	  int right;
	  int no;
	  int count;
	  float p_left;
	  float p_right;
	}classRses[MAXCLASS], classRtot[MAXCLASS];

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
        command_line_parse(argc,argv,&box_id,&subjectid,&starthour,&stophour,&startmin,&stopmin,
			   &resp_wind,&timeout_val,&stimfname,
			   &prbrate,&CRf,&XRf,&prbRf,&prbTO); 
       	if(DEBUG){
	  fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, xresp=%d, resp_wind=%d, do_rand_start=%d, timeout_val=%f flash=%d stimfile:%s, prbrate=%d, CRf=%d, XRf=%d, prbRf=%d, prbTO=%d, motmax=%d, motmin=%d\n",
		  box_id, subjectid, starthour, stophour, startmin, stopmin, xresp, resp_wind, do_rand_start, timeout_val, flash, stimfname,
		  prbrate, CRf, XRf, prbRf, prbTO, motmax, motmin);
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
	  fprintf(stderr, "\tERROR: try '2ac_entropy -help' for available options\n");
	  exit(-1);
	}
	if( (prbRf<0) || (prbTO<0) || (CRf<=0) || (XRf<=0) ){
	  fprintf(stderr, "\tYou must specify the reinforcemnt rate for all baseline and probe stimuli on the command line!\n"); 
	  fprintf(stderr, "\tERROR: try '2ac_entropy -help' for available options\n");
	  snd_pcm_close(handle);
	  exit(-1);
	}
	else if ((prbRf+prbTO)>100){
	  fprintf(stderr, "\tFATAL ERROR: The sum of the probe feed (prbRf) and timeout (prbTO) rates can't be greater than 100!\n"); 
	  snd_pcm_close(handle);
	  exit(-1);
	}
	if(motmax > MAXMOTSPERSEQ ){
	  fprintf(stderr, "\tyou can't have more than %d motifs in each sequence\n", MAXMOTSPERSEQ); 
	  fprintf(stderr, "\tERROR: try '2ac_entropy -help'\n");
	  snd_pcm_close(handle);
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
	if ((stimfp = fopen(stimfname, "r")) != NULL){
	  while (fgets(buf, sizeof(buf), stimfp))
	    nstims++;
	  fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
	  rewind(stimfp);
	                
	  for (i = 0; i < nstims; i++){
	    fgets(buf, 128, stimfp);
	    tmpDur=0.0; mindx =0; cindx=0; 
	    if((ret=sscanf(buf, "%d\%s\%g", &tmpClass, tmpExemplar, &tmpHit)) != 3){
	      printf("ERROR: bad line in '.stim' file (read in '%s'\n", buf);
	      snd_pcm_close(handle);
	      exit(-1);
	    } 
	    if(DEBUG){fprintf(stderr, "\n%d %s\n", tmpClass, tmpExemplar);}
      
	    /* count total stim and probe classes*/
		/*NOTE: these counters only work if classes are numbered consecutively in the .stim file w/o any skips */
	    if (nclasses<tmpClass){  
	      nclasses=tmpClass;
	      if (DEBUG){printf("total number of stimlus classes set to: %d\n", nclasses);}
	    }
	    if((tmpClass>=3) && (nprbclasses<tmpClass-2)){ /*count the number of probe classes*/
		  nprbclasses=tmpClass-2;
		  if (DEBUG){fprintf(stderr,"number of probe classes set to %d\n", nprbclasses);}
	    }
			
		/*verify soundfile*/
	    sprintf(fstim,"%s%s", STIMPATH, tmpExemplar);  
	    if((tmpDur = verify_soundfile(fstim))<1){
		 fprintf(stderr, "Unable to verify %s!\n",fstim );  
		 snd_pcm_close(handle);
		 exit(0); 
		} 
	    if(DEBUG){printf("soundfile %s verified, duration: %lu \tclass: %d\n", tmpExemplar, tmpDur, tmpClass);}

		/*now load up the seqstims struct*/
		cindx = tmpClass-1;
		seqstims[cindx].count++;
		mindx = seqstims[cindx].count-1;
		seqstims[cindx].exemplar[mindx] = (malloc(sizeof (tmpExemplar)));   
	    memcpy(seqstims[cindx].exemplar[mindx], tmpExemplar, sizeof(tmpExemplar));
	    seqstims[cindx].hit[mindx]=tmpHit;
		if(DEBUG){fprintf(stderr,"seqstims[%d].exemplar[%d]: %s ", cindx, mindx, seqstims[cindx].exemplar[mindx]);}
		if (DEBUG){printf("occurs at sequence postion '%d' in %g%% of all sequences in class %d\n", mindx+1, seqstims[cindx].hit[mindx], cindx+1);}
	  }
	}
	else{
	  fprintf(stderr,"Error opening stimulus input file '%s' or possible file format problem! \nTry '2ac_entropy -help' for proper formatting.\n", stimfname);  
	  snd_pcm_close(handle);
	  exit(0);     
	}
	fclose(stimfp);
	if(DEBUG){fprintf(stderr,"\n\nDone reading in stims. I found %d stims in %d classes,with %d probe classes\n", 
			  nstims,nclasses,nprbclasses);}
	
	/*check your seqstims struct */
	if(DEBUG){
	  for (i=0; i<nclasses; i++){
	    fprintf(stderr, "\nclass %d has %d motifs\n", i+1, seqstims[i].count);
	    for (j=0; j<seqstims[i].count; j++){
	      fprintf(stderr, "class:%d \tentry:%d \tname:%s \tpcnt: %g\n", i+1, j, seqstims[i].exemplar[j], seqstims[i].hit[j]);
	    }
	  }
	}/*done reading in stims*/
	pseqstims = seqstims;
 
    /* Don't allow correction trials on 'no response' trials when probe stimuli are presentedduring probe session */
    if(xresp==1 && nclasses>2){
	  fprintf(stderr, "ERROR!: You cannot use corrections on 'no-response' trials and probe stimuli in the same session.\n  Exiting now\n");
	  snd_pcm_close(handle);
	  exit(-1);
    }
	
    /*  Open & setup data logging files */
	curr_tt = time (NULL);
	loctime = localtime (&curr_tt);
	strftime (timebuff, 64, "%d%b%y", loctime);
	sprintf (stimftemp, "%s", stimfname);
	stimfroot = strtok (stimftemp, delimiters); 
	sprintf(datafname, "%i_%s.2ac_rDAT", subjectid, stimfroot);
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
	//tot_trial_num = 0;
	correction = 1;

	for(i=0;i<nclasses;i++){
	  classRses[i].left = classRses[i].right = classRses[i].no = classRses[i].count = 0;
	  classRtot[i].left = classRtot[i].right = classRtot[i].no = classRtot[i].count = 0;
	  classRses[i].p_left = classRtot[i].p_left =classRses[i].p_right = classRtot[i].p_right = 0.0;
	}
	if (DEBUG){printf("class counters zeroed!\n");} 

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
	    
	    do_prb = 1+(int) (100.0*rand()/(RAND_MAX+1.0) );                /*choose a baseline or probe trial*/
	    if(DEBUG){fprintf(stderr,"do_prb = %d\t prbrate = %d\n", do_prb, prbrate);}
	    if (do_prb <= prbrate) /*its a  probe trial*/
	      stim_class = (int) ((nprbclasses+0.0)*rand()/(RAND_MAX+1.0) ) + 3;       /*choose the probe class (1 to nprobeclasses+2) add 2 to map onto the .stim file nums*/ 
	    else /*its a baseline trial*/
	      stim_class = (int) ((2.0)*rand()/(RAND_MAX+1.0) ) + 1;       /*choose the baseline class for this trial */ 
	    
	    if(DEBUG){fprintf(stderr,"make a sequence %d motifs long using stim class %d \n", seqstims[stim_class-1].count, stim_class);}
	    if ((foo=make_entseq(pseqstims, stim_class, &entseq, outsf)) < 1){
	      fprintf(stderr, "ERROR!: problem building probe sequence on box %d trial %d\n", box_id, trial_num+1);
	      snd_pcm_close(handle);
	      fclose(datafp);
	      fclose(dsumfp);
	      exit(-1);
	    }	
	    if(DEBUG){fprintf(stderr, "make_entseq returned '%d'-- %s for stim class %d is %s\n", foo, outsf, stim_class, entseq);}
	    strcpy (stimexm, entseq); 
	    strcpy(fstim, outsf);
	    if(DEBUG){fprintf(stderr, "%s for stim class %d is %s\n", fstim, stim_class, stimexm);}
	    
	    do{                                             /* start correction trial loop */
	      resp_sel = resp_acc = resp_rxt = 0;           /* zero trial variables  */
	      ++trial_num;
	      curr_tt = time(NULL);
	      
	      /* Wait for center key press */
	      if (DEBUG){printf("\n\nWaiting for center key press\n");}
	      operant_write (box_id, HOUSELT, 1);        /* house light on */
	      right=left=center=0;
	      do{                                         
		nanosleep(&rsi, NULL);	               	       
		center = operant_read(box_id, CENTERPECK);   /*get value at center response port*/	
	      }while (center==0);  
	      
	      sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	      
	      /* play the stimulus*/
	      if (DEBUG){printf("START '%s'\n", stimexm);}
	      if(DEBUG){fprintf(stderr, "full stim is '%s', period is %d\n", fstim, period);}
	      gettimeofday(&stimon, NULL);
	      if (DEBUG){
		fprintf(stderr,"stim_on sec: %d \t usec: %d\n", (int)stimon.tv_sec, (int)stimon.tv_usec);}
	      if ((played = playwav(fstim, period))!=1){
		fprintf(stderr, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
			pcm_name, stimexm, asctime(localtime (&curr_tt)) );
		fprintf(datafp, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
			pcm_name, stimexm, asctime(localtime (&curr_tt)) );
		fclose(datafp);
		fclose(dsumfp);
		exit(-1);
	      }
	      if (DEBUG){printf("STOP  '%s'\n", stimexm);}
	      gettimeofday(&stimoff, NULL);
	      if (DEBUG){
		fprintf(stderr,"stim_off sec: %d \t usec: %d\n", (int)stimoff.tv_sec, (int)stimoff.tv_usec);
		timersub (&stimoff, &stimon, &stim_dur);
		fprintf(stderr,"stim_dur sec: %d \t usec: %d\n", (int)stim_dur.tv_sec, (int)stim_dur.tv_usec);
	      }

	      /* Wait for left or right response */
	      if (DEBUG){printf("flag: waiting for right/left response\n");}
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
        
        if((left==0) && (right==0) && (center == 0) && flash){
		  ++loop;
		  if(loop%80==0){
		    if(loop%160==0){ 
		      operant_write (box_id, LFTKEYLT, 1);
		      operant_write (box_id, RGTKEYLT, 1);
              operant_write (box_id, CTRKEYLT, 1);
		    }
		    else{
		      operant_write (box_id, LFTKEYLT, 0);
		      operant_write (box_id, RGTKEYLT, 0);
              operant_write (box_id, CTRKEYLT, 0);
		    }
		  }
		}
		gettimeofday(&resp_lag, NULL);
		if (DEBUG==3){printf("flag: values at right & left = %d %d\t", right, left);}
	      }while ( (left==0) && (right==0) && (timercmp(&resp_lag, &resp_window, <)) );
                   
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
	      if (DEBUG){printf("flag: stim_class = %d\n", stim_class);}
	      if (DEBUG){printf("flag: exit value left = %d, right = %d\n", left, right);}
	      

          if ((center == 1) && (left == 0) && (right == 0)) {
            resp_sel = 3;
            resp_acc = -1;
            reinfor = 0;
          }
          
	      if ( (left==0 ) && (right==0) && (center == 0)){
		resp_sel = 0;
		resp_acc = 2;
		++classRses[stim_class].no; ++classRtot[stim_class].no; 
		reinfor = 0;
		if (DEBUG){ printf("flag: no response to stim\n");}
	      }
	      else if (stim_class == 1){                                 /* LEFT STIM*/
		if (left != 0){
		  resp_sel = 1;
		  resp_acc = 1;
		  ++classRses[stim_class].left; ++classRtot[stim_class].left; 
		  if((reinfor=feed(CRf, &f))==1)
		    ++fed;
		  if (DEBUG){printf("flag: correct response to stim class 1\n");}
		}
		else if (right != 0){
		  resp_sel = 2;
		  resp_acc = 0;
		  ++classRses[stim_class].right; ++classRtot[stim_class].right; 
		  reinfor =  timeout(XRf);
		  if (DEBUG){printf("flag: incorrect response to stim class 1\n");}
		} 
	      }
	      else if (stim_class == 2){                           /*  RIGHT STIM*/
		if (left!=0){
		  resp_sel = 1;
		  resp_acc = 0;
		  ++classRses[stim_class].left; ++classRtot[stim_class].left; 
		  reinfor =  timeout(XRf);
		  if (DEBUG){printf("flag: incorrect response to stimtype 2\n");}
		}
		else if (right!=0){
		  resp_sel = 2;
		  resp_acc = 1;
		  ++classRses[stim_class].right; ++classRtot[stim_class].right; 
		  if((reinfor=feed(CRf, &f))==1)
		    ++fed;
		  if (DEBUG){printf("flag: correct response to stimtype 2\n");}
		} 
	      }
	      else if (stim_class >= 3){           /* stim_class >2 SO ITS A PROBE STIMULUS */
		if (left!=0){
		  resp_sel = 1;
		  if(DEBUG){printf("flag: LEFT response to PROBE\n");} 
		  resp_acc = 3;
		  ++classRses[stim_class].left; ++classRtot[stim_class].left; 
		  if((reinfor = probeRx(prbRf, prbTO, &f))==1)
		    ++fed;
		}
		else if (right!=0){
		  resp_sel = 2;
		  if(DEBUG){printf("flag: RIGHT response to PROBE\n");}
		  resp_acc = 3;
		  ++classRses[stim_class].right; ++classRtot[stim_class].right; 
		  if((reinfor =  probeRx(prbRf, prbTO, &f))==1)
		    ++fed;
		}
	      }
	      else{
		fprintf(stderr,"BAD STIM CLASS '%d' ENCOUNTERED, during reinforcement of box %d trial %d\n", stim_class, box_id, trial_num);
		snd_pcm_close(handle);
		fclose(datafp);
		fclose(dsumfp);
		exit(-1);
	      }
	    
	      /* Pause for ITI */
	      reinfor_sum += reinfor; 
	      operant_write(box_id, HOUSELT, 1);         /*make sure houselight is on*/
	      nanosleep(&iti, NULL);                     /*wait ITI*/
	      if (DEBUG){printf("flag: ITI passed\n");}
                                        
	      /* Write trial data to output file */
	      strftime (tod, 256, "%H%M", loctime);
	      strftime (date_out, 256, "%m%d", loctime);
	      
	      fprintf(datafp, "%d\t%d\t%d\t%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", 
		      session_num,trial_num,correction,stimexm,stim_class,
		      resp_sel,resp_acc,resp_rxt, reinfor,tod,date_out);
	      fflush (datafp);
	      if (DEBUG){printf("flag: trial data written\n");}
	      if(DEBUG){fprintf(stderr, "%d\t%d\t%d\t%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", 
				session_num,trial_num,correction,stimexm,stim_class,
				resp_sel,resp_acc,resp_rxt, reinfor,tod,date_out);
	      }
	      /*generate some output numbers for summaries*/
	      for (i=1;i<=nclasses;++i){
		classRses[i].p_left = (float) (classRses[i].left)/ (float)(classRses[i].left+classRses[i].right);
		classRtot[i].p_left = (float) (classRtot[i].left)/ (float)(classRtot[i].left+classRtot[i].right); 
		classRses[i].p_right = (float) (classRses[i].right)/ (float)(classRses[i].left+classRses[i].right);
		classRtot[i].p_right = (float) (classRtot[i].right)/ (float)(classRtot[i].left+classRtot[i].right); 
	      }
	      
	      /* Update summary data */
	      if(freopen(dsumfname,"w",dsumfp)!= NULL){
		fprintf (dsumfp, "SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
		fprintf (dsumfp, "\tNum. left, right and no resps, and percent correct(including correction trials)\n\n");
		fprintf (dsumfp, "\tClass\t\tToday\t\t\tTotals\n");
		for (i = 1; i<=nclasses;++i){
		  fprintf (dsumfp, "\t%d\t\t%d-%d-%d (%1.4f)\t\t%d-%d-%d (%1.4f)\n", i,
			 classRses[i].left, classRses[i].right, classRses[i].no, classRses[i].p_left,
			 classRtot[i].left, classRtot[i].right, classRtot[i].no, classRtot[i].p_left);
		}

		fprintf (dsumfp, "\n\nLast trial run @: %s\n", asctime(loctime) );
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
	      if (resp_acc == 0)
		correction = 0;
	      else
		correction = 1;                                              /* set correction trial var */
	      if ((xresp==1)&&(resp_acc == 2))
		correction = 0;                                              /* set correction trial var for no-resp */
	    
	      curr_tt = time(NULL);
	      loctime = localtime (&curr_tt);
	      strftime (hour, 16, "%H", loctime);
	      strftime(min, 16, "%M", loctime);
	      currtime=(atoi(hour)*60)+atoi(min);
	      if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
	    
	    }while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); /* correction trial loop */
	    
	    cindx = stim_number = -1;                                                /* reset the stim number for correct trial*/
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
	  for(i=0;i<=nclasses;i++){
	    classRses[i].left = classRses[i].right = classRses[i].no = classRses[i].count = 0;
	    classRses[i].p_left = classRses[i].p_right = 0.0;
	  } 
	  if (DEBUG){printf("class counters zeroed!\n");} 


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

	  
	 	  
	}while (1);// main loop forever if(1)
	
	
	/*  Cleanup */
	fclose(datafp);
	fclose(dsumfp);
	snd_pcm_close(handle);
	return 0;
}                         

