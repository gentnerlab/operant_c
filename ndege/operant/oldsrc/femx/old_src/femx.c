/*****************************************************************************
**  FEMX - operant procedure for female preference/choice task
**     
******************************************************************************


HISTORY:
3-25-01: TQG : this is an adaptation of the 2choice_bsl+tone code.  It uses
               the same techniques for monitoring behavior and playing  
	       stimuli.  The procedure is as follows:
	   (1) The app has 3 IR perches, & speakers associated with each.           
	   (2) S lands on one of the perches, and after some small
	       delay a signal is presented through the associated speaker. 
	   (3) Different sets of songs are associated with each perch, and one
	       is a 'silent' perch that does not trigger stimulus playback.
           (4) After some preset duration of stimuli have been presented (a block), 
	       the stimuli are switched. Generally this switch involves the location 
	       of the stimuli used in the first block, but may also involve the 
	       addition of new stimuli.
	   (5) The reinforcement schedule is set by the user. (see -help screen below)
	       WARNING! the max number of loops per trial (fn or vn max) must be less
	       than or equal to the smallest number of stimuli/class/block.  

	       example stimulus file format.... 'filename.stim'
		   #block class filename
		   1 1 stima.pcm
		   1 1 stimb.pcm
		   1 2 stimc.pcm
		   1 2 stimd.pcm    
		   2 2 stimc.pcm
		   2 2 stimd.pcm
		   1 1 stima.pcm
		   1 1 stime.pcm
		   

          During block 1, class 1 stimuli are assigned to perch 1, and class 2 stimuli
	  are assigned to perch 2; in block 2 class 1 goes to perch 2, and class 2 goes 
	  to perch 1.  Use the class variable to reflect the stimulus variaiton for which
	  you want to examine the preference/choice. 
	  Each perch should be associated with one stimulus class/block, 
	  Any line that starts with '#' is ignored 
	  There can be up MAXSTIM assigned to each perch/block (default is 100), 
	  and is bounded by available memory on the soundserver

**
**                   
*/




#define DEBUG 0
#define NODSP 0

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <math.h>
#include <sys/wait.h>
#include "/usr/local/src/aplot/dataio/pcmio.h"
#include "/usr/src/dio96/dio96.h"
#include "remotesound.h"


#define SIMPLEIOMODE 0
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


/* -------- INPUTS ---------*/
#define PERCH1    6
#define PERCH2    5
#define PERCH3    3   
#define NOPERCH   7
#define ALLCLEAR  7

/* -------- OUTPUTS --------*/
#define LFTPERCHLT	246  
#define CTRPERCHLT	245  
#define RGTPERCHLT	243  
#define HOUSELT		247   
#define ALLOFF		255        
#define ALLPERCHLT      242 

/* --------- OPERANT VARIABLES ---------- */
#define MAXSTIM                  128           /* maximum number of stimulus exemplars/perch/block */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define DACSAMPLERATE            20000         /* stimulus sampling rate */  
#define DACBITDEPTH              16            /* stimulus bit depth */
#define BLOCK_DURATION           900          /* length of a block, in seconds */
                                               // the default setting is for block duration set equal to the time on
                                               // the perches while stimuli are playing.
					       // you can set this to the total stimulus exposure in the code. 
#define RESPONSE_DELAY           1000000       /* delay btwn landing and the start of the 1st playback (in microsecs)*/
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             20            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */


long block_max = BLOCK_DURATION;
int startH = EXP_START_TIME; 
int stopH = EXP_END_TIME;
int sleep_interval = SLEEP_TIME;
int all_off = ALLOFF;
int house_lt = HOUSELT;
int samplerate = DACSAMPLERATE;  

//static char *bit_inA = "/dev/dio96/portaa";
//static char *bit_inB = "/dev/dio96/portac";
static char *bit_inC = "/dev/dio96/portbb";
//static char *bit_inD = "/dev/dio96/portca";
//static char *bit_inE = "/dev/dio96/portcc";

//static char *bit_outA = "/dev/dio96/portab";
//static char *bit_outB = "/dev/dio96/portba";
static char *bit_outC = "/dev/dio96/portbc";
//static char *bit_outD = "/dev/dio96/portcb";
//static char *bit_outE = "/dev/dio96/portda";

//static char *boxa = "A";
//static char *boxb = "B";
static char *boxc = "C";
//static char *boxd = "D";
//static char *boxe = "E";

const char exp_name[] = "FEMX";
char *stimpath = "/nfs/singin/soundserver/stims/";


char *bit_dev = NULL;
char *bit_out = NULL;
char *box = NULL;

const char delimiters[] = " .,;:!-";
unsigned char obit;

