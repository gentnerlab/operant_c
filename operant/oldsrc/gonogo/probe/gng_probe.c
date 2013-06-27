/*****************************************************************************
** gonogo2.c - temp code for running go/nogo operant training procedure
******************************************************************************
**
** 9-19-01 TQG: Adapted from most current 2choice.c
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
#include <pcmio.h>
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
#define HOUSELT	  3   
#define LFTFEED	  4
#define RGTFEED   5
 


/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             2             /* seconds from simulus end until NORESP is registered  (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  128            /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 16            /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define TIMEOUT_DURATION         10000000       /* duration of timeout in microseconds */
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


int feed(int rval);
int timeout(int rval);
int probeGO(int rval, int mirr);

const char exp_name[] = "GONOGO2";
int box_id = -1;
int dual_hopper=0;
int  resp_sel, resp_acc;

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
	int dinfd=0, doutfd=0, nclasses, nstims, stim_class, stim_number, stim_reinf, stim_freq, subjectid, loop, flash, box,
	  correction, trial_num, session_num, playval, i,j,k, *playlist=NULL, totnstims=0, mirror=0;
	float resp_rxt, timeout_val;
	//int splus_no, splus_go, sminus_no, sminus_go, Tsplus_no, Tsplus_go, Tsminus_no, Tsminus_go;
	time_t curr_tt;
	struct timeval stimoff, resp_window, resp_lag, resp_rt;
	struct tm *loctime;
	int left = 0, right= 0, center = 0, fed = 0;
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
	  int go;
	  int no;
	  float ratio;
	} stimRses[MAXSTIM], stimRtot[MAXSTIM], classRses[MAXCLASS], classRtot[MAXCLASS];


	sigset_t trial_mask;
        flash=0;
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
			perror("FAILED connection to soundserver");
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
		      fprintf(stderr, "\n\tTry 'gng_probe -help' for help\n");
		      exit(-1);
		      break;
		     }
		  }
		else if (strncmp(av[i], "-S", 2) == 0)
		  {sscanf(av[++i], "%i", &subjectid);}
		else if (strncmp(av[i], "-mirrorP", 8) == 0){
		  mirror = 1;}                                     //mirror reinforcement/punishment rates for responses to all probe stimuli 
		 else if (strncmp(av[i], "-t", 2) == 0)
                  {
                    sscanf(av[++i], "%f", &timeout_val);
                    timeout_duration = (int) (timeout_val*1000000);
                    fprintf(stderr, "timeout duration set to %d microsecs\n", (int) timeout_duration);
                  }
		else if (strncmp(av[i], "-on", 3) == 0){
		  sscanf(av[++i], "%i", &startH);}
		else if (strncmp(av[i], "-off", 4) == 0){
		  sscanf(av[++i], "%i", &stopH);}
		
		else if (strncmp(av[i], "-help", 5) == 0){
		  fprintf(stderr, "gng_probe usage:\n");
		  fprintf(stderr, "    gng_probe [-help] [-B x] [-mirrorP] [-S <subject number>] <filename>\n\n");
		  fprintf(stderr, "        -help        = show this help message\n");
		  fprintf(stderr, "        -B x         = use '-B 1' '-B 2' ... '-B 12' \n");
		  fprintf(stderr, "        -mirrorP     = set the timeout rate equal to the 'GO_reinforcement rate' in the .stim file\n");
		  fprintf(stderr, "                       ***** MirrorP applies only to probe trials ****\n");		  
fprintf(stderr, "                       Note that if you use 'mirrorP' the base rate in the .stim file must be less than or equal to 50\%\n"); 
		  fprintf(stderr, "        -t 'x'       = set the timeout duration to 'x' secs (use a real number, e.g 2.5 )\n");
		  fprintf(stderr, "        -on          = set hour for exp to start eg: '-on 7' (default is 7AM)\n");
		  fprintf(stderr, "        -off         = set hour for exp to stop eg: '-off 19' (default is 7PM)\n");
		  fprintf(stderr, "        -S xxx       = specify the subject ID number (required)\n");
		  fprintf(stderr, "        filename     = specify the name of the text file containing the stimuli (required)\n");
		  fprintf(stderr, "                       where each line is: 'class' 'pcmfile' 'present_freq' 'GO_reinforcement_rate'\n");
		  fprintf(stderr, "                         'class'= 1 for S+, 2 for S-, 3 or greater for nondifferential (e.g. probe stimuli) \n");
		  fprintf(stderr, "                         'pcmfile' is the name of the stimulus soundfile\n");
		  fprintf(stderr, "                         'presfreq' is the overall rate (compared to the other stimuli) at which the stimulus is presented. \n"); 
		  fprintf(stderr, "                                    The actual rate for each stimulus (expressed as an integer) is that value divded by the\n");
		  fprintf(stderr, "                                    sum for all stimuli. Set all prefreq values to 1 for equal probablility \n"); 
		  fprintf(stderr, "                          'GO_reinforcement_rate' is the rate at which GO responses to this stimulus are reinforced.  For class '1'\n");
		  fprintf(stderr, "                                                  stimuli this is the rate of food reward following a correct response.  For class '2'\n");
		  fprintf(stderr, "                                                  stimuli this is the rate of punishment (timeout) following an incorrect response.\n"); 
		  fprintf(stderr, "                                                  For probe stimuli this is the rate of nondifferential reinforcement Use '-mirrorP'\n");
		  fprintf(stderr, "                                                  if you want to reinforce with food reward and timeout at equal rates on probe trials.\n");		  
		  exit(-1);
		}
		else
		  {
		    fprintf(stderr, "Unknown option: %s\t", av[i]);
		    fprintf(stderr, "Try 'gng_probe -help' for help\n");
		  }
	      }
	    else
	      {
		stimfname = av[i];
	      }
	  }

	/* watch for terminal errors*/
	if (stopH <= startH){
	  fprintf(stderr, "\tTERMINAL ERROR: exp start-time must be greater than stop-time\n");
	  exit(-1);
	} 
	if (box_id <= 0){
	  fprintf(stderr, "\tYou must enter a box ID!: %s \n", av[i]); 
	  fprintf(stderr, "\tERROR: try 'gng_probe -help' for available options\n");
	  exit(-1);
	}

	/* Initialize box */
	else
	  {
	   printf("Initializing box #%d...", box_id);
	   operant_init();
	   operant_clear(box_id);
	   printf("done\n");
	  }

	
	/* give user some feedback*/
	fprintf(stderr, "Loading stimulus file '%s' for box '%d' session\n", stimfname, box_id); 
	fprintf(stderr, "Subject ID number: %i\n", subjectid);
	if (mirror==0){
	  fprintf(stderr, "GO responses to S- (class2) stimuli always yeild a timeout. \n");
	  fprintf(stderr, "GO responses to probe stimuli never yield a timeout.\n");
	  }
       

