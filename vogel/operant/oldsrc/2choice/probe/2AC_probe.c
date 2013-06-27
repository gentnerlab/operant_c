/*****************************************************************************
**  2CHOICE - BASELINE operant procedure for 2 choice task
**                subject may need to run on 2choice_flash prior to this  
******************************************************************************
**
**
**  HISTORY:
**  09/16/97 PJ  Extensive modifications to support multiple channels and either soundcard
**               or chorus output
**  03/11/99 ASD Cleanup/rewrite.  Changed pin assignments.  Made possible
**               to run two behave's simultaneously (on different soundcards)
**               Eliminate chorus output option, related cruft.  Nicer
**		 command line arguments.
**  04/99    TQG Adapted from behave.c to run 2 3key/1hopper operant panels on 1-interval 
**               yes/no auditory discrimination task. Uses 2 soundblaster cards and the 
**               dio96 I/O card.
**                  
**               TRIAL SEQUENCE: select stim at random, wait for center key peck, play 
**               stim, wait for left or right key peck, consequate response, log trial 
**               data in output file, wait for iti, loop for correction trials, select 
**               new stim for non-correction trials. The session ends when the lights go 
**               off.  A new session begins when the lights come on the following day.  
**      	 A block of sessions ends when the pre determined number of trials have 
**               been run.  
**
**               Raw data is output to a text file labelled with the bird number and the 
**               stimulus file followed by the suffix 'recbsl_rDAT'.  
**                          For example,  '103_test.recbsl_rDAT'.  
**               Each time a subject runs with a new stimulus file <stimfilename.stim> 
**               a new data file is created.  Subsequent sessions with the same stimulus 
**               file are appended in the data file.  
**
** 01/00   TQG   Additions made to accomodate reinforcement arg in the command line. 
**               Enter value 0-10 to set the rate of reinforcement for any response 
**               to 0-100%.  Also added a saftey feature that will write to a backup 
**               data file at a prespecified location.
**
** 06/27/00 TQG  Additions made to accomodate the use of the sounderver, no longer 
**               works with soundcards.
**
** 09-19-00 TQG Removed the lame backup system of writing an extrafile to drozd.
**              backups will now occur nightly via cron (run separately)  
**
** 01-25-01 TQG Add facility for more stimulus classes (using labels 3 & 4)
**
** 02-25-01 TQG Added routine to verify that none of the ports are clogged prior to each trial
**              If a port is  blocked, the program halts and emails me
**
** 5-09-01 TQG Incorrect responses are always punished with a timeout!
**             The poke-light flash can now be toggled on/off with the command
**
** 6-28-01 TQG Modified to use the 'operant_io.h' library.  This library contains the functions that 
**             run both interface drivers (dio96 & comedi). Boxes a-f use the dio96 interface and g-l
**             use comedi.  The functions called to read and write are independant of the interface 
**             differences. 
**
** 6-28-01 TQG Added facility (through the command line) for dual hopper control.
**             Removed the (non-functional) port checking saftey.
**
** 8-21-01 DS  All kinds of crazy changes.  Lots of unused variables removed (probably more than 
**             _can_ be removed).  Driver interface now REALLY uses operantio.  Added a 
**             soundserver_close where i saw an exit called.  Perhaps they'll play nicey with
**             each other.
**
** 9-14-01 TQG Changed box id scheme, now uses numbers only!
** 
** 10-08-01 TQG Added facility to change the timeout duration with the command line.
**
** 12-12-01 TQG Now runs in box 6.
**
**6-18-04 TQG modified to run probe trials from 2choice.c, 
**            no longer need 2choice.const
**            cleaned up data output formats
*/


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
#include <time.h>
#include <unistd.h>
#include "remotesound.h"
#include "/usr/local/src/operantio/operantio.c"


#define SIMPLEIOMODE 0
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

#define LEFTPECK   0
#define CENTERPECK 1
#define RIGHTPECK  2

#define LFTKEYLT  0
#define CTRKEYLT  1
#define RGTKEYLT  2
#define HOUSELT   3
#define LFTFEED   4
#define RGTFEED   5


/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             2             /* secs from simulus end until NORESP is registered */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  1024            /* maximum number of stimulus exemplars */   
#define MAXCLASS                 64            /* maximum number of stimulus classes */
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define TIMEOUT_DURATION         2000000       /* default duration of timeout in microseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define DACSAMPLERATE            20000         /* stimulus sampling rate */  
#define DACBITDEPTH              16            /* stimulus bit depth */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define DEF_REF                  10            /* default reinforcement for corr. resp. set to 100% */

