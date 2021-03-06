/*****************************************************************************
**  CHRON2CHOICE - variant of 2choice.c that runs using sabe
******************************************************************************
**
**
**  HISTORY:
**
** 12-13-01 TQG: Adapted from most recent version of 2choice.c
**
**          chron2choice links to saber & uses the AT-MIO buffers to play sound files
**               and record an analog trace of the subject's behavior (an neural data) 
**               Note that we need the DAC hardware patch on box 6 to get the behavioral 
**               data to saber.  'chron2choice' tells saber when to play a 
**               stimulus file, what file to play, when to trigger, what to log, and 
**               handles all behavioral reinforcement.
**
**              ********** THIS ONLY RUNS IN BOX 6 **************** 
*/


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include "pcmio.h"
#include "operantio.h"


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
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define DEF_REF                  10            /* default reinforcement for corr. resp. set to 100% */

/*------------- SABER VARIABLE DEFINES ------------ */
#define PREFIX                     2            /* number of seconds prepended to the trigger*/
#define POSTFIX                    4            /* should be equal to the behav response window */ 
#define PORTNUMBER               13003          /* sclient port for connection to saber */    
#define BUF_SIZE                   128

/* some variables */
long timeout_duration = TIMEOUT_DURATION;
long feed_duration    = FEED_DURATION;
int  sleep_interval   = SLEEP_TIME;
int  reinf_val        = DEF_REF;
int samplerate = DACSAMPLERATE;
int pretrig_val = PREFIX;  
int posttrig_val = POSTFIX;
size_t bufsize = BUF_SIZE;

/* some functions */
int feed(int reinf_val, int doutfd, int resp_sel, int dual_hopper);
int timeout(int reinf_val, int doutfd);
static int open_connection(char *hostname, u_short portnum);
int trig_saber (int trig_dur);
int play_saber (char *stim_val); 
int log_saber (char *log_message);
static void pause_process (int signum);
static void sig_pipe(int signo);
void pause_loop (int epoch_num);

const char exp_name[] = "CHRON2CHIOCE";
char *stimpath = "/usr/local/users/tim/stimuli/";
char *host = "sturnus";
char *dac_init_pcm = "silence.pcm";

int box_id = -1;
int client_fd = -1;
int dsp_fd = 0;
int pause_flag = 0;   /*start with pause off*/  

time_t curr_tt;

struct timespec iti = { INTER_TRIAL_INTERVAL, 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};


/* -------- Signal handling --------- */

static void pause_process (int signum){
  if (pause_flag == 1) {
    pause_flag = 0;
    signal(SIGUSR1, pause_process);
  }
  else {
    pause_flag = 1;
    signal(SIGUSR1, pause_process);
  }
}

static void sig_pipe(int signum)
{
#ifndef __USE_BSD_SIGNAL
  if (signal(SIGPIPE, sig_pipe) == SIG_ERR)
    fprintf(stderr, "error re-installing signal handler for SIG_PIPE");
#endif
  fprintf(stderr, "saber caught SIGPIPE\n");
  return;
}




