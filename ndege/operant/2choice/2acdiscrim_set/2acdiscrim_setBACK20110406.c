// added optional cue lights ec 11/8/07 //
// /* 8-11-09 MB: Reworking to 2ac version of motif discrimination task
// */
// /* 03-01-10 DK: Adding ability to track performance on motifs within stimfile
// */
// /* 04-02-10 DK: Adding ability to schedule trial availability
// */
#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <math.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

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
#define GREENLT   6
#define BLUELT    7
#define REDLT     8


/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             2             /* seconds from simulus end until NORESP is registered  (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  1024          /* maximum number of stimulus exemplars */ 
#define MAXCLASS                 256            /* maximum number of stimulus classes */   
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define SCHEDULING_SAMPLE_INTERVAL  50000000   /* scheduling trials off polling rate in nanoseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */
#define TIMEOUT_DURATION         10000000       /* default duration of timeout in microseconds */
#define CUE_DURATION             1000000       /* cue duration in microseconds */
#define SECONDARYRNUM            5           /* secondary reinforcer # flashes */
#define SECONDARYRUSON           50000            /* secondary reinforcer microseconds on */
#define SECONDARYRUSOFF          50000            /* secondary reinforcer microseconds off */
#define EXP_START_TIME           7             /* hour(0-24) session will start */
#define EXP_END_TIME             19            /* hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define STIMPATH       "/usr/local/stimuli/"
#define HOPPER_DROP_MS           300           /*time for hopper to fall before checking that it did */

/*Scheduling variables*/
#define TRIALSONMINUTESMIN	1400
#define TRIALSONMINUTESMAX	1400
#define TRIALSOFFMINUTESMIN	1
#define TRIALSOFFMINUTESMAX	1

/* Variables for criterion checking */
#define CRITERION_CORRECT_OT	15	/* Number correct (out of previous 20 non-correction trials) necessary to move to next set */
#define CRITERION_CORRECT_T	0	/* Number correct (out of previous 20 non-correction trials) necessary to move to next set */
#define CRITERION_CORRECT_UT	15	/* Number correct (out of previous 20 non-correction trials) necessary to move to next set */
#define MAX_TRIALS		10000	/* Max trials before moving on if criterion not reached */


long feed_duration = FEED_DURATION;
long timeout_duration = TIMEOUT_DURATION;
long cue_duration = CUE_DURATION;
int secondaryRnum = SECONDARYRNUM;
long secondaryRuson = SECONDARYRUSON;
long secondaryRusoff = SECONDARYRUSOFF;
int dosecondaryR = 0;
int docorrectioncue = 0;
int noreinforcecorrection = 0;
int  starthour = EXP_START_TIME;
int  stophour = EXP_END_TIME;
int criterion_correct_OT = CRITERION_CORRECT_OT;
int criterion_correct_T = CRITERION_CORRECT_T;
int criterion_correct_UT = CRITERION_CORRECT_UT;
int numconsecutivecorrect = 0;
int frcritical = 1;
int  stopmin = 0;
int  startmin = 0;
int  sleep_interval = SLEEP_TIME;
const char exp_name[] = "2ACdiscrim_set";
int box_id = -1;
int flash = 0;
int usepreviousset = 0;
int xresp = 0;
int cueflag = 0;
int correctiontrialsoff = 0;
char outsfname[256];
long unsigned int pause_secs = 2646; /*Time between exemplars in match to sample corresponds to 60ms at 44.1khz sample rate*/

struct timespec iti = { INTER_TRIAL_INTERVAL-(HOPPER_DROP_MS/1000), 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timespec ssi = { 0, SCHEDULING_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};

typedef struct {
	int hopper_failures;
	int hopper_wont_go_down_failures;
	int hopper_already_up_failures;
	int response_failures;
} Failures;


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
		if(DEBUG){fprintf(stderr,"feed left\n");}
		return(1);
	}
	else{return (0);}
}