long timeout_duration = TIMEOUT_DURATION;
long feed_duration    = FEED_DURATION;
int  trial_max        = MAX_NO_OF_TRIALS;
int  startH           = EXP_START_TIME; 
int  stopH            = EXP_END_TIME;
int  sleep_interval   = SLEEP_TIME;



int feed(int rval);
int timeout(int rval);

const char exp_name[] = "2AC_PROBE";

int box_id = -1;
int hopper=1, mirror = 0;
int  resp_sel=-1, resp_acc=-1;

int correction = 1; /* there are no correction trials in probe sesssions, but include this to keep data output formats the same */


struct timespec iti = { INTER_TRIAL_INTERVAL, 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};


/* -------- Signal handling --------- */
int client_fd = -1;
int dsp_fd = 0;

static void sig_pipe(int signum)
{ printf("SIGPIPE caught\n"); client_fd = -1;}

static void termination_handler (int signum)
{
  close_soundserver(dsp_fd);
  printf("term signal caught: closing soundserver\n");
  exit(-1);
}



int main(int ac, char *av[])
{
	FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
	char *stimfname = NULL;
	char *stimfroot;
	const char delimiters[] = " .,;:!-";
	char datafname[128], hour [16], min[16], month[16], 
	  day[16], dsumfname[128], stimftemp[128];
	char  buf[128], stimexm[128],
	  timebuff[64], tod[256], date_out[256];
	int dinfd=0, doutfd=0, nstims, nclasses, totnstims=0, stim_class, stim_number, stim_reinf, subjectid, i,j, k, 
	  loop, flash=0, resp_wind, trial_num, playval, session_num, *playlist=NULL;
	float resp_rxt, timeout_val;
	time_t curr_tt;
	struct timeval stimoff, resp_window, resp_lag, resp_rt;
	struct tm *loctime;
	int left=0, right=0, center=0, fed=0;
	int reinfor_sum = 0, reinfor = 0;
	int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
	struct stim {
          char exemplar[128];
          int class;
          int reinf;
          int freq;
          int playnum;
        }stimulus[MAXSTIM];

	struct response {
          int count;
          int lft;
	  int rgt;
          int no;
        } stimRses[MAXSTIM], stimRtot[MAXSTIM], classRses[MAXCLASS], classRtot[MAXCLASS];

	sigset_t trial_mask;
	srand (time (0) );


	/* Make sure the server can see you, and set up termination handler*/
	if (signal(SIGPIPE, sig_pipe) == SIG_ERR)
	  {
	    perror("error installing signal handler for SIG_PIPE");
	    exit (-1);
	  }

	sigemptyset (&trial_mask);
	sigaddset (&trial_mask, SIGINT);
	sigaddset (&trial_mask, SIGTERM);
	
	signal(SIGTERM, termination_handler);
	signal(SIGINT, termination_handler);
	
	
	/* Parse the command line */
	
	for  (i = 1; i < ac; i++)
	  {
	    if (*av[i] == '-')
	      {
		if (strncmp(av[i], "-B", 2) == 0)
		  { 
		    sscanf(av[++i], "%i", &box_id);
		    if(DEBUG){printf("box number = %d\n", box_id);}
		    switch(box_id) {
		    case 1 :
		      if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/box1")) == -1){ 
			perror("FAILED connection to soundserver");
			exit (-1);}                                                       /*assign sound device*/
		      break;
		    case 2 :
		      if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/box2")) == -1){ 
			perror("FAILED connection to soundserver");
			exit (-1);}                                                       /*assign sound device*/
		      break;
		    case 3 :
		      if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/box3")) == -1){ 
			perror("FAILED connection to soundserver");
			exit (-1);}                                                       /*assign sound device*/
		      break;
		    case 4 :
		      if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/box4")) == -1){ 
			perror("FAILED connection to soundserver");
			exit (-1);}                                                       /*assign sound device*/  
		      break;
		    case 5 :
		      if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/box5")) == -1){ 
			perror("FAILED connection to soundserver");
			exit (-1);}                                                       /*assign sound device*/
		      break;
		    case 6 :
		      if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/box6")) == -1){ 
		      perror("FAILED TO RUN IN BOX 6: AUDIO OUTPUT MAY BE CONFIGURED IMPROPERLY\n");
		      exit (-1);}
		      break;
		    case 7 :
		      if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/box7")) == -1){ 
			perror("FAILED connection to soundserver");
			exit (-1);}                                                       /*assign sound device*/
		      break;
		    case 8 :
		      if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/box8")) == -1){ 
			perror("FAILED connection to soundserver");
			exit (-1);}                                                       /*assign sound device*/
		      break;
		    case 9 :
		      if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/box9")) == -1){ 
			perror("FAILED connection to soundserver");
			exit (-1);}                                                       /*assign sound device*/
		      break;
		    case 10 :
		      if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/box10")) == -1){ 
			perror("FAILED connection to soundserver");
			exit (-1);}                                                       /*assign sound device*/
		      break;
		    case 11 :
		      if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/box11")) == -1){ 
			perror("FAILED connection to soundserver");
			exit (-1);}                                                       /*assign sound device*/
		      break;
		    case 12 :
		      if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", "/dev/box12")) == -1){ 
			perror("FAILED connection to soundserver");
			exit (-1);}                                                       /*assign sound device*/
		      break;
		    default :
		      fprintf(stderr, "\tERROR: NO BOX NUMBER DETECTED OR INCORRECT VALUE GIVEN !!\n"); 
		      fprintf(stderr, "\tYou must provide a valid box ID \n"); 
		      fprintf(stderr, "\n\tTry '2choice -help' for help\n");
		      exit(-1);
		      break;
		     }
		  }
		else if (strncmp(av[i], "-S", 2) == 0)
		  sscanf(av[++i], "%i", &subjectid);	
		else if (strncmp(av[i], "-m", 2) == 0)
                  mirror = 1;                             //mirror reinf & punishment rate for stim class 1 & 2
		else if (strncmp(av[i], "-w", 2) == 0){
		  sscanf(av[++i], "%d", &resp_wind);
		  respoff.tv_sec = resp_wind;
		  fprintf(stderr, "response window duration set to %d secs\n", resp_wind);
		}
		else if (strncmp(av[i], "-t", 2) == 0){ 
		  sscanf(av[++i], "%f", &timeout_val);
		  timeout_duration = (int) (timeout_val*1000000);
		  fprintf(stderr, "timeout duration set to %d microsecs\n", (int) timeout_duration);
		}
		else if (strncmp(av[i], "-on", 3) == 0)
                  sscanf(av[++i], "%i", &startH);
                else if (strncmp(av[i], "-off", 4) == 0)
                  sscanf(av[++i], "%i", &stopH);
		else if (strncmp(av[i], "-f", 2) == 0)
		  flash = 1;
		else if (strncmp(av[i], "-d", 2) == 0){
		  hopper = 2;
		  fprintf(stderr, "\nUSING 2 HOPPERS!\n");
		}
		else if (strncmp(av[i], "-help", 5) == 0){
		  fprintf(stderr, "2AC_probe usage:\n");
		  fprintf(stderr, "    2AC_probe [-help] [-B <val>] [-w <val>] [-m] [-t <val>] [-on <val>] [-off <val>] [-f] [-d] [-S <val>] <filename>\n\n");
		  fprintf(stderr, "        -help        = show this help message\n");
		  fprintf(stderr, "        -B value     = required. SEt value equalt to box number, e.g. '-B 1' \n");
		  fprintf(stderr, "        -w value     = set the response window duration in integer secs, e.g. '-w 3' \n");
		  fprintf(stderr, "        -m           = set the timeout rate equal to the reinforcement rate (only applies to stim classes 1 & 2)\n");
		  fprintf(stderr, "                       The default rate for incorrect responses to stim classes 1 & 2 is 100%%\n");
		  fprintf(stderr, "                       Probe trials never yield a timeout.\n");
		  fprintf(stderr, "        -t value     = set the timeout duration in secs using a real number, e.g 2.5 \n");
		  fprintf(stderr, "        -on hour     = set hour for exp to start eg: '-on 8' (default is 7AM)\n");
                  fprintf(stderr, "        -off hour    = set hour for exp to stop eg: '-off 20' (default is 7PM)\n");
		  fprintf(stderr, "        -f           = flash left & right pokelights during response window\n");
		  fprintf(stderr, "        -d           = use the dual hopper protocol\n");
		  fprintf(stderr, "        -S value     = specify the subject ID number (required)\n");
		  fprintf(stderr, "        filename     = specify the name of the text file containing the stimuli (required)\n");
                  fprintf(stderr, "                       Each line of 'filename' should contain the variables: CLASS PCMFILE FREQ REINF\n");
                  fprintf(stderr, "                        CLASS: 1=go left; 2=go right; 3+ for nondifferentially reinforced probe stimuli \n");
                  fprintf(stderr, "                        PCMFILE is the name of the stimulus soundfile\n");
                  fprintf(stderr, "                        FREQ is the overall rate (compared to the other stimuli) at which the stimulus is presented. \n");
                  fprintf(stderr, "                            The actual rate for each stimulus (expressed as an integer) is that value divded by the\n");
                  fprintf(stderr, "                            sum for all stimuli. Set all prefreq values to 1 for equal probablility \n");
                  fprintf(stderr, "                        REINF is the rate at which correct responses to this stimulus are reinforced.\n");  
                  fprintf(stderr, "                            If you use the '-m' flag REINF is also the rate of reinforcement for incorrect\n"); 
		  fprintf(stderr, "                               responses to stim classes 1 & 2\n");
                  fprintf(stderr, "                            For probe stimuli (stim 2 or greater) REINF is the rate of food reward,\n"); 
		  fprintf(stderr, "                               regardless of the response location (left or right).\n"); 
		  fprintf(stderr, "                               Probe trials never yield a timeout.\n");
		  exit(-1);
		}
		else {
		  fprintf(stderr, "Unknown option: %s\t", av[i]);
		  fprintf(stderr, "Try '2AC_gng -help' \n");
		}
	      }
	    else{
	      stimfname = av[i];
	    }
	  }

	/* Check for terminal errors */
	if (stopH <= startH){
          fprintf(stderr, "\tTERMINAL ERROR: exp start-time must be greater than stop-time\n");
          exit(-1);
        }
	if (box_id <= 0){
	  fprintf(stderr, "\tYou must enter a box ID!: %s \n", av[i]); 
	  fprintf(stderr, "\tERROR: try '2choice -help' for help\n");
	  exit(-1);
	}
	/* Initialize box */
	else{
	  printf("Initializing box #%d...", box_id);
	  operant_init();
	  operant_clear(box_id);
	  printf("done\n");
	}
	
	/* give user some feedback*/
	fprintf(stderr, "Loading stimulus file '%s' for box '%d' session\n", stimfname, box_id); 
	fprintf(stderr, "Subject ID number: %i\n", subjectid);
	if (mirror==0)
          fprintf(stderr, "Incorrect responses to baseline stimuli (classes 1 & 2) always yeilds a timeout. \n");
        else
	  fprintf(stderr, "Mirroring reinforcement rates for correct and incorrect responses stim classes 1 & 2\n");
 	fprintf(stderr, "Responses to probe stimuli never yield a timeout.\n");
	



