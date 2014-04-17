/*****************************************************************************
** 2acNoise.c - code for running 2ac operant training procedure
******************************************************************************
**
** 9-19-01  TQG: Adapted from most current 2choice.c
** 6-22-05  TQG: Now runs using alsa sound driver
** 7-12-05  EDF: Adding support for beam break hopper verification
** 4-13-06  TQG: added sunrise/sunset timing and cleaned up functions
** 9-25-07  DPK: created new branch called gngp.c to record all peck times during a trial
** 11-20-07 DPK: created gngpnoise which will probe with songs that have had snippets of white noise interspersed
** 11-27-07 DPK: created 2acnoise which will probe with songs that have had snippets of white noise interspersed
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>

#include "/usr/local/src/operantio/operantio.c"
#include "/usr/local/src/audioio/dev/audout_pecknoise.c"

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include <sunrise.h>
#include "/usr/local/src/audioio/audoutCH.h"

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

long timeout_duration = TIMEOUT_DURATION;
long feed_duration    = FEED_DURATION;
int  trial_max        = MAX_NO_OF_TRIALS;
int  starthour = EXP_START_TIME;
int  stophour = EXP_END_TIME;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval   = SLEEP_TIME;
const char exp_name[] = "GNGPECK";
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
	fprintf(stderr, "gngpNoise usage:\n");
	fprintf(stderr, "    gngpNoise [-help] [-B int] [-mirrorP] [-t float][-on int:int] [-off int:int] [-x] [-S <subject number>] <filename>\n\n");
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
	fprintf(stderr, "        -x             = use correction trials for incorrect responses\n");
	fprintf(stderr, "        -d float       = desired decibel level for noise, default is 60.0 (must be between 0 and 80)\n");
	fprintf(stderr, "        -w 'x'         = set the response window duration to 'x' secs (use an integer)\n");
	fprintf(stderr, "        -S int         = specify the subject ID number (required)\n");
	fprintf(stderr, "        filename       = specify the name of the text file containing the stimuli (required)\n");
	fprintf(stderr, "                       where each line is: 'Class' 'Wavfile' 'Present_freq' 'Rnf_rate 'Pun_rate''\n");
	fprintf(stderr, "                       'Class'= 1 for LEFT-, 2 for RIGHT-key assignment \n");
	fprintf(stderr, "                       'Wavfile' is the name of the stimulus soundfile (must be 44100 Hz sample rate\n");
	fprintf(stderr, "                       'Presfreq' is the overall rate (compared to the other stimuli) at which the stimulus is presented. \n");
	fprintf(stderr, "                       The actual integer rate for each stimulus is that value divided by the sum for all stimuli.\n");
	fprintf(stderr, "                       Use '1' for equal probablility \n");
	fprintf(stderr, "                       'Rnf_rate' percentage of time that food is available following correct responses to this stimulus.\n");
	fprintf(stderr, "                       'Pun_rate' percentage of time that a timeout follows incorrect responses to this stimulus.\n");
	fprintf(stderr, "                       for 2acWN, make a new line for each stim you'd like to probe w/white noise and assign \n");
	fprintf(stderr, "                       it to class 3 for WN added to class 1 and to class 4 for WN added to class 2 stimuli\n");

	
	exit(-1);
}

/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *resp_wind, float *dbdesired, float *timeout_val, char **stimfname)
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
			else if (strncmp(argv[i], "-d", 2) == 0)
				sscanf(argv[++i], "%f", dbdesired);
			else if (strncmp(argv[i], "-help", 5) == 0)
				do_usage();
			else{
				fprintf(stderr, "Unknown option: %s\t", argv[i]);
				fprintf(stderr, "Try 'gngp -help' for help\n");
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
	char *stimfname = NULL;
	char *stimfroot, *pcm_name;
	char fullsfname[256];
	const char delimiters[] = " .,;:!-";
	char datafname[128], hour [16], min[16], month[16], day[16], year[16],
	dsumfname[128], stimftemp[128],buf[128], stimexm[128],fstim[256],
	timebuff[64], tod[256], date_out[256],temphour[16],tempmin[16], buffer[30];
	int dinfd=0, doutfd=0, nclasses, nstims, stim_class, stim_number, stim_reinf, stim_punish,
	                    subjectid, loop, period, resp_wind,correction,
	                    trial_num, session_num, playval, i,j,k, *playlist=NULL, totnstims=0,
	                                    dosunrise=0,dosunset=0,starttime,stoptime,currtime;
	int numpecks=0;
	float resp_rxt=0.0, timeout_val=0.0;
	float dbdesired=60.0; /*default db for white noise snippets*/
	float latitude = 32.82, longitude = 117.14;
	time_t curr_tt, rise_tt, set_tt;
	unsigned long int stimulus_duration, offset, wn_offset, wn_dur;
	struct timeval stimoff, resp_window, resp_lag, resp_rt;
	struct tm *loctime;
	struct PECK trial, *trl;
	trl = &trial;
	trl->left=trl->center=trl->right=0;
	if(DEBUG){printf("PREtrial trl left:%d center:%d right:%d",trl->left,trl->center,trl->right);}
	int center = 0, left = 0, right = 0, fed = 0;
	Failures f = {0,0,0,0};
	int reinfor_sum = 0, reinfor = 0;
	int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
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
	}Rstim[MAXSTIM], Tstim[MAXSTIM], Rclass[MAXCLASS], Tclass[MAXCLASS];

	struct PECKTIME pecktimes, *ptm; /*need to get right input to playandpeck */
	ptm = &pecktimes;
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
	command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &resp_wind, &dbdesired, &timeout_val, &stimfname);
	if(DEBUG){
		fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, xresp=%d, resp_wind=%d, dbdesired=%f, timeout_val=%f flash=%d stimfile: %s\n",
		        box_id, subjectid, starthour, stophour, startmin, stopmin, xresp, resp_wind, dbdesired, timeout_val, flash, stimfname);
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
		fprintf(stderr, "\tERROR: try 'gngp -help' for available options\n");
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
	if(dbdesired<0.0 || dbdesired>80.0){
		dbdesired = 60.0;
		fprintf(stderr, "db desired for noise out of range (0-80) setting to %4.2f db\n", dbdesired);
	}
	fprintf(stderr, "db desired for noise set to %4.2f db\n", dbdesired);
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

	if(flash){fprintf(stderr, "!!WARNING: Flashing keylights during response window!!\n");}
	if(xresp){fprintf(stderr, "!!WARNING: Enabling correction trials for incorrect responses !!\n");}


	/* Read in the list of exmplars from stimulus file */
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
				printf("ERROR: insufficnet data or bad format in '.stim' file. Try '2ac -help'\n");
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

			sprintf(fullsfname,"%s%s", STIMPATH, stimulus[i].exemplar);
			if(DEBUG){printf("\n\ntrying to verify %s\n",fullsfname);}

			if((stimulus[i].dur = verify_soundfile(fullsfname))<1){
				fprintf(stderr, "Unable to verify %s!\n",fullsfname );
				snd_pcm_close(handle);
				exit(0);
			}
		}
	}
	else
	{
		fprintf(stderr,"Error opening stimulus input file! Try '2acWN -help' for proper file formatting.\n");
		snd_pcm_close(handle);
		exit(0);
	}
	fclose(stimfp);
	if(DEBUG){printf("Done reading in stims; %d stims in %d classes found\n", nstims, nclasses);}

	/* Don't allow correction trials when probe stimuli are presented */
	if(xresp==1 && nclasses>2){
		fprintf(stderr, "ERROR!: You cannot use correction trials and probe stimuli in the same session.\n  Exiting now\n");
		snd_pcm_close(handle);
		exit(-1);
	}

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
	sprintf (stimftemp, "%s", stimfname);
	stimfroot = strtok (stimftemp, delimiters);
	sprintf(datafname, "%i_%s.2acWN_rDAT", subjectid, stimfroot);
	sprintf(dsumfname, "%i.summaryDAT", subjectid);
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
	fprintf (datafp, "Stimulus source: %s\tDesired Noise dB: %4.2f\n", stimfname,dbdesired);
	fprintf (datafp, "reinforcement is set in the .stim\t  mirror:%d \n", mirror );
	fprintf (datafp, "Sess#\tTrl#\tTrlTyp\tStimulus\tClass\tNoiseOffset\tNoiseDur\tRspSL\tRspAC\tRspRT\tReinf\tTOD\tDate\n");


	/********************************************
	 +++++++++++ Trial sequence ++++++++++++++
	********************************************/
	session_num = 1;
	trial_num = 0;
	correction = 1;

	for(i = 0; i<nstims;++i){
		Rstim[i].C = Rstim[i].X = Rstim[i].N = Rstim[i].count =0;
		Tstim[i].C = Tstim[i].X = Tstim[i].N = Tstim[i].count = 0;
	}
	for(i=1;i<=nclasses;i++){
		Rclass[i].C = Rclass[i].X = Rclass[i].N = Rclass[i].count = 0;
		Tclass[i].C = Tclass[i].X = Tclass[i].N = Tclass[i].count = 0;
	}

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
			stimulus_duration = stimulus[stim_number].dur;
			stim_class = stimulus[stim_number].class;                               /* set stimtype variable */
			strcpy (stimexm, stimulus[stim_number].exemplar);                       /* get exemplar filename */
			stim_reinf = stimulus[stim_number].reinf;
			stim_punish = stimulus[stim_number].punish;

			sprintf(fstim,"%s%s", STIMPATH, stimexm);                                /* add full path to file name */

			/*here, select random offset and duration for white noise (in msec) set some lower dur size (50 msec) if it's a probe trial*/
			if(stim_class>2){
				wn_offset = (long unsigned int) ((stimulus_duration+0.0)*rand()/(RAND_MAX+0.0));
				wn_dur = (long unsigned int) (((stimulus_duration-50.0)*rand()/(RAND_MAX+0.0))+50.0); /*50 msec is shortest dur to mask*/
			}
			else{wn_offset = -1; wn_dur = -1;}


			if(DEBUG){
				printf("stim_num: %d\t", stim_number);
				printf("stim_dur: %lu\t",stimulus_duration);
				printf("class: %d\t", stim_class);
				printf("wn_offset: %lu\t", wn_offset);
				printf("wn_dur: %lu\n", wn_dur);
				printf("reinf: %d\t", stim_reinf);
				printf("name: %s\n", stimexm);
				printf("full stim path: %s\n", fstim);
				printf("exemplar chosen: %s\tnumber: %d\n", stimulus[stim_number].exemplar, stim_number );
			}

			do{                                             /* start correction trial loop */
				resp_sel = resp_acc = resp_rxt = 0;        /* zero trial variables        */
				trl->left=trl->center=trl->right=0;
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

				/* Play stimulus file */
			if (DEBUG){printf("STARTING PLAYBACK '%s'\n", stimexm);}

				offset = 0;

				if (play_and_peck_wn2ac(fstim, period,stimulus_duration, offset, wn_dur, wn_offset, dbdesired, box_id, stim_class, trl, ptm, &numpecks)!=1){
					fprintf(stderr, "play_and_count error on pcm:%s stimfile:%s. Program aborted %s\n",
					        pcm_name, stimexm, asctime(localtime (&curr_tt)) );
					fprintf(datafp, "play_and_count error on pcm:%s stimfile:%s. Program aborted %s\n",
					        pcm_name, stimexm, asctime(localtime (&curr_tt)) );
					fclose(datafp);
					fclose(dsumfp);
					snd_pcm_close(handle);
					exit(-1);
				}
				if (DEBUG){printf("STOP  '%s'\n", stimexm);
					printf("trial peck counter left:%d center:%d right:%d\n", trl->left, trl->center, trl->right);
					for(i=1;i<numpecks;i++){
						printf("peck number %d: key %c time after stim onset: %0.5f \n",i,pecktimes.peckkey[i],pecktimes.pecktime[i]);
					}
				}
				gettimeofday(&stimoff, NULL);
				if (DEBUG){
					stimoff_sec = stimoff.tv_sec;
					stimoff_usec = stimoff.tv_usec;
					printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);}

				/* Wait for response */
				if (DEBUG){printf("flag: waiting for response\n");}
				timeradd (&stimoff, &respoff, &resp_window);
				if (DEBUG){
					respwin_sec = resp_window.tv_sec;
					respwin_usec = resp_window.tv_usec;
					printf("resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);}

				loop = 0; center = 0, right = 0, left = 0;
				do{
					nanosleep(&rsi, NULL);
					center = operant_read(box_id, CENTERPECK);
					right = operant_read(box_id, RIGHTPECK);
					left = operant_read(box_id, LEFTPECK);
					gettimeofday(&resp_lag, NULL);
					if((right==0) && (left==0) && flash){
						++loop;
						if(loop%80==0){
							if(loop%160==0){
								operant_write(box_id, CTRKEYLT, 1);}
							else{
								operant_write(box_id, CTRKEYLT, 0);}
						}
					}
				}while ( (right==0) && (left==0) && (timercmp(&resp_lag, &resp_window, <)) );

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

				/* Consequate responses */
				if (DEBUG){printf("flag: stim_class = %d\n", stim_class);}
				if (DEBUG){printf("flag: exit value left = %d, right = %d\n", left, right);}
				if (stim_class == 1){                                 /* GO LEFT */
					if ( (left==0 ) && (right==0) ){
						resp_sel = 0;
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
					else
						fprintf(datafp, "DEFAULT SWITCH for bit value:ERROR CODE REMOVED");
				}
				else if (stim_class == 2){                           /* GO RIGHT */
					if ( (left==0) && (right==0) ){
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
					else
						fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
				}
				else if (stim_class == 3){                           /* PROBE go left STIMULUS */
					if ( (left==0) && (right==0) ){ /*no response to probe */
						resp_sel = 0;
						resp_acc = 2;
						++Rstim[stim_number].N;++Tstim[stim_number].N;
						++Rclass[stim_class].N;++Tclass[stim_class].N;
						reinfor = 0;
						if (DEBUG){printf("flag: no response to probe go left stimulus\n");}
					}
					else if (left!=0){
						resp_sel = 1;
						if(DEBUG){printf("flag: LEFT response to PROBE go left\n");}
						resp_acc = 3;
						++Rstim[stim_number].X;++Tstim[stim_number].X;
						++Rstim[stim_class].X; ++Tstim[stim_class].X;
						reinfor =  feed(stim_reinf,&f);
						if (reinfor){++fed;}
					}
					else if (right!=0){
						resp_sel = 2;
						if(DEBUG){printf("flag: RIGHT response to PROBE go left\n");}
						resp_acc = 4;
						++Rstim[stim_number].X;++Tstim[stim_number].X;
						++Rstim[stim_class].X; ++Tstim[stim_class].X;
						reinfor =  timeout(stim_punish);
					}
					else
						fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
				}
				else if (stim_class == 4){                           /* PROBE go right STIMULUS */
					if ( (left==0) && (right==0) ){ /*no response to probe */
						resp_sel = 0;
						resp_acc = 2;
						++Rstim[stim_number].N;++Tstim[stim_number].N;
						++Rclass[stim_class].N;++Tclass[stim_class].N;
						reinfor = 0;
						if (DEBUG){printf("flag: no response to probe go left stimulus\n");}
					}
					else if (left!=0){
						resp_sel = 1;
						if(DEBUG){printf("flag: LEFT response to PROBE go right\n");}
						resp_acc = 4;
						++Rstim[stim_number].X;++Tstim[stim_number].X;
						++Rstim[stim_class].X; ++Tstim[stim_class].X;
						reinfor =  timeout(stim_punish);
					}
					else if (right!=0){
						resp_sel = 2;
						if(DEBUG){printf("flag: RIGHT response to PROBE go right\n");}
						resp_acc = 3;
						++Rstim[stim_number].X;++Tstim[stim_number].X;
						++Rstim[stim_class].X; ++Tstim[stim_class].X;
						reinfor =  feed(stim_reinf,&f);
						if (reinfor){++fed;}
					}
					else
						fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
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
				nanosleep(&iti, NULL);                     /* wait intertrial interval */
				if (DEBUG){printf("flag: ITI passed\n");}

				/* Write trial data to output file */
				strftime (tod, 256, "%H%M", loctime);
				strftime (date_out, 256, "%m%d", loctime);
				if(stim_class<3){wn_offset=0;wn_dur=0;}
				fprintf(datafp, "%d\t%d\t%d\t%s\t\t%d\t\t%lu\t\t%lu\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num,
				        correction, stimexm, stim_class, wn_offset, wn_dur, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
				if(numpecks>1){
					fprintf(datafp,"\t\t\tnum\tkey\tstimlag\n");
					for(i=1;i<numpecks;i++){
						fprintf(datafp, "\t\tpeck\t%d\t%c\t%0.3f\n",i,pecktimes.peckkey[i],pecktimes.pecktime[i]);
					}
				}
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

				if (DEBUG){printf("flag: ouput numbers done\n");}
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
				sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                   /* unblock termination signals */

				if(xresp == 1 &&resp_acc != 1){correction = 0;}
				else{correction=1;}

				curr_tt = time(NULL);
				loctime = localtime (&curr_tt);
				strftime (hour, 16, "%H", loctime);
				strftime(min, 16, "%M", loctime);
				currtime=(atoi(hour)*60)+atoi(min);
				if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}

			}while ((correction==0)&&(trial_num<=trial_max)&&(currtime>=starttime)&&(currtime<stoptime)); /* correction trial loop */

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

		/*reset some vars for the new day */
		++session_num;
		trial_num = 0;
		for(i = 0; i<nstims;++i){
			Rstim[i].C = Rstim[i].X = Rstim[i].N = Rstim[i].count =0;
		}
		for(i=1;i<=nclasses;i++){
			Rclass[i].C = Rclass[i].X = Rclass[i].N = Rclass[i].count = 0;
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

