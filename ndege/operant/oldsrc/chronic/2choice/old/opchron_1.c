
/*****************************************************************************
**  OPCHRON -   this is the version of 2choice_bsl that runs along with saber
**  
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
** 05/00         OPCHRON now links to saber & uses the AT-MIO buffers to play sound files
**               and record an analog trace of the subject's behavior -- you need the 
**               DAC hardware patch to do this.  OPCHRON tells saber when to play a 
**               stimulus file (and what file to play) and handles all reinforcement.
**
**              ********** THIS ONLY RUNS IN BOX F**************** 
**
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
#include <math.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include "/usr/local/src/aplot/dataio/pcmio.h"
#include "/usr/src/dio96/dio96.h"


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
#define LEFTPECK     6
#define CENTERPECK   5
#define RIGHTPECK    3   
#define NOPECK       7

/* -------- OUTPUTS --------*/
#define LFTKEYLT	246  
#define CTRKEYLT	245  
#define RGTKEYLT	243  
#define HOUSELT		247   
#define FEED		231 
#define ALLOFF		255        

/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             2             /* seconds from simulus end until NORESP is registered  (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  64            /* maximum number of stimulus exemplars */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define TIMEOUT_DURATION         2000000       /* duration of timeout in microseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define DACSAMPLERATE            20000         /* stimulus sampling rate */  
#define DACBITDEPTH              16            /* stimulus bit depth */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             24            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define DEF_REF                  10            /* default reinforcement for corr. resp. set to 100% */

/*------------- SABER VARIABLES ------------ */
#define PREFIX                     2            /* number of seconds appended to the beginning or end*/
#define POSTFIX                    4             /* should be equal to the response window */ 
#define PORTNUMBER               13003            /* connection to saber-- sclient port */    

long timeout_duration = TIMEOUT_DURATION;
long feed_duration = FEED_DURATION;
int trial_max = MAX_NO_OF_TRIALS;
int startH = EXP_START_TIME; 
int stopH = EXP_END_TIME;
int sleep_interval = SLEEP_TIME;
int all_off = ALLOFF;
int reinf_val = DEF_REF;
int samplerate = DACSAMPLERATE;
int pretrig_val = PREFIX;  
int posttrig_val = POSTFIX;

int feed(int reinf_val, int doutfd);
static void house_light_only(int doutfd);
int timeout(int reinf_val, int doutfd);
static int open_connection(char *hostname, u_short portnum);
int trig_saber (int trig_dur);
int play_saber (char *stim_val); 
int log_saber (char *log_message);

static char *bit_inF = "/dev/dio96/portdb";
static char *bit_outF = "/dev/dio96/portdc";
static char *boxf = "F";

char *stimpath = "/usr/local/users/tim/stimuli/";
char *bak_data_path = "/nfs/drozd/users/tim/operant_bak/rawdata_bak/";
const char exp_name[] = "OPCHRON_1";
char *host = "sturnus";

char *bit_dev = NULL;
char *bit_out = NULL;
char *box = NULL;

int client_fd = -1;
unsigned char obit;

struct timespec iti = { INTER_TRIAL_INTERVAL, 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};