/* Read in the list of exmplars from stimulus file */

	nstims = 0;
	nclasses=0;

	if ((stimfp = fopen(stimfname, "r")) != NULL){
	  while (fgets(buf, sizeof(buf), stimfp))
	    nstims++;
	  
	  fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
	  rewind(stimfp);
	  
	  for (i = 0; i < nstims; i++){
	    fgets(buf, 128, stimfp);
	    stimulus[i].freq = stimulus[i].reinf = stimulus[i].class = 0;  /* set catch values */
	    sscanf(buf, "%d\%s\%d\%d", &stimulus[i].class, stimulus[i].exemplar, &stimulus[i].freq, &stimulus[i].reinf);
	    if((stimulus[i].freq==0) || (stimulus[i].reinf==0)){
	      printf("FATAL ERROR: insufficnet data or bad format in '.stim' file. Try '2AC_probe -help'\n");
	      close_soundserver(dsp_fd);
	      exit(0);}
	    totnstims += stimulus[i].freq;
	    if(DEBUG){printf("totnstims: %d\n", totnstims);}
	    if (load2soundserver(dsp_fd, i, stimulus[i].exemplar) != 0 ){
	      printf("FATAL ERROR: Trouble loading stimulus file '%s' to soundserver\n",  stimulus[i].exemplar);
	      close_soundserver(dsp_fd);
	      exit(0);}
	    printf("Loaded %s in soundserver\t", stimulus[i].exemplar);
	    printf("Class: %i\n", stimulus[i].class);
	    
	    /* count stimulus classes*/
	    if (nclasses<stimulus[i].class){nclasses=stimulus[i].class;}
	    if (DEBUG){printf("nclasses: %d\n", nclasses);}
	
	    /*check the reinforcement rates */
	    if (stimulus[i].class<3){
	      if(mirror==1){
		if (stimulus[i].reinf>50){
		  fprintf(stderr, "FATAL ERROR!: To mirror food and timeout rates for stim classes 1 & 2 you must use a base rate less than 51%%\n");
		  close_soundserver(dsp_fd);
		  exit(0);
		}
		else if (stimulus[i].reinf>0)
		  fprintf(stderr, "P(food) and P(timeout) for stimulus %s is set to %d%%\n", stimulus[i].exemplar, stimulus[i].reinf );
	      }
	      else
		fprintf(stderr, "P(food) for correct resp to %s is %d%%, P(timeout) for incorrect resp is 100%%.\n", stimulus[i].exemplar, stimulus[i].reinf);
	    }
	    else /* probe stim class */ 
	      fprintf(stderr, "P(food) for all resps to %s is %d%%, P(timeout) is 0%%\n", stimulus[i].exemplar, stimulus[i].reinf);
	  }
	}
        else{
	  printf("Error opening stimulus input file! Try 'gng_probe -help' for proper file formatting.\n");
	  close_soundserver(dsp_fd);
	  exit(0);
	}
	
	fclose(stimfp);
	if(DEBUG){printf("flag: done reading in stims\n");}
	
	/* make the stimulus playlist */
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
	if (DEBUG){printf("time: %s\n" , asctime (loctime));}
	strftime (timebuff, 64, "%d%b%Y", loctime);
	if (DEBUG){printf ("stimfname: %s\n", stimfname);}
	sprintf (stimftemp, "%s", stimfname);
	if (DEBUG){printf ("stimftemp: %s\n", stimftemp);}
	stimfroot = strtok (stimftemp, delimiters); 
	if (DEBUG){printf ("stimftemp: %s\n", stimftemp);}
	if (DEBUG){printf ("stimfname: %s\n", stimfname);}
	sprintf(datafname, "%i_%s.2choice_rDAT", subjectid, stimfroot);
	sprintf(dsumfname, "%i.summaryDAT", subjectid);
	datafp = fopen(datafname, "a");
        dsumfp = fopen(dsumfname, "w");
	
	if ( (datafp==NULL) || (dsumfp==NULL) ){
          fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", datafname);
	  close(dsp_fd);
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
	fprintf (datafp, "Sess#\tTrl#\tTrlTyp\tStimulus\t\t\tClass\tRspSL\tRspAC\tRspRT\tReinf\tTOD\tDate\n");


   /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
   ********************************************/

	for(i = 0; i<nstims;++i){   /*zero out the response tallies */
          stimRses[i].lft = stimRses[i].rgt = stimRses[i].no = stimRtot[i].lft = stimRtot[i].rgt = stimRtot[i].no = stimRses[i].count = stimRtot[i].count = 0;
	}
        if (DEBUG){printf("stimulus counters zeroed!\n");}
        for(i=1;i<nclasses+1;i++){
          classRses[i].lft = classRses[i].rgt = classRses[i].no = classRtot[i].lft = classRtot[i].rgt = classRtot[i].no = classRses[i].count = classRtot[i].count = 0;
	}
        if (DEBUG){printf("class counters zeroed!\n");}

	session_num = 1;
	trial_num = 0;
 	
       
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	if (DEBUG){printf("atoi(hour) at loop start: %d \n", atoi(hour));} 

	do{                                                                                       /* start the block loop */
	  while ((trial_num <= trial_max) && (atoi(hour) >= startH) && (atoi(hour) < stopH)){     /* start main trial loop */
	    playval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));                     /* select stim exemplar at random */
            if (DEBUG){printf("playval: %d\t", playval);}
            stim_number = playlist[playval];
            stim_class = stimulus[stim_number].class;                               /* set stimtype variable */
            strcpy (stimexm, stimulus[stim_number].exemplar);                       /* get exemplar filename */
            stim_reinf = stimulus[stim_number].reinf;
 
	    if(DEBUG){
              printf("stim_num: %d\t", stim_number);
              printf("class: %d\t", stim_class);
              printf("reinf: %d\t", stim_reinf);
              printf("name: %s\n", stimexm);
              printf("exemplar chosen: %s\tnumber: %d\n", stimulus[stim_number].exemplar, stim_number );
            }
	    
	  
	    resp_sel = resp_acc = resp_rxt = 0;        /* zero trial variables        */
	    ++trial_num;
	    
	    /* Wait for center key press */
	    if (DEBUG==1){printf("flag: waiting for center key press\n");}
	    operant_write (box_id, HOUSELT, 1);        /* house light on */
	    center = 0;
	    if (DEBUG==1){printf("flag: value read from center = %d\n", center);}
	    do{
	      nanosleep(&rsi, NULL);	               	       
	      center = operant_read(box_id, CENTERPECK);   /*get value at center peck position*/		 	       
	      if (DEBUG==2){printf("flag: value read from center = %d\n", center);}	 
	    }while (center==0);  
	    if (DEBUG==1){printf("flag: value read from center = %d\n", center);}

	    sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	    
	    /* Play stimulus file */
	    if (DEBUG){printf("START '%s'\n", stimexm);}
	    if (play2soundserver (dsp_fd, stim_number) == -1) {
	      fprintf(stderr, "play2soundserver failed on dsp_fd:%d stim_number: %d. Program aborted %s\n", dsp_fd, stim_number, asctime(localtime (&curr_tt)) );
	      fprintf(datafp, "play2soundserver failed on dsp_fd:%d stim_number: %d. Program aborted %s\n", dsp_fd, stim_number, asctime(localtime (&curr_tt)) );
	      close_soundserver(dsp_fd);
	      fclose(datafp);
	      fclose(dsumfp);
	      exit(-1);
	    } 
	    if (DEBUG){printf("STOP  '%s'\n", stimexm);}
	    gettimeofday(&stimoff, NULL);
	    if (DEBUG){
	      stimoff_sec = stimoff.tv_sec;
	      stimoff_usec = stimoff.tv_usec;
	      printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);
	    }
	    
	    /* Wait for right/left key press */
	    if (DEBUG==2){printf("flag: waiting for right/left response\n");}
	    timeradd (&stimoff, &respoff, &resp_window);
	    if (DEBUG){ 
	      respwin_sec = resp_window.tv_sec;
	      respwin_usec = resp_window.tv_usec;
	      printf("resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);
	    }
	    
	    loop = 0; left = 0; right = 0;
	    operant_write (box_id, LFTKEYLT, 1);       /* turn left key light on */
	    operant_write (box_id, RGTKEYLT, 1);       /* turn right key light on */
	    do{
	      nanosleep(&rsi, NULL);
	      left = operant_read(box_id, LEFTPECK);
	      right = operant_read(box_id, RIGHTPECK );
	      if((left==0) && (right==0) && flash){
		++loop;
		if ( loop % 7 == 0 ){
		  if ( loop % 14 == 0 ){ 
		    operant_write (box_id, LFTKEYLT, 1);
		    operant_write (box_id, RGTKEYLT, 1);
		  }
		  else{
		    operant_write (box_id, LFTKEYLT, 0);
		    operant_write (box_id, RGTKEYLT, 0);
		  }
		}
	      }
	      gettimeofday(&resp_lag, NULL);
	      if (DEBUG==2){printf("flag: values at right & left = %d %d\t", right, left);}
	    }while ( (left==0) && (right==0) && (timercmp(&resp_lag, &resp_window, <)) );
	    if (DEBUG==1){printf("flag: values at right & left = %d %d\t", right, left);}
	    
	    operant_write (box_id, LFTKEYLT, 0);    /*make sure the key lights are off after resp interval*/
	    operant_write (box_id, RGTKEYLT, 0);
	    
	    
            /* Calculate response time */
	    
	    curr_tt = time (NULL); 
	    loctime = localtime (&curr_tt);                     /* date and wall clock time of resp */
	    timersub (&resp_lag, &stimoff, &resp_rt);           /* reaction time */
	    if (DEBUG){
	      resp_sec = resp_rt.tv_sec;      
	      resp_usec = resp_rt.tv_usec;
	      printf("resp rt sec: %d \t usec: %d\n", resp_sec, resp_usec); 
	    }
	    resp_rxt = (float) resp_rt.tv_sec + ( (float) resp_rt.tv_usec/1000000);  /* format reaction time */
	    if (DEBUG){printf("flag: resp_rxt = %.4f\n", resp_rxt);}
	    
	    strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
	    strftime (min, 16, "%M", loctime);
	    strftime (month, 16, "%m", loctime);
	    strftime (day, 16, "%d", loctime);
	   
	    /*increase the trial counters for this stimulus*/
	    ++stimRses[stim_number].count; ++stimRtot[stim_number].count; ++classRses[stim_class].count; ++classRtot[stim_class].count;
	    
	    /* Consequate responses */
	    if (DEBUG){printf("flag: stim_class = %d\n", stim_class);}
	    if (DEBUG){printf("flag: exit value left = %d, right = %d\n", left, right);}
	    
	    if (stim_class == 1){                                 /* BSL GO LEFT */                          
	      if ( (left==0 ) && (right==0) ){
		resp_sel = 0;
		resp_acc = 2;
		++stimRses[stim_number].no; ++stimRtot[stim_number].no;	++classRses[stim_class].no; ++classRtot[stim_class].no;
		reinfor = 0;
		if (DEBUG){ printf("flag: no response to stimtype 1\n");}
	      }
	      else if (left != 0){
		resp_sel = 1;
		resp_acc = 1;
		++stimRses[stim_number].lft; ++stimRtot[stim_number].lft; ++classRses[stim_class].lft; ++classRtot[stim_class].lft;
		reinfor = feed(stim_reinf);
		if (reinfor == 1) { ++fed;}
		if (DEBUG){printf("flag: correct response to stimtype 1\n");}
	      }
	      else if (right != 0){
		resp_sel = 2;
		resp_acc = 0;
		++stimRses[stim_number].rgt; ++stimRtot[stim_number].rgt; ++classRses[stim_class].rgt; ++classRtot[stim_class].rgt;
		reinfor =  timeout(stim_reinf);
		if (DEBUG){printf("flag: incorrect response to stimtype 1\n");}
	      } 
	      else{
		fprintf(datafp, "DEFAULT SWITCH for bit value:ERROR CODE REMOVED");
	      }
	    }
	    else if (stim_class == 2){                           /* BSL GO RIGHT */
	      if ((left==0) && (right==0)){
		resp_sel = 0;
		resp_acc = 2;
		++stimRses[stim_number].no; ++stimRtot[stim_number].no; ++classRses[stim_class].no; ++classRtot[stim_class].no;
		reinfor = 0;
		if (DEBUG){printf("flag: no response to stimtype 2\n");}
	      }
	      else if (left!=0){
		resp_sel = 1;
		resp_acc = 0;
		++stimRses[stim_number].lft; ++stimRtot[stim_number].lft; ++classRses[stim_class].lft; ++classRtot[stim_class].lft;
		reinfor =  timeout(stim_reinf);
		if (DEBUG){printf("flag: incorrect response to stimtype 2\n");}
	      }
	      else if (right!=0){
		resp_sel = 2;
		resp_acc = 1;
		++stimRses[stim_number].rgt; ++stimRtot[stim_number].rgt; ++classRses[stim_class].rgt; ++classRtot[stim_class].rgt;
		reinfor = feed(stim_reinf);
		if (reinfor == 1) { ++fed;}
		if (DEBUG){printf("flag: correct response to stimtype 2\n");}
	      } 
	      else{
		fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
	      }
	    }
	    if (stim_class >= 3){                                    /* PROBE TRIAL */
	      if ((left==0) && (right==0)){
		resp_sel = 0;
		resp_acc = 2;
		++stimRses[stim_number].no; ++stimRtot[stim_number].no; ++classRses[stim_class].no; ++classRtot[stim_class].no;
		reinfor = 0;
		if (DEBUG){printf("flag: no response to probe stimulus\n");}
	      }
	      else if (left!=0){
		resp_sel = 1;
		resp_acc = 1;
		++stimRses[stim_number].lft; ++stimRtot[stim_number].lft; ++classRses[stim_class].lft; ++classRtot[stim_class].lft;
		reinfor=feed(stim_reinf);
		if (reinfor == 1) { ++fed;}
		if (DEBUG){printf("flag: left response to probe stimulus\n");}
	      }
	      else if (right!=0){
		resp_sel = 2;
		resp_acc = 0;
		++stimRses[stim_number].rgt; ++stimRtot[stim_number].rgt; ++classRses[stim_class].rgt; ++classRtot[stim_class].rgt;
		reinfor=feed(stim_reinf);
		if (DEBUG){printf("flag: right response to probe stimulus\n");}
	      } 
	      else{
		fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
	      }
	    }

	    	    
	    
	    /* Pause for ITI */
	    
	    reinfor_sum = reinfor + reinfor_sum;
	    operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	    nanosleep(&iti, NULL);                     /* wait intertrial interval */
	    if (DEBUG){printf("flag: ITI passed\n");}
	    
	    



	    /* Write trial data to output file */
	    
	    strftime (tod, 256, "%H%M", loctime);
	    strftime (date_out, 256, "%m%d", loctime);
	    fprintf(datafp, "%d\t%d\t%d\t%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, 
		    correction, stimexm, stim_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
	    fflush (datafp);
	    if (DEBUG){printf("flag: trial data written\n");}
	    
	    /* Update summary datafile */ 	       
	   
	    fprintf (dsumfp, "  SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
	    fprintf (dsumfp, " \t\tSESSION TOTALS \t\t\t\tLAST %d SESSIONS)\n", session_num);
	    fprintf (dsumfp, "Class \tCount \tLeft \tRight \tNoResp \t\tCount\tLeft \tRight \tNoRsp\n"); 

	    for (i = 1; i<nclasses+1;++i){
	      fprintf (dsumfp, "%d \t%d \t%d \t%d \t%d \t\t%d \t%d \t%d \t%d\n", i, classRses[i].count, classRses[i].lft, classRses[i].rgt, classRses[i].no, 
		      classRtot[i].count, classRtot[i].lft, classRtot[i].rgt, classRtot[i].no);
	    }
	    
	    fprintf (dsumfp, "\nStim \tCount \tLeft \tRight \tNoResp \t\tCount\tLeft \tRight \tNoRsp\n"); 

	    for (i = 0; i<nstims;++i){
	      fprintf (dsumfp, "%d \t%d \t%d \t%d \t%d \t\t%d \t%d \t%d \t%d\n", i, stimRses[i].count, stimRses[i].lft, stimRses[i].rgt, stimRses[i].no, 
		       stimRtot[i].count, stimRtot[i].lft, stimRtot[i].rgt, stimRtot[i].no);
	    }


	    fprintf (dsumfp, "Last trial run @: %s\n", asctime(loctime) );
	    fprintf (dsumfp, "Feeder ops today: %d\n", fed );
	    fprintf (dsumfp, "Rf'd responses: %d\n\n", reinfor_sum); 
	    
	    fflush (dsumfp);
	    rewind (dsumfp);
		    
	    if (DEBUG){printf("flag: summaries updated\n");}
	   

	    /* End of trial chores */
      
	    sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                  /* unblock termination signals */ 
	    stim_number = -1;                                          /* reset the stim number for correct trial*/
	  }                                                          /* end main trial (while) loop */
	    
	  curr_tt = time (NULL);
	  	    
	    
	  /* Loop while lights out */

	  while ( (trial_num <= trial_max) && ((atoi(hour) < startH) || (atoi(hour) >= stopH))){
	    if (hopper==2) {
	      operant_write(box_id, HOUSELT, 0);
	      operant_write(box_id, LFTFEED, 0);
	      operant_write(box_id, RGTFEED, 0);
	      operant_write(box_id, LFTKEYLT, 0);
	      operant_write(box_id, CTRKEYLT, 0);
	      operant_write(box_id, RGTKEYLT, 0);
	    }
	    else{
	      operant_write(box_id, HOUSELT, 0);
	      operant_write(box_id, LFTFEED, 0);
	      operant_write(box_id, LFTKEYLT, 0);
	      operant_write(box_id, CTRKEYLT, 0);
	      operant_write(box_id, RGTKEYLT, 0);
	    }
	    sleep (sleep_interval);
	    curr_tt = time(NULL);
	    loctime = localtime (&curr_tt);
	    strftime (hour, 16, "%H", loctime);
	  }
	  operant_write(box_id, HOUSELT, 1);
	  
	  if (trial_num <= trial_max ){ 
	    curr_tt = time(NULL);
	    ++session_num;                            /* increase sesion number */ 
	    for(i = 0; i<nstims;++i){                 /*zero out the response tallies */
	      stimRses[i].lft = 0;
	      stimRses[i].rgt = 0;
	      stimRses[i].no = 0;
	      stimRses[i].count = 0;
	    }
	    for(i=1;i<nclasses+1;i++){
	      classRses[i].lft = 0;
	      classRses[i].rgt = 0;
	      classRses[i].no = 0;
	      classRses[i].count = 0;
	    }
	    fed = reinfor_sum = 0;
	  }
	  
	}while (trial_num <= trial_max);                            /* end block (do) loop */
	curr_tt = time(NULL);
	
	
	/*  Cleanup */
	
	close_soundserver(dsp_fd);
	fclose(datafp);
	fclose(dsumfp);
	return 0;
}                         


