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
#include "/usr/local/src/aplot/dataio/pcmio.h"
#include "/usr/src/dio96/dio96.h"
#include "remotesound.h"
#include "/usr/src/operantio/operantio.c"

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
#define HOUSELT	  3   
#define LFTFEED	  4
#define RGTFEED   5
 


/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             2             /* seconds from simulus end until NORESP is registered  (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  64            /* maximum number of stimulus exemplars */   
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
int  reinf_val        = DEF_REF;


int feed(int reinf_val, int doutfd, int resp_sel, int dual_hopper);
int timeout(int reinf_val, int doutfd);

const char exp_name[] = "2CHIOCE";

int box_id = -1;

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
	int dinfd=0, doutfd=0, nstims, stim_class, stim_number, subjectid, loop, flash, box,
	  correction, trial_num, session_num,  reinf_val, resp_sel, resp_acc, i, stimtemp, xresp;
	float resp_rxt, timeout_val;
	int no_resp1, no_resp2, corr1, corr2, incorr1, incorr2;
	int GT_no_resp1, GT_no_resp2, GT_corr1, GT_corr2, GT_incorr1, GT_incorr2;
	int no_resp3, no_resp4, corr3, corr4, incorr3, incorr4;
	int GT_no_resp3, GT_no_resp4, GT_corr3, GT_corr4, GT_incorr3, GT_incorr4;
	time_t curr_tt;
	struct timeval stimoff, resp_window, resp_lag, resp_rt;
	struct tm *loctime;
	int left = 0, right= 0, center = 0, fed = 0;
	int reinfor_sum = 0, reinfor = 0;
	int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
	struct stim {
	  char exemplar[128];
	  int class;
	}stimulus[MAXSTIM];
	
	int dual_hopper=0;
	sigset_t trial_mask;
        flash = xresp = 0;
	
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
	
	
	/* Initialize the IO interface */
	
	/*if ((operant_comedi_device = comedi_open("/dev/comedi0")) == -1)
	  { 
	    perror("FAILED to open comedi device driver");
	    exit (-1);
	  }		
	  */
	
	/* Parse the command line */
	
	for (i = 1; i < ac; i++)
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
		  { 
		    sscanf(av[++i], "%i", &subjectid);
		  }
		else if (strncmp(av[i], "-x", 2) == 0)
		  { 
		   xresp = 1;
		  }
		else if (strncmp(av[i], "-t", 2) == 0)
		  { 
		    sscanf(av[++i], "%f", &timeout_val);
		    timeout_duration = (int) (timeout_val*1000000);
		    fprintf(stderr, "timeout duration set to %d microsecs\n", timeout_duration);
		  }
		else if (strncmp(av[i], "-R", 2) == 0){
		  sscanf(av[++i], "%i", &reinf_val);}
		else if (strncmp(av[i], "-f", 2) == 0){
		  flash = 1;}
		else if (strncmp(av[i], "-d", 2) == 0){
		  dual_hopper = 1;
		  fprintf(stderr, "\nUSING 2 HOPPERS!\n");
		}

		else if (strncmp(av[i], "-help", 5) == 0){
		  fprintf(stderr, "2choice usage:\n");
		  fprintf(stderr, "    2choice [-help] [-B x] [-R x] [-f] [-d] [-S <subject number>] <filename>\n\n");
		  fprintf(stderr, "        -help        = show this help message\n");
		  fprintf(stderr, "        -B x         = use '-B 1' '-B 2' ... '-B 12' \n");
		  /*fprintf(stderr, "                       Note: 2choice will not run in box 6\n");*/
		  fprintf(stderr, "        -R x         = specify P(Reinforcement) for correct response\n");
		  fprintf(stderr, "                       ex: Use '-R 5' for P(R) = 50%% \n");
		  fprintf(stderr, "                       'x' must be 0 to 10 \n");
		  fprintf(stderr, "        -f           = flash left & right pokelights during response window\n");
		  fprintf(stderr, "        -d           = use the dual hopper protocol\n");
		  fprintf(stderr, "        -t val       = set the timeout duration to 'val' secs (use a real number, e.g 2.5 )\n");
		  fprintf(stderr, "        -x           = use this flag to enable correction trials for 'no-response' trials,\n");
		  fprintf(stderr, "                       This is useful if you want to break a key bias\n");
		  fprintf(stderr, "        -S xxx       = specify the subject ID number (required)\n");
		  fprintf(stderr, "        filename     = specify stimulus filename (required)\n\n");
		  exit(-1);
		  }
		else
		  {
		    fprintf(stderr, "Unknown option: %s\t", av[i]);
		    fprintf(stderr, "Try '2choice -help' for help\n");
		  }
	      }
	    else
	      {
		stimfname = av[i];
	      }
	  }

	if (box_id <= 0)
	  {
	    fprintf(stderr, "\tYou must enter a box ID!: %s \n", av[i]); 
	    fprintf(stderr, "\tERROR: try '2choice -help' for help\n");
	    exit(-1);
	  }
	/* Initialize box */
	else
	  {
	   printf("Initializing box #%d...", box_id);
	   operant_init(box_id);
	   operant_clear(box_id);
	   printf("done\n");
	  }

	fprintf(stderr, "Loading stimulus file '%s' for box '%d' session\n", stimfname, box_id); 
	fprintf(stderr, "Subject ID number: %i\n", subjectid);
	fprintf(stderr, "Reinforcement set at: %i%%\n", (reinf_val*10) );