int main(int ac, char *av[])
{
	FILE *stimfp = NULL, *datafp = NULL, *data_bakfp = NULL, *dsumfp = NULL;
	char *stimfname = NULL;
	char *stimfroot, *message_buf, *command_msg, *trialdata;
	const char delimiters[] = " .,;:!-";
	char datafname[128], hour [16], min[16], month[16], 
	  day[16], dsumfname[128], stimftemp[128], exmtemp[128], full_stim [128];
	char data_bakfname[256], buf[128], stimexm[128],
	  timebuff[64], tod[256], date_out[256];
	int dinfd, doutfd, nstims, stim_class, num, subjectid, byt3,
	  correction, trial_num, session_num,  reinf_val, resp_sel, resp_acc, i, stimtemp;
	float resp_rxt;
	int no_resp1, no_resp2, corr1, corr2, incorr1, incorr2;
	int GT_no_resp1, GT_no_resp2, GT_corr1, GT_corr2, GT_incorr1, GT_incorr2;
	time_t curr_tt;
	struct timeval stimoff, resp_window, resp_lag, resp_rt;
	struct tm *loctime;
	int bits = 0;
	int reinfor_sum = 0, reinfor = 0, trig_duration = 0;
	//int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;
	PCMFILE *exm_fp = NULL;
	short *stimbuf;
	int nsamples, stimsec = 0;
	struct timeval tv, stimdur_tv, target_tv;
	struct timespec wait_ts;  
	struct stim {
	  char exemplar[128];
	  int class;
	  float dur;
	  int nsamp;
	}stimulus[MAXSTIM];
	int net_fd = -1;
	int n,k;

	sigset_t trial_mask;
	obit = 0;
	srand (time (0) );
	wait_ts.tv_sec = 0;
        wait_ts.tv_nsec = 10000;
 

/* block until you connect to saber via sclinet */
	
	message_buf = (char *)malloc(255*sizeof(char));

	while (1) {
	  /* Check for socket connection */
	  if ((client_fd = open_connection( host, PORTNUMBER)) == -1) {      
	    perror("open_connection");
	    return(-1);
	  } else {
	    /* initialize the socket connection-- send a message across it */
	    n = strlen(message_buf);
	    for (k=0;k<n;k++) {
	      message_buf[k]='\0';
	    }
	    //sprintf(message_buf,"connection to OPCHRON\n");
	    //if (write(client_fd, message_buf, strlen(message_buf)) == -1)
	    //  {
	    //	close(client_fd);
	    //  close(net_fd);
	    //	client_fd= -1;
	    //  } 
	    break;
	  }
	}


/* Parse the command line */
	
	for (i = 1; i < ac; i++)
	  {
	    if (*av[i] == '-')
	      {
		if (strncmp(av[i], "-F", 2) == 0)
		  { 
		    bit_dev = bit_inF;                             /*assign input port for box F = db*/
		    bit_out = bit_outF;                            /*assign output port for box E = dc*/
		    box = boxf;
		    obit = 1;
		  }
		else if (strncmp(av[i], "-S", 2) == 0)
		  { 
		    sscanf(av[++i], "%i", &subjectid);
		  }
		else if (strncmp(av[i], "-R", 2) == 0)
		  {
		    sscanf(av[++i], "%i", &reinf_val);
		  }
		else if (strncmp(av[i], "-h", 2) == 0)
		  {
		    fprintf(stderr, "opchron usage:\n");
		    fprintf(stderr, "    opchron [-h] [-BOX_ID] [-R x] [-S] <subject number> <filename>\n\n");
		    fprintf(stderr, "        -h           = show this help message\n");
		    fprintf(stderr, "        -BOX_ID      = must be '-F' \n");
		    fprintf(stderr, "        -R x         = specify P(Reinforcement) for either response\n");
		    fprintf(stderr, "                       ex: Use '-R 5' for P(R) = 50%% \n");
		    fprintf(stderr, "                       'x' must be 0 to 10 \n");
		    fprintf(stderr, "        -S xxx       = specify the subject ID number (required)\n");
		    fprintf(stderr, "        filename     = specify stimulus filename (required)\n\n");
		    exit(-1);
		  }
		else
		  {
		    fprintf(stderr, "Unknown option: %s\t", av[i]);
		    fprintf(stderr, "Try 'opchron -h' for help\n");
		  }
	      }
	    else
	      {
		stimfname = av[i];
	      }
	  }
	if (obit == 0)
	  {
	    fprintf(stderr, "ERROR: try 'opchron -h' for help\n");
	    exit(-1);
	  }
	
	fprintf(stderr, "Loading stimulus file '%s' for box '%s' session\n", stimfname, box); 
	fprintf(stderr, "Subject ID number: %d\n", subjectid);
	fprintf(stderr, "Reinforcement set at: %d%%\n", (reinf_val*10) );

/* Send some intial commands to saber */

	command_msg = (char *)malloc(256*sizeof(char));

	sprintf(command_msg ,"set pretrig %d\n", pretrig_val);
	if (write(client_fd, command_msg, strlen(command_msg)) == -1)
	  {
	    close(client_fd);
	    client_fd= -1;
	    perror("remote: write(client_fd) ");
	  } 

	sprintf(command_msg ,"exp %d_opchron\n", subjectid );
	if (write(client_fd, command_msg, strlen(command_msg)) == -1)
	  {
	    close(client_fd);
	    client_fd= -1;
	    perror("remote: write(client_fd) ");
	  } 

	
/* Read in the list of exmplars from stimulus file & get some soundfile info*/

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
		//printf("load stimulus file to saber\n");
		//load_saber(stimulus[i].exemplar);
		printf("  stimulus file: %s\n", stimulus[i].exemplar);

		strcpy (exmtemp, stimulus[i].exemplar);   
		sprintf(full_stim ,"%s%s", stimpath, exmtemp);  
		if ((exm_fp = pcm_open(full_stim, "r")) == NULL)
		  {fprintf(stderr, "Error opening stimulus file '%s'. ", full_stim);}
		if ((pcm_read(exm_fp, &stimbuf, &nsamples)) == -1)
		  {fprintf(stderr, "Error reading stimulus file '%s'. ", stimexm);}
		pcm_close (exm_fp);
		printf("\tnum samples: %d\n", nsamples);
		stimulus[i].dur = (float)nsamples / (float)samplerate;
		stimulus[i].nsamp = nsamples;
		printf("\tduration: %f\n\n", stimulus[i].dur);
		//printf(" stimulus class: %i\n", stimulus[i].class);
	      }
	  }
	else 
	  {
	    printf("Error opening stimulus input file!\n");
	    exit(0);	  
	  }
        
	fclose(stimfp);
	//printf("flag: done reading in stims\n");
	

