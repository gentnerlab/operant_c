/*****************************************************************************
** gngdiscrim.c - code for running go/nogo motif discrimination
******************************************************************************
**
** 7-08-09 MB: starting with gng code
** 7-14-09 MB: adding flags to stop after 100 trials with same stims and stop at night.
** changing criterion to reflect 7 out of 10 correct
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>
#include <string.h>

#include "/usr/local/src/operantio/operantio.c"
#include "/usr/local/src/audioio/audout.c"
#include <alsa/asoundlib.h>
#include <sunrise.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
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
#define MAXSTIM                  1024            /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 256           /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define TIMEOUT_DURATION         10000000       /* duration of timeout in microseconds */
#define FEED_DURATION            4000000       /* duration of feeder access in microseconds */
#define DACSAMPLERATE            20000         /* stimulus sampling rate */  
#define DACBITDEPTH              16            /* stimulus bit depth */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define HOPPER_DROP_MS           300           /*time to give hopper to fall before checking if it actually fell after command to do so*/

/* Variables for criterion checking */
#define CRITERION_CORRECT	7	/* Number correct necessary to move to next stimuli */
#define MAX_TRIALS		150	/* Max trials before moving on if criterion not reached */

long timeout_duration = TIMEOUT_DURATION;
long feed_duration    = FEED_DURATION;
int  trial_max        = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME;
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval   = SLEEP_TIME;
const char exp_name[] = "GNG";
int box_id = -1;
int flash = 0;
int xresp = 0;
int mirror = 0;
int resp_sel, resp_acc;

typedef struct {
  int hopper_failures;
  int hopper_wont_go_down_failures;
  int hopper_already_up_failures;
  int response_failures;
} Failures;

int feed(int rval, Failures *f);
int timeout(int rval);
int probeGO(int rval, int mirr, Failures *f);

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};

/* -------- Signal handling --------- */
int client_fd = -1;
static void sig_pipe(int signum){ 
  fprintf(stdout,"SIGPIPE caught\n"); client_fd = -1;
}
static void termination_handler (int signum){
  snd_pcm_close(handle);
  fprintf(stdout,"closed soundserver: term signal caught: exiting\n");
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
    if(DEBUG){fprintf(stderr,"feed left\n");}
    return(1);
  }
  else{return (0);}
}