/* Read in the list of exmplars from stimulus file */

	nstims = 0;

	if ((stimfp = fopen(stimfname, "r")) != NULL)
	  {
	    while (fgets(buf, sizeof(buf), stimfp))
	     
		nstims++;
		fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
		rewind(stimfp);
	      
	    for (i = 0; i < nstims; i++)
	      {
		fgets(buf, 128, stimfp);
		sscanf(buf, "%d\%s", &stimtemp, stimulus[i].exemplar);
		stimulus[i].class = stimtemp;
		if (load2soundserver(dsp_fd, i, stimulus[i].exemplar) != 0 )
		  { 
		    printf("Error loading stimulus file: %s\n",  stimulus[i].exemplar);
		    close_soundserver(dsp_fd);
		    exit(0);
		  }
		if(DEBUG){printf("  stimulus file: %s\t", stimulus[i].exemplar);}
		if(DEBUG){printf(" stimulus class: %i\n", stimulus[i].class);}


	      }
	  }
	else 
	  {
	    printf("Error opening stimulus input file!\n");
	    close_soundserver(dsp_fd);
	    exit(0);	  
	  }
        
	fclose(stimfp);
	if(DEBUG){printf("flag: done reading in stims\n");}
	

	
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
	fprintf (datafp, "Reinforcement for correct resp: %i%%\n", (reinf_val*10) );
	fprintf (datafp, "Sess#\tTrl#\tTrlTyp\tStimulus\t\t\tClass\tRspSL\tRspAC\tRspRT\tReinf\tTOD\tDate\n");


   /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
   ********************************************/

	session_num = 1;
	trial_num = 0;
 	correction = 1;
	corr1 = corr2 = incorr1 = incorr2 = no_resp1 = no_resp2 = 0;                     /* zero session tallies */
	GT_corr1 = GT_corr2 = GT_incorr1 = GT_incorr2 = GT_no_resp1 = GT_no_resp2 = 0;   /* zero block tallies */
	corr3 = corr4 = incorr3 = incorr4 = no_resp3 = no_resp4 = 0;                     /* zero session tallies */
	GT_corr3 = GT_corr4 = GT_incorr3 = GT_incorr4 = GT_no_resp3 = GT_no_resp4 = 0;   /* zero block tallies */
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	if (DEBUG){printf("atoi(hour) at loop start: %d \n", atoi(hour));}

	do                                                                               /* start the block loop */
	  {
	    while ((trial_num <= trial_max) && (atoi(hour) >= startH) && (atoi(hour) < stopH))
	      {
		
		stim_number = ((nstims+0.0)*rand()/(RAND_MAX+0.0));                     /* select stim exemplar at random */ 
		stim_class = stimulus[stim_number].class;                               /* set stimtype variable */
		strcpy (stimexm, stimulus[stim_number].exemplar);                       /* get exemplar filename */
		
		if (DEBUG){printf("chose exemplar: %s\tnumber: %d\n", stimulus[stim_number].exemplar, stim_number );}
		
		do                                             /* start correction trial loop */
		  {
		    resp_sel = resp_acc = resp_rxt = 0;        /* zero trial variables        */
		    ++trial_num;
		    

	       /* Wait for center key press */
    
		    if (DEBUG){printf("flag: waiting for center key press\n");}
		    operant_write (box_id, HOUSELT, 1);        /* house light on */
		    center = 0;
		    do{
		      nanosleep(&rsi, NULL);	               	       
		      center = operant_read(box_id, CENTERPECK);   /*get value at center peck position*/		 	       
		      if (DEBUG){printf("flag: value read from center = %d\n", center);}	 
		    }while (center==0);  
		    
		    sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/


	       /* Play stimulus file */
		    
		    if (DEBUG){printf("START '%s'\n", stimexm);}
		    play2soundserver (dsp_fd, stim_number); 
		    if (DEBUG){printf("STOP  '%s'\n", stimexm);}
		    gettimeofday(&stimoff, NULL);
		    if (DEBUG){stimoff_sec = stimoff.tv_sec;}
		    if (DEBUG){stimoff_usec = stimoff.tv_usec;}
		    if (DEBUG){printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);}
		   

	     /* Wait for right/left key press */

                    if (DEBUG){printf("flag: waiting for right/left response\n");}
                    timeradd (&stimoff, &respoff, &resp_window);
		    if (DEBUG){ respwin_sec = resp_window.tv_sec;}
		    if (DEBUG){respwin_usec = resp_window.tv_usec;}
		    if (DEBUG){printf("resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);}
                    
                    loop = 0; left = 0; right = 0;
                    operant_write (box_id, LFTKEYLT, 1);       /* turn left key light on */
		    operant_write (box_id, RGTKEYLT, 1);       /* turn right key light on */
                    do
                      {
                        nanosleep(&rsi, NULL);
			left = operant_read(box_id, LEFTPECK);
			right = operant_read(box_id, RIGHTPECK );
                        if((left==0) && (right==0) && flash){
			  ++loop;
			  if ( loop % 7 == 0 )
			    {
			      if ( loop % 14 == 0 ){ 
				  operant_write (box_id, LFTKEYLT, 1);
				  operant_write (box_id, RGTKEYLT, 1);
			      }
			      else {
				operant_write (box_id, LFTKEYLT, 0);
				operant_write (box_id, RGTKEYLT, 0);
			      }
			    }
			}
                        gettimeofday(&resp_lag, NULL);
                        if (DEBUG){printf("flag: values at right & left = %d %d\t", right, left);}
                      } while ( (left==0) && (right==0) && (timercmp(&resp_lag, &resp_window, <)) );
		   
		    operant_write (box_id, LFTKEYLT, 0);    /*make sure the key lights are off after resp interval*/
		    operant_write (box_id, RGTKEYLT, 0);


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
		    
		    if (stim_class == 1){                                 /* GO LEFT */                          
		      if ( (left==0 ) && (right==0) )
			{
			  resp_sel = 0;
			  resp_acc = 2;
			  ++no_resp1; ++GT_no_resp1;
			  reinfor = 0;
			  if (DEBUG){ printf("flag: no response to stimtype 1\n");}
			}
		      else if (left != 0)
			{
			  resp_sel = 1;
			  resp_acc = 1;
			  ++corr1; ++GT_corr1;
			  reinfor = feed(reinf_val, doutfd, resp_sel, dual_hopper);
			  if (reinfor == 1) { ++fed;}
			  if (DEBUG){printf("flag: correct response to stimtype 1\n");}
			}
		      else if (right != 0)
			{
			  resp_sel = 2;
			  resp_acc = 0;
			  ++incorr1; ++GT_incorr1;
			  reinfor =  timeout(reinf_val, doutfd);
			  if (DEBUG){printf("flag: incorrect response to stimtype 1\n");}
			} 
		      else
			{
			  fprintf(datafp, "DEFAULT SWITCH for bit value:ERROR CODE REMOVED");
			}
		    }
		    else if (stim_class == 2){                           /* GO RIGHT */
		      if ( (left==0) && (right==0) )
			{ 
			  resp_sel = 0;
			  resp_acc = 2;
			  ++no_resp2; ++GT_no_resp2;
			  reinfor = 0;
			  if (DEBUG){printf("flag: no response to stimtype 2\n");}
			}
		      else if (left!=0)
			{
			  resp_sel = 1;
			  resp_acc = 0;
			  ++incorr2; ++GT_incorr2;
			  reinfor =  timeout(reinf_val, doutfd);
			  if (DEBUG){printf("flag: incorrect response to stimtype 2\n");}
			}
		      else if (right!=0)
			{
			  resp_sel = 2;
			  resp_acc = 1;
			  ++corr2; ++GT_corr2; 
			  reinfor = feed(reinf_val, doutfd, resp_sel, dual_hopper);
			  if (reinfor == 1) { ++fed;}
			  if (DEBUG){printf("flag: correct response to stimtype 2\n");}
			} 
		      else
			{
			  fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
			}
		    }
		    if (stim_class == 3){                                    /* GO LEFT */
		      if ( (left==0) && (right==0) )
			{
			  resp_sel = 0;
			  resp_acc = 2;
			  ++no_resp3; ++GT_no_resp3;
			  reinfor = 0;
			  if (DEBUG){printf("flag: no response to stimtype 3\n");}
			}
		      else if (left!=0)
			{
			  resp_sel = 1;
			  resp_acc = 1;
			  ++corr3; ++GT_corr3;
			  reinfor = feed(reinf_val, doutfd, resp_sel, dual_hopper);
			  if (reinfor == 1) { ++fed;}
			  if (DEBUG){printf("flag: correct response to stimtype 3\n");}
			}
		      else if (right!=0)
			{
			  resp_sel = 2;
			  resp_acc = 0;
			  ++incorr3; ++GT_incorr3;
			  reinfor =  timeout(reinf_val, doutfd);
			  if (DEBUG){printf("flag: incorrect response to stimtype 3\n");}
			} 
		      else
			{
			  fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
			}
		    }
		    else if (stim_class == 4){                                 /* GO RIGHT */
		      if ( (left==0) && (right==0) )
			{ 
			  resp_sel = 0;
			  resp_acc = 2;
			  ++no_resp4; ++GT_no_resp4;
			  reinfor = 0;
			  if (DEBUG){printf("flag: no response to stimtype 4\n");}
			}
		      else if (left!=0)
			{
			  resp_sel = 1;
			  resp_acc = 0;
			  ++incorr4; ++GT_incorr4;
			  reinfor =  timeout(reinf_val, doutfd);
			  if (DEBUG){printf("flag: incorrect response to stimtype 4\n");}
			}
		      else if (right!=0)
			{
			  resp_sel = 2;
			  resp_acc = 1;
			  ++corr4; ++GT_corr4; 
			  reinfor = feed(reinf_val, doutfd, resp_sel, dual_hopper);
			  if (reinfor == 1) { ++fed;}
			  if (DEBUG){printf("flag: correct response to stimtype 4\n");}
			}  
		      else
			{
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
		    if (DEBUG){printf("flag: trail data written\n");}
      
          /* Update summary data */ 	       

		    fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
		    fprintf (dsumfp, "SESSION TOTALS              GRAND TOTALS (%d sessions)\n", session_num);
		    fprintf (dsumfp, "  Stim Class1                 Stim Class1\n"); 
		    fprintf (dsumfp, "     N: %d                      N: %d\n", no_resp1, GT_no_resp1); 
		    fprintf (dsumfp, "     C: %d                      C: %d\n", corr1, GT_corr1); 
		    fprintf (dsumfp, "     X: %d                      X: %d\n\n", incorr1, GT_incorr1); 
		    fprintf (dsumfp, "  Stim Class2                 Stim Class2\n");
		    fprintf (dsumfp, "     N: %d                      N: %d\n", no_resp2, GT_no_resp2); 
		    fprintf (dsumfp, "     C: %d                      C: %d\n", corr2, GT_corr2); 
		    fprintf (dsumfp, "     X: %d                      X: %d\n\n", incorr2, GT_incorr2); 
		    
		    fprintf (dsumfp, "  Stim Class3                 Stim Class3\n"); 
		    fprintf (dsumfp, "     N: %d                      N: %d\n", no_resp3, GT_no_resp3); 
		    fprintf (dsumfp, "     C: %d                      C: %d\n", corr3, GT_corr3); 
		    fprintf (dsumfp, "     X: %d                      X: %d\n\n", incorr3, GT_incorr3); 
		    fprintf (dsumfp, "  Stim Class4                 Stim Class4\n");
		    fprintf (dsumfp, "     N: %d                      N: %d\n", no_resp4, GT_no_resp4); 
		    fprintf (dsumfp, "     C: %d                      C: %d\n", corr4, GT_corr4); 
		    fprintf (dsumfp, "     X: %d                      X: %d\n\n", incorr4, GT_incorr4); 
		    
		    fprintf (dsumfp, "  Total (class1+2)            Total (class1+2)\n");
		    fprintf (dsumfp, "     N: %d                      N: %d\n", no_resp1 + no_resp2, GT_no_resp1 + GT_no_resp2); 
		    fprintf (dsumfp, "     C: %d                      C: %d\n", corr1 + corr2, GT_corr1 + GT_corr2); 
		    fprintf (dsumfp, "     X: %d                      X: %d\n\n", incorr1 + incorr2, GT_incorr1 + GT_incorr2); 
		    fprintf (dsumfp, "  Total (class3+4)            Total (class1+2)\n");
		    fprintf (dsumfp, "     N: %d                      N: %d\n", no_resp3+no_resp4, GT_no_resp3+GT_no_resp4); 
		    fprintf (dsumfp, "     C: %d                      C: %d\n", corr3+corr4, GT_corr3+GT_corr4); 
		    fprintf (dsumfp, "     X: %d                      X: %d\n\n", incorr3+incorr4, GT_incorr3+GT_incorr4); 
		    fprintf (dsumfp, "  Total (grand)               Total (grand)\n");
		    fprintf (dsumfp, "     N: %d                      N: %d\n", no_resp1+no_resp2+no_resp3+no_resp4, GT_no_resp1+GT_no_resp2+GT_no_resp3+GT_no_resp4); 
		    fprintf (dsumfp, "     C: %d                      C: %d\n", corr1+corr2+corr3+corr4, GT_corr1+GT_corr2+GT_corr3+GT_corr4); 
		    fprintf (dsumfp, "     X: %d                      X: %d\n\n", incorr1+incorr2+incorr3+incorr4, GT_incorr1+GT_incorr2+GT_incorr3+GT_incorr4); 
		    
		    fprintf (dsumfp, "Last trial run @: %s\n", asctime(loctime) );
		    fprintf (dsumfp, "Feeder ops today: %d\n", fed );
		    fprintf (dsumfp, "Rf'd responses: %d\n\n", reinfor_sum); 
		    
		    fflush (dsumfp);
		    rewind (dsumfp);
		    
		    if (DEBUG){printf("flag: summaries updated\n");}


           /* End of trial chores */
      
		    sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                   /* unblock termination signals */ 
		    if (resp_acc == 0){correction = 0;}else{correction = 1;}        /* set correction trial var */
		    if ((xresp==1)&&(resp_acc == 2)){correction = 0;}              /* set correction trial var for no-resp */

		  }while ( (correction == 0) && (trial_num <= trial_max) && (atoi(hour) >= startH) 
			   && (atoi(hour) < stopH) );                             /* correction trial loop */
		
		stim_number = -1;                                          /* reset the stim number for correct trial*/
	      }                                                                  /* main trial loop */
	    
	    curr_tt = time (NULL);
	  	    
	    
	  /* Loop while lights out */

	    while ( (trial_num <= trial_max) &&  ( (atoi(hour) < startH) || (atoi(hour) >= stopH) ) ) 
	      {
		operant_write(box_id, HOUSELT, 0);
		operant_write(box_id, LFTFEED, 0);
		operant_write(box_id, RGTFEED, 0);
		operant_write(box_id, LFTKEYLT, 0);
		operant_write(box_id, CTRKEYLT, 0);
		operant_write(box_id, RGTKEYLT, 0);
		sleep (sleep_interval);
		curr_tt = time(NULL);
		loctime = localtime (&curr_tt);
		strftime (hour, 16, "%H", loctime);
	      }
	    operant_write(box_id, HOUSELT, 1);
	    
	    if (trial_num <= trial_max ) 
	      { 
		curr_tt = time(NULL);
		++session_num;                                                                     /* increase sesion number */ 
		corr1 = corr2 = incorr1 = incorr2 = no_resp1 = no_resp2 = reinfor_sum = fed = 0;   /* reset session tallies */
		corr3 = corr4 = incorr3 = incorr4 = no_resp3 = no_resp4 = 0;                       /* reset session tallies */
	      }
	    
	  }while (trial_num <= trial_max);
	curr_tt = time(NULL);
	
	
   /*  Cleanup */
	
	close_soundserver(dsp_fd);
	
	fclose(datafp);
	fclose(dsumfp);
	return 0;
}                         


int feed(int reinf_val, int doutfd, int resp_sel, int dual_hopper)
{
  int feed_me;
  
  if(DEBUG){fprintf(stderr,"feed-> reinf_val= %d\t", reinf_val);}
  feed_me = ( 10.0*rand()/(RAND_MAX+0.0) ); 
  if(DEBUG){fprintf(stderr,"feed_me = %d\t resp_sel = %d\t dual_hopper = %d\n", feed_me, resp_sel, dual_hopper);}
  
  if(dual_hopper){
    if (feed_me < reinf_val){
      if (resp_sel == 1){                 /* feed left */
	operant_write(box_id, LFTFEED, 1);
	usleep(feed_duration);
	operant_write(box_id, LFTFEED, 0);
	if(DEBUG){fprintf(stderr,"feed left\n");}
	return(1);
      }
      else if (resp_sel == 2){                 /* feed right */
	operant_write(box_id, RGTFEED, 1);
	usleep(feed_duration);
	operant_write(box_id, RGTFEED, 0);
	if(DEBUG){fprintf(stderr,"feed right\n");}
	return(1);
      }
      else{return (0);}
    }
  }
  else{
    if (feed_me < reinf_val){
      operant_write(box_id, LFTFEED, 1);
      usleep(feed_duration);
      operant_write(box_id, LFTFEED, 0);
      return(1);
    }
    else{return (0);}
  }
}

int timeout(int reinf_val, int doutfd)
{
  int do_time_out;
  
  if(DEBUG){fprintf(stderr,"timeout-> reinf_val= %d\t", reinf_val);}
  /* do_time_out = ( 10.0*rand()/(RAND_MAX+0.0) ); */
  do_time_out = -1;                                   /* always reinforce incorrect responses */
  if(DEBUG){fprintf(stderr,"do_time_out = %d\n", do_time_out);}
  if (do_time_out < reinf_val)
    {
      operant_write(box_id, HOUSELT, 0);
      usleep(timeout_duration);
      operant_write(box_id, HOUSELT, 1);
      return (1);
    }
  else{return (0);}
}