/* Open file handles for DIO96 and configure ports */

	if ((dinfd = open(bit_dev, O_RDONLY)) == -1)                    /* open input port */
	  {
	    fprintf(stderr, "ERROR: Failed to open %s. ", bit_dev);
	    perror(NULL);
	    exit(-1);
	  }
	if ((doutfd = open(bit_out, O_WRONLY)) == -1)                   /* open output port */
	  {
	    fprintf(stderr, "ERROR: Failed to open %s. ", bit_out);
	    perror(NULL);
	    close(dinfd);
	    exit(-1);
	  }
	
	byt3 = ALLOFF;  
	write (dinfd, &byt3, 1);
	write (doutfd, &byt3, 1);			                 /* clear input/output ports */
        //printf("flag: dio96 I/O set up and cleared \n");

	
/*  Open & setup data logging files */

	curr_tt = time (NULL);
	loctime = localtime (&curr_tt);
	//printf("time: %s\n" , asctime (loctime));
	strftime (timebuff, 64, "%d%b%y", loctime);
	//printf ("stimfname: %s\n", stimfname);
	sprintf (stimftemp, "%s", stimfname);
	//printf ("stimftemp: %s\n", stimftemp);
	stimfroot = strtok (stimftemp, delimiters); 
	//printf ("stimftemp: %s\n", stimftemp);
	//printf ("stimfname: %s\n", stimfname);
	sprintf(datafname, "%i_%s.opchron_rDAT", subjectid, stimfroot);
        sprintf(data_bakfname, "%s%i_%s.opchron_rDAT_bak", bak_data_path, subjectid, stimfroot);
	sprintf(dsumfname, "%i.op_summaryDAT", subjectid);
	datafp = fopen(datafname, "a");
	data_bakfp = fopen(data_bakfname, "a");
        dsumfp = fopen(dsumfname, "w");
	
	if ( (datafp==NULL) || (data_bakfp==NULL) || (dsumfp==NULL) ){
          fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", datafname);
	  close(dinfd);
	  close(doutfd);
	  fclose(datafp);
	  fclose(data_bakfp);
	  fclose(dsumfp);
	  exit(-1);
        }