int feedFR(int rval, Failures *f, int *numconsecutivecorrect, int *frcritical)
{
	*numconsecutivecorrect = *numconsecutivecorrect + 1;
	if(DEBUG){fprintf(stderr,"numconsecutivecorrect = %d\t frcritical = %d\n", *numconsecutivecorrect, *frcritical);}
	if (*numconsecutivecorrect >= *frcritical){

		*numconsecutivecorrect = 0;
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
	fprintf(stderr, "2ac usage:\n");
	fprintf(stderr, "     [-help] [-B int] [-R int] [-fx] [-t int] [-w int] [-on int] [-off int] [-S int] <filename>\n\n");
	fprintf(stderr, "        -help        = show this help message\n");
	fprintf(stderr, "        -B int       = use '-B 1' '-B 2' ... '-B 12' \n");
	fprintf(stderr, "        -f           = flash left & right pokelights during response window\n");
	fprintf(stderr, "        -t 'x'       = set the timeout duration to 'x' secs (use a real number, e.g 2.5 )\n");
	fprintf(stderr, "        -w 'x'       = set the response window duration to 'x' secs (use an integer)\n");
	fprintf(stderr, "        -x           = use this flag to enable correction trials for 'no-response' trials,\n");
	fprintf(stderr, "        -noC         = use this flag to disable all correction trials (supercedes '-x' flag)\n");
	fprintf(stderr, "        -q           = use this flag to enable cue lights. will only work in appropriately modified boxes. Class 1 = red, class 2 = green\n");
	fprintf(stderr, "        -J           = use this flag to enable the secondary reinforcer. will only work in appropriately modified boxes.\n");
	fprintf(stderr, "        -CC          = use this flag to enable 'correction cues' the keylight associated with the correct response will flash after an incorrect trial.\n");
	fprintf(stderr, "        -NRX         = use this flag to disable feeds for correction trials (secondary reinforcer will still occur if -J is set)\n");
	fprintf(stderr, "        -OTT int:int:int = use this flag to set the number of desired OverTrained:Trained:Untrained Pairs Per Set\n");
	fprintf(stderr, "                     	eg: '-OTT 1:0:2' would create sets with 2 overtrained stimuli 0 trained stimuli and 4 training stimuli\n");
	fprintf(stderr, "        -PS          = use this flag to use the previous set. For example, if you'd like to change one aspect of the program without starting the bird on a while new set.\n");
	fprintf(stderr, "        -FR int      = set the fixed ratio parameters. Bird must get int consecutive trials correct before it is fed (default is 1)\n");
	fprintf(stderr, "        -R int:int,int:int,int:int	= use this flag to set the reinforcement and punishment rate for overtrained, training, and untrained stimuli\n");
	fprintf(stderr, "                      	eg: '-R rnfOT:punOT;rnfT:punT;rnfUT:punUT' \n");
	fprintf(stderr, "                   	eg: '-R 60:100;80:100;100:100'\n");
	fprintf(stderr, "        -Fq int:int:int = use this flag to set the desired presentation frequency of OverTrained:Trained:Untrained in a set\n");
	fprintf(stderr, "        -onM int:int     = set on minutes (min:max) for trial scheduling (trials available for onM, then unavailable for offM then available for onM etcetc)\n");
	fprintf(stderr, "        -offM int:int    = set off minutes (min:max) for trial scheduling (trials available for onM, then unavailable for offM then available for onM etcetc)\n");
	fprintf(stderr, "        -on int:int  = set hour:min for exp to start eg: '-on 7:35' (default is 7AM, use 99 for sunrise)\n");
	fprintf(stderr, "        -off int:int = set hour:min for exp to stop eg: '-off 19:01' (default is 7PM, use 99 for sunset)\n");
	fprintf(stderr, "        -S int       = specify the subject ID number (required)\n");
	fprintf(stderr, "        filename     = specify the name of the text file containing the stimuli (required)\n");
	fprintf(stderr, "                       where each line is: 'Wavfile' 'Class' 'Presfreq' '#trials' '#correct' '#incorrect' '#noresp''\n");
	fprintf(stderr, "                       'Wavfile' is the name of the stimulus soundfile (must be 44100 Hz sample rate\n");
	fprintf(stderr, "                       'Class'= 1 for LEFT-, 2 for RIGHT-key assignment for Overtrained exemplars\n");
	fprintf(stderr, "                       'Class'= 3 for LEFT-, 4 for RIGHT-key assignment for training exemplars (-1 is default, 3,4 will be set by program)\n");
	fprintf(stderr, "                       'Presfreq' is the overall rate (compared to the other stimuli) at which the stimulus is presented. \n");
	fprintf(stderr, "                         The actual integer rate for each stimulus is that value divided by the sum for all stimuli.\n");
	fprintf(stderr, "                         Use '1' for equal probablility \n");
	fprintf(stderr, "                       '#trials' '#correct' '#incorrect' '#noresp' will be set by this program (set to 0 for default).\n");
	exit(-1);
}

/**********************************************************************
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *starthour, int *stophour, int *startmin, int *stopmin, int *trialsonminutesmin, int *trialsonminutesmax, int *trialsoffminutesmin, int *trialsoffminutesmax, int *resp_wind, float *timeout_val, int *numOTpairs, int *numTpairs, int *numUTpairs, int *freqOT, int *freqT, int *freqUT, int *reinf_OT, int *pun_OT, int *reinf_T, int *pun_T, int *reinf_UT, int *pun_UT, int *frcritical, char **stimfname)
{
	int i=0;

	for (i = 1; i < argc; i++){
		if (*argv[i] == '-'){
			if (strncmp(argv[i], "-B", 2) == 0)
				sscanf(argv[++i], "%i", box_id);
			else if (strncmp(argv[i], "-S", 2) == 0)
				sscanf(argv[++i], "%i", subjectid);
			else if (strncmp(argv[i], "-x", 2) == 0)
				xresp = 1;
			else if (strncmp(argv[i], "-PS", 3) == 0)
				usepreviousset = 1;
			else if (strncmp(argv[i], "-w", 2) == 0){
				sscanf(argv[++i], "%i", resp_wind);
			}
			else if (strncmp(argv[i], "-FR", 3) == 0){
				sscanf(argv[++i], "%i", frcritical);
			}
			else if (strncmp(argv[i], "-t", 2) == 0){
				sscanf(argv[++i], "%f", timeout_val);
			}
			else if (strncmp(argv[i], "-noC",4) == 0){
				correctiontrialsoff = 1;}
			else if (strncmp(argv[i], "-OTT", 4) == 0){
				sscanf(argv[++i], "%i:%i:%i", numOTpairs,numTpairs,numUTpairs);
			}
			else if (strncmp(argv[i], "-Fq", 3) == 0){
				sscanf(argv[++i], "%i:%i:%i", freqOT,freqT,freqUT);
			}
			else if (strncmp(argv[i], "-R", 2) == 0){
				sscanf(argv[++i], "%i:%i,%i:%i,%i:%i", reinf_OT,pun_OT,reinf_T,pun_T,reinf_UT,pun_UT);
			}
			else if (strncmp(argv[i], "-f", 2) == 0)
				flash = 1;
			else if (strncmp(argv[i], "-q", 2) == 0)
				cueflag = 1;
			else if (strncmp(argv[i], "-J", 2) == 0)
				dosecondaryR = 1;
			else if (strncmp(argv[i], "-CC", 3) == 0)
				docorrectioncue = 1;
			else if (strncmp(argv[i], "-NRX", 4) == 0)
				noreinforcecorrection = 1;
			else if (strncmp(argv[i], "-onM", 4) == 0)
				sscanf(argv[++i], "%i:%i", trialsonminutesmin, trialsonminutesmax);
			else if (strncmp(argv[i], "-offM", 5) == 0)
				sscanf(argv[++i], "%i:%i", trialsoffminutesmin, trialsoffminutesmax);
			else if (strncmp(argv[i], "-on", 3) == 0)
				sscanf(argv[++i], "%i:%i", starthour, startmin);
			else if (strncmp(argv[i], "-off", 4) == 0)
				sscanf(argv[++i], "%i:%i", stophour, stopmin);
			else if (strncmp(argv[i], "-help", 5) == 0){
				do_usage();
			}
			else{
				fprintf(stderr, "Unknown option: %s\t", argv[i]);
				fprintf(stderr, "Try '2ac -help'\n");
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
int main(int argc, char *argv[])
{
	FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
	char *stimfname = NULL;
	char *stimfroot;
	const char delimiters[] = " .,;:!-";
	char datafname[128], hour[16], min[16], month[16],day[16], year[16], dsumfname[128], stimftemp[128];
	char *pcm_name[128];
	char  buf[128], stimexm[128],fstim[256], timebuff[64], tod[256], date_out[256], buffer[30],temphour[16],tempmin[16];
	int nclasses, nstims, stim_class, stim_reinf, stim_punish, resp_sel, resp_acc, subjectid, period, tot_trial_num,
	played, resp_wind=0,trial_num=1, session_num=1, i,j,k, correction, loop, stim_number, cue[3], *playlist=NULL, *setplaylist=NULL, totnstims=0, dosunrise=0,dosunset=0,starttime,stoptime,currtime, stimset_plays, eff_class[MAXCLASS], sum_check;
	int last_20_OT[20],last_20_T[20],last_20_UT5[20],last_20_UT6[20];
	int curr_cycle_position_OT,curr_cycle_position_T,curr_cycle_position_UT5,curr_cycle_position_UT6,setupset=1;
	int criterion_reached, criterion_reached_OT, criterion_reached_T, criterion_reached_UT5,criterion_reached_UT6,someOT=0,someT=0,someUT=0;
	int numOTpairs, numTpairs, numUTpairs, freqOT=1, freqT=1, freqUT=1, reinf_OT=100, pun_OT=100, reinf_T=100, pun_T=100, reinf_UT=100, pun_UT=100, setrequirements[7],setcontents[7];
	int setplayval,currsetsize,setplistitems,setstimnum,havegoodset=0,setstims[MAXSTIM],goodrndstim=0,rndstim;
	int addedstim=0;
	int trialson = 1;
	int trialsonminutesmin = TRIALSONMINUTESMIN;
	int trialsonminutesmax = TRIALSONMINUTESMAX;
	int trialsoffminutesmin = TRIALSOFFMINUTESMIN;
	int trialsoffminutesmax = TRIALSOFFMINUTESMAX;
	int currtrialsonminutes = TRIALSONMINUTESMIN;
	int currtrialsoffminutes = TRIALSOFFMINUTESMIN;
	int nexttrialsontime = -1;
	int nexttrialsofftime = -1;
	float timeout_val=0.0, resp_rxt=0.0;
	int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */
	float latitude = 32.82, longitude = 117.14;
	time_t curr_tt, rise_tt, set_tt;
	struct timeval stimoff, resp_window, resp_lag, resp_rt;
	struct tm *loctime;
	Failures f = {0,0,0,0};
	int left = 0, right= 0, center = 0, fed = 0;
	int reinfor_sum = 0, reinfor = 0;
	struct stim {
		char exemplar[128];
		int class;
		int freq;
		int ntrials;
		int corr;
		int incorr;
		int noresp;
		int currset;
		int reinf;
		int punish;
		unsigned long int dur;
		int num;
	}stimulus[MAXSTIM],currset[MAXSTIM];
	struct resp{
		int C;
		int X;
		int N;
		int count;
	}Rstim[MAXSTIM], Tstim[MAXSTIM], Rclass[MAXCLASS];
	sigset_t trial_mask;
	cue[1]=REDLT; cue[2]=GREENLT; cue[0]=BLUELT;
	eff_class[1] = 1;
	eff_class[2] = 2;
	eff_class[3] = 1;
	eff_class[4] = 2;
	eff_class[5] = 1;
	eff_class[6] = 2;

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
	command_line_parse(argc, argv, &box_id, &subjectid, &starthour, &stophour, &startmin, &stopmin, &trialsonminutesmin, &trialsonminutesmax, &trialsoffminutesmin, &trialsoffminutesmax, &resp_wind, &timeout_val, &numOTpairs, &numTpairs, &numUTpairs, &freqOT, &freqT, &freqUT, &reinf_OT, &pun_OT, &reinf_T, &pun_T, &reinf_UT, &pun_UT, &frcritical, &stimfname);
	if(DEBUG){
		fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, startM=%d, stopM=%d, trialsonminutesmin=%d, trialsonminutesmax=%d, trialsoffminutesmin=%d, trialsoffminutesmax=%d, xresp=%d, cue=%d, dosecondaryR=%d, docorrectioncue=%d, noreinforcecorrection=%d, corr_flag=%d resp_wind=%d timeout_val=%3.3f numOTpairs=%d numTpairs=%d numUTpairs=%d freqOT=%d freqT=%d freqUT=%d reinf_OT=%d pun_OT=%d reinf_T=%d pun_T=%d reinf_UT=%d pun_UT=%d frcritical=%d usepreviousset=%d flash=%d stimfile: %s ",
		        box_id, subjectid, starthour, stophour, startmin, stopmin, trialsonminutesmin, trialsonminutesmax, trialsoffminutesmin, trialsoffminutesmax, xresp, cueflag, dosecondaryR, docorrectioncue, noreinforcecorrection, correctiontrialsoff, resp_wind, timeout_val, numOTpairs, numTpairs, numUTpairs, freqOT, freqT, freqUT, reinf_OT, pun_OT, reinf_T, pun_T, reinf_UT, pun_UT, frcritical, usepreviousset, flash, stimfname );
	}
	sprintf(&pcm_name, "dac%i", box_id);
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
		fprintf(stderr, "\tERROR: try '2ac -help' for available options\n");
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
		fprintf(stderr,"Initializing box %d ...\n", box_id);
		fprintf(stderr,"trying to execute setup(%s)\n", pcm_name);
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
	nstims = nclasses = 0;
	if ((stimfp = fopen(stimfname, "r")) != NULL){
		while (fgets(buf, sizeof(buf), stimfp))
			nstims++;
		fprintf(stderr, "Found %d stimulus exemplars in '%s'\n", nstims, stimfname);
		rewind(stimfp);

		for (i = 0; i < nstims; i++){
			fgets(buf, 128, stimfp);
			sscanf(buf, "%s\%d\%d\%d\%d\%d\%d\%d", stimulus[i].exemplar, &stimulus[i].class, &stimulus[i].freq, &stimulus[i].ntrials, &stimulus[i].corr, &stimulus[i].incorr, &stimulus[i].noresp, &stimulus[i].currset);
			if((stimulus[i].freq==0)){
				printf("ERROR: insufficnet data or bad format in '.stim' file. Try '2ac -help'\n");
				exit(0);}
			totnstims += stimulus[i].freq;
			if(DEBUG){printf("totnstims: %d\n", totnstims);}
		}
	}
	else
	{
		fprintf(stderr,"Error opening stimulus input file! Try 'gng_probe -help' for proper file formatting.\n");
		snd_pcm_close(handle);
		exit(0);
	}
	fclose(stimfp);
	if(DEBUG){printf("Done reading in stims; %d stims found\n", nstims);}


	/* build the stimulus playlists */
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
	fprintf (datafp, "#INFOstart#\n");
	fprintf (datafp, "File name: %s\n", datafname);
	fprintf (datafp, "Procedure source: %s\n", exp_name);
	fprintf (datafp, "Program Start time: %s", asctime(loctime));
	fprintf (datafp, "Start of Day: %d:%d\n", starthour,startmin);
	fprintf (datafp, "End of Day: %d:%d\n", stophour,stopmin);
	fprintf (datafp, "Subject ID: %d\n", subjectid);
	fprintf (datafp, "Stimulus source: %s\n", stimfname);
	fprintf (datafp, "trialsonminutes(min:max) %d:%d\ttrialsoffminutes(min:max) %d:%d\n", trialsonminutesmin, trialsonminutesmax, trialsoffminutesmin, trialsoffminutesmax);
	fprintf (datafp, "Flags set:\n");
	fprintf (datafp, "resp_wind: %d\ttimeout_val: %3.2f\txresp: %d\tcorrectiontrialsoff: %d\tflash: %d\tcueflag: %d\tdosecondaryR: %d\tdocorrectioncue: %d\tnoreinforcecorrection: %d\n", resp_wind,timeout_val,xresp,correctiontrialsoff,flash,cueflag,dosecondaryR,docorrectioncue,noreinforcecorrection);
	fprintf (datafp, "numOTpairs: %d\tnumTpairs: %d\tnumUTpairs: %d\t\n",numOTpairs,numTpairs,numUTpairs);
	fprintf (datafp, "freqOT: %d\tfreqT: %d\tfreqUT: %d\n",freqOT,freqT,freqUT);
	fprintf (datafp, "reinf_OT: %d\treinf_T: %d\treinf_UT: %d\n",reinf_OT,reinf_T,reinf_UT);
	fprintf (datafp, "pun_OT: %d\tpun_T: %d\tpun_UT: %d\n",pun_OT,pun_T,pun_UT);
	/********************************************
	 +++++++++++ Trial sequence ++++++++++++++
	********************************************/
	trial_num = 0;
	tot_trial_num = 0;
	correction = 1;

	curr_tt = time(NULL);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	strftime(min, 16, "%M", loctime);
	if (DEBUG){printf("hour:min at loop start: %d:%d \n", atoi(hour),atoi(min));}
	currtime=(atoi(hour)*60)+atoi(min);

	operant_write (box_id, HOUSELT, 1);        /* make sure houselight is on */

	free(setplaylist);
	setplaylist = malloc( (((numOTpairs*freqOT)*2)+((numTpairs*freqT)*2)+((numUTpairs*freqUT)*2)+1)*sizeof(int) );

	do{ /* start the main loop */
		while ((currtime>=starttime) && (currtime<stoptime)){ /* start trials loop */
			/*zero out the response tallies */
			for(i = 0; i<nstims;++i){
				Rstim[i].C = Rstim[i].X = Rstim[i].N = Rstim[i].count =0;
				Tstim[i].C = Tstim[i].X = Tstim[i].N = Tstim[i].count = 0;
			}
			if (DEBUG){printf("minutes since midnight at loop start: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}

			srand(time(0));

			if (setupset == 1) {
				if (usepreviousset == 0) {
					if (DEBUG){printf("\n\n***Creating a new set***\n");}
					for (i = 0; i < nstims; i++){stimulus[i].currset = 0;} /*reset currset*/
					/*Create current Set*/
					setrequirements[1] = numOTpairs;
					setrequirements[2] = numOTpairs;
					setrequirements[3] = numTpairs;
					setrequirements[4] = numTpairs;
					setrequirements[5] = numUTpairs;
					setrequirements[6] = numUTpairs;
					currsetsize = setrequirements[1] + setrequirements[2] + setrequirements[3] + setrequirements[4] + setrequirements[5] + setrequirements[6];


					if (numOTpairs == 0) {criterion_reached_OT = 1;}
					else {criterion_reached_OT = 0;}
					if (numTpairs == 0) {criterion_reached_T = 1;}
					else {criterion_reached_T = 0;}
					if (numUTpairs == 0) {criterion_reached_UT5 = criterion_reached_UT6 = 1;}
					else {criterion_reached_UT5 = criterion_reached_UT6 = 0;}
					setcontents[1] = 0;
					setcontents[2] = 0;
					setcontents[3] = 0;
					setcontents[4] = 0;
					setcontents[5] = 0;
					setcontents[6] = 0;


					setplistitems = 0;
					setstimnum = 0;
					havegoodset = 0;
					while (havegoodset == 0) {

						setstims[setstimnum] = -1;
						goodrndstim = 0;
						while (goodrndstim == 0) { /*make sure we don't already have this stim in our current set*/
							rndstim = (int) ((totnstims+0.0)*rand()/(RAND_MAX+0.0));
							goodrndstim = 1;
							for (i=0;i<=setstimnum;i++){
								if (rndstim == setstims[i]){
									goodrndstim = 0;}
							}
						}

						addedstim = 0;
						switch (stimulus[rndstim].class) { /*see if the current stim fits into the current set*/

						case -1: { /*never before presented*/
								if (setcontents[5] != setrequirements[5]){
									currset[setstimnum].class = 5; /*Undertrained Left exemplar*/
									stimulus[rndstim].class = 5;
									setcontents[5] = setcontents[5] + 1;
									currset[setstimnum].reinf = reinf_UT;
									currset[setstimnum].punish = pun_UT;
									for(k=0;k<freqUT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}
									addedstim = 1;
									break;
								}
								else {
									if (setcontents[6] != setrequirements[6]){
										currset[setstimnum].class = 6; /*Undertrained Right exemplar - will now be 'training'*/
										stimulus[rndstim].class = 6; /*Undertrained Right exemplar - will now be 'training'*/
										setcontents[6] = setcontents[6] + 1;
										currset[setstimnum].reinf = reinf_UT;
										currset[setstimnum].punish = pun_UT;
										for(k=0;k<freqUT;k++){
											setplaylist[setplistitems] = setstimnum;
											setplistitems++;
										}
										addedstim = 1;
										break;
									}
									else {
										break;
									}
								}
							}
						case 1: {/*Overtrained Left exemplar*/
								if (setcontents[1] == setrequirements[1]){break;}
								else {setcontents[1] = setcontents[1] + 1;
									currset[setstimnum].class = 1;
									currset[setstimnum].reinf = reinf_OT;
									currset[setstimnum].punish = pun_OT;
									for(k=0;k<freqOT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}
									addedstim = 1;
									break;
								}
							}
						case 2: {/*Overtrained Right exemplar*/
								if (setcontents[2] == setrequirements[2]){break;}
								else {setcontents[2] = setcontents[2] + 1;
									currset[setstimnum].class = 2;
									currset[setstimnum].reinf = reinf_OT;
									currset[setstimnum].punish = pun_OT;
									for(k=0;k<freqOT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}
									addedstim = 1;
									break;
								}
							}
						case 3: {/*Training Left exemplar*/
								if (setcontents[3] == setrequirements[3]){break;}
								else {setcontents[3] = setcontents[3] + 1;
									currset[setstimnum].class = 3;
									currset[setstimnum].reinf = reinf_T;
									currset[setstimnum].punish = pun_T;
									for(k=0;k<freqT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}
									addedstim = 1;
									break;
								}
							}
						case 4: {/*Training Right exemplar*/
								if (setcontents[4] == setrequirements[4]){break;}
								else {setcontents[4] = setcontents[4] + 1;
									currset[setstimnum].class = 4;
									currset[setstimnum].reinf = reinf_T;
									currset[setstimnum].punish = pun_T;
									for(k=0;k<freqT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}
									addedstim = 1;
									break;
								}
							}
						case 5: {/*Undertrained Left exemplar*/
								if (setcontents[5] == setrequirements[5]){break;}
								else {setcontents[5] = setcontents[5] + 1;
									currset[setstimnum].class = 5;
									currset[setstimnum].reinf = reinf_UT;
									currset[setstimnum].punish = pun_UT;
									for(k=0;k<freqUT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}
									addedstim = 1;
									break;
								}
							}
						case 6: {/*Undertrained Left exemplar*/
								if (setcontents[6] == setrequirements[6]){break;}
								else {setcontents[6] = setcontents[6] + 1;
									currset[setstimnum].class = 6;
									currset[setstimnum].reinf = reinf_UT;
									currset[setstimnum].punish = pun_UT;
									for(k=0;k<freqUT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}
									addedstim = 1;
									break;
								}
							}
						default: {/*get here if got no good value*/
								if (DEBUG){printf("Uh OH, fell through the switch. there's likely an error in the stimfile\n");}
								snd_pcm_close(handle);
								exit(0);
								break;
							}
						}

						if (addedstim == 1) {
							setstims[setstimnum] = rndstim;
							strcpy(currset[setstimnum].exemplar, stimulus[setstims[setstimnum]].exemplar);
							currset[setstimnum].freq = stimulus[setstims[setstimnum]].freq;
							currset[setstimnum].ntrials = stimulus[setstims[setstimnum]].ntrials;
							currset[setstimnum].corr = stimulus[setstims[setstimnum]].corr;
							currset[setstimnum].incorr = stimulus[setstims[setstimnum]].incorr;
							currset[setstimnum].noresp = stimulus[setstims[setstimnum]].noresp;
							currset[setstimnum].currset = 1;
							stimulus[setstims[setstimnum]].currset = 1;
							currset[setstimnum].dur = stimulus[setstims[setstimnum]].dur;
							currset[setstimnum].num = stimulus[setstims[setstimnum]].num;

							if (DEBUG){
								printf("stimulus %s (%d) added to set\t", currset[setstimnum].exemplar,setstims[setstimnum]);
								printf("class: %d\tntrials: %d\n", currset[setstimnum].class,currset[setstimnum].ntrials);

							}

							setstimnum = setstimnum + 1;
						}


						if ((setcontents[1] == setrequirements[1]) &
						                (setcontents[2] == setrequirements[2]) &
						                (setcontents[3] == setrequirements[3]) &
						                (setcontents[4] == setrequirements[4]) &
						                (setcontents[5] == setrequirements[5]) &
						                (setcontents[6] == setrequirements[6])) {
							havegoodset = 1;}
					}

					if (DEBUG){
						printf("have a good set:\n");
						for (i=0;i<setstimnum;i++) {
							printf("setstimnum: %d\tstimnum: %d\tname: %s\tclass: %d\tntrials: %d\n",i,setstims[i],currset[i].exemplar,currset[i].class,currset[i].ntrials);
						}
					}
					fprintf (datafp, "#INFOstart#\n");
					fprintf (datafp, "Created Set\n");
					for (i=0;i<setstimnum;i++) {
						fprintf(datafp,"setstimnum: %d\tstimnum: %d\tname: %s\tclass: %d\tntrials: %d\tcorr: %d\tincorr: %d\tnoresp: %d\n",i,setstims[i],currset[i].exemplar,currset[i].class,currset[i].ntrials,currset[i].corr,currset[i].incorr,currset[i].noresp);
					}
					fprintf (datafp, "End Set Definition\n");
				}
				else {
					/*we should only ever get here when the program is started for the first time*/
					usepreviousset = 0;
					someOT=someT=someUT=0;
					setplistitems = 0;
					setstimnum = 0;
					currsetsize = 0;
					for (i = 0; i < nstims; i++){
						if (stimulus[i].currset == 1){
							setstims[setstimnum] = i;
							currsetsize++;
							strcpy(currset[setstimnum].exemplar, stimulus[i].exemplar);
							currset[setstimnum].class = stimulus[i].class;
							switch (stimulus[i].class) { /*see if the current stim fits into the current set*/
							case 1: {/*Overtrained Left exemplar*/
									someOT=1;
									currset[setstimnum].reinf = reinf_OT;
									currset[setstimnum].punish = pun_OT;
									for(k=0;k<freqOT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}
									break;
								}
							case 2: {/*Overtrained Right exemplar*/
									someOT=1;
									currset[setstimnum].reinf = reinf_OT;
									currset[setstimnum].punish = pun_OT;
									for(k=0;k<freqOT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}
									break;
								}
							case 3: {/*Training Left exemplar*/
									someT=1;
									currset[setstimnum].reinf = reinf_T;
									currset[setstimnum].punish = pun_T;
									for(k=0;k<freqT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}

									break;
								}
							case 4: {/*Training Right exemplar*/
									someT=1;
									currset[setstimnum].reinf = reinf_T;
									currset[setstimnum].punish = pun_T;
									for(k=0;k<freqT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}
									break;
								}
							case 5: {/*Training Right exemplar*/
									someUT=1;
									currset[setstimnum].reinf = reinf_UT;
									currset[setstimnum].punish = pun_UT;
									for(k=0;k<freqUT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}
									break;
								}
							case 6: {/*Training Right exemplar*/
									someUT=1;
									currset[setstimnum].reinf = reinf_UT;
									currset[setstimnum].punish = pun_UT;
									for(k=0;k<freqUT;k++){
										setplaylist[setplistitems] = setstimnum;
										setplistitems++;
									}
									break;
								}
							default: {/*get here if got no good value*/
									if (DEBUG){printf("Uh OH, fell through the switch. there's likely an error in the stimfile\n");}
									snd_pcm_close(handle);
									exit(0);
									break;
								}
							}
							currset[setstimnum].freq = stimulus[i].freq;
							currset[setstimnum].ntrials = stimulus[i].ntrials;
							currset[setstimnum].corr = stimulus[i].corr;
							currset[setstimnum].incorr = stimulus[i].incorr;
							currset[setstimnum].noresp = stimulus[i].noresp;
							currset[setstimnum].currset = 1;
							stimulus[i].currset = 1;
							currset[setstimnum].dur = stimulus[i].dur;
							currset[setstimnum].num = stimulus[i].num;
							setstimnum++;
						}
					}
					if (someOT == 0){criterion_reached_OT=1;}
					else {criterion_reached_OT = 0;}
					if (someT == 0){criterion_reached_T=1;}
					else {criterion_reached_T = 0;}
					if (someUT == 0){criterion_reached_UT5 = criterion_reached_UT6 = 1;}
					else {criterion_reached_UT5 = criterion_reached_UT6 = 0;}


					fprintf (datafp, "Created Set (Using Previous Set)\n");
					for (i=0;i<setstimnum;i++) {
						fprintf(datafp,"setstimnum: %d\tstimnum: %d\tname: %s\tclass: %d\tntrials: %d\tcorr: %d\tincorr: %d\tnoresp: %d\n",i,setstims[i],currset[i].exemplar,currset[i].class,currset[i].ntrials,currset[i].corr,currset[i].incorr,currset[i].noresp);
					}
					fprintf (datafp, "End Set Definition\n");
				}

				fprintf (datafp, "Sess#\tTrl#\tType\tStimulus\t\t\tClass\tR_sel\tR_acc\tRT\tReinf\tTOD\tDate\t#INFOend#\n");
			}
			/* reset variables for discrimination learning */
			stimset_plays = 0;
			criterion_reached = 0;
			curr_cycle_position_OT = 0;
			curr_cycle_position_T = 0;
			curr_cycle_position_UT5 = 0;
			curr_cycle_position_UT6 = 0;

			for (j=0; j<20; j++){
				last_20_OT[j] = 0;
				last_20_T[j] = 0;
				last_20_UT5[j] = 0;
				last_20_UT6[j] = 0;
			}


			do{ /* start loop for one set */
				if (trialson == 1){
					if(DEBUG==2){printf("entered trialson if(){}\n");}

					if (nexttrialsofftime < 0){ /*first trial of new trialson session*/
						if(DEBUG==2){printf("entered if (nexttrialsofftime < 0)\n");}
						curr_tt = time(NULL);
						loctime = localtime (&curr_tt);
						strftime (hour, 16, "%H", loctime);
						strftime(min, 16, "%M", loctime);
						currtime=(atoi(hour)*60)+atoi(min);
						if(DEBUG){
							for(i=0;i<100;i++){

								currtrialsonminutes = (int) ((((trialsonminutesmax-trialsonminutesmin)+1.0)*rand()/(RAND_MAX+0.0))+trialsonminutesmin);

								if (DEBUG){printf("currtrialsonminutes: %d\n",currtrialsonminutes);}
							}
						}
						currtrialsonminutes = (int) ((((trialsonminutesmax-trialsonminutesmin)+0.0)*rand()/(RAND_MAX+0.0))+trialsonminutesmin);
						nexttrialsofftime = currtime + currtrialsonminutes;
						fprintf (datafp, "#INFOstart# TrialsOn session starting: trials available for next %d minutes #INFOend#\n",currtrialsonminutes);
						fflush (datafp);
					}

					/*setplayval = (int) ((currsetsize+0.0)*rand()/(RAND_MAX+0.0));   */         /* select set exemplar at random */
					setplayval = (int) ((setplistitems+0.0)*rand()/(RAND_MAX+0.0));            /* select set exemplar at random */
					setplayval = setplaylist[setplayval];
					stim_class = currset[setplayval].class;
					strcpy(stimexm, currset[setplayval].exemplar);                       /* get exemplar filename */
					stim_reinf = currset[setplayval].reinf;
					stim_punish = currset[setplayval].punish;
					sprintf(fstim,"%s%s", STIMPATH, stimexm);
					stim_number = setstims[setplayval];

					do{ /* start correction trial loop */
						left = right = center = 0;        /* zero trial peckcounts */
						resp_sel = resp_acc = resp_rxt = 0;                 /* zero trial variables        */
						++trial_num;++tot_trial_num;

						/* Wait for center key press */
						if (DEBUG){printf("\n\nWaiting for center key press with stim: %s class:%d\n",stimexm,stim_class);}
						operant_write (box_id, HOUSELT, 1);        /* house light on */
						operant_write(box_id, cue[2], 1);	   /* cue light on */
						right=left=center=0;
						do{
							nanosleep(&rsi, NULL);
							center = operant_read(box_id, CENTERPECK);   /*get value at center response port*/

							curr_tt = time(NULL);
							loctime = localtime (&curr_tt);
							strftime (hour, 16, "%H", loctime);
							strftime(min, 16, "%M", loctime);
							currtime=(atoi(hour)*60)+atoi(min);
							if (currtime>=nexttrialsofftime){
								nexttrialsofftime = -1;
								trialson = 0;
								correction = 1; /*bird gets a freebie if trialsoff starts during a correction trial*/
							}
						}while ((center==0) && (trialson == 1));
						operant_write(box_id, cue[2], 0);	   /* cue light off */
						if (trialson == 1) { /*lets us turn off trials if we are in a trialsoff loop*/
							sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
							/* if cueflag, turn on cue light */
							if(cueflag){
								operant_write(box_id, cue[eff_class[stim_class]], 1);
								usleep(cue_duration);
								if (DEBUG){fprintf(stderr,"displaying cue light for class %d\n", stim_class);}
								operant_write(box_id, cue[eff_class[stim_class]], 0);
							}
							/* play the stimulus*/

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
								stimoff_sec = stimoff.tv_sec;
								stimoff_usec = stimoff.tv_usec;
								printf("stim_off sec: %d \t usec: %d\n", stimoff_sec, stimoff_usec);}

							/* Wait for response */
							if (DEBUG){printf("flag: waiting for right/left response\n");}
							timeradd (&stimoff, &respoff, &resp_window);
							if (DEBUG){respwin_sec = resp_window.tv_sec;}
							if (DEBUG){respwin_usec = resp_window.tv_usec;}
							if (DEBUG){printf("resp window sec: %d \t usec: %d\n", respwin_sec, respwin_usec);}

							loop=left=right=0;
							do{
								nanosleep(&rsi, NULL);
								left = operant_read(box_id, LEFTPECK);
								right = operant_read(box_id, RIGHTPECK );
								if((left==0) && (right==0) && flash){
									++loop;
									if(loop%80==0){
										if(loop%160==0){
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
							if (eff_class[stim_class] == 1){                                 /* GO LEFT */
								if ( (left==0 ) && (right==0) ){
									resp_sel = 0;
									resp_acc = 2;
									++Rstim[stim_number].N;++Tstim[stim_number].N;
									reinfor = 0;
									numconsecutivecorrect = 0;
									if (DEBUG){ printf("flag: no response to stimtype 1\n");}
								}
								else if (left != 0){
									resp_sel = 1;
									resp_acc = 1;
									++Rstim[stim_number].C;++Tstim[stim_number].C;
									/*Give secondary reinforcer if requested by command line flag*/
									if (dosecondaryR == 1){
										if (resp_acc == 1) {
											if (DEBUG){printf("giving secondary reinforcer now\n");}
											for (i=0;i<secondaryRnum;i++){
												operant_write(box_id, cue[2], 1);
												usleep(secondaryRuson);
												operant_write(box_id, cue[2], 0);
												usleep(secondaryRusoff);
											}
										}
										else{
											operant_write(box_id, cue[2], 0); /*Just to be explicit!*/
										}
									}
									
									if ((correction == 0) && (noreinforcecorrection == 1)) {
										reinfor = 0;
									}
									else{
									reinfor=feedFR(stim_reinf, &f, &numconsecutivecorrect, &frcritical);
									}
									
									if (reinfor == 1) { ++fed;}
									if (DEBUG){printf("flag: correct response to stimtype 2\n");}
									
								}
								else if (right != 0){
									resp_sel = 2;
									resp_acc = 0;
									++Rstim[stim_number].X;++Tstim[stim_number].X;
									/*Give correctioncue if requested by command line flag*/
									if (docorrectioncue == 1){
											if (DEBUG){printf("giving correctioncue now\n");}
											for (i=0;i<secondaryRnum;i++){
												operant_write(box_id, LFTKEYLT, 1);
												usleep(secondaryRuson);
												operant_write(box_id, LFTKEYLT, 0);
												usleep(secondaryRusoff);
											}
									}
									reinfor =  timeout(stim_punish);
									numconsecutivecorrect = 0;
									
									if (DEBUG){printf("flag: incorrect response to stimtype 1\n");}
								}
								else
									fprintf(datafp, "DEFAULT SWITCH for bit value:ERROR CODE REMOVED");
							}
							else if (eff_class[stim_class] == 2){                           /* GO RIGHT */
								if ( (left==0) && (right==0) ){
									resp_sel = 0;
									resp_acc = 2;
									++Rstim[stim_number].N;++Tstim[stim_number].N;
									reinfor = 0;
									numconsecutivecorrect = 0;
									if (DEBUG){printf("flag: no response to stimtype 2\n");}
								}
								else if (left!=0){
									resp_sel = 1;
									resp_acc = 0;
									++Rstim[stim_number].X;++Tstim[stim_number].X;
									/*Give correctioncue if requested by command line flag*/
									if (docorrectioncue == 1){
											if (DEBUG){printf("giving correctioncue now\n");}
											for (i=0;i<secondaryRnum;i++){
												operant_write(box_id, RGTKEYLT, 1);
												usleep(secondaryRuson);
												operant_write(box_id, RGTKEYLT, 0);
												usleep(secondaryRusoff);
											}
									}
									reinfor =  timeout(stim_punish);
									numconsecutivecorrect = 0;
									
									if (DEBUG){printf("flag: incorrect response to stimtype 2\n");}
								}
								else if (right!=0){
									resp_sel = 2;
									resp_acc = 1;
									++Rstim[stim_number].C;++Tstim[stim_number].C;

									/*Give secondary reinforcer if requested by command line flag*/
									if (dosecondaryR == 1){
										if (resp_acc == 1) {
											if (DEBUG){printf("giving secondary reinforcer now\n");}
											for (i=0;i<secondaryRnum;i++){
												operant_write(box_id, cue[2], 1);
												usleep(secondaryRuson);
												operant_write(box_id, cue[2], 0);
												usleep(secondaryRusoff);
											}
										}
										else{
											operant_write(box_id, cue[2], 0); /*Just to be explicit!*/
										}
									}
									if ((correction == 0) && (noreinforcecorrection == 1)) {
										reinfor = 0;
									}
									else{
									reinfor=feedFR(stim_reinf, &f, &numconsecutivecorrect, &frcritical);
									}
									
									if (reinfor == 1) { ++fed;}
									if (DEBUG){printf("flag: correct response to stimtype 2\n");}
								}
								else
									fprintf(datafp, "DEFAULT SWITCH for bit value: ERROR, CODE REMOVED");
							}
							else if (eff_class[stim_class] >= 2){                           /* PROBE STIMULUS */
								if ( (left==0) && (right==0) ){ /*no response to probe */
									resp_sel = 0;
									resp_acc = 2;
									++Rstim[stim_number].N;++Tstim[stim_number].N;
									reinfor = 0;
									if (DEBUG){printf("flag: no response to probe stimulus\n");}
								}
								else if (left!=0){
									resp_sel = 1;
									if(DEBUG){printf("flag: LEFT response to PROBE\n");}
									resp_acc = 3;
									++Rstim[stim_number].X;++Tstim[stim_number].X;
									reinfor =  probeRx(stim_reinf, stim_punish,&f);
									if (reinfor){++fed;}
								}
								else if (right!=0){
									resp_sel = 2;
									if(DEBUG){printf("flag: RIGHT response to PROBE\n");}
									resp_acc = 3;
									++Rstim[stim_number].X;++Tstim[stim_number].X;
									reinfor =  probeRx(stim_reinf, stim_punish, &f);
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

							if (correction == 1){
								if (resp_acc != 2) { /*don't count no response trials in criterion checking*/
									if ((stim_class == 1) || (stim_class == 2)) {
										last_20_OT[curr_cycle_position_OT] = resp_acc; /* keep track for history */
									}
									else if ((stim_class == 3) || (stim_class == 4)) {
										last_20_T[curr_cycle_position_T] = resp_acc; /* keep track for history */
									}
									else if (stim_class == 5){
										last_20_UT5[curr_cycle_position_UT5] = resp_acc; /* keep track for history */
									}
									else if (stim_class == 6){
										last_20_UT6[curr_cycle_position_UT6] = resp_acc; /* keep track for history */
									}
								}
							}

							/* Pause for ITI */
							reinfor_sum = reinfor + reinfor_sum;
							operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
							nanosleep(&iti, NULL);                                   /* wait intertrial interval */
							if (DEBUG){printf("flag: ITI passed\n");}


							/* Write trial data to output file */
							strftime (tod, 256, "%H%M", loctime);
							strftime (date_out, 256, "%Y%m%d", loctime);
							if (strlen(stimexm) > 15){
								fprintf(datafp, "%d\t%d\t%d\t%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num,
								        correction, stimexm, stim_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
							}
							else {
								fprintf(datafp, "%d\t%d\t%d\t%s\t\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n", session_num, trial_num,
								        correction, stimexm, stim_class, resp_sel, resp_acc, resp_rxt, reinfor, tod, date_out );
							}
							fflush (datafp);
							if (DEBUG){printf("flag: trial data written\n");}

							/*generate some output numbers*/
							for (i = 0; i<nstims;++i){
								Rstim[i].count = Rstim[i].X + Rstim[i].C;
								Tstim[i].count = Tstim[i].X + Tstim[i].C;
							}

							/* Update summary data */
							if(freopen(dsumfname,"w",dsumfp)!= NULL){
								fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name);
								fprintf (dsumfp, "\tPROPORTION CORRECT RESPONSES (by stimulus, including correction trials)\n");
								fprintf (dsumfp, "\tStim\t\tCount\tToday     \t\tCount\tTotals\t(excluding 'no' responses)\n");
								for (i = 0; i<nstims;++i){
									fprintf (dsumfp, "\t%s\t%d\t%1.4f     \t\t%d\t%1.4f\n",
									         stimulus[i].exemplar, Rstim[i].count, (float)Rstim[i].C/(float)Rstim[i].count, Tstim[i].count, (float)Tstim[i].C/(float)Tstim[i].count );
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


							/*Update stim file with new values*/
							++stimulus[setstims[setplayval]].ntrials;
							++currset[setplayval].ntrials;
							if (resp_acc==1){
								++stimulus[setstims[setplayval]].corr;
								++currset[setplayval].corr;
							}
							else {
								if (resp_acc==0){
									++stimulus[setstims[setplayval]].incorr;
									++currset[setplayval].incorr;
								}
								else {
									if (resp_acc==2){
										++stimulus[setstims[setplayval]].noresp;
										++currset[setplayval].noresp;
									}
								}
							}

							if ((stimfp = fopen(stimfname, "w")) != NULL){
								for (i=0;i<nstims; i++){
									fprintf (stimfp, "%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", stimulus[i].exemplar, stimulus[i].class, stimulus[i].freq, stimulus[i].ntrials, stimulus[i].corr, stimulus[i].incorr, stimulus[i].noresp, stimulus[i].currset);
								}
							}
							else
							{
								fprintf(stderr,"Error opening stimulus file for output! Try '-help' for proper file formatting.\n");
								snd_pcm_close(handle);
								exit(0);
							}
							fclose(stimfp);

							if (DEBUG){printf("flag: stim file updated\n");}


							/* End of trial chores */
							sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);  /* unblock termination signals */
							if (correctiontrialsoff == 0){
								if (resp_acc == 0)
									correction = 0;
								else
									correction = 1;               /* set correction trial var */
								if ((xresp==1)&&(resp_acc == 2))
									correction = 0; }             /* set correction trial var for no-resp */
							else if (correctiontrialsoff == 1) {
								correction = 1;}

							curr_tt = time(NULL);
							loctime = localtime (&curr_tt);
							strftime (hour, 16, "%H", loctime);
							strftime(min, 16, "%M", loctime);
							currtime=(atoi(hour)*60)+atoi(min);
							if (DEBUG){printf("minutes since midnight at trial end: %d\t starttime: %d\tstoptime: %d\n",currtime,starttime,stoptime);}
							if (DEBUG) {printf("correction trial is now set to %d, correctiontrialsoff=%d\n", correction, correctiontrialsoff);}
						} /*above is trialson check to see if we get into a trialsoff loop while waiting for a peck*/
					}while ((correction==0)&&(currtime>=starttime)&&(currtime<stoptime)&&(trialson==1)); /* correction trial loop */

					if (trialson == 1){ /*this clause will get skipped if the reason you were kicked out of the correction loop was a trialsoff period starting*/
						stimset_plays++;

						if ((stim_class == 1) || (stim_class == 2)) {
							curr_cycle_position_OT++;
							/* Check now for criterion reached */
							sum_check = 0;
							for (j = 0; j<20; j++){
								if (last_20_OT[j] == 1){
									sum_check = sum_check + 1;}
								if(sum_check>=criterion_correct_OT){
									criterion_reached_OT = 1;
								}
							}

						}
						else if ((stim_class == 3) || (stim_class == 4)) {
							curr_cycle_position_T++;
							/* Check now for criterion reached */
							sum_check = 0;
							for (j = 0; j<20; j++){
								if (last_20_T[j] == 1){
									sum_check = sum_check + 1;}
								if(sum_check>=criterion_correct_T){
									criterion_reached_T = 1;
								}
							}
						}
						else if (stim_class == 5){
							curr_cycle_position_UT5++;
							/* Check now for criterion reached */
							sum_check = 0;
							for (j = 0; j<20; j++){
								if (last_20_UT5[j] == 1){
									sum_check = sum_check + 1;}
								if(sum_check>=criterion_correct_UT){
									criterion_reached_UT5 = 1;
								}
							}
						}
						else if (stim_class == 6){
							curr_cycle_position_UT6++;
							/* Check now for criterion reached */
							sum_check = 0;
							for (j = 0; j<20; j++){
								if (last_20_UT6[j] == 1){
									sum_check = sum_check + 1;}
								if(sum_check>=criterion_correct_UT){
									criterion_reached_UT6 = 1;
								}
							}
						}

						if ((criterion_reached_OT==1) && (criterion_reached_T==1) && (criterion_reached_UT5==1) && (criterion_reached_UT6==1)){
							if(DEBUG){printf("CRITERION REACHED for current set\n");}
							criterion_reached = 1;}

						if (curr_cycle_position_OT == 20) {curr_cycle_position_OT = 0;}
						if (curr_cycle_position_T == 20) {curr_cycle_position_T = 0;}
						if (curr_cycle_position_UT5 == 20) {curr_cycle_position_UT5 = 0;}
						if (curr_cycle_position_UT6 == 20) {curr_cycle_position_UT6 = 0;}

						curr_tt = time(NULL);
						loctime = localtime (&curr_tt);
						strftime (hour, 16, "%H", loctime);
						strftime(min, 16, "%M", loctime);
						currtime=(atoi(hour)*60)+atoi(min);

						if (currtime>=nexttrialsofftime){
							nexttrialsofftime = -1;
							trialson = 0;
						}
					}
				}
				else { /*get here if in a trialsoff loop*/
					if(DEBUG==2){printf("entered trialson else clause\n");}
					operant_write(box_id, cue[2], 0);
					curr_tt = time(NULL);
					loctime = localtime (&curr_tt);
					strftime (hour, 16, "%H", loctime);
					strftime(min, 16, "%M", loctime);
					currtime=(atoi(hour)*60)+atoi(min);

					if(DEBUG==2){printf("currtime = %d\n",currtime);}
					if(DEBUG==2){printf("nexttrialsontime = %d\n",nexttrialsontime);}
					if(nexttrialsontime<0){
						if(DEBUG==2){printf("entered if(nexttrialsontime<0){ clause\n");}
						currtrialsoffminutes = (int) ((((trialsoffminutesmax-trialsoffminutesmin)+1.0)*rand()/(RAND_MAX+0.0))+trialsoffminutesmin);
						nexttrialsontime = currtime + currtrialsoffminutes;
						fprintf (datafp, "#INFOstart# TrialsOff session starting: trials NOT available for next %d minutes #INFOend#\n",currtrialsoffminutes);
						fflush (datafp);
					}
					if (currtime >= nexttrialsontime){
						if(DEBUG==2){printf("entered if (currtime >= nexttrialsontime){ clause\n");}
						trialson = 1;
						nexttrialsontime = -1;
					}
					nanosleep(&ssi, NULL);
				}
			}while((criterion_reached == 0) && (stimset_plays < MAX_TRIALS) && ((currtime>=starttime) && (currtime<stoptime))); /*Set/Session Loop*/


			/*reset some vars for the new set */
			if (criterion_reached == 1) {

				for (i = 0; i<currsetsize; i++) {
					if (currset[i].class == 5) {
						currset[i].class = 3;
						stimulus[setstims[i]].class = 3;
					}
					if (currset[i].class == 6) {
						currset[i].class = 4;
						stimulus[setstims[i]].class = 4;
					}
				}
				if ((stimfp = fopen(stimfname, "w")) != NULL){
					for (i=0;i<nstims; i++){
						fprintf (stimfp, "%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", stimulus[i].exemplar, stimulus[i].class, stimulus[i].freq, stimulus[i].ntrials, stimulus[i].corr, stimulus[i].incorr, stimulus[i].noresp, stimulus[i].currset);
					}
				}
				else
				{
					fprintf(stderr,"Error opening stimulus file for output! Try '-help' for proper file formatting.\n");
					snd_pcm_close(handle);
					exit(0);
				}
				fclose(stimfp);

				++session_num;
				trial_num = 0;
				setupset=1;
			}
			else {
				setupset = 0;
			}
			if(DEBUG){fprintf(stderr,"Exiting Current Set Loop - in trialsoff loop or Going to sleep or going to make new set\n");}
			stim_number = -1;                                                /* reset the stim number for correct trial*/

		}/* trial loop - Top is TimeofDay Check*/
		trialson = 1;
		nexttrialsontime = nexttrialsofftime = -1;

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
		for(i = 0; i<nstims;++i){
			Rstim[i].C = Rstim[i].X = Rstim[i].N = Rstim[i].count =0;
		}
		for(i=1;i<=nclasses;i++){
			Rclass[i].C = Rclass[i].X = Rclass[i].N = Rclass[i].count = 0;
		}
		f.hopper_wont_go_down_failures = f.hopper_already_up_failures = f.hopper_failures = f.response_failures = fed = reinfor_sum = 0;

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



	}while (1);// main loop


	/*  Cleanup */
	fclose(datafp);
	fclose(dsumfp);
	return 0;
}