/* Read in the list of exmplars from stimulus file */

	nstims = 0;
	nclasses=0;
	if ((stimfp = fopen(stimfname, "r")) != NULL)
	  {
	    while (fgets(buf, sizeof(buf), stimfp))
	     
		nstims++;
		fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
		rewind(stimfp);

	      
	    for (i = 0; i < nstims; i++)
	      {
		fgets(buf, 128, stimfp);
		stimulus[i].freq = stimulus[i].reinf=0;
		sscanf(buf, "%d\%s\%d\%d", &stimulus[i].class, stimulus[i].exemplar, &stimulus[i].freq, &stimulus[i].reinf);
		if((stimulus[i].freq==0) || (stimulus[i].reinf==0)){
		  printf("ERROR: insufficnet data or bad format in '.stim' file. Try 'gng_probe -help'\n");
		  close_soundserver(dsp_fd);
		  exit(0);} 
		totnstims += stimulus[i].freq;
		if(DEBUG){printf("totnstims: %d\n", totnstims);}
		if (load2soundserver(dsp_fd, i, stimulus[i].exemplar) != 0 ){ 
		  printf("Error loading stimulus file: %s\n",  stimulus[i].exemplar);
		  close_soundserver(dsp_fd);
		  exit(0);}
		printf("Loaded %s in soundserver\t", stimulus[i].exemplar);
		printf("Class: %i\n", stimulus[i].class);
		
		/* count stimulus classes*/
		if (nclasses<stimulus[i].class){nclasses=stimulus[i].class;}
		if (DEBUG){printf("nclasses: %d\n", nclasses);}


		/*check the reinforcement rates */
		if (stimulus[i].class==1){
		  fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct GO responses\n", stimulus[i].exemplar, stimulus[i].reinf);
		}
		else if (stimulus[i].class==2){
		  fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect GO responses\n", stimulus[i].exemplar, stimulus[i].reinf);
		}
		else{
		  if(mirror=1){
		    if (stimulus[i].reinf>50){
		      fprintf(stderr, "ERROR!: To mirror food and timeout reinforcement values you must use a base rate less than or equal to 50\%\n");
		      close_soundserver(dsp_fd);
		      exit(0);
		    }
		    else{
		      fprintf(stderr, "p(food) reward and p(timeout) on probe trials with %s is set to %d%%\n", stimulus[i].exemplar, stimulus[i].reinf );
		    }
		  }
		  else{fprintf(stderr, "Reinforcement rate on probe trials is set to %d%% for correct GO responses, 100%% for incorrect GO responses\n");
		  }
		}
	      }
	  }
	else 
	  {
	    printf("Error opening stimulus input file! Try 'gng_probe -help' for proper file formatting.\n");  
	    close_soundserver(dsp_fd);
	    exit(0);	  
	  }
        
	fclose(stimfp);
	if(DEBUG){printf("Done reading in stims; %d stims in %d classes found\n", nstims, nclasses);}
	
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
	strftime (timebuff, 64, "%d%b%y", loctime);
	if (DEBUG){printf ("stimfname: %s\n", stimfname);}
	sprintf (stimftemp, "%s", stimfname);
	if (DEBUG){printf ("stimftemp: %s\n", stimftemp);}
	stimfroot = strtok (stimftemp, delimiters); 
	if (DEBUG){printf ("stimftemp: %s\n", stimftemp);}
	if (DEBUG){printf ("stimfname: %s\n", stimfname);}
	sprintf(datafname, "%i_%s.gonogo_rDAT", subjectid, stimfroot);
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
	fprintf (datafp, "reinforcement is set in the .stim\t  mirror:%d \n", mirror );
	fprintf (datafp, "Sess#\tTrl#\tTrlTyp\tStimulus\t\t\tClass\tRspSL\tRspAC\tRspRT\tReinf\tTOD\tDate\n");


   /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
   ********************************************/
	session_num = 1;
	trial_num = 0;
 	correction = 1;
	for(i = 0; i<nstims;++i){   /*zero out the response tallies */
	  stimRses[i].go = stimRses[i].no = stimRtot[i].go = stimRtot[i].no = 0;
	  stimRses[i].ratio = stimRtot[i].ratio = 0.0;
	}
 	if (DEBUG){printf("stimulus counters zeroed!\n");} 
	for(i=0;i<nclasses;i++){
	  classRses[i].go = classRses[i].no = classRtot[i].go = classRtot[i].no = 0;
	  classRses[i].ratio = classRtot[i].ratio = 0.0;;
	}
	if (DEBUG){printf("class counters zeroed!\n");} 
	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	if (DEBUG){printf("atoi(hour) at loop start: %d \n", atoi(hour));}


	do{                                                                               /* start the block loop */
	  while ((atoi(hour) >= startH) && (atoi(hour) < stopH)){
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
	    
	    do{                                             /* start correction trial loop */
	      resp_sel = resp_acc = resp_rxt = 0;        /* zero trial variables        */
	      ++trial_num;
	      
	      /* Wait for center key press */
	      if (DEBUG){printf("flag: waiting for center key press\n");}
	      operant_write (box_id, HOUSELT, 1);        /* house light on */
	      center = 0;
	      do{                                         
		nanosleep(&rsi, NULL);	               	       
		center = operant_read(box_id, CENTERPECK);   /*get value at center peck position*/		 	       
		
	      }while (center==0);  
	      
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
		printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);}
	     	      
	      /* Wait for center key press */
	      if (DEBUG){printf("flag: waiting for right/left response\n");}
	      timeradd (&stimoff, &respoff, &resp_window);
	      if (DEBUG){ 
		respwin_sec = resp_window.tv_sec;
		respwin_usec = resp_window.tv_usec;
		printf("resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);}
	      
	      loop = 0; center = 0;
	      do{
		nanosleep(&rsi, NULL);
		center = operant_read(box_id, CENTERPECK);
		gettimeofday(&resp_lag, NULL);
	      } while ( (center==0) && (timercmp(&resp_lag, &resp_window, <)) );
		   
	      operant_write (box_id, CTRKEYLT, 0);    /*make sure the key lights are off after resp interval*/
		   
	      /* Calculate response time */
	      curr_tt = time (NULL); 
	      loctime = localtime (&curr_tt);                     /* date and wall clock time of resp */
	      timersub (&resp_lag, &stimoff, &resp_rt);           /* reaction time */
	      if (DEBUG){
		resp_sec = resp_rt.tv_sec;      
		resp_usec = resp_rt.tv_usec;
		printf("resp rt sec: %d \t usec: %d\n", resp_sec, resp_usec);} 
	      resp_rxt = (float) resp_rt.tv_sec + ( (float) resp_rt.tv_usec/1000000);  /* format reaction time */
	      if (DEBUG){printf("flag: resp_rxt = %.4f\n", resp_rxt);}
		    
	      strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
	      strftime (min, 16, "%M", loctime);
	      strftime (month, 16, "%m", loctime);
	      strftime (day, 16, "%d", loctime);
	      ++stimRses[stim_number].count; ++stimRtot[stim_number].count; ++classRses[stim_class].count; ++classRtot[stim_class].count;	   

	      /* Consequate responses */

	      if (DEBUG){
		printf("flag: stim_class = %d\n", stim_class);
		printf("flag: exit value center = %d\n",center);}



	      if(stim_class == 1){   /* If S+ stimulus */
		if (center==0){   /*no response*/
		  resp_sel = 0;
		  resp_acc = 0;
		  if (DEBUG){ printf("flag: no response to s+ stim\n");}
		  ++stimRses[stim_number].no; ++stimRtot[stim_number].no; ++classRses[stim_class].no; ++classRtot[stim_class].no; 
		  reinfor = 0;
		}
		else if (center != 0){  /*go response*/
		  resp_sel = 1;
		  resp_acc = 1; 
		  reinfor = feed(stim_reinf);
		  if (DEBUG){printf("flag: go response to s+ stim\n");}
		  ++stimRses[stim_number].go; ++stimRtot[stim_number].go; ++classRses[stim_class].go; ++classRtot[stim_class].go;
		  if (reinfor == 1) { ++fed;}
		}
	      }
	      else if(stim_class == 2){  /* If S- stimulus */
		if (center==0){ /*no response*/
		  resp_sel = 0;
		  resp_acc = 1;
		  if (DEBUG){printf("flag: no response to s- stim\n");}
		  ++stimRses[stim_number].no; ++stimRtot[stim_number].no; ++classRses[stim_class].no; ++classRtot[stim_class].no;
		  reinfor = 0;
		}
		else if (center!=0){ /*go response*/
		  resp_sel = 1;
		  resp_acc = 0;
		  if (DEBUG){printf("flag: go response to s- stim\n");}
		  ++stimRses[stim_number].go; ++stimRtot[stim_number].go; ++classRses[stim_class].go; ++classRtot[stim_class].go;	 
		  reinfor =  timeout(stim_reinf);
		}
	      }
	      else{	        /* probe trial */
		if (center==0){ /*no response*/
		  resp_sel = 0;
		  resp_acc = 3;
		  if (DEBUG){printf("flag: no response to probe stim\n");}
		  ++stimRses[stim_number].no; ++stimRtot[stim_number].no; ++classRses[stim_class].no; ++classRtot[stim_class].no;
		  reinfor=0;
		}
		else if (center!=0){ /*go response*/
		  resp_sel = 1;
		  resp_acc = 3;
		  if (DEBUG){printf("flag: go response to probe stim\n");}
		  ++stimRses[stim_number].go; ++stimRtot[stim_number].go; ++classRses[stim_class].go; ++classRtot[stim_class].go;
		  reinfor =  probeGO(stim_reinf, mirror);  
		  if (reinfor == 1) { ++fed;}
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
      
	      /*generate some output numbers*/
	      for (i = 0; i<nstims;++i){
		stimRses[i].ratio = (float)(stimRses[i].go) /(float)(stimRses[i].count);
		stimRtot[i].ratio = (float)(stimRtot[i].go) /(float)(stimRtot[i].count);	
	      }
	      for (i = 1; i<nclasses+1;++i){
		classRses[i].ratio = (float) (classRses[i].go)/ (float)(classRses[i].count);
		classRtot[i].ratio = (float) (classRtot[i].go)/ (float)(classRtot[i].count);
	      }


	      if (DEBUG){printf("flag: ouput numbers done\n");}
	      /* Update summary data */
	     
	      fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
	      fprintf (dsumfp, "\tRESPONSE RATIOS (by stimulus)\n");
	      fprintf (dsumfp, "\tStim     \t\tSession     \t\tTotals\n");
	      for (i = 0; i<nstims;++i){
		fprintf (dsumfp, "\t%s     \t\t%1.4f     \t\t%1.4f\n", stimulus[i].exemplar, stimRses[i].ratio, stimRtot[i].ratio);
	      }
	      fprintf (dsumfp, "\n\n RESPONSE DATA (by stim class)\n");
	      fprintf (dsumfp, "               Today's Session               Totals\n");
	      fprintf (dsumfp, "Class \t\t Count \tResponse Ratio \t\t Count \tResponse Ratio\n");
	      for (i = 1; i<nclasses+1;++i){
		fprintf (dsumfp, "%d \t\t %d \t%1.4f \t\t %d \t%1.4f\n", i, classRses[i].count, classRses[i].ratio, classRtot[i].count, classRtot[i].ratio);
	      }
	      
	      fprintf (dsumfp, "Last trial run @: %s\n", asctime(loctime) );
	      fprintf (dsumfp, "Feeder ops today: %d\n", fed );
	      fprintf (dsumfp, "Rf'd responses: %d\n\n", reinfor_sum); 
	      
	      fflush (dsumfp);
	      rewind (dsumfp);
	      
	      if (DEBUG){printf("flag: summaries updated\n");}


	      /* End of trial chores */
	      
	      sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                   /* unblock termination signals */ 
	      if (resp_acc == 0){correction = 0;}else{correction = 1;}        /* set correction trial var */
	      correction = 1;
	      
	    }while ( (correction == 0) && (atoi(hour) >= startH) && (atoi(hour) < stopH) ); /* correction trial loop */
		
	    stim_number = -1;                                          /* reset the stim number for correct trial*/
	  }                                                        /* main trial loop */
	    
	  curr_tt = time (NULL);
	  	    
	    
	  /* Loop while lights out */

	  while ( (atoi(hour) < startH) || (atoi(hour) >= stopH) ){  
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
	  curr_tt = time(NULL);
	  ++session_num;                                                                     /* increase sesion number */ 
	
	  for (i = 0; i<nstims;++i){
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
	  fed = reinfor_sum = 0;

	}while (1);
	
	curr_tt = time(NULL);
	
	
	/*  Cleanup */
	
	close_soundserver(dsp_fd);
	
	fclose(datafp);
	fclose(dsumfp);
	return 0;
}                         

int feed(int rval)
{
  int feed_me=0;
  
  feed_me = 1+(int) (100.0*rand()/(RAND_MAX+1.0) ); 
  if(DEBUG){fprintf(stderr,"feed_me = %d\t rval = %d\n", feed_me, rval);}
  
  if (feed_me <= rval){
    operant_write(box_id, LFTFEED, 1);
    usleep(feed_duration);
    operant_write(box_id, LFTFEED, 0);
    if(DEBUG){fprintf(stderr,"feed left\n");}
    return(1);
  }
  else{return (0);}
}


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


int probeGO( int rval, int mirr)
{
  int outcome=0;

  if (mirr==0){
    outcome = 1+(int) (100.0*rand()/(RAND_MAX+1.0)); 
    if (outcome <= rval){
      operant_write(box_id, LFTFEED, 1);
      usleep(feed_duration);
      operant_write(box_id, LFTFEED, 0);
      if(DEBUG){printf("mirror OFF, reinforced GO to probe stim\n");}
      return(1);
    }
    else{ 
      if(DEBUG){printf("mirror OFF, unreinforced GO to probe stim\n");}
      return (0);} 
  }
  else{  /*mirror=1, so p(feed)=p(timeout)=rval*/
    outcome = 1+ (int) (100.0*rand()/(RAND_MAX+1.0) );
    if (outcome <= rval){
      operant_write(box_id, LFTFEED, 1);
      usleep(feed_duration);
      operant_write(box_id, LFTFEED, 0);
      if(DEBUG){printf("mirror ON, reinforced GO to probe stim\n");}
      return(1);
    }
    else if(outcome <= (2*rval) ){
      operant_write(box_id, HOUSELT, 0);
      usleep(timeout_duration);
      operant_write(box_id, HOUSELT, 1);
      if(DEBUG){printf("mirror ON, punished GO to probe stim\n");}
      return (1);
    }
    else{
      if(DEBUG){printf("mirror ON, unreinforced GO to probe stim\n");}
      return (0);
    }
  }
}