/* Write data file header info */

	printf ("Data output to '%s'\n", datafname);
	printf( "Tail '%s' for running totals\n", dsumfname);
	printf( "backup data file written to '%s'\n", data_bakfname);

	fprintf (datafp, "File name: %s\n", datafname);
	fprintf (datafp, "Procedure source: %s\n", exp_name);
	fprintf (datafp, "Start time: %s", asctime(loctime));
	fprintf (datafp, "Subject ID: %d\n", subjectid);
	fprintf (datafp, "Stimulus source: %s\n", stimfname);  
	fprintf (datafp, "Reinforcement for correct resp: %i%%\n", (reinf_val*10) );
	fprintf (datafp, "Sess#\tTrl#\tTrlTyp\tStimulus\t\t\tClass\tRspSL\tRspAC\tRspRT\tReinf\tTOD\tDate\n");

	fprintf (data_bakfp, "File name: %s\n", datafname);
	fprintf (data_bakfp, "Procedure source: %s\n", exp_name);
	fprintf (data_bakfp, "Start time: %s", asctime(loctime));
	fprintf (data_bakfp, "Subject ID: %d\n", subjectid);
	fprintf (data_bakfp, "Stimulus source: %s\n", stimfname);  
	fprintf (data_bakfp, "Reinforcement for correct resp: %i%%\n", (reinf_val*10) );
	fprintf (data_bakfp, "Sess#\tTrl#\tTrlTyp\tStimulus\t\t\tClass\tRspSL\tRspAC\tRspRT\tReinf\tTOD\tDate\n");


   /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
   ********************************************/

	session_num = 1;
	trial_num = 0;
	correction = 1;
	corr1 = corr2 = incorr1 = incorr2 = no_resp1 = no_resp2 = 0;                     /* zero session tallies */
	GT_corr1 = GT_corr2 = GT_incorr1 = GT_incorr2 = GT_no_resp1 = GT_no_resp2 = 0;   /* zero block tallies */
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	//printf("atoi(hour) at loop start: %d \n", atoi(hour));
	nanosleep(&iti, NULL);                     /* wait intertrial interval */

	do                                                                               /* start the block loop */
	  {
	    while ((trial_num <= trial_max) && (atoi(hour) >= startH) && (atoi(hour) < stopH))
	      {
		
		num = ((nstims+0.0)*rand()/(RAND_MAX+0.0));                     /* select stim exemplar at random */ 
		stim_class = stimulus[num].class;                               /* set stimtype variable */
		strcpy (stimexm, stimulus[num].exemplar);                       /* get exemplar filename */
		//printf("chose exemplar: %s\tnumber: %d\n", stimulus[num].exemplar, num);
		stimsec = floor(stimulus[num].dur);
		stimdur_tv.tv_sec = (time_t)stimsec;
		stimdur_tv.tv_usec = (long)floor((stimulus[num].dur - stimsec) * 1000000);
		trig_duration = ceil(stimulus[num].dur) + posttrig_val;  
		printf("CUED:\nstimulus number %d - length %d.%d seconds\n", num , stimdur_tv.tv_sec, stimdur_tv.tv_usec);
                    

		do                                             /* start correction trial loop */
		  {
		    resp_sel = resp_acc = resp_rxt = 0;        /* zero trial variables        */
		    ++trial_num;
		    house_light_only(doutfd);                  /* make sure houselight is on  */
		    

/* Wait for center key press */
    
		    printf("Waiting for center key press\n");
		    sprintf(message_buf,"Waiting for center response");
                    //printf("log to saber\n");
		    log_saber(message_buf);
                    
		    do                                         
		      {
			nanosleep(&rsi, NULL);	               	       
			bits = 0;
			read(dinfd, &bits, 1);		 	       
			//printf("flag: bits value read = %d\n", &bits);	 
		      } while (bits != CENTERPECK);  
		    
		    
/* Play stimulus file */
		    
		    
		    printf("trig ON\t");
		    trig_saber (trig_duration); 
                 

		    gettimeofday(&tv, NULL);
		    timeradd(&stimdur_tv, &tv, &target_tv);
		    printf("playing ..... ");
		    play_saber (stimulus[num].exemplar);
		    gettimeofday(&tv,NULL);
                    while (timercmp(&tv, &target_tv, <)) {
                      nanosleep(&wait_ts, NULL);          /* sleep until balance of stimulus is done */
                      gettimeofday(&tv,NULL);
                    }
		    printf("done\t");

		    gettimeofday(&stimoff, NULL);
		    //stimoff_sec = stimoff.tv_sec;
		    //stimoff_usec = stimoff.tv_usec;
		    //printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec); 
		   

/* Wait for right/left key press */

		    //printf("flag: waiting for right/left response\n");
		    timeradd (&stimoff, &respoff, &resp_window);
		    //respwin_sec = resp_window.tv_sec;
		    //respwin_usec = resp_window.tv_usec;
		    //printf("resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);
		      
		    do
		      {
			nanosleep(&rsi, NULL);
			bits = 0;
			read(dinfd, &bits, 1);
			gettimeofday(&resp_lag, NULL);
			//printf("flag: bit value at port = %d\t", bits);
		      } while ( (bits != LEFTPECK) && (bits != RIGHTPECK) && (timercmp(&resp_lag, &resp_window, <)) );
		    
		    
		    printf("trig OFF\n");
			
			
/* Calculate response time */

		    curr_tt = time (NULL); 
		    loctime = localtime (&curr_tt);                     /* date and wall clock time of resp */
		    timersub (&resp_lag, &stimoff, &resp_rt);           /* reaction time */
		    //resp_sec = resp_rt.tv_sec;      
		    //resp_usec = resp_rt.tv_usec;
		    //printf("resp rt sec: %d \t usec: %d\n", resp_sec, resp_usec); 
		    resp_rxt = (float) resp_rt.tv_sec + ( (float) resp_rt.tv_usec/1000000);  /* format reaction time */
		    //printf("flag: resp_rxt = %.4f\n", resp_rxt);
		    
		    strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
		    strftime (min, 16, "%M", loctime);
		    strftime (month, 16, "%m", loctime);
		    strftime (day, 16, "%d", loctime);
	       

/* Consequate responses  (version1: stimtype1 = go left, stimtype2 = go right) */

		    //printf("flag: stimtype = %d\n", stimtype);
		    //printf("flag: exit bit value = %d\n", bits);
		    
		    if (stim_class == 1){
		      if ( (bits == NOPECK ) || (bits == CENTERPECK) )
			{
			  resp_sel = 0;
			  resp_acc = 2;
			  ++no_resp1; ++GT_no_resp1;
			  reinfor = 0;
			  //printf("flag: no response to stimtype 1\n");
			}
		      else if (bits == LEFTPECK)
			{
			  resp_sel = 1;
			  resp_acc = 1;
			  ++corr1; ++GT_corr1;
			  reinfor = feed(reinf_val, doutfd);
			  //printf("flag: correct response to stimtype 1\n");
			}
		      else if (bits == RIGHTPECK)
			{
			  resp_sel = 2;
			  resp_acc = 0;
			  ++incorr1; ++GT_incorr1;
			  reinfor =  timeout(reinf_val, doutfd);
			  //printf("flag: incorrect response to stimtype 1\n");
			} 
		      else
			{
			  fprintf(datafp, "DEFAULT SWITCH for bit value %d on trial %d\n", bits, trial_num);
			}
		    }
		    else if (stim_class == 2){
		      if ( (bits == NOPECK) || (bits == CENTERPECK) )
			{ 
			  resp_sel = 0;
			  resp_acc = 2;
			  ++no_resp2; ++GT_no_resp2;
			  reinfor = 0;
			  //printf("flag: no response to stimtype 2\n");
			}
		      else if (bits == LEFTPECK)
			{
			  resp_sel = 1;
			  resp_acc = 0;
			  ++incorr2; ++GT_incorr2;
			  reinfor =  timeout(reinf_val, doutfd);
			  //printf("flag: incorrect response to stimtype 2\n");
			}
		      else if (bits == RIGHTPECK)
			{
			  resp_sel = 2;
			  resp_acc = 1;
			  ++corr2; ++GT_corr2; 
			  reinfor = feed(reinf_val, doutfd);
			  //printf("flag: correct response to stimtype 2\n");
			} 
		      else
			{
			  fprintf(datafp, "DEFAULT SWITCH for bit value %d on trial %d\n", bits, trial_num);
			}
		    }
		    

/* Pause for ITI */

		    reinfor_sum = reinfor + reinfor_sum;
		    house_light_only(doutfd);
		    nanosleep(&iti, NULL);                     /* wait intertrial interval */
		    //printf("flag: ITI passed\n");
		    

/* Write trial data to output file */

      trialdata = (char *)malloc(256*sizeof(char));
      strftime (tod, 256, "%H%M", loctime);
      strftime (date_out, 256, "%m%d", loctime);
      sprintf (trialdata, "%d,%d,%d,%s,%d,%d,%d,%.4f,%d,%s,%s", session_num, trial_num, 
	      correction, stimexm, stim_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );

      fprintf(datafp, "%d\t%d\t%d\t%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, 
	      correction, stimexm, stim_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
      fprintf(data_bakfp, "%d\t%d\t%d\t%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, 
	      correction, stimexm, stim_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
      
      fflush (datafp);
      fflush (data_bakfp);

      log_saber (trialdata);
      printf("%s\n", trialdata);
      

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
     
      fprintf (dsumfp, "  Total (class1+2)            Total (class1+2)\n");
      fprintf (dsumfp, "     N: %d                      N: %d\n", no_resp1 + no_resp2, GT_no_resp1 + GT_no_resp2); 
      fprintf (dsumfp, "     C: %d                      C: %d\n", corr1 +  corr2, GT_corr1 + GT_corr2); 
      fprintf (dsumfp, "     X: %d                      X: %d\n\n", incorr1 + incorr2, GT_incorr1 + GT_incorr2); 
 
      fprintf (dsumfp, "Total number of reinforced correct responses: %d\n", reinfor_sum); 
      fprintf (dsumfp, "Note: N = no response, C = correct response, X= incorrect response\n"); 
      fflush (dsumfp);
      rewind (dsumfp);

      //printf("flag: summaries updated\n");


/* End of trial chores */

      sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                   /* unblock termination signals */ 
      if (resp_acc == 0){correction = 0;}else{correction = 1;}        /* set correction trial var */
           
      }while ( (correction == 0) && (trial_num <= trial_max) && (atoi(hour) >= startH) 
	       && (atoi(hour) < stopH) );                             /* correction trial loop */
		
		num = -1;                                             /* reset the stim number for correct trial*/
   }                                                                  /* main trial loop */
	
      curr_tt = time (NULL);
      printf("SESSION NO. %d COMPLETE:\n\t Last trial: %d\n\tTime of day: %s\n", 
	     session_num, trial_num, asctime(localtime(&curr_tt)));
       
    

/* Loop while lights out */

       while ( (trial_num <= trial_max) &&  ( (atoi(hour) < startH) || (atoi(hour) >= stopH) ) ) 
	 {
	    write(doutfd, &all_off, 1);
	    sleep (sleep_interval);
	    curr_tt = time(NULL);
	    loctime = localtime (&curr_tt);
	    strftime (hour, 16, "%H", loctime);
	 }
        house_light_only (doutfd);

       if (trial_num <= trial_max ) 
	 { 
	   curr_tt = time(NULL);
	   printf("SESSION NO. %d STARTED:\n\t First trial: %d\n\tTime of day: %s\n", 
		  session_num, trial_num , asctime(localtime(&curr_tt)));
	   ++session_num;                                                               /* increase sesion number */ 
	   corr1 = corr2 = incorr1 = incorr2 = no_resp1 = no_resp2 = reinfor_sum = 0;   /* reset session tallies */
	 }

      }while (trial_num <= trial_max);
      curr_tt = time(NULL);
      printf("SESSION BLOCK COMPLETE\t %s\n\tNo. trials run = %d\n\tNo. sessions required = %d\n", 
	     asctime(localtime(&curr_tt)), (trial_num - 1), session_num );
      

/*  Cleanup */

      close(dinfd);
      close(doutfd);
      fclose(datafp);
      fclose(data_bakfp);
      fclose(dsumfp);
      return 0;
}                         


int feed(int reinf_val, int doutfd)
{
  int byt1 = FEED;
  int byt2 = HOUSELT;
  int feed_me;
  
  //fprintf(stderr,"feed-> reinf_val= %d\t", reinf_val);
  feed_me = ( 10.0*rand()/(RAND_MAX+0.0) ); 
  //fprintf(stderr,"feed_me = %d\n", feed_me);
  if (feed_me < reinf_val)
    {
      write(doutfd, &byt1, 1);
      usleep(feed_duration);
      write(doutfd, &byt2, 1);
      return(1);
    }
  else{return (0);}
}

int timeout(int reinf_val, int doutfd)
{
  int byt1 = ALLOFF;
  int byt2 = HOUSELT;
  int do_time_out;
  
  //fprintf(stderr,"timeout-> reinf_val= %d\t", reinf_val);
  do_time_out = ( 10.0*rand()/(RAND_MAX+0.0) ); 
  //fprintf(stderr,"do_time_out = %d\n", do_time_out);
  if (do_time_out < reinf_val)
    {
      write(doutfd, &byt1, 1);
      usleep(timeout_duration);
      write(doutfd, &byt2, 1);
      return (1);
    }
  else{return (0);}
}

static void house_light_only(int doutfd)
{
  int byt1 = HOUSELT;
  write(doutfd, &byt1, 1);
}

static int open_connection(char *hostname, u_short portnum)
{
  struct hostent *hp;
  struct sockaddr_in sin;
  int sock;
  
  if ((hp = gethostbyname(hostname)) != NULL)
    {
      if ((sock = socket(hp->h_addrtype, SOCK_STREAM, 0)) >= 0)
	{
	  sin.sin_family = hp->h_addrtype;
	  memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);
	  sin.sin_port = htons(portnum);
	  if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) >= 0)
	    return sock;
	  else fprintf(stderr, "remotesound: connect(): %s\n", strerror(errno));
	}
      else fprintf(stderr, "remotesound: socket(): %s\n", strerror(errno));
    }
  else fprintf(stderr, "remotesound: gethostname(): %s\n", strerror(errno));
  return -1;
}



int trig_saber (int trig_dur) {
  char *msg;
  msg = (char *)malloc(256*sizeof(char));
  
  sprintf(msg,"trig %d\n", trig_dur);
  if (write(client_fd, msg, strlen(msg)) == -1)
    {
      close(client_fd);
      client_fd= -1;
      perror("remote: write(client_fd) ");
    } 
  return(0);
}


int play_saber (char *stim_val) {
  char *msg;
  msg = (char *)malloc(256*sizeof(char));
  
  sprintf(msg,"play %s\n", stim_val);
  if (write(client_fd, msg, strlen(msg)) == -1)
    {
      close(client_fd);
      client_fd= -1;
      perror("remote: write(client_fd) ");
    } 
  return(0);
}

int log_saber (char *log_message) {
  char *msg;
  msg = (char *)malloc(256*sizeof(char));
  
  sprintf(msg,"note %s\n", log_message);
  if (write(client_fd, msg, strlen(msg)) == -1)
    {
      close(client_fd);
      client_fd= -1;
      perror("remote: write(client_fd) ");
    } 
  return(0);
}