int feed(int rval)
{
  int feedme;
  
  if(DEBUG){fprintf(stderr,"feed got rval= %d\t", rval);}
  feedme = ( 100.0*rand()/(RAND_MAX+1.0) ); 
  if(DEBUG){fprintf(stderr,"feedme = %d\t resp_sel = %d\t hoppers = %d\n", feedme, resp_sel, hopper);}
  
  if(hopper==2){                           /* there are 2 hoppers */
    if (feedme <= rval){                      
      if (resp_sel == 1){                          /* feed at left */
	operant_write(box_id, LFTFEED, 1);
	usleep(feed_duration);
	operant_write(box_id, LFTFEED, 0);
	if(DEBUG){fprintf(stderr,"fed left\n");}
	return(1);
      }
      else if (resp_sel == 2){                     /* feed at right */
	operant_write(box_id, RGTFEED, 1);
	usleep(feed_duration);
	operant_write(box_id, RGTFEED, 0);
	if(DEBUG){fprintf(stderr,"fed right\n");}
	return(1);
      }
      else
	return(0);
    }
    else{return (0);}  /* no feed this time */
  }
  else{                /* if there is only 1 hopper */   
    if (feedme <= rval){
      operant_write(box_id, LFTFEED, 1);
      operant_write(box_id, RGTFEED, 1);
      usleep(feed_duration);
      operant_write(box_id, LFTFEED, 0);    
      operant_write(box_id, RGTFEED, 0);
      return(1);
    }
    else 
      return (0);
  }
}

int timeout(int rval)
{
  int dotimeout;

if(DEBUG){fprintf(stderr,"timeout-> reinf_val= %d\t", rval);}
dotimeout = ( 100.0*rand()/(RAND_MAX+1.0) );

if(DEBUG){fprintf(stderr,"do_time_out = %d\n", dotimeout);}
if (dotimeout <= rval)
{
  operant_write(box_id, HOUSELT, 0);
  usleep(timeout_duration);
  operant_write(box_id, HOUSELT, 1);
  return (1);
}
 else{return (0);}
}