int probeGO( int rval, int mirr, Failures *f)
{
  int outcome=0;

  if (mirr==0){
    outcome = 1+(int) (100.0*rand()/(RAND_MAX+1.0)); 
    if (outcome <= rval){
      doCheckedFeed(f);
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
      doCheckedFeed(f);
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

/*******************************************************
 ******************************************************/
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

/*******************************************************
 ******************************************************/
void do_usage()
{
  fprintf(stderr, "gng usage:\n");
  fprintf(stderr, "    gng [-help] [-B int] [-mirrorP] [-t float][-on int:int] [-off int:int] [-x] [-S <subject number>] <filename>\n\n");
  fprintf(stderr, "        -help          = show this help message\n");
  fprintf(stderr, "        -B int         = use '-B 1' '-B 2' ... '-B 12' \n");
  fprintf(stderr, "        -mirrorP       = set the timeout rate equal to the 'GO_reinforcement rate' in the .stim file\n");
  fprintf(stderr, "                         ***** MirrorP applies only to probe trials ****\n");		  
  fprintf(stderr, "                         NOTE:If you use 'mirrorP' the base rate in the .stim file must be <= to 50%%\n"); 
  fprintf(stderr, "        -t float       = set the timeout duration to float secs (use a real number, e.g 2.5 )\n");
  fprintf(stderr, "        -f             = flash the center key light during the response window \n");
  fprintf(stderr, "        -on int:int    = set hour:min for exp to start eg: '-on 7:30' (default is 7:00 AM)\n");
  fprintf(stderr, "        -off int:int   = set hour for exp to stop eg: '-off 19:45' (default is 7:00 PM)\n");
  fprintf(stderr, "                         To use daily sunset or sunrise times set 'on' or 'off' to '99'\n");
  fprintf(stderr, "        -x             = use correction trials for incorrect GO responses\n");
  fprintf(stderr, "        -w 'x'         = set the response window duration to 'x' secs (use an integer)\n");
  fprintf(stderr, "        -S int         = specify the subject ID number (required)\n");
  fprintf(stderr, "        filename       = specify the name of the text file containing the stimuli (required)\n");
  fprintf(stderr, "                         where each line is: 'Exemplar1' 'Exemplar2' 'Freq' 'Go_Rf'\n");
  fprintf(stderr, "                         Exemplar1 will be reinforced, while Exemplar2 will be punished for go responses. \n");
  fprintf(stderr, "                         'Sndfile' is the name of the stimulus soundfile (use WAV format 44.1kHz only)\n");
  fprintf(stderr, "                         'Freq' is the overall stimulus presetation rate (relative to the other stimuli). \n"); 
  fprintf(stderr, "                           The actual rate for each stimulus (expressed as an integer) is that value divded by the\n");
  fprintf(stderr, "                           sum for all stimuli. Set all prefreq values to 1 for equal probablility \n"); 
  fprintf(stderr, "                         'Go_Rf' is the rate at which GO responses to the Sndfile are reinforced.\n");
  fprintf(stderr, "                           For class 1 Go_Rf is the percentage of food reward following a correct response.\n");
  fprintf(stderr, "                           For class 2 Go_Rf is the percentage of timeouts following an incorrect response.\n"); 
  fprintf(stderr, "                           For probe classes (class > 2) Go_Rf is the percentage of nondifferential reinforcement.\n");
  exit(-1);
}


/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *resp_wind, float *timeout_val, char **stimfname)
{
int i=0;
 for (i = 1; i < argc; i++){
   if (*argv[i] == '-'){
     if (strncmp(argv[i], "-B", 2) == 0)
       sscanf(argv[++i], "%i", box_id);
     else if (strncmp(argv[i], "-S", 2) == 0)
       sscanf(argv[++i], "%i", subjectid);
     else if (strncmp(argv[i], "-f", 2) == 0)
       flash=1;
     else if (strncmp(argv[i], "-w", 2) == 0)
       sscanf(argv[++i], "%d", resp_wind);
     else if (strncmp(argv[i], "-mirrorP", 8) == 0)
       mirror = 1;
     else if (strncmp(argv[i], "-t", 2) == 0)
       sscanf(argv[++i], "%f", timeout_val);
     else if (strncmp(argv[i], "-on", 3) == 0){
       sscanf(argv[++i], "%i:%i", starthour,startmin);}
     else if (strncmp(argv[i], "-off", 4) == 0){
       sscanf(argv[++i], "%i:%i", stophour, stopmin);}
     else if (strncmp(argv[i], "-x", 2) == 0){
       xresp = 1;}
     else if (strncmp(argv[i], "-help", 5) == 0)
       do_usage();
     else{
       fprintf(stderr, "Unknown option: %s\t", argv[i]);
       fprintf(stderr, "Try 'gng -help' for help\n");
     }
   }
   else
     *stimfname = argv[i];
 }
 return 1;
}

/********************************************
 ** MAIN
 ********************************************/
int main(int argc, char *argv[])
{
  FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
  char *stimfname=NULL, *stimfroot=NULL, *pcm_name=NULL;
  char *datafname=NULL, *dsumfname=NULL, *stimftemp=NULL;
  const char delimiters[] = " .,;:!-";
  char hour [16], min[16], month[16], day[16], year[16], 
    buf[128], stimexma[128],fstima[256], fstimb[256],stimexmb[128], 
    timebuff[64], tod[256], date_out[256],temphour[16],tempmin[16], buffer[30];
  int dinfd=0, doutfd=0, nclasses, nstims, stim_class, stim_number, stim_reinf, 
    subjectid, loop, period, played, resp_wind=2,correction, 
    trial_num, session_num, playval, i,j,k, *playlist=NULL, totnstims=0, 
    sessionTrials=0, dosunrise=0,dosunset=0,starttime,stoptime,currtime, criterion_reached, stimset_plays, last_10[10], eff_class, curr_cycle_position, choose_stim, sum_check;
  float resp_rxt=0.0, timeout_val=0.0;
  float latitude = 32.82, longitude = 117.14;
  time_t curr_tt, rise_tt, set_tt;
  struct timeval stimoff, resp_window, resp_lag, resp_rt;
  struct tm *loctime;
	int center = 0, fed = 0;
	Failures f = {0,0,0,0};
	int reinfor_sum = 0, reinfor = 0;
	int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
	struct stim {
	  char exemplara[128];
	  char exemplarb[128];
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
	srand(time(0));
	
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
 	
	
    	/* Parse the command line */
	command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &resp_wind, &timeout_val, &stimfname);
        if(DEBUG){
          fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, xresp=%d, resp_wind=%d timeout_val=%f flash=%d stimfile: %s\n",
                box_id, subjectid, starthour, stophour, startmin, stopmin, xresp, resp_wind, timeout_val, flash, stimfname);
        }

	//sprintf(pcm_name, "dac%i", box_id);
	asprintf(&pcm_name, "dac%i", box_id);
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
	  fprintf(stderr, "\tERROR: try 'gng -help' for available options\n");
	  snd_pcm_close(handle);
	  exit(-1);
	}
	printf("got to here\n");

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
          fprintf(stderr,"Initializing box %d ... ", box_id);
          fprintf(stderr, "trying to execute setup(%s)\n", pcm_name);
        }
        if((period=setup_pcmdev(pcm_name))<0){
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
	if (mirror==0){
	  fprintf(stderr, "GO responses to S- (class2) stimuli always yeild a timeout. \n");
	  fprintf(stderr, "GO responses to probe stimuli never yield a timeout.\n");
	}
	if(flash){fprintf(stderr, "!!WARNING: Flashing keylights during response window!!\n");}
        if(xresp){fprintf(stderr, "!!WARNING: Enabling correction trials for 'No' responses !!\n");}
	

	/* Read in the list of exmplars from stimulus file */
	nstims = nclasses = 0;
	if ((stimfp = fopen(stimfname, "r")) != NULL){
	  while (fgets(buf, sizeof(buf), stimfp))
	    nstims++;
	  fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
	  rewind(stimfp);

	  for (i = 0; i < nstims; i++){
	    fgets(buf, 128, stimfp);
	    sscanf(buf, "%s\%s\%d\%d", stimulus[i].exemplara, stimulus[i].exemplarb, &stimulus[i].freq, &stimulus[i].reinf);
	    if((stimulus[i].freq==0) || (stimulus[i].reinf==0)){
	      printf("ERROR: insufficnet data or bad format in '.stim' file. Try 'gng -help'\n");
	      snd_pcm_close(handle);
	      exit(-1);} 
	    totnstims += stimulus[i].freq;
	    if(DEBUG){printf("totnstims: %d\n", totnstims);}
	
	    /*check the reinforcement rates */
	    if (stimulus[i].class==1){
	      fprintf(stderr, "Reinforcement rate for %s is set to %d%% for correct GO responses\n", 
		      stimulus[i].exemplara, stimulus[i].reinf);
	    }
	    else if (stimulus[i].class==2){
	      fprintf(stderr, "Punishment rate for %s is set to %d%% for incorrect GO responses\n", 
		      stimulus[i].exemplarb, stimulus[i].reinf);
	    }
	    else{
	      if((mirror==1)){
		if (stimulus[i].reinf>50){
		  fprintf(stderr, "ERROR!: To mirror food and timeout reinforcement values you must use a base rate less than or equal to 50%%\n");
		  snd_pcm_close(handle);
		  exit(-1);
		}
		else{
		  fprintf(stderr, "p(food) reward and p(timeout) on probe trials with %s is set to %d%%\n", stimulus[i].exemplara, stimulus[i].reinf );
		}
	      }
	      else{fprintf(stderr, "Reinforcement rate on probe trials is set to %d%% pct for correct GO responses, 100%% for incorrect GO responses\n", 
			   stimulus[i].reinf);
	      }
	    }
	  }
	}
	else{ 
	  printf("Error opening stimulus input file! Try 'gng_probe -help' for proper file formatting.\n");  
	  snd_pcm_close(handle);
	  exit(-1);	  
	}
	fclose(stimfp);
	if(DEBUG){printf("Done reading in stims; %d stims \n", nstims);}


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
	strftime (timebuff, 64, "%d%b%y", loctime);
	printf("stimftemp is '%s', stimfname is '%s'\n", stimftemp, stimfname);
	asprintf (&stimftemp, "%s", stimfname);
	printf("stimftemp is '%s', stimfname is '%s'\n", stimftemp, stimfname);
	stimfroot = strtok (stimftemp, delimiters); 
	printf("stimfroot is '%s'\n", stimfroot);
	asprintf(&datafname, "%i_%s.gonogo_rDAT", subjectid, stimfroot);
	asprintf(&dsumfname, "%i.summaryDAT", subjectid);
	printf("datafname is '%s', dsumfname is '%s'\n", datafname, dsumfname);
	datafp = fopen(datafname, "a");
        dsumfp = fopen(dsumfname, "w");
	
	if ( (datafp==NULL) || (dsumfp==NULL) ){
          fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", datafname);
	  snd_pcm_close(handle);
	  close(dinfd);
	  close(doutfd);
	  fclose(datafp);
	  fclose(dsumfp);
	  exit(-1);
        }

	/* Write data file header info */
	fprintf (stderr, "Data output to '%s'\n", datafname);
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

	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
        if (DEBUG){printf("hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
        currtime=(atoi(hour)*60)+atoi(min);

	operant_write (box_id, HOUSELT, 1);        /* house light on */

	do{                                                                              /* start the block loop */
	  while ((currtime>=starttime) && (currtime<stoptime)){                          /* start main trial loop */
	    if (DEBUG){printf("minutes since midnight at loop start: %d\t starttime: %d\tstoptime: %d\n",
			      currtime,starttime,stoptime);}
	    playval = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));                     /* select stim exemplar at random */ 
	    if (DEBUG){printf("playval: %d\t", playval);}
	    stim_number = playlist[playval];
	    strcpy (stimexma, stimulus[stim_number].exemplara);                       /* get exemplar filename */
	    strcpy (stimexmb, stimulus[stim_number].exemplarb);                       /* get exemplar filename */
	    stim_reinf = stimulus[stim_number].reinf;
	    sprintf(fstima,"%s%s", STIMPATH, stimexma);                                /* add full path to file name */
            sprintf(fstimb,"%s%s", STIMPATH, stimexmb);                                /* add full path to file name */
	    if(DEBUG){
	      printf("stim_num: %d\t", stim_number);
	      printf("reinf: %d\t", stim_reinf);
	      printf("name: %s\n", stimexma);
	      printf("full stima path: %s\n", fstima);
   	      printf("full stimb path: %s\n", fstimb);
	    }
		
	    stimset_plays = 0;				/* reset variables for discrimination learning */
	    criterion_reached = 0;
	    curr_cycle_position = 0;
	    /* reset last_10 array */
	    for (j=0; j<10; j++){
		    last_10[j] = 0;
	    }

	    do{						/* start loop for one motif pair */
	    do{                                             /* start correction trial loop */
	      resp_sel = resp_acc = resp_rxt = 0;        /* zero trial variables        */
	      ++trial_num;
	      curr_tt = time(NULL);
	     	      
	      /* Wait for center key press */
	      if (DEBUG){printf("flag: waiting for center key press\n");}
	      operant_write (box_id, HOUSELT, 1);        /* house light on */
	      center = 0;
	      do{                                         
		nanosleep(&rsi, NULL);	               	       
		center = operant_read(box_id, CENTERPECK);   /*get value at center peck position*/		 	       
		
	      }while (center==0);  
	      
	      sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
	      /* now choose either stima or stimb to play and set an
	       * effective class variable that will be used for
	       * reinforcement */
	      if (rand()/(RAND_MAX+1.0) <0.5) {
			choose_stim = 1;
			eff_class = 1;
	      }
	      else{
		      choose_stim = 0;
		      eff_class = 2;
	      }
		
		if (DEBUG){printf("chosen stimulus = %d\n", choose_stim);}

	      /* Play stimulus file */
		if (DEBUG){printf("playback START\n");}

		if (choose_stim) {
	      if ((played = playwav(fstima, period))!=1){
		fprintf(stderr, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
			pcm_name, stimexma, asctime(localtime (&curr_tt)) );
		fprintf(datafp, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
			pcm_name, stimexma, asctime(localtime (&curr_tt)) );
		fclose(datafp);
		fclose(dsumfp);
		exit(-1);
	      }}
	      else {
		if ((played = playwav(fstimb, period))!=1){
		fprintf(stderr, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
			pcm_name, stimexmb, asctime(localtime (&curr_tt)) );
		fprintf(datafp, "playwav failed on pcm:%s stimfile:%s. Program aborted %s\n", 
			pcm_name, stimexmb, asctime(localtime (&curr_tt)) );
		fclose(datafp);
		fclose(dsumfp);
		exit(-1);
	      }}

		      
	      if (DEBUG){printf("STOP\n");}
	      gettimeofday(&stimoff, NULL);
	      if (DEBUG){
		stimoff_sec = stimoff.tv_sec;
		stimoff_usec = stimoff.tv_usec;
		printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);}
	     	      
	      /* Wait for center key press */
	      if (DEBUG){printf("flag: waiting for response\n");}
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
		 if((center==0) && flash){
		   ++loop;
		   if(loop%80==0){
		     if(loop%160==0){
		       operant_write(box_id, CTRKEYLT, 1);}
		     else{
		       operant_write(box_id, CTRKEYLT, 0);}
		   } 
		 }
	      }while ( (center==0) && (timercmp(&resp_lag, &resp_window, <)) );

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
		    
	      strftime (hour, 16, "%H", loctime);                    /* format wall clock times for trial end*/
	      strftime (min, 16, "%M", loctime);
	      strftime (month, 16, "%m", loctime);
	      strftime (day, 16, "%d", loctime);
	      ++stimRses[stim_number].count; ++stimRtot[stim_number].count; 

	      /* Consequate responses */
	      if (DEBUG){
		printf("flag: stim_class = %d\n", eff_class);
		printf("flag: exit value center = %d\n",center);}

	      if(eff_class == 1){   /* If S+ stimulus */
		if (center==0){   /*no response*/
		  resp_sel = 0;
		  resp_acc = 0;
		  if (DEBUG){ printf("flag: no response to s+ stim\n");}
		  ++stimRses[stim_number].no; ++stimRtot[stim_number].no; 
		  reinfor = 0;
		}
		else if (center != 0){  /*go response*/
		  resp_sel = 1;
		  resp_acc = 1; 
		  reinfor = feed(stim_reinf, &f);
		  if (DEBUG){printf("flag: go response to s+ stim\n");}
		  ++stimRses[stim_number].go; ++stimRtot[stim_number].go; 
		  if (reinfor == 1) { ++fed;}
		}
	      }
	      else if(eff_class == 2){  /* If S- stimulus */
		if (center==0){ /*no response*/
		  resp_sel = 0;
		  resp_acc = 1;
		  if (DEBUG){printf("flag: no response to s- stim\n");}
		  ++stimRses[stim_number].no; ++stimRtot[stim_number].no; 
		  reinfor = 0;
		}
		else if (center!=0){ /*go response*/
		  resp_sel = 1;
		  resp_acc = 0;
		  if (DEBUG){printf("flag: go response to s- stim\n");}
		  ++stimRses[stim_number].go; ++stimRtot[stim_number].go; 
		  reinfor =  timeout(stim_reinf);
		}
	      }
	
		last_10[curr_cycle_position] = resp_acc; /* keep track for history */

	      /* Pause for ITI */
	      reinfor_sum = reinfor + reinfor_sum;
	      operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
	      nanosleep(&iti, NULL);                     /* wait intertrial interval */
	      if (DEBUG){printf("flag: ITI passed\n");}
					
	      /* Write trial data to output file */
	      strftime (tod, 256, "%H%M", loctime);
	      strftime (date_out, 256, "%m%d", loctime);
	      fprintf(datafp, "%d\t%d\t%d\t%s\t%s\t\t%d\t\%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num, 
		      correction, stimexma, stimexmb, stimset_plays,eff_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
	      fflush (datafp);
	      if (DEBUG){printf("flag: trail data written\n");}
      
	      /*generate some output numbers*/
	      for (i = 0; i<nstims;++i){
		stimRses[i].ratio = (float)(stimRses[i].go) /(float)(stimRses[i].count);
		stimRtot[i].ratio = (float)(stimRtot[i].go) /(float)(stimRtot[i].count);	
	      }

	      if (DEBUG){printf("flag: ouput numbers done\n");}
	      /* Update summary data */
	     
	      if(freopen(dsumfname,"w",dsumfp)!= NULL){
		fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
		fprintf (dsumfp, "\tRESPONSE RATIOS (by stimulus)\n");
		fprintf (dsumfp, "\tStim     \t\tSession     \t\tTotals\n");
		for (i = 0; i<nstims;++i){
		  fprintf (dsumfp, "\t%s     \t\t%1.4f     \t\t%1.4f\n", 
			   stimulus[i].exemplara, stimRses[i].ratio, stimRtot[i].ratio);
		}
		
		fprintf (dsumfp, "Last trial run @: %s\n", asctime(loctime) );
		fprintf (dsumfp, "Trials this session: %d\n",sessionTrials);
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
	      sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                   /* unblock termination signals */ 
	      if(xresp)
		if (eff_class==2 && resp_acc == 0){correction = 0;}else{correction = 1;}   /*run correction for GO resp to s-*/
	      else 
		correction = 1; /* make sure you don't invoke a correction trial by accident */
	      
	      curr_tt = time(NULL);
	      loctime = localtime (&curr_tt);
	      strftime (hour, 16, "%H", loctime);
	      strftime(min, 16, "%M", loctime);
	      currtime=(atoi(hour)*60)+atoi(min);
	      if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
	    
	    }while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); /* correction trial loop */
		
		
	    stimset_plays++; /* increment stimset plays counter */
	    curr_cycle_position++;



/* Check now for criterion reached */
        sum_check = 0;
for (j = 0; j<10; j++){
sum_check = sum_check + last_10[j];}
if(sum_check>=CRITERION_CORRECT){criterion_reached = 1;}

	if (curr_cycle_position == 9) {
		if(DEBUG){printf("resetting stimset_plays\n");}
		curr_cycle_position = 0;}
/* Increment trial number */
sessionTrials++;
	    }while((criterion_reached == 0) && (stimset_plays < MAX_TRIALS) && ((currtime>=starttime) && (currtime<stoptime)));

	    stim_number = -1;                                          /* reset the stim number for correct trial*/
	  }                                                        /* main trial loop*/
	    
	  /* Loop with lights out during the night */
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
	
	  /*reset some vars for a new day */
	  ++session_num;                                                                     /* increase sesion number */ 
      sessionTrials = 0;
	  for (i = 0; i<nstims;++i){
	    stimRses[i].go = 0;
	    stimRses[i].no = 0;
	    stimRses[i].ratio = 0.0;
	    stimRses[i].count = 0;
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

	}while (1); /* if (1) loop forever */
	
	/*  Cleanup */
	fclose(datafp);
	fclose(dsumfp);
	snd_pcm_close(handle);
	return 0;
}                         