int main(int ac, char *av[])
{
	FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
    	char *stimfroot, *message_buf, *command_msg, *trialdata, *saber_msg;
	const char delimiters[] = " .,;:!-";
	char datafname[128], hour [16], min[16], month[16], stimfname [128], 
	  day[16], dsumfname[128], stimftemp[128], exmtemp [128], full_stim [128];
	char  buf[128], stimexm[128], input [64],
	  timebuff[64], tod[256], date_out[256];
	int dinfd=0, doutfd=0, nstims, stim_class, subjectid, loop, flash, no_cx,
	  correction, trial_num, reinf_val, resp_sel, resp_acc, i, stimtemp, xresp;
	float resp_rxt, timeout_val;
	int no_resp1, no_resp2, corr1, corr2, incorr1, incorr2;
	int GT_no_resp1, GT_no_resp2, GT_corr1, GT_corr2, GT_incorr1, GT_incorr2;
	int no_resp3, no_resp4, corr3, corr4, incorr3, incorr4;
	int GT_no_resp3, GT_no_resp4, GT_corr3, GT_corr4, GT_incorr3, GT_incorr4;
	struct timeval stimoff, resp_window, resp_lag, resp_rt;
	struct tm *loctime;
	int left = 0, right= 0, center = 0, fed = 0;
	int reinfor_sum = 0, reinfor = 0, trig_duration = 0;
	int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
	PCMFILE *exm_fp = NULL;
        short *stimbuf;
        int nsamples, stimsec = 0, optval = 1, epoch_num, nreps, repcount, j, k, 
	  checksum1, checksum2, event_num, stim_number = 0, epoch_len, *playlist=NULL;
        struct timeval tv, stimdur_tv, target_tv;
        struct timespec wait_ts;  
	struct stim {
	  char exemplar[128];
	  int class;
	  float dur;
	  int nsamp;
	}stimulus[MAXSTIM];
	
	int dual_hopper=0;
	sigset_t trial_mask;
        flash = xresp = no_cx = 0;

        command_msg = (char *)malloc(256*sizeof(char));
        saber_msg = (char *)malloc(256*sizeof(char));
        message_buf = (char *)malloc(256*sizeof(char));
        srand (time (0) );
        wait_ts.tv_sec = 0;
        wait_ts.tv_nsec = 10000;

        sigemptyset (&trial_mask);
        sigaddset (&trial_mask, SIGINT);
        sigaddset (&trial_mask, SIGTERM);
	

	/* Make sure the saber can see you, and set up sig handlers*/
	
	signal(SIGUSR1, pause_process);
        if (signal(SIGPIPE, sig_pipe) == SIG_ERR)
          {
            fprintf(stderr, "error installing SIG_PIPE handler: %s\n", strerror(errno));
            return 0;
          }


      /* block until you connect to saber via sclinet */
        printf("trying to connect to saber.......");

	while (1) {
          /* Check for socket connection */
          if ((client_fd = open_connection( host, PORTNUMBER)) == -1) {      
            perror("open_connection");
            return(-1);
          } else {
            fcntl(client_fd, F_SETFL, O_NONBLOCK);
            setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
            break;
          }
        }
	printf("..connected!\n");
	play_saber (dac_init_pcm);


	/* Parse the command line */
	
	for (i = 1; i < ac; i++)
	  {
	    if (*av[i] == '-')
	      {
		if (strncmp(av[i], "-B", 2) == 0)
		  { 
		    sscanf(av[++i], "%i", &box_id);
		    if(DEBUG){printf("box number = %d\n", box_id);}
		    if (box_id != 6) {
		      fprintf(stderr, "ERROR: Box '%d' is not a valid box number\n", box_id);
		      fprintf(stderr, "\tchron2choice only runs in box '6'\n");
		      fprintf(stderr, "\n\tTry 'chron2choice -help' for help\n");
		      fprintf(stderr, "\tABORTING!\n");
		      exit (-1);
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
		else if (strncmp(av[i], "-XX", 2) == 0)
		  { 
		    fprintf(stderr, "************ CORRECTION TRIALS TURNED OFF ************\n");
		    no_cx = 1;
		  }
		else if (strncmp(av[i], "-t", 2) == 0)
		  { 
		    sscanf(av[++i], "%f", &timeout_val);
		    timeout_duration = (int) timeout_val*1000000;
		    fprintf(stderr, "timeout duration set to %d secs\n", (int) timeout_val);
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
		  fprintf(stderr, "chron2choice usage:\n");
		  fprintf(stderr, "    chron2choice [-help] [-B x] [-R x] [-f] [-d] [-S <subject number>] \n\n");
		  fprintf(stderr, "        -help        = show this help message\n");
		  fprintf(stderr, "        -B x         = use '-B 6' \n");
		  fprintf(stderr, "                       Note: chron2choice will only run in box 6 (it needs the DAC hardware patch)\n");
		  fprintf(stderr, "        -R x         = specify P(Reinforcement) for correct response\n");
		  fprintf(stderr, "                       ex: Use '-R 5' for P(R) = 50%% \n");
		  fprintf(stderr, "                       'x' must be 0 to 10 \n");
		  fprintf(stderr, "        -f           = flash left & right pokelights during response window\n");
		  fprintf(stderr, "        -d           = use the dual hopper protocol\n");
		  fprintf(stderr, "        -t val       = set the timeout duration to 'val' secs (use a real number, e.g 2.5 )\n");
		  fprintf(stderr, "        -x           = use this flag to enable correction trials for 'no-response' trials,\n");
                  fprintf(stderr, "        -XX          = diables all correction trials\n");
		  fprintf(stderr, "        -S xxx       = specify the subject ID number (required)\n");
		  //fprintf(stderr, "        filename     = specify stimulus filename (required)\n\n");
		  exit(-1);
		  }
		else
		  {
		    fprintf(stderr, "Unknown option: %s\t", av[i]);
		    fprintf(stderr, "Try 'chron2choice -help' for help\n");
		  }
	      }
	  }


       /* Initialize box */
	printf("Initializing box #%d...", box_id);
	operant_init(box_id);
	operant_clear(box_id);
	printf("done\n");


	fprintf(stderr, "Subject ID number: %i\n", subjectid);
	fprintf(stderr, "Reinforcement set at: %i%%\n", (reinf_val*10) );


/* Send some intial commands to saber */

        sprintf(command_msg ,"exp %d_chron\n", subjectid );
        if (write(client_fd, command_msg, strlen(command_msg)) == -1)
          {
            close(client_fd);
            client_fd= -1;
            perror("remote: write(client_fd) ");
          } 
        sprintf(command_msg ,"set pretrig %d\n", pretrig_val);
        if (write(client_fd, command_msg, strlen(command_msg)) == -1)
          {
            close(client_fd);
            client_fd= -1;
            perror("remote: write(client_fd) ");
          } 
        curr_tt = time (NULL);
        sprintf(message_buf,"%s CHRONIC 2CHOICE SESSION INITIATED", 
                asctime(localtime(&curr_tt)) );
	printf("message_buf: %s\n", message_buf); 
        log_saber(message_buf);



/*  Open & setup data logging files */

	curr_tt = time (NULL);
	loctime = localtime (&curr_tt);
	if (DEBUG){printf("time: %s\n" , asctime (loctime));}
	strftime (timebuff, 64, "%d%b%y", loctime);
	sprintf(datafname, "%i_chron2choice.rDAT", subjectid);
	sprintf(dsumfname, "%i.chron_summaryDAT", subjectid);
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
	fprintf (datafp, "Reinforcement for correct resp: %i%%\n", (reinf_val*10) );
	fprintf (datafp, "Epoch#\tTrl#\tTrlTyp\tStimulus\t\t\tClass\tRspSL\tRspAC\tRspRT\tReinf\tTOD\tDate\n");




 /* Zero some session variables*/
	trial_num = epoch_num = 0;                                                   /* zero trial count for session */
	corr1 = corr2 = incorr1 = incorr2 = no_resp1 = no_resp2 = 0;                     /* zero session tallies */
	GT_corr1 = GT_corr2 = GT_incorr1 = GT_incorr2 = GT_no_resp1 = GT_no_resp2 = 0;   /* zero block tallies */
	corr3 = corr4 = incorr3 = incorr4 = no_resp3 = no_resp4 = 0;                     /* zero session tallies */
	GT_corr3 = GT_corr4 = GT_incorr3 = GT_incorr4 = GT_no_resp3 = GT_no_resp4 = 0;   /* zero block tallies */
	




/* --------------EPOCH SEQUENCE ------------*/

	do{                      /* start of epoch loop */
	  ++epoch_num;

	/* get the stimulus name and rep number */
	  printf("Enter a stimulus file name and number of reps for this epoch <stimfname n>\n");
	  scanf ("%s", stimfname);
	  scanf ("%d", &nreps);
	  if (DEBUG){printf("stimfile name: %s \n", stimfname);}
	  if (DEBUG){printf("number of reps: %d \n", nreps);}
	  sprintf (stimftemp, "%s", stimfname);
	  stimfroot = strtok (stimftemp, delimiters); 
	  fprintf (datafp, "Stimulus source: %s\n", stimfname);  
	  fprintf (datafp, "Reps: %d\n", nreps);  

        /* Read in the list of exmplars from stimulus file */
	  nstims = 0;
	  if ((stimfp = fopen(stimfname, "r")) != NULL){
	    while (fgets(buf, sizeof(buf), stimfp))
	      nstims++;
	    
	    fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
	    rewind(stimfp);
	  
	    for (i = 0; i < nstims; i++){
	      fgets(buf, 128, stimfp);
	      sscanf(buf, "%d\%s", &stimtemp, stimulus[i].exemplar);
	      stimulus[i].class = stimtemp;
	      printf("stimulus num: %d\t", i); 
	      printf("file: %s\t", stimulus[i].exemplar);
	      printf("class: %i\t", stimulus[i].class);
	      strcpy (exmtemp, stimulus[i].exemplar);   
	      sprintf(full_stim ,"%s%s", stimpath, exmtemp);  
	      if ((exm_fp = pcm_open(full_stim, "r")) == NULL)
		{fprintf(stderr, "Error opening stimulus file '%s'. ", full_stim);}
	      if ((pcm_read(exm_fp, &stimbuf, &nsamples)) == -1)
		{fprintf(stderr, "Error reading stimulus file '%s'. ", stimexm);}
	      pcm_close (exm_fp);
	      stimulus[i].dur = (float)nsamples / (float)samplerate;
	      stimulus[i].nsamp = nsamples;
	      printf("num samples: %d\t", stimulus[i].nsamp);
	      printf("duration: %f\n", stimulus[i].dur);
	    }
	  }
	  else{ 
	    printf("Error opening stimulus input file!\n");
	    close(client_fd);
	    exit(0);	  
	  }
	  
	  fclose(stimfp);
	  if(DEBUG){printf("flag: done reading in stims\n");}
	     
	     
	/* generate a pseudo-random playlist for the epoch */ 
	  checksum1 = checksum2 = 0;
	  epoch_len = nstims * nreps;
	  if(DEBUG){printf("epoch length = %i\n", epoch_len);}
	  repcount = 0;
	  free(playlist);
	  playlist = malloc(((nstims*nreps)+1)*sizeof(int));
	  
	  for (i= 0; i < epoch_len; i++){         /* Generate playlist items*/
	    j=1;
	    while (j){  /* Generate random songs (that aren't in list)*/
	      playlist[i] = (int) ( ((float)nstims)*rand()/(RAND_MAX+1.0));
	      j=0;
	      for(k=0; k<i; k++){
		if (playlist[k] == playlist[i]){   /*then it's in the list*/
		  if(DEBUG==2){printf("playlist[%i] = %i\n", i, playlist[i]);}
		  repcount++;
		  if(DEBUG==2){printf("repcount: %d\n", repcount);}
		}
		if(repcount >= nreps){
		  j=1;repcount=0;break;
		}
	      }
	      repcount=0;
	      if(DEBUG==2){printf("playlist[%i] = %i\n", i, playlist[i]);}
	    }
	  }
	     
	  
	  for  (i= 0; i < (nstims*nreps); i++){
	    checksum1 += playlist[i];
	    if(DEBUG){printf("playlist[%i] = %i\n", i, playlist[i]);}
	  }
	  for (i= 0; i < nstims; i++){
	    checksum2 += (i*nreps);
	  }
	  if(DEBUG){ printf("checksum1: %i\n", checksum1);}
	  if(DEBUG){ printf("checksum2: %i\n", checksum2);}
	  if(checksum1 != checksum2){
	    printf("WARNING: checksums don't match!  There may be a problem with the epoch playlist\n");}
	 

      /* Wait for user OK to start */
	  do{
	    printf("Type 'go' to start the epoch\n");
	    scanf ("%s", input);
	  }while(strcmp(input, "go"));
	     


  /*+++++++++++ Epoch Trial Sequence ++++++++++++++*/
	  event_num = 0;                           /* internal count of the number of trials w/in an epoch */ 
	  correction = 1;
	  curr_tt = time(NULL);
	  loctime = localtime (&curr_tt);
	  strftime (hour, 16, "%H", loctime);
	  if (DEBUG==2){printf("atoi(hour) at loop start: %d \n", atoi(hour));}
	  
	  do{                                                                       /* event loop w/in epoch */
	    ++event_num;
	    if(DEBUG){printf("event number: %d\n", event_num);}
	    stim_number = playlist[(event_num-1)];                                    /* select stim exemplar from the playlist */ 
	    if(DEBUG){printf("playlist[event-1] = %d\n", playlist[event_num-1]);} 
	    stim_class = stimulus[stim_number].class;                               /* set stimtype variable */
	    strcpy (stimexm, stimulus[stim_number].exemplar);                       /* get exemplar filename */
	    stimsec = floor(stimulus[stim_number].dur);
	    stimdur_tv.tv_sec = (time_t)stimsec;
	    stimdur_tv.tv_usec = (long)floor((stimulus[stim_number].dur - stimsec) * 1000000);
	    trig_duration = ceil(stimulus[stim_number].dur) + posttrig_val;  
	    	       
	    if (DEBUG){printf("chose exemplar: %s\tnumber: %d\n", stimulus[stim_number].exemplar, stim_number );}
	    
	    if(DEBUG==2){
	      read(client_fd, saber_msg, bufsize);                           /*read any incoming messages from saber*/
	      printf("SABER SAYS: %s\n", saber_msg);}                        /* and print to screen */
	       
	    do{                                             /* start correction trial loop */
	      printf("Event: %d\t cued --> stim: %d '%s'\t %d.%d secs\n", event_num, stim_number, 
		   stimulus[stim_number].exemplar, (int) stimdur_tv.tv_sec, (int)stimdur_tv.tv_usec);
	    
	      resp_sel = resp_acc = resp_rxt = 0;           /* zero trial variables        */
	      ++trial_num;
	      operant_write (box_id, HOUSELT, 1);           /* make sure house light is on */
		 
	 /* Wait for center key press */
	      printf("Waiting for center key press\n");
	      sprintf(message_buf,"Waiting for center response\n");
	      log_saber(message_buf);
	      center = 0;
	     
	      do{ 
		nanosleep(&rsi, NULL);	               	       
		center = operant_read(box_id, CENTERPECK);   /*get value at center peck position*/		 	       
		if(pause_flag){pause_loop(epoch_num);} 
	      }while(center==0);

	      sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
		
		 
           /* Play stimulus file */
	      printf("trig ON\t");
	      fflush(stdout);
	      trig_saber (trig_duration); 
		   
	      gettimeofday(&tv, NULL);
	      timeradd(&stimdur_tv, &tv, &target_tv);
	      printf("playing '%s' ...", stimulus[stim_number].exemplar);
	      fflush(stdout);
	      if (DEBUG){printf("I think I'm playing '%s'\n", stimulus[stim_number].exemplar);}
	      play_saber (stimulus[stim_number].exemplar);
	      gettimeofday(&tv,NULL);
	      while (timercmp(&tv, &target_tv, <)) {
		nanosleep(&wait_ts, NULL);          /* sleep until balance of stimulus is done */
		gettimeofday(&tv,NULL);
	      }
	      printf("... done\t");
	      fflush(stdout);
	      gettimeofday(&stimoff, NULL);
	      
	      if (DEBUG==2){stimoff_sec = stimoff.tv_sec;}
	      if (DEBUG==2){stimoff_usec = stimoff.tv_usec;}
	      if (DEBUG==2){printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);}
	      
	   /* Wait for right/left key press */
	      timeradd (&stimoff, &respoff, &resp_window);
	      if (DEBUG==2){ respwin_sec = resp_window.tv_sec;}
	      if (DEBUG==2){respwin_usec = resp_window.tv_usec;}
	      if (DEBUG==2){printf("resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);}
	      
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
	      } while ( (left==0) && (right==0) && (timercmp(&resp_lag, &resp_window, <)) );
	      
	      printf("trig OFF\n\n");
	      operant_write (box_id, LFTKEYLT, 0);    /*make sure the key lights are off after resp interval*/
	      operant_write (box_id, RGTKEYLT, 0);
	      
	      
	  /* Calculate response time */
	      curr_tt = time (NULL); 
	      loctime = localtime (&curr_tt);                     /* date and wall clock time of resp */
	      timersub (&resp_lag, &stimoff, &resp_rt);           /* reaction time */
	      if (DEBUG==2){resp_sec = resp_rt.tv_sec;}      
	      if (DEBUG==2){resp_usec = resp_rt.tv_usec;}
	      if (DEBUG==2){printf("resp rt sec: %d \t usec: %d\n", resp_sec, resp_usec);} 
	      resp_rxt = (float) resp_rt.tv_sec + ( (float) resp_rt.tv_usec/1000000);  /* format reaction time */
	      if (DEBUG==2){printf("flag: resp_rxt = %.4f\n", resp_rxt);}
	      
	      strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
	      strftime (min, 16, "%M", loctime);
	      strftime (month, 16, "%m", loctime);
	      strftime (day, 16, "%d", loctime);
	      
		
	 /* Consequate responses */
	      if (DEBUG){printf("flag: stim_class = %d\n", stim_class);}
	      if (DEBUG){printf("flag: exit value left = %d, right = %d\n", left, right);}
	      
	      if (stim_class == 1){                                 /* GO LEFT */                          
		if ( (left==0 ) && (right==0) ){
		  resp_sel = 0;
		  resp_acc = 2;
		  ++no_resp1; ++GT_no_resp1;
		  reinfor = 0;
		  if (DEBUG){ printf("flag: no response to stimtype 1\n");}
		}
		else if (left != 0){
		  resp_sel = 1;
		  resp_acc = 1;
		  ++corr1; ++GT_corr1;
		  reinfor = feed(reinf_val, doutfd, resp_sel, dual_hopper);
		  if (reinfor == 1) { ++fed;}
		  if (DEBUG){printf("flag: correct response to stimtype 1\n");}
		  correction = 1;
		}
		else if (right != 0){
		  resp_sel = 2;
		  resp_acc = 0;
		  ++incorr1; ++GT_incorr1;
		  reinfor =  timeout(reinf_val, doutfd);
		  if (DEBUG){printf("flag: incorrect response to stimtype 1\n");}
		  correction = 0;
		} 
		else{
		  fprintf(datafp, "DEFAULT SWITCH for bit value:ERROR CODE REMOVED");
		}
	      }
	      else if (stim_class == 2){                           /* GO RIGHT */
		if ( (left==0) && (right==0) ){
		  resp_sel = 0;
		  resp_acc = 2;
		  ++no_resp2; ++GT_no_resp2;
		  reinfor = 0;
		  if (DEBUG){printf("flag: no response to stimtype 2\n");}
		}
		else if (left!=0){
		  resp_sel = 1;
		  resp_acc = 0;
		  ++incorr2; ++GT_incorr2;
		  reinfor =  timeout(reinf_val, doutfd);
		  if (DEBUG){printf("flag: incorrect response to stimtype 2\n");}
		  correction = 0;
		}
		else if (right!=0){
		  resp_sel = 2;
		  resp_acc = 1;
		  ++corr2; ++GT_corr2; 
		  reinfor = feed(reinf_val, doutfd, resp_sel, dual_hopper);
		  if (reinfor == 1) { ++fed;}
		  if (DEBUG){printf("flag: correct response to stimtype 2\n");}
		  correction = 1;
		} 
		else{
		  fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
		}
	      }
	      if (stim_class == 3){                                    /* GO LEFT */
		if ( (left==0) && (right==0) ){
		  resp_sel = 0;
		  resp_acc = 2;
		  ++no_resp3; ++GT_no_resp3;
		  reinfor = 0;
		  if (DEBUG){printf("flag: no response to stimtype 3\n");}
		}
		else if (left!=0){
		  resp_sel = 1;
		  resp_acc = 1;
		  ++corr3; ++GT_corr3;
		  reinfor = feed(reinf_val, doutfd, resp_sel, dual_hopper);
		  if (reinfor == 1) { ++fed;}
		  if (DEBUG){printf("flag: correct response to stimtype 3\n");}
		  correction = 1;
		}
		else if (right!=0){
		  resp_sel = 2;
		  resp_acc = 0;
		  ++incorr3; ++GT_incorr3;
		  reinfor =  timeout(reinf_val, doutfd);
		  if (DEBUG){printf("flag: incorrect response to stimtype 3\n");}
		  correction = 0;
		} 
		else{
		  fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
		}
	      }
	      else if (stim_class == 4){                                 /* GO RIGHT */
		if ( (left==0) && (right==0) ){
		  resp_sel = 0;
		  resp_acc = 2;
		  ++no_resp4; ++GT_no_resp4;
		  reinfor = 0;
		  if (DEBUG){printf("flag: no response to stimtype 4\n");}
		}
		else if (left!=0){
		  resp_sel = 1;
		  resp_acc = 0;
		  ++incorr4; ++GT_incorr4;
		  reinfor =  timeout(reinf_val, doutfd);
		  if (DEBUG){printf("flag: incorrect response to stimtype 4\n");}
		  correction = 0;
		}
		else if (right!=0){
		  resp_sel = 2;
		  resp_acc = 1;
		  ++corr4; ++GT_corr4; 
		  reinfor = feed(reinf_val, doutfd, resp_sel, dual_hopper);
		  if (reinfor == 1) { ++fed;}
		  if (DEBUG){printf("flag: correct response to stimtype 4\n");}
		  correction = 1;
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
	      trialdata = (char *)malloc(256*sizeof(char));
	      strftime (tod, 256, "%H%M", loctime);
	      strftime (date_out, 256, "%m%d", loctime);
	      sprintf(trialdata, "%d\t%d\t%d\t%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", epoch_num, trial_num, 
		      correction, stimexm, stim_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
	      fprintf(datafp, "%d\t%d\t%d\t%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", epoch_num, trial_num, 
		      correction, stimexm, stim_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
	      fflush (datafp);
	      log_saber(trialdata);
	      if (DEBUG){printf("%s\n", trialdata);}
	      if (DEBUG){printf("flag: trial data written\n");}
	      
	  /* Update summary data */ 	       
	      fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
	      fprintf (dsumfp, "EPOCH TOTALS              GRAND TOTALS (%d epochs\n", epoch_num);
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
	
	      if (correction == 0 && no_cx == 1){
	       	if (DEBUG){printf("correction value forced to '1'\n");}
		correction = 1;}                                             /* override for correction trials */
	    }while (correction == 0);                             /* correction trial loop */
	    
	    if (DEBUG){printf("flag: broke correction trial loop\n");}   
	    stim_number = -1;
	    trial_num++;
	  }while (event_num < epoch_len);                               /*epoch loop */
	
	}while (1);                                                       /* session loop */ 
		     
   /*Cleanup*/
	close(client_fd);
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
      else{
	return (0);
      }
    }
    else{
      return (0);
    }
  }
  else{
    if (feed_me < reinf_val){
      operant_write(box_id, LFTFEED, 1);
      usleep(feed_duration);
      operant_write(box_id, LFTFEED, 0);
      return(1);
    }
    else{
      return (0);
    }
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
void pause_loop (int epoch_num)
{
  char *msg;
  msg = (char *)malloc(256*sizeof(char));

  curr_tt = time (NULL);
  sprintf(msg,"EPOCH %d PAUSED at %s", epoch_num, asctime(localtime(&curr_tt)) );
  printf("\n ----------- %s", msg );
  log_saber(msg);                                                  /* log pause start time */  
  operant_write (box_id, HOUSELT, 0);                              /* turn houselight off */
  while (pause_flag){                                              /* loop until pause is turned off */
    sleep (1);}
  curr_tt = time (NULL);
  operant_write (box_id, HOUSELT, 1);                                                      /* turn houselight on */
  sprintf(msg,"EPOCH %d RESUMED at %s", epoch_num, asctime(localtime(&curr_tt)));
  printf("\n ----------- %s\n", msg );                                              /* log pause stop time */  
  log_saber(msg);
}

/* REMOTE SABER FUNCTIONS */

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
  if(DEBUG){printf("play_saber just tried to play '%s'\n", stim_val);}
  return(0);
}


int log_saber (char *log_message) {
  char *msg;
  msg = (char *)malloc(256*sizeof(char));
  
  sprintf(msg,"note %s", log_message);
  if (write(client_fd, msg, strlen(msg)) == -1)
    {
      close(client_fd);
      client_fd= -1;
      perror("remote: write(client_fd) ");
    } 
  return(0);
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
	  else fprintf(stderr, "remotesaber: connect(): %s\n", strerror(errno));
	}
      else fprintf(stderr, "remotesaber: socket(): %s\n", strerror(errno));
    }
  else fprintf(stderr, "remotesaber: gethostname(): %s\n", strerror(errno));
  return -1;
}