struct timespec iti = { INTER_TRIAL_INTERVAL, 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval resp_delay = { 0, RESPONSE_DELAY};



/* -------- Signal handling --------- */
int client_fd = -1;
int dsp1_fd = 0;
int dsp2_fd = 0;

static void sig_pipe(int signum)
{ printf("SIGPIPE caught\n"); client_fd = -1;}

static void sig_chld(int signum)
{
  wait(NULL);
}

static void termination_handler (int signum)
{
  if (NODSP==0){close_soundserver(dsp1_fd); close_soundserver(dsp2_fd);}
}




int playfork(int dspfd, int stimnum){
  int pid;
  if((pid = fork()) <  0) {exit (-1);}
  if (pid == 0){
      play2soundserver (dspfd, stimnum);
      exit(1);}
  signal(SIGCHLD, sig_chld);
  return 0;
}



int main(int ac, char *av[])
{
	FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
	PCMFILE *exm_fp = NULL;
	short *stimbuf;
	
	char *stimfname = NULL;
	char *stimfroot;

	char hour [16], subjectid[128];
	char datafname[128], dsumfname[128], stimftemp[128], full_stim[128], exemtemp[128];
	char buf[128], tempbuf[128], r_sched[128];
	char timebuff[64], tod[256], date_out[256];
	
	int dinfd, doutfd, loop, nsamples, tempsec;
	int blocktemp, classtemp, fn_stim, vn_min, vn_max, dsp1num, dsp2num;
	int nstims, nlines, i, j, nstimsP1B1, nstimsP2B1, nstimsP1B2, nstimsP2B2;
	int block_num, trial_num, session_num, t_class;
	int stim_num, k, index, post_t, sess_flag;
	int *P1list = NULL, *P2list = NULL, *t_list=NULL, *playlist=NULL;
	int perch1_cnt, perch2_cnt, perch3_cnt;

	float tempdur, block_dur, resp_dur, perch1_sum, perch2_sum, perch3_sum, resp_offset;
	float tot_perch1, tot_perch2, stim1_exp, stim2_exp;

	time_t curr_tt;
	struct timeval stim_off, stim_on, stim_dur, realtime, resp_on, resp_lag, temp_len; 
	struct timeval t_dur, resp_start, resp_stop, resp_len, post_lag, offset;
	struct tm *loctime;

	int bits = 0, t_location = 0, t_dsp = 0, max_loops = 0;
	int P1nstims = 0, P2nstims = 0;
	struct stim {
	  char exemplar[128];
	  int class;
	  int dur_sec;
	  long dur_usec;
	  int stimnum;
	};
	struct stim P1B1[MAXSTIM], P2B1[MAXSTIM], P1B2[MAXSTIM], P2B2[MAXSTIM]; 
	struct stim **P1=NULL, **P2=NULL, **T=NULL;

	sigset_t trial_mask;
	obit = 0;
	srand (time (0) );

/* Make sure the server can see you, and set up termination handler*/

 if (signal(SIGPIPE, sig_pipe) == SIG_ERR){
   perror("error installing signal handler for SIG_PIPE");
   exit (-1);
 }

 sigemptyset (&trial_mask);
 sigaddset (&trial_mask, SIGINT);
 sigaddset (&trial_mask, SIGTERM);
 
 signal(SIGTERM, termination_handler);
 signal(SIGINT, termination_handler);
 signal(SIGCHLD, sig_chld);
 
 /* Parse the command line */
 
 for (i = 1; i < ac; i++){
   if (*av[i] == '-'){              /*assign dsps 3 & 5 to box C*/
     if (strncmp(av[i], "-C", 2) == 0){
       if(NODSP==0){
	 if ((dsp1_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/dsp3")) == -1){ 
	   perror("FAILED connection to soundserver");
	   exit (-1);}
	 if ((dsp2_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/dsp5")) == -1){ 
	   perror("FAILED connection to soundserver");
	   exit (-1);}
       }
       else if(NODSP==1){
	 printf("Would have opened dsp3 here\n");    
	 printf("Would have opened dsp5 here\n");
       } 
       bit_dev = bit_inC;                             /*input port for box C = bb*/
       bit_out = bit_outC;                            /*output port for box C = bc*/
       box = boxc ;
       obit = 1;
     }
     else if (strncmp(av[i], "-S", 2) == 0){ 
       sscanf(av[++i], "%s", subjectid);
     }
     else if (strncmp(av[i], "-R", 2) == 0){
       sscanf(av[++i], "%s", tempbuf);
       if (strncmp(av[i], "FN", 2) == 0){
	 sprintf(r_sched, "%s", av[i]);
	 sscanf(av[++i], "%i", &fn_stim);}
       if (strncmp(av[i], "VN", 2) == 0){
	 sprintf(r_sched, "%s", av[i]);
	 sscanf(av[++i], "%i", &vn_min);
	 sscanf(av[++i], "%i", &vn_max); }
       else{
	 fprintf(stderr, "Unknown reinforcemnet schedule: %s\t", av[i]);
	 fprintf(stderr, "Try 'femx -h' for help\n");}
     }
     else if (strncmp(av[i], "-h", 2) == 0){
       fprintf(stderr, "femx usage:\n");
       fprintf(stderr, "    femx [-h] [-BOX_ID] [-R <schedule> <var>] [-S <id string>] <filename>\n\n");
       fprintf(stderr, "      -h               Show this help message\n\n");
       fprintf(stderr, "      -BOX_ID          Must be '-C' for now\n");
       fprintf(stderr, "      -R sched var     Specify the reinforcement schedule\n");
       fprintf(stderr, "                        'FN 10' play a fixed number of stimuli, in this case 10 \n");
       fprintf(stderr, "                        'VN 1 10' play 1 to 10 stimuli, uniform random variable\n");
       fprintf(stderr, "                        The default is VN 1 5\n");
       fprintf(stderr, "                        For both FN and VN, if the subject leaves the perch before\n");
       fprintf(stderr, "                         the alotted number of stimuli have played, playback is terminated\n");
       fprintf(stderr, "                         as soon as the current song is finished\n");
       fprintf(stderr, "      -S <value>       Specify the subject ID number (eg OR999)\n\n");
       fprintf(stderr, "      <filename>       Specify stimulus filename\n\n");
       fprintf(stderr, "   ALL FILEDS ARE REQUIRED!\n\n");
       exit(-1);
     }
     else{
       fprintf(stderr, "Unknown option: %s\t", av[i]);
       fprintf(stderr, "Try 'femx -h' for help\n");
     }
   }
   else{
     stimfname = av[i];
   }
 }
 if (obit == 0){
   fprintf(stderr, "ERROR: try 'femx -h' for help\n");
   exit(-1);
 }
 if(DEBUG==2){printf("done reading command line\n");}

 fprintf(stderr, "Loading stimulus file '%s' for box '%s' session\n", stimfname, box); 
 fprintf(stderr, "Subject ID number: %s\n", subjectid);
 fprintf(stderr, "Reinforcement schedule: %s ", r_sched );
 if (strncmp(r_sched, "FN", 2) == 0){ fprintf(stderr, "%i \n", fn_stim ); }
 if (strncmp(r_sched, "VN", 2) == 0){ fprintf(stderr, "%i:%i \n", vn_min, vn_max ); }
 
 
 /* Read in the list of exmplars from stimulus file */

 if(DEBUG==2){printf("trying to load the stimulus file\n");}

 nstimsP1B1 = nstimsP2B1 = nstimsP1B2 = nstimsP2B2 = nstims = 0;
 nstims = nlines = 0;
 
 if ((stimfp = fopen(stimfname, "r")) != NULL){
   while (fgets(buf, sizeof(buf), stimfp)){
     nlines++;
     if (strncmp(buf, "#", 1) != 0){
       nstims++;
     }
   }
   fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
   rewind(stimfp);
   dsp1num = dsp2num = 0;
   
   for (j = 0; j < nlines; j++){
     fgets(buf, 128, stimfp);
     if (strncmp(buf, "#", 1) != 0){
       sscanf(buf, "%d %d %s",&blocktemp, &classtemp, exemtemp);
       if(NODSP==1){
	 printf("Dummy loading '%s' to dsp\n", exemtemp);}
       if(NODSP==0){
	 if (blocktemp == 1){
	   if (classtemp == 1){
	     if (load2soundserver(dsp1_fd, dsp1num, exemtemp) != 0 ){
	       printf("Error loading stimulus file: %s\n", exemtemp);
	       exit(0);}
	   }
	   if (classtemp == 2){
	     if (load2soundserver(dsp2_fd, dsp2num, exemtemp) != 0 ){
	       printf("Error loading stimulus file: %s\n", exemtemp);
	       exit(0);}
	   }
	 }
	 if (blocktemp == 2){
	   if (classtemp == 1){
	     if (load2soundserver(dsp2_fd, dsp2num, exemtemp) != 0 ){
	       printf("Error loading stimulus file: %s\n", exemtemp);
	       exit(0);}
	   }
	   if (classtemp == 2){
	     if (load2soundserver(dsp1_fd, dsp1num, exemtemp) != 0 ){
	       printf("Error loading stimulus file: %s\n", exemtemp);
	       exit(0);}
	   }
	 }
       }
       if(DEBUG==2){ printf(" block: %i\t", blocktemp); }
       if(DEBUG==2){ printf(" stimulus class: %i\t", classtemp); }
       if(DEBUG==2){ printf(" stimulus file: %s\n", exemtemp); }
       
       if ((blocktemp == 1) && (classtemp == 1)){
	 P1B1[nstimsP1B1].class = classtemp;
	 strcpy(P1B1[nstimsP1B1].exemplar, exemtemp);
	 P1B1[nstimsP1B1].stimnum = dsp1num;
	 dsp1num++;
	 /*open the stimfile & get the duration*/
	 sprintf(full_stim ,"%s%s", stimpath, exemtemp); 
	 if ((exm_fp = pcm_open(full_stim, "r")) == NULL)
	   {fprintf(stderr, "Error opening stimulus file '%s'. ", full_stim);}
	 if ((pcm_read(exm_fp, &stimbuf, &nsamples)) == -1)
	   {fprintf(stderr, "Error reading stimulus file '%s'. ", full_stim);}
	 pcm_close (exm_fp);
	 if(DEBUG==2){ printf(" number of samples: %i\t", nsamples); }
	 tempdur = (float)nsamples / (float)samplerate;
	 if(DEBUG==2){ printf(" tempdur: %2.10f\t", tempdur); }
	 tempsec = floor(tempdur);
	 if(DEBUG==2){ printf(" number of seconds: %i\t", tempsec); }
	 P1B1[nstimsP1B1].dur_sec = (time_t) tempsec;
	 P1B1[nstimsP1B1].dur_usec = (long) ((tempdur - tempsec) * 1000000);
	 if(DEBUG==2){ printf(" number of microsecs: %li\n", P1B1[nstimsP1B1].dur_usec ); }
	 nstimsP1B1++;
       }
       if ((blocktemp == 1) && (classtemp == 2)){
	 P2B1[nstimsP2B1].class = classtemp;
	 strcpy(P2B1[nstimsP2B1].exemplar, exemtemp);
	 P2B1[nstimsP2B1].stimnum = dsp2num;
	 dsp2num++;
	 /*open the stimfile & get the duration*/
	 sprintf(full_stim ,"%s%s", stimpath, exemtemp); 
	 if ((exm_fp = pcm_open(full_stim, "r")) == NULL){
	   fprintf(stderr, "Error opening stimulus file '%s'. ", full_stim);
	 }
	 if ((pcm_read(exm_fp, &stimbuf, &nsamples)) == -1){
	   fprintf(stderr, "Error reading stimulus file '%s'. ", exemtemp);
	 }
	 pcm_close (exm_fp);
	 if(DEBUG==2){ printf(" number of samples: %i\t", nsamples); }
	 tempdur = (float)nsamples / (float)samplerate;
	 if(DEBUG==2){ printf(" tempdur: %2.10f\t", tempdur); }
	 tempsec = floor(tempdur);
	 if(DEBUG==2){ printf(" number of seconds: %i\t", tempsec); }
	 P2B1[nstimsP2B1].dur_sec = (time_t) tempsec;
	 P2B1[nstimsP2B1].dur_usec = (long)((tempdur - tempsec) * 1000000);
	 if(DEBUG==2){ printf(" number of microsecs: %li\n", P2B1[nstimsP2B1].dur_usec ); }
	 nstimsP2B1++;
       }
       if ((blocktemp == 2) && (classtemp == 2)){
	 P1B2[nstimsP1B2].class = classtemp;
	 strcpy(P1B2[nstimsP1B2].exemplar, exemtemp);
	 P1B2[nstimsP1B2].stimnum = dsp1num;
	 dsp1num++;
	 /*open the stimfile & get the duration*/
	 sprintf(full_stim ,"%s%s", stimpath, exemtemp); 
	 if ((exm_fp = pcm_open(full_stim, "r")) == NULL){
	   fprintf(stderr, "Error opening stimulus file '%s'. ", full_stim);
	 }
	 if ((pcm_read(exm_fp, &stimbuf, &nsamples)) == -1){
	   fprintf(stderr, "Error reading stimulus file '%s'. ", full_stim);
	 }
	 pcm_close (exm_fp);
	 if(DEBUG==2){ printf(" number of samples: %i\t", nsamples); }
	 tempdur = (float)nsamples / (float)samplerate;
	 if(DEBUG==2){ printf(" tempdur: %2.10f\t", tempdur); }
	 tempsec = floor(tempdur);
	 if(DEBUG==2){ printf(" number of seconds: %i\t", tempsec); }
	 P1B2[nstimsP1B2].dur_sec = (time_t) tempsec;
	 P1B2[nstimsP1B2].dur_usec = (long)((tempdur - tempsec) * 1000000);
	 if(DEBUG==2){ printf(" number of microsecs: %li\n", P1B2[nstimsP1B2].dur_usec ); }
	 nstimsP1B2++;
       }
       if ((blocktemp == 2) && (classtemp == 1)){
	 P2B2[nstimsP2B2].class = classtemp;
	 strcpy(P2B2[nstimsP2B2].exemplar, exemtemp);
	 P2B2[nstimsP2B2].stimnum = dsp2num;
	 dsp2num++;
	 /*open the stimfile & get the duration*/
	 sprintf(full_stim ,"%s%s", stimpath, exemtemp); 
	 if ((exm_fp = pcm_open(full_stim, "r")) == NULL){
	   fprintf(stderr, "Error opening stimulus file '%s'. ", full_stim);
	 }
	 if ((pcm_read(exm_fp, &stimbuf, &nsamples)) == -1){
	   fprintf(stderr, "Error reading stimulus file '%s'. ", full_stim);
	 }
	 pcm_close (exm_fp);
	 if(DEBUG==2){ printf(" number of samples: %i\t", nsamples); }
	 tempdur = (float)nsamples / (float)samplerate;
	 if(DEBUG==2){ printf(" tempdur: %2.10f\t", tempdur); }
	 tempsec = floor(tempdur);
	 if(DEBUG==2){ printf(" number of seconds: %i\t", tempsec); }
	 P2B2[nstimsP2B2].dur_sec = (time_t) tempsec;
	 P2B2[nstimsP2B2].dur_usec = (long)((tempdur - tempsec) * 1000000);
	 if(DEBUG==2){ printf(" number of microsecs: %li\n", P2B2[nstimsP2B2].dur_usec ); }
	 nstimsP2B2++;
       }
     }
   }
 }
 else{
   printf("Error opening stimulus input file!\n");
   exit(0);	  
 }
 fclose(stimfp);
 if(DEBUG==2){printf("flag: done reading in stims\n");}
 if(DEBUG==2){ printf("P1B1 nstims: %i\n", nstimsP1B1); }
 if(DEBUG==2){ printf("P2B1 nstims: %i\n", nstimsP2B1); }
 if(DEBUG==2){ printf("P1B2 nstims: %i\n", nstimsP1B2); }
 if(DEBUG==2){ printf("P2B2 nstims: %i\n", nstimsP2B2); }

 /* Open file handles for DIO96 and configure ports */
 if ((dinfd = open(bit_dev, O_RDONLY)) == -1){                  /* open input port */
   fprintf(stderr, "ERROR: Failed to open %s. ", bit_dev);
   perror(NULL);
   exit(-1);
 }
 if ((doutfd = open(bit_out, O_WRONLY)) == -1){                 /* open output port */
   fprintf(stderr, "ERROR: Failed to open %s. ", bit_out);
   perror(NULL);
   close(dinfd);
   exit(-1);
 }
 write (doutfd, &all_off, 1);			                 /* clear input/output ports */
 if(DEBUG==1){printf("flag: dio96 I/O set up and cleared \n");}

	
/*  Open & setup data logging files */
 if(DEBUG==1){printf("setting up the data files\n");}
 
 curr_tt = time (NULL);
 loctime = localtime (&curr_tt);
 strftime (timebuff, 64, "%d%b%y", loctime);
 sprintf (stimftemp, "%s", stimfname);
 stimfroot = strtok (stimftemp, delimiters); 
 sprintf(datafname, "%s_%s.2choice_rDAT", subjectid, stimfroot);
 sprintf(dsumfname, "%s.summaryDAT", subjectid);
 datafp = fopen(datafname, "a");
 dsumfp = fopen(dsumfname, "w");
 
 if ( (datafp==NULL) || (dsumfp==NULL) ){
   fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", datafname);
   if (NODSP == 0){
     close_soundserver(dsp1_fd); 
     close_soundserver(dsp2_fd);
   }
   close(dinfd); 
   close(doutfd);
   fclose(datafp);
   fclose(dsumfp);
   exit(-1);
 }

 if(DEBUG==1){printf ("data files set up\n"); }

/* Write data file header info */

 if(DEBUG==1){printf ("Data output to '%s'\n", datafname);}
 if(DEBUG==1){printf( "Tail '%s' for running totals\n", dsumfname);}
 
 fprintf (datafp, "File name: %s\n", datafname);
 fprintf (datafp, "Procedure source: %s\n", exp_name);
 fprintf (datafp, "Start time: %s", asctime(loctime));
 fprintf (datafp, "Subject ID: %s\n", subjectid);
 fprintf (datafp, "Stimulus source: %s\n", stimfname);  
 fprintf(datafp, "Reinforcement schedule: %s ", r_sched );
 if (strncmp(r_sched, "FN", 2) == 0){ fprintf(datafp, "%i \n", fn_stim ); }
 if (strncmp(r_sched, "VN", 2) == 0){ fprintf(datafp, "%i:%i \n", vn_min, vn_max ); }

 /*output the stim_num-exemplar pairings*/
 fprintf (datafp, "Class 1 stimlist:\n");
 for (i = 0; i < nstimsP1B1; i++){
   fprintf (datafp, "\t%s\t%d\n", P1B1[i].exemplar, P1B1[i].stimnum);}
 for (i = 0; i < nstimsP2B2; i++){
   fprintf (datafp, "\t%s\t%d\n", P2B2[i].exemplar, P2B2[i].stimnum);} 
 fprintf (datafp, "\nClass 2 stimlist:\n");
 for (i = 0; i < nstimsP2B1; i++){
   fprintf (datafp, "\t%s\t%d\n", P2B1[i].exemplar, P2B1[i].stimnum);}
 for (i = 0; i < nstimsP1B2; i++){
   fprintf (datafp, "\t%s\t%d\n", P1B2[i].exemplar, P1B2[i].stimnum);}
 fprintf (datafp, "Ses#\tBlk#\tTrl#\tStimuli\tClass\tRespDUR\tOffset\tBlkDUR\tTOD\tDate\n");
 fflush(datafp);

 if(DEBUG==2){printf ("header info written\n");} 
 if(DEBUG==1){printf ("Ses#\tBlk#\tTrl#\tStimuli\tClass\tRespDUR\tOffset\tBlkDUR\tTOD\tDate\n");}

 /****************************************     
 +++++++++++ Trial sequence +++++++++++++
 ****************************************/
 
	
 session_num = 1;
 block_num = 1;
 trial_num = 0;
 block_dur = 0.0;
 tot_perch1 = tot_perch2 = 0.0;
 stim1_exp = stim2_exp = 0.0;
 loop = 0;
 perch1_sum = perch2_sum = perch3_sum = 0.0;
 perch1_cnt = perch2_cnt = perch3_cnt = 0;

 gettimeofday(&stim_off, NULL);           /*set to tod for 1st trial*/ 


 /*set some variables for block 1 */		
 if(DEBUG==2){printf ("setting vars for block 1\n");} 
 if(DEBUG==2){
   for(i = 0; i < nstimsP1B1; i++){ 
     printf ("P1B1[%d].exemplar = %s\n", i, P1B1[i].exemplar);
     printf ("P1B1[%d].class = %i\n", i, P1B1[i].class);
     printf ("P1B1[%d].dur_sec = %i\n", i, P1B1[i].dur_sec);
     printf ("P1B1[%d].dur_usec = %li\n", i, P1B1[i].dur_usec);
     printf ("P1B1[%d].stimnum = %i\n", i, P1B1[i].stimnum);} 
   for(i = 0; i < nstimsP2B1; i++){ 
     printf ("P2B1[%d].exemplar = %s\n", i, P2B1[i].exemplar);
     printf ("P2B1[%d].class = %i\n", i, P2B1[i].class);
     printf ("P2B1[%d].dur_sec = %i\n", i, P2B1[i].dur_sec);
     printf ("P2B1[%d].dur_usec = %li\n", i, P2B1[i].dur_usec);
     printf ("P2B1[%d].stimnum = %i\n", i, P2B1[i].stimnum);
   } 
 }
 
 P1 = malloc ((nstimsP1B1+1) * sizeof(struct stim*));
 for (i = 0; i < nstimsP1B1; i++){
   P1[i] = &P1B1[i];
 }
 P2 = malloc ((nstimsP2B1+1) * sizeof(struct stim*));
 for (i = 0; i < nstimsP2B1; i++){
   P2[i] = &P2B1[i];
 }
 

P1nstims = nstimsP1B2;
P2nstims = nstimsP2B2;

 if (strncmp(r_sched, "FN", 2) == 0){           /* set fixed number of stim/trial*/
   max_loops = fn_stim;                        /* if necessary, do this once    */
   if(DEBUG==1){printf("max loops set by FN to: %i \n", max_loops);}
}
 
 do{                                             /* trial loop */
   if(DEBUG==2){printf("into trial loop \n");} 
   
   t_location = t_class = t_dsp = 0;
   t_dur.tv_sec =  t_dur.tv_usec = 0; 
   loop = stim_num = 0;                       /* zero trial variables */
   
   trial_num++;                               /* increase trial counter */
   write (doutfd, &house_lt, 1);              /* make sure houselight is on */
   
   /* get variable num of stim for this trial, if vn schedule*/
   if (strncmp(r_sched, "VN", 2) == 0){
     max_loops = ( ( (float)(vn_max - vn_min) )*rand()/( (float) RAND_MAX) + vn_min);
     if(DEBUG==1){printf("max loops set by VN to: %i \n", max_loops);}
   }

   free(P1list);
   free(P2list);
   free(playlist);
   playlist = malloc((max_loops+1)*sizeof(int));
   P1list = malloc((max_loops+1)*sizeof(int));
   P2list = malloc((max_loops+1)*sizeof(int));
   t_list = 0;

   /*fill the song lists randomly without repeats*/ 
   if(DEBUG==2){printf("P1nstims = %i\t", P1nstims);}
   if(DEBUG==2){printf("P2nstims = %i\n", P2nstims);}
   
   
   if(DEBUG==2){printf("filling P1 song list .....\n");}
   for (i= 0; i < max_loops; i++){         /* Generate P1 playlist items*/
     j=1;
     while (j)  /* Generate random songs (that aren't in list)*/
       {
	 P1list[i] = (int) ( ((float)P1nstims)*rand()/(RAND_MAX+1.0));
	 j=0;
	 for(k=0; k<i; k++)
	  if (P1list[k] == P1list[i])
	    {j=1;break;}
       }
     if(DEBUG==2){printf("P1list[%i] = %i\n", i, P1list[i]);}
   }
   if(DEBUG==2){printf("filling P2 song list .....\n");}
   for (i= 0; i < max_loops; i++){        /* Generate P2 playslist items*/    
     j=1;
     while (j)           /*Generate random songs (that aren't in list)*/
       {
	 P2list[i] = (int) ( ((float)P2nstims)*rand()/(RAND_MAX+1.0));
	 j=0;
	 for(k=0; k<i; k++)
	   if (P2list[k] == P2list[i])
	     {j=1;break;}
       }
     if(DEBUG==2){printf("P2list[%i] = %i\n", i, P2list[i]);}
   }
   
   if(DEBUG==2){printf("...song lists filled \n");}
   
   /*Hold until the stimulus is done playing*/
   gettimeofday(&realtime, NULL);
   while ( timercmp(&realtime, &stim_off, <) ){
     gettimeofday(&realtime, NULL);}


   /* Wait for perch landing */
   
   if (DEBUG==2){printf("flag: waiting for initial response \n");}
   do{                                         
     nanosleep(&rsi, NULL);	               	       
     bits = 0;
     read(dinfd, &bits, 1);	  	       
   } while ((bits!=PERCH1)&&(bits!=PERCH2)&&(bits!=PERCH3)) ;  
   
   gettimeofday(&resp_on, NULL);
   timeradd (&resp_on, &resp_delay, &resp_lag);   
   
   sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
   
   if (bits != PERCH3){
     /*figure out location of subject & set vars accordingly */
     if (bits == PERCH1){
       t_location = PERCH1;
       t_dsp = dsp1_fd;
       t_list = P1list;
       index = t_list[loop];
       T = P1;
       t_class = T[index]->class;
       stim_num = T[index]->stimnum;
       stim_dur.tv_sec = 0;
       stim_dur.tv_usec = 0;
       stim_dur.tv_sec = T[index]->dur_sec;
       stim_dur.tv_usec = T[index]->dur_usec; 
     }
     else if (bits == PERCH2){ 
       t_location = PERCH2; 
       t_dsp = dsp2_fd;
       t_list = P2list;
       index = t_list[loop];
       T = P2;
       t_class = T[index]->class;
       stim_num = T[index]->stimnum;
       stim_dur.tv_sec = 0;
       stim_dur.tv_usec = 0;
       stim_dur.tv_sec = T[index]->dur_sec;
       stim_dur.tv_usec = T[index]->dur_usec; 
     }
     else{       
       printf("ERROR: ABORT! -invalid response detected\n");
       if(NODSP==0){close_soundserver(dsp1_fd); close_soundserver(dsp2_fd);}
       close(dinfd);
       close(doutfd);
       fclose(datafp);
       fclose(dsumfp);
       exit(-1);
     }
     
     /*loop until response delay is reached or bird moves */
     do{        
       nanosleep(&rsi, NULL);	               	       
       bits = 0;
       read(dinfd, &bits, 1);
       gettimeofday(&realtime, NULL);		 	       
     } while ( (bits == t_location) && (timercmp(&realtime, &resp_lag, <)) ); 

     if (DEBUG==2){
       printf("\ntarget location: %i \n", bits);
       printf("target class: %i \n",t_class);
       printf("stim number: %i \n",stim_num);
       printf("stim secs: %i \n",(int)stim_dur.tv_sec);
       printf("stim usec: %li \n",stim_dur.tv_usec);
     }

     if(bits == t_location){                           /* if still on target perch, so run a trial */     
       
       sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
       
       /* Play stimulus file */
       gettimeofday(&stim_on, NULL);	    
       if(NODSP==0){playfork(t_dsp,stim_num);}
       else {printf("dummy play:stimnum '%i' to dsp '%i'\n", stim_num, t_dsp);}
       resp_start = stim_on;                            /* set resp start time */
       t_dur = stim_dur;
       timeradd (&stim_on, &stim_dur, &stim_off);       /*figure out when the stim will end*/
       playlist[loop] = stim_num;
       loop = 1;

       /* get the next sound file */
       if(max_loops > loop){
	 index = t_list[loop];
	 stim_num = T[index]->stimnum;
	 stim_dur.tv_sec = 0;
	 stim_dur.tv_usec = 0;
	 stim_dur.tv_sec = T[index]->dur_sec;
	 stim_dur.tv_usec = T[index]->dur_usec;	
       }
	 
	 do{                        /*watch the perch til bird moves or stim is done */      
	   nanosleep(&rsi, NULL);
	   bits = 0;
	   read(dinfd, &bits, 1);
	   gettimeofday(&realtime, NULL);
	 } while ( (bits == t_location) && (timercmp(&realtime, &stim_off, <)) );
	
	 while ((loop < max_loops) && (bits == t_location)){
	 
	   if (DEBUG==2){
	     printf("\ntarget location: %i \n", bits);
	     printf("target class: %i \n",t_class);
	     printf("stim number: %i \n",stim_num);
	     printf("stim secs: %i \n",(int)stim_dur.tv_sec);
	     printf("stim usec: %li \n",stim_dur.tv_usec);
	   }
	   
	   /* Play stimulus file */
	   gettimeofday(&stim_on, NULL);	    
	   if(NODSP==0){playfork(t_dsp,stim_num);}
	   else {printf("dummy play:stimnum '%i' to dsp '%i'\n", stim_num, t_dsp);}
	   timeradd (&stim_on, &stim_dur, &stim_off); 
	   timeradd (&t_dur, &stim_dur, &t_dur); 
	   playlist[loop] = stim_num;
	   loop++;


	   if(max_loops > loop){ //get the next soundfile
	     index = t_list[loop];
	     stim_num = T[index]->stimnum;
	     stim_dur.tv_sec = 0;
	     stim_dur.tv_usec = 0;
	     stim_dur.tv_sec = T[index]->dur_sec;
	     stim_dur.tv_usec = T[index]->dur_usec;	
	   }
	   
	   do{                        /* watch the perch til bird moves or stim is done */      
	     nanosleep(&rsi, NULL);
	     bits = 0;
	     read(dinfd, &bits, 1);
	     gettimeofday(&realtime, NULL);
	   } while ( (bits == t_location) && (timercmp(&realtime, &stim_off, <)) );
	 }
	 gettimeofday(&resp_stop, NULL);
     }
     else{   /*aborted trial set all timers to zero*/
       resp_start.tv_sec = 0;
       resp_start.tv_usec = 0;
       resp_stop.tv_sec = 0;
       resp_stop.tv_usec = 0;
       t_dur.tv_sec = 0;
       t_dur.tv_sec = 0;
       loop = 1;
       playlist[0] = 8888;  
       if(DEBUG==1){printf("aborted trial\n");}
     }
   }
   else if ( bits == PERCH3){         /*do something for 'silent' perch*/
     gettimeofday(&resp_start, NULL);
     t_location = PERCH3;
     t_class = 3;
     loop = 1;
     playlist[0] = 9999;  
     t_dur.tv_sec = 0;
     t_dur.tv_usec = 0;
     stim_dur.tv_sec = 0;
     stim_dur.tv_usec = 0;
     do{                               /* watch the perch til bird moves */      
       nanosleep(&rsi, NULL);
       bits = 0;
       read(dinfd, &bits, 1);
       gettimeofday(&realtime, NULL);
     } while (bits == t_location);
     gettimeofday(&resp_stop, NULL);
   }
   

 /* make sure that the subject leaves between trials*/
   post_t = 0;
   bits = 0;
   post_lag.tv_sec = 0;
   post_lag.tv_usec = 0;
   read(dinfd, &bits, 1);
   if (bits == t_location){                   
     post_t = 1;                              
     do{
       nanosleep(&rsi, NULL);
       bits = 0;
       read(dinfd, &bits, 1);
       gettimeofday(&realtime, NULL);
     } while (bits == t_location);
     strftime (tod, 256, "%H%M", loctime);
     strftime (date_out, 256, "%m%d", loctime);
     timersub(&realtime, &resp_stop, &post_lag );
   }


   /*caluclate some trial data*/
   timersub(&resp_stop, &resp_start, &resp_len );
   timeradd (&resp_len, &post_lag, &temp_len);
   timersub(&temp_len, &t_dur, &offset);
   resp_dur = (float) resp_len.tv_sec + ( (float) resp_len.tv_usec/1000000);
   resp_offset = (float) offset.tv_sec + ( (float) offset.tv_usec/1000000);
   strftime (tod, 256, "%H%M", loctime);
   strftime (date_out, 256, "%m%d", loctime);

   if (t_location==PERCH1){
     block_dur += resp_dur;

     /*set some output vars*/
     perch1_sum += resp_dur;                                          /* time on perch1 while stimulus is playing*/ 
     perch1_cnt++;
     if (resp_offset <= 0){
       stim1_exp += resp_dur + abs(resp_offset);                      /* total exposure to stimulus associated with perch 1*/ 
       tot_perch1 += resp_dur;}                                       /* total time on perch 1*/
     else {
       stim1_exp += resp_dur;
       tot_perch1 += resp_dur + resp_offset;}
   }
   if (t_location==PERCH2){
     block_dur += resp_dur;

     /*set some output vars*/
     perch2_sum += resp_dur;                                          /* time on perch2 while stimulus is playing*/ 
     perch2_cnt++;
     if (resp_offset <= 0){
       stim2_exp += resp_dur + abs(resp_offset);                      /* total exposure to stimulus associated with perch 2*/ 
       tot_perch2 += resp_dur;}                                       /* total time on perch 2*/
     else {
       stim2_exp += resp_dur;
       tot_perch2 += resp_dur + resp_offset;}
   }
   if (t_location==PERCH3){
     perch3_sum += resp_dur;
     perch3_cnt++;}


   /*write to main data file*/
   fprintf (datafp, "%d\t%d\t%d\t", session_num, block_num, trial_num );
   for (i= 0;i<loop;i++){
     fprintf (datafp, "-%d",playlist[i]);
   }
   fprintf (datafp, "\t%d\t%.2f\t%.2f\t%g\t%s\t%s\n", t_class, resp_dur, resp_offset, block_dur, tod, date_out );
   fflush (datafp);	      
   
   if(DEBUG==1){
     printf ("%d\t%d\t%d\t", session_num, block_num, trial_num );
     for (i= 0;i<loop;i++){printf ("-%d",playlist[i]);}
     printf ("\t%d\t%.2f\t%.2f\t%g\t%s\t%s\n", t_class, resp_dur, resp_offset, block_dur, tod, date_out );}
   
   /*write to summary file*/
   fprintf (dsumfp, "%d\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%d\t%d\t%d\n", 
	    trial_num, perch1_sum, perch2_sum, perch3_sum, stim1_exp, stim2_exp, tot_perch1, tot_perch2, block_dur, perch1_cnt, perch2_cnt, perch3_cnt);    
   fflush (dsumfp);
   if(DEBUG==2){printf ("%d\t%g\t%g\t%g\n", trial_num, perch1_sum, perch2_sum, perch3_sum);}
  
   sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                  /* unblock termination signals */ 



   /* check for block change */
   if ((block_dur>block_max) && (block_num < 3) ){        
     free(P1);
     P1 = malloc ((nstimsP1B2+1) * sizeof(struct stim*));  
     for (i = 0; i < nstimsP1B2; i++){
       P1[i] = &P1B2[i];
     }
     free(P2);
     P2 = malloc ((nstimsP2B2+1) * sizeof(struct stim*));
     for (i = 0; i < nstimsP2B2; i++){
       P2[i] = &P2B2[i];
     }
     if(DEBUG==1){printf("\nBLOCK CHANGE\n");}
     P1nstims = nstimsP1B2;
     P2nstims = nstimsP2B2;
     block_num++;
     trial_num = 0;
     block_dur = 0.0;
     tot_perch1 = tot_perch2 = 0.0;
     stim1_exp = stim2_exp = 0.0;
     perch1_sum = perch2_sum = perch3_sum = 0.0;
     perch1_cnt = perch2_cnt = perch3_cnt = 0;
   }
   
   /* what time is it */
   curr_tt = time(NULL);
   loctime = localtime (&curr_tt);
   strftime (hour, 16, "%H", loctime);
   
 /* Loop during the night */
   if(block_num < 3){
     sess_flag = 0;
     while( (atoi(hour) < startH) || (atoi(hour) >= stopH) ){
       write(doutfd, &all_off, 1);
       sleep (sleep_interval);
       curr_tt = time(NULL);
       loctime = localtime (&curr_tt);
       strftime (hour, 16, "%H", loctime);
       sess_flag=1;
     }
     write(doutfd, &house_lt, 1);
     if(sess_flag){session_num++;}
   }
   
 } while( (atoi(hour)>=startH) && (atoi(hour)<stopH) && (block_num<3) );
 
 
 /*  Cleanup */
 fprintf (datafp, "Session Completed!" );
 if(DEBUG==1){printf ("Session Completed!\n");}
 if (NODSP==0){close_soundserver(dsp1_fd);close_soundserver(dsp2_fd);}
 close(dinfd);
 close(doutfd);
 fclose(datafp);
 fclose(dsumfp);
 return 0;
}













