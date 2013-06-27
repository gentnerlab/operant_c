/*****************************************************************************
** gng.c - code for running go/nogo operant training procedure
******************************************************************************
** 9-19-01 TQG: Adapted from most current 2choice.c
** 4-1-05 TQG:  Adapted from gonogo2.c & gng_prbe.c: added support for 
**              dsp24 soundcard via ALSA
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <bits/sigaction.h> // XXX: why do I "have" to do this
#include <math.h>
#include <assert.h>
#include <sndfile.h>
#include <alsa/asoundlib.h>

#include "/usr/local/src/operantio/operantio.c"

//#include <asm/signal.h> 
/* #include <stddef.h> */
/* #include <ctype.h> */
/* #include <sys/types.h> */
/* #include <sys/stat.h> */
/* #include <unistd.h> */
/* #include <errno.h> */
/* #include <sys/ioctl.h> */
/* #include <sys/soundcard.h> */
/* #include <sys/socket.h> */
/* #include <netinet/in.h> */
/* #include <netdb.h> */
/* #include <fcntl.h> */
/* #include <pcmio.h> */

//#include "remotesound.h"
//#include "constants.h"

//#include <gsl/gsl_combination.h>
//#include <gsl/gsl_permutation.h>
//#include <regex.h> 

#define ALSA_PCM_NEW_HW_PARAMS_API
//#define SIMPLEIOMODE 0
#define DEBUG 1


/* --- OPERANT IO CHANNELS ---*/

#define LEFTPECK   1
#define CENTERPECK 2
#define RIGHTPECK  3

#define LFTKEYLT  1  
#define CTRKEYLT  2  
#define RGTKEYLT  3  
#define HOUSELT	  4   
#define FEEDER	  5

 


/* --------- OPERANT VARIABLES ---------- */
#define RESP_INT_SEC             2             /* seconds from simulus end until NORESP is registered  (see below) */
#define RESP_INT_USEC            0             /* microsecs in the response window (added to above) */
#define MAXSTIM                  256            /* maximum number of stimulus exemplars */   
#define MAXCLASS                 128            /* maximum number of stimulus classes */
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define TIMEOUT_DURATION         10000000       /* duration of timeout in microseconds */
#define FEED_DURATION            3000000       /* duration of feeder access in microseconds */ // XXX : mcc never used 
//#define DACSAMPLERATE            20000         /* stimulus sampling rate */  
//#define DACBITDEPTH              16            /* stimulus bit depth */
#define MAX_NO_OF_TRIALS         100000        /* maximum number of trials in block of sessions */
#define EXP_START_TIME           7             /* default hour(0-24) session will start */
#define EXP_END_TIME             19            /* default hour(0-24) session will stop */
#define SLEEP_TIME               30            /* night loop polling interval (in sec) */
#define DEF_REF                  10            /* default reinforcement for corr. resp. set to 100% */

long timeout_duration = TIMEOUT_DURATION;
long feed_duration    = FEED_DURATION;
int  trial_max        = MAX_NO_OF_TRIALS;
int  startH           = EXP_START_TIME; 
int  stopH            = EXP_END_TIME;
int  sleep_interval   = SLEEP_TIME;
//int  hopper           = LFTFEED;
//int  peck             = CENTERPECK;
//int  setup            = 1;
int  reinf_val        = DEF_REF;

int feed(int rv, int d);
int timeout(void);
int setup_pcmdev(char *pcm_name);
int playwav(char *sfname, double period);
int probeGO(int rval, int mirr);

const char exp_name[] = "GNG";
int box_id = -1;
int  resp_sel, resp_acc;

struct timespec iti = { INTER_TRIAL_INTERVAL, 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};
struct timeval respoff = { RESP_INT_SEC, RESP_INT_USEC};


/* -------- Signal handling --------- */
int client_fd = -1;
int dsp_fd = 0;

sigset_t trial_mask;

# define timercmp(a, b, CMP) 						      \
  (((a)->tv_sec == (b)->tv_sec) ? 					      \
   ((a)->tv_usec CMP (b)->tv_usec) : 					      \
   ((a)->tv_sec CMP (b)->tv_sec))
# define timeradd(a, b, result)						      \
  do {									      \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;			      \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;			      \
    if ((result)->tv_usec >= 1000000)					      \
      {									      \
	++(result)->tv_sec;						      \
	(result)->tv_usec -= 1000000;					      \
      }									      \
  } while (0)
# define timersub(a, b, result)						      \
  do {									      \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;			      \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;			      \
    if ((result)->tv_usec < 0) {					      \
      --(result)->tv_sec;						      \
      (result)->tv_usec += 1000000;					      \
    }									      \
  } while (0)


static void sig_pipe(int signum) { printf("SIGPIPE caught\n"); client_fd = -1;}

static void termination_handler (int signum)
{
	close_soundserver(dsp_fd);/*close the pcm*/
	
	printf("term signal caught: closing pcm device\n");
	exit(-1);
}

typedef struct stim {
  char exemplar[128];
  int class;
  int reinf;
  int freq;
  int playnum;
}stimulus[MAXSTIM];

typedef struct response {
  int count;
  int go;
  int no;
  float ratio;
} stimRses[MAXSTIM], stimRtot[MAXSTIM], classRses[MAXCLASS], classRtot[MAXCLASS];


//substimulus g_substimuli[MAXSTIM];
//stimulus *stimuli = NULL;
//stimulus *stimuli_splus = NULL;
//stimulus *stimuli_sminus = NULL;
//stimulus_class_stats *stim_class_stats; // per day 
//stimulus_class_stats *total_stim_class_stats; // whole experiment

//int n_stimuli_splus = 0;
//int n_stimuli_sminus = 0;


/**********************************************************************
 *
 *
 *
 *
 **********************************************************************/
void do_usage() {
	fprintf(stderr, "gng usage:\n");
	fprintf(stderr, "    gng [-help] [-B x] [-M] [-t x] [-on x] [-off x] [-S <subject number>] <stimfile>\n\n");
	fprintf(stderr, "        -help        = show this help message\n");
	fprintf(stderr, "        -B x         = use '-B 1' '-B 2' ... '-B 16' \n");
	fprintf(stderr, "        -M     = For probe trials only, set the timeout rate equal to the 'GO_reinforcement rate' in the .stim file\n");
	fprintf(stderr, "                       'mirrorP' requires the base rate in the .stim file to be less than or equal to 50\%\n");
	fprintf(stderr, "        -t x         = set the timeout duration to 'x' secs (use a real number, e.g 2.5 )\n");
	fprintf(stderr, "        -on          = set hour for exp to start eg: '-on 7' (default is 7AM)\n");
	fprintf(stderr, "        -off         = set hour for exp to stop eg: '-off 19' (default is 7PM)\n");
	fprintf(stderr, "        -S xxx       = specify the subject ID number (required)\n");
	fprintf(stderr, "        stimfile     = specify the text file containing the stimuli (required)\n");
	fprintf(stderr, "                       where each line has the variables: 'class' 'pcmfile' 'p_freq' 'S+_rate'\n");
	fprintf(stderr, "                         'class'= 1 for S+, 2 for S-, 3 or greater for nondifferential (e.g. probe stimuli) \n");
	fprintf(stderr, "                         'pcmfile' is the name of the stimulus soundfile\n");
	fprintf(stderr, "                         'p_freq' is the stimulus presentation rate relative to the other stimuli in the file. \n");
	fprintf(stderr, "                             The actual rate for each stimulus (expressed as an integer) is that value divded by the\n");
	fprintf(stderr, "                             sum of p_freq over all stimuli. Set all p_freq values to 1 for equal probablility \n");
	fprintf(stderr, "                         'S+_rate' is the rate at which responses to this stimulus are reinforced.\n");
	fprintf(stderr, "                             For class '1' stimuli this is the rate of food reward following a correct response.\n");
	fprintf(stderr, "                             For class '2' stimuli this is the rate of punishment following an incorrect response.\n");
	fprintf(stderr, "                             For class>2 (i.e. probe) stimuli this is the rate of nondifferential reinforcement.'\n");
	fprintf(stderr, "                             Use '-mirrorP' to reinforce with food reward and timeout at equal rates on probe trials.\n");
	
}

/**********************************************************************
 *
 *
 *
 *
 **********************************************************************/
int command_line_parse(int argc, char **argv, int *box_id, int *subjectid, int *startH, int *stopH, float *toval, int *mirror, char **stimfname)
{
	int i=0;
	
	for (i = 1; i < argc; i++)  {
	    if (*argv[i] == '-') {
	      if (strncmp(argv[i], "-B", 2) == 0) { 
		sscanf(argv[++i], "%i", box_id);
		if(DEBUG){printf("box number = %d\n", *box_id);}
	      }
	      else if (strncmp(argv[i], "-S", 2) == 0) {
		sscanf(argv[++i], "%i", subjectid);}
	      else if (strncmp(argv[i], "-on", 3) == 0){
		sscanf(argv[++i], "%i", startH);}
	      else if (strncmp(argv[i], "-off", 4) == 0){
		sscanf(argv[++i], "%i", stopH);}
	      else if (strncmp(argv[i], "-t", 2) == 0){
		sscanf(argv[++i], "%f", toval);}
	      else if (strncmp(av[i], "-M", 2) == 0){
		mirror = 1;}                                  
	    }
	    
	    else if (strncmp(argv[i], "-help", 5) == 0){
	      do_usage();
	      exit(-1);
	    }
	    // mcc new arguments...
	    else {
	      fprintf(stderr, "Unknown option: %s\t", argv[i]);
	      fprintf(stderr, "Try 'gng -help' for help\n");
	    }
	}
	else
	  {
	    *stimfname = argv[i];
	  }
}



/**********************************************************************
 *
 *
 *
 *
 **********************************************************************/
void setup_logs(char *stimfname, FILE **datafp, FILE **dsumfp, int subjectid)
{
/* Open & setup data logging files */
	time_t curr_tt;
	struct tm *loctime;
	char timebuff[64];
	char stimftemp[128];
	const char delimiters[] = " .,;:!-";
	char *stimfroot;
	char datafname[128];
	char dsumfname[128];

	fprintf(stderr, "setup_logs()\n");
	
	curr_tt = time (NULL);
	loctime = (struct tm*) localtime (&curr_tt);
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
	*datafp = fopen(datafname, "a");
	*dsumfp = fopen(dsumfname, "w");
	
	if ( ((*datafp)==NULL) || ((*dsumfp)==NULL) ) {
		fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", datafname);
		close(dsp_fd);
		fclose(*datafp);
		fclose(*dsumfp);
		exit(-1);
	}

/* Write data file header info */

	printf ("Data output to '%s'\n", datafname);
	
	fprintf (*datafp, "File name: %s\n", datafname);
	fprintf (*datafp, "Procedure source: %s\n", exp_name);
	fprintf (*datafp, "Start time: %s", asctime(loctime));
	fprintf (*datafp, "Subject ID: %d\n", subjectid);
	fprintf (*datafp, "Stimulus source: %s\n", stimfname);  
	fprintf (*datafp, "Reinforcement for correct response are in the stimulus file\n" );
	fprintf (*datafp, "Sess#\tTrl#\tTrlTyp\tStimulus\t\t\t\t\tClass\tRspSL\tRspAC\tRspRT\tReinf\tTOD\tDate\n");



}


/**********************************************************************
 *
 *
 *
 *
 **********************************************************************/
int get_stimulus_class_to_play(int *stim_class_probs, int n_stimulus_classes)
{
	int randval = rand();
	double rand_1_0 = (double) randval / (double)RAND_MAX;
	
	// assume stimulus_class_probs add up to 100 -- if they don't , user is a tool
	int x = (int) floor(rand_1_0  * 100.0);

	fprintf(stderr, "get_stimulus_class_to_play: randval=%d, rand_1_0=%lf, x=%d\n", randval, rand_1_0, x);
	
	int count = 0;
	for (int i=0; i< n_stimulus_classes; i++) {
		count += stim_class_probs[i];
		if (x < count) {
			return i+1; // XXX this is obscure, but STIMULUS_CLASS_S_PLUS=1, STIMULUS_CLASS_S_MINUS=2. wack
		}
	}

	return -1;

}

/**********************************************************************
 *
 *
 *
 *
 **********************************************************************/
void do_trial(stimulus *stimuli_splus, stimulus *stimuli_sminus, FILE *datafp, FILE *dsumfp, int n_stimulus_classes, int subjectid)
{

   /********************************************     
    +++++++++++ Trial sequence ++++++++++++++
   ********************************************/
	int session_num = 1;
	int trial_num = 0; // what is the difference between session_num and trial_num
	
	struct timeval stimoff, resp_window;
	struct timeval resp_lag, resp_rt;
	float resp_rxt; // XXX (???)
	int splus_no=0, splus_go=0, sminus_no=0, sminus_go=0, Tsplus_no=0, Tsplus_go=0, Tsminus_no=0, Tsminus_go=0;
	int fed = 0;	
	int center = 0;
	int bird_gets_food_sum = 0;
	// stim_class_stats are already zeroed out but maybe we are supposed to start over?  nah do_trial only happens once !!
	char tod[256], date_out[256]; // XXX some date bullshit below
	char hour[16], min[16], month[16], 
		day[16], stimftemp[128];
	
	time_t curr_tt = time(NULL);
	struct tm *loctime = (struct tm*) localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
	if (DEBUG){printf("atoi(hour) at loop start: %d \n", atoi(hour));}

	int stim_reward_probability = 0; //nee stim_rv
	int stim_reward_duration = 0; // nee stim_rd
	// mcc: .rv (reward probability) 
    //      .rd (reward duration) 
	do {                                                                               /* start the block loop */
		while ((atoi(hour) >= startH) && (atoi(hour) < stopH)) { 

			fprintf(stderr, "calling get_stimulus_class_to_play()\n");
			int stimulus_class_to_play = get_stimulus_class_to_play(stimulus_class_probs, n_stimulus_classes);
			if (-1 == stimulus_class_to_play) {
				fprintf(stderr, "FATAL ERROR: stimulus_class_to_play=-1\n");
				return;
			}
			int stim_num = 0;
			int stim_class_index = 0;
			double rand_1_0 = 0.0;
			// now choose a random stimuli within that class
			stimulus *stim_to_play = NULL;
			if (stimulus_class_to_play == STIMULUS_CLASS_S_PLUS) {
				rand_1_0 = (double) rand() / (double)RAND_MAX;
				stim_num = (int) (floor(rand_1_0  * (double) n_stimuli_splus));
				fprintf(stderr, "STIMULUS_CLASS_S_PLUS: n_stimuli_splus=%d, rand_1_0=%lf, playing %d\n", n_stimuli_splus, rand_1_0, stim_num);
				stim_to_play = &(stimuli_splus[stim_num]); // XXX ensure that it is possible to get, e.g. the very last element
				stim_reward_probability = reward_probs[0];
				stim_reward_duration = reward_times[0];
				stim_class_index = 0; // XXX hardcoded, dumb
			} else {
				rand_1_0 = (double) rand() / (double)RAND_MAX;
				stim_num = (int) floor(rand_1_0  * (double) n_stimuli_sminus);
				fprintf(stderr, "STIMULUS_CLASS_S_MINUS: n_stimuli_sminus=%d, rand_1_0=%lf, playing %d\n", n_stimuli_sminus, rand_1_0, stim_num);
				stim_to_play = &(stimuli_sminus[stim_num]); // XXX ensure that it is possible to get, e.g. the very last element
				stim_reward_probability = reward_probs[1];
				stim_reward_duration = reward_times[1];
				stim_class_index = 1; // XXX hardcoded, dumb
			}

			/* we now want to get a list of all the file names that make up this random stimulus !!*/

			int substimulus_nums[4];
			char **substimulus_filenames = calloc(4, sizeof(char));
			for (int i=0; i< 4; i++) {
				substimulus_nums[i] = stim_to_play->substimuli[i];
				substimulus_filenames[i] = (char *) strdup((g_substimuli[stim_to_play->substimuli[i]]).exemplar);
				
			}

//			stim_rv = stimulus[stim_number].rv;
//			stim_rd = stimulus[stim_number].rd;
			
			if (DEBUG){printf("exemplars chosen: %s %s %s %s\tnumber: %d %d %d %d\n", 
							  substimulus_filenames[0],substimulus_filenames[1],substimulus_filenames[2],substimulus_filenames[3] , 
							  substimulus_nums[0], substimulus_nums[1], substimulus_nums[2], substimulus_nums[3]); }
			
			int bird_responded = 0; // aka "resp_sel"
			int bird_is_correct = 0; // aka "resp_acc"
			int bird_gets_food = 0; // aka "reinfor"
			
			fprintf(stderr, "top of trial do-loop...\n");
			/*
			  "resp_sel = 0" == "bird did not respond"
			  "resp_acc = 1" == "bird did the right thing"

			  "reinfor = 0" == "bird doesn't get food"
			*/
			//resp_sel = resp_acc = resp_rxt = 0;        /* zero trial variables        */
			resp_rxt=0; // XXX what is this
				
			bird_responded = 0;
			bird_is_correct = 0;
			bird_gets_food = 0;

			trial_num++;
				
			/* Wait for center key press */
			if (DEBUG){printf("waiting for center key press\n");}
			operant_write (box_id, HOUSELT, 1);        /* house light on */
			center = 0;
			do {                                         
				nanosleep(&rsi, NULL);	               	       
				center = operant_read(box_id, peck);   /*get value at center peck position*/		 	       
				//if (DEBUG){printf("flag: value read from center = %d\n", center);}
				//fprintf(stderr, "XXXXXX SKIPPING center key press\n");
				//break; // XXXXXXXX! TESTING
			}while (center==0);  
	      
			sigprocmask (SIG_BLOCK, &trial_mask, NULL);     /* block termination signals*/
				
			/* Play stimulus file */
			if (DEBUG){printf("START [%s %s %s %s]\n",
							  substimulus_filenames[0],substimulus_filenames[1],substimulus_filenames[2],substimulus_filenames[3]);}
			if (play2soundserver_multiple (dsp_fd, substimulus_nums, 4) == -1) {
				fprintf(stderr, "play2soundserver failed on dsp_fd:%d substim_numbers: %d %d %d %d. Program aborted %s\n", dsp_fd, 
						substimulus_nums[0], substimulus_nums[1], substimulus_nums[2], substimulus_nums[3],
						asctime(localtime (&curr_tt)) );
				fprintf(datafp, "play2soundserver failed on dsp_fd:%d substim_numbers: %d %d %d %d. Program aborted %s\n", dsp_fd, 
						substimulus_nums[0], substimulus_nums[1], substimulus_nums[2], substimulus_nums[3],
						asctime(localtime (&curr_tt)) );
				close_soundserver(dsp_fd);
				fclose(datafp);
				fclose(dsumfp);
				exit(-1);
			} 
			if (DEBUG){printf("STOP [%s %s %s %s]\n",
							  substimulus_filenames[0],substimulus_filenames[1],substimulus_filenames[2],substimulus_filenames[3]);}
			// increment stim_class_stats !!
			(stim_class_stats[stim_class_index].count)++;
			(total_stim_class_stats[stim_class_index].count)++;
				
			gettimeofday(&stimoff, NULL);
			if (DEBUG){printf("stim_off happened at sec/usec: [%d/%d]\n", stimoff.tv_sec, stimoff.tv_usec);}
				
			/* Wait for center key press */
			if (DEBUG){printf("flag: waiting for right/left response\n");}
			timeradd (&stimoff, &respoff, &resp_window); // give it 2 secs to respond
			if (DEBUG){printf("resp_window = [%d/%d]\n", resp_window.tv_sec, resp_window.tv_usec);}

			// loop = 0; // XXX what is this for
			center = 0;
			fprintf(stderr, "looping on center peck:");
			do{
				nanosleep(&rsi, NULL);
				center = operant_read(box_id, peck);	
				if (center == 0) { fprintf(stderr, "."); }
				gettimeofday(&resp_lag, NULL);
			} while ( (center==0) && (timercmp(&resp_lag, &resp_window, <)) );
			fprintf(stderr, "\n");
			
			/* Calculate response time */
			curr_tt = time (NULL); 
			loctime = (struct tm*) localtime (&curr_tt);                     /* date and wall clock time of resp */
			timersub (&resp_lag, &stimoff, &resp_rt);           /* reaction time */
			if (DEBUG){printf("resp_rt=[%d/%d]\n", resp_rt.tv_sec, resp_rt.tv_usec);} 
			resp_rxt = (float) resp_rt.tv_sec + ( (float) resp_rt.tv_usec/1000000);  /* format reaction time */
			if (DEBUG){printf("flag: resp_rxt = %.4f\n", resp_rxt);}
				
			strftime (hour, 16, "%H", loctime);                    /* format wall clock times */
			strftime (min, 16, "%M", loctime);
			strftime (month, 16, "%m", loctime);
			strftime (day, 16, "%d", loctime);
				

			/* Consequate responses */ // is this english ? - mcc
				
			if (DEBUG){printf("flag: stim_class_index = %d\n", stim_class_index);}
			if (DEBUG){printf("flag: exit value center = %d\n",center);}
				
			/* 
			   figure out what to do

			   if S+ then we want to look at stim_rv=REWARD_PROB_0 
			   if S- then stim_rv=REWARD_PROB_1

			   "resp_sel = 0" == "bird did not respond"
			   "resp_acc = 1" == "bird did the right thing"

			   "reinfor = 0" == "bird doesn't get food"
				   
			*/

			fprintf(stderr, "stim_reward_probability=%d, center=%d\n", stim_reward_probability, center);
			if (center==0){ /*no response*/
				if (stim_reward_probability == 0){
					bird_responded = 0;
					bird_is_correct = 1;
					bird_gets_food = 0;
						
					//++resp[stim_number].no; ++tot_resp[stim_number].no; ++sminus_no; ++Tsminus_no;
					++sminus_no; ++Tsminus_no;
						
					if (DEBUG){printf("flag: no response to s- stim\n");}}
				if (stim_reward_probability != 0){
					bird_responded = 0;
					bird_is_correct = 0;
					bird_gets_food = 0;
					//++resp[stim_number].no;++tot_resp[stim_number].no; ++splus_no; ++Tsplus_no;
					++splus_no; ++Tsplus_no;
					if (DEBUG){printf("flag: response to s- stim\n");}}
			}
			else if (center!=0){ /*go response*/
				if (stim_reward_probability == 0) {
					bird_responded = 1;
					bird_is_correct = 0;

					//++resp[stim_number].go;++tot_resp[stim_number].go; ++sminus_go; ++Tsminus_go;
					(stim_class_stats[stim_class_index].go)++;
					(total_stim_class_stats[stim_class_index].go)++;
					++sminus_go; ++Tsminus_go;

					bird_gets_food =  timeout();
				}
				if (stim_reward_probability != 0){
					bird_responded = 1;
					bird_is_correct = 1; 
					//++resp[stim_number].go;++tot_resp[stim_number].go;++splus_go; ++Tsplus_go;

					(stim_class_stats[stim_class_index].go)++;
					(total_stim_class_stats[stim_class_index].go)++;
					++splus_go; ++Tsplus_go;
						
					bird_gets_food = feed(stim_reward_probability, stim_reward_duration);
					if (bird_gets_food == 1) { ++fed;}
				}
					
			}


			/* Pause for ITI */
			bird_gets_food_sum += bird_gets_food;
			operant_write(box_id, HOUSELT, 1);         /* make sure the houselight is on */
			nanosleep(&iti, NULL);                     /* wait intertrial interval */
			if (DEBUG){printf("flag: ITI passed\n");}
				
			/* Write trial data to output file */
			strftime (tod, 256, "%H%M", loctime);
			strftime (date_out, 256, "%m%d", loctime);
			// XXX hacked up %s%s%s%s for 4 substims
			fprintf(datafp, "%d\t%d\t%d\t%s,%s,%s,%s\t\t%d\t%d\t%d\t%.4f\t%d\t%s\t%s\n",
					session_num, trial_num, 0,
					substimulus_filenames[0],substimulus_filenames[1],substimulus_filenames[2],substimulus_filenames[3],
					stim_class_index, bird_responded, bird_is_correct, resp_rxt, bird_gets_food, tod, date_out );
			fflush (datafp);
			if (DEBUG){printf("flag: trail data written\n");}
				
			// XXX: don't need to compute ratios..
			/*
			  for (i = 0; i<nstims;++i){
			  if (DEBUG){printf("resp.go: %d\t resp.count: %d\n", resp[i].go, resp[i].count);}
			  resp[i].ratio = (float)(resp[i].go) /(float)(resp[i].count);
			  if (DEBUG){printf("tot_resp.go: %d\t tot_resp.count: %d\n", tot_resp[i].go, tot_resp[i].count);}
			  tot_resp[i].ratio = (float) (tot_resp[i].go)/ (float)(tot_resp[i].count);
			  }
			*/
				
			if (DEBUG){printf("flag: ouput numbers done\n");}
			/* Update summary data */ 	       
			fprintf (dsumfp, "          SUMMARY DATA FOR st%d, %s\n", subjectid, exp_name); 
			fprintf (dsumfp, "SESSION TOTALS     \tGRAND TOTALS (%d sessions)\n", session_num);
			fprintf (dsumfp, "  S+ Stim            \tS+ Stim\n"); 
			fprintf (dsumfp, "   GO: %d               \tGO: %d\n", splus_go, Tsplus_go); 
			fprintf (dsumfp, "   NO: %d               \tNO: %d\n\n", splus_no, Tsplus_no); 
				
			fprintf (dsumfp, "  S- Stim            \tS- STIM\n");
			fprintf (dsumfp, "   GO: %d               \tGO: %d\n", sminus_go, Tsminus_go); 
			fprintf (dsumfp, "   NO: %d               \tNO: %d\n\n", sminus_no, Tsminus_no);  
				
			fprintf (dsumfp, "  \t\tSTIMULS RESPONSE RATIOS\n");
			fprintf (dsumfp, "\tStim     \t\tSession     \t\tTotals\n");
			for (int i = 0; i<n_stimulus_classes; i++){
//						fprintf (dsumfp, "\t%d     \t\t%1.4f     \t\t%1.4f\n", i, resp[i].ratio, tot_resp[i].ratio);
				fprintf(dsumfp, "\t%d     \t\t%1.4f     \t\t%1.4f\n", i,
						(double) (stim_class_stats[i].go) / (double) (stim_class_stats[i].count),
						(double) (total_stim_class_stats[i].go) / (double) (total_stim_class_stats[i].count));
					
			}
			fprintf (dsumfp, "Last trial run @: %s\n", asctime(loctime) );
			fprintf (dsumfp, "Feeder ops today: %d\n", fed );
			fprintf (dsumfp, "Rf'd responses: %d\n\n", bird_gets_food_sum); 
				
			fflush (dsumfp);
			rewind (dsumfp);
				
			if (DEBUG){printf("flag: summaries updated\n");}
				
				
			/* End of trial chores */
				
			sigprocmask (SIG_UNBLOCK, &trial_mask, NULL);                   /* unblock termination signals */ 

			fprintf(stderr, "man: atoi(hour)=%d, startH=%d, atoi(hour)=%d, stopH=%d\n",atoi(hour), startH, atoi(hour), stopH);
			
			//stim_number = -1;                                          /* reset the stim number for correct trial*/
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
			loctime = (struct tm*) localtime (&curr_tt);
			strftime (hour, 16, "%H", loctime);
		}
		operant_write(box_id, HOUSELT, 1);
		curr_tt = time(NULL);
		++session_num;
		/*
		for (i = 0; i<nstims;++i){
			resp[i].count = 0;
			resp[i].go = 0;
			resp[i].no = 0;
			resp[i].ratio = 0.0;
		}
		*/
		// clear out stim_class_stats
		for (int i=0; i<n_stimulus_classes; i++) {
			stim_class_stats[i].count = 0;
			stim_class_stats[i].go = 0;
		}
		
		splus_go = sminus_go = splus_no = sminus_no = 0;
		
	}while (1);
	
}

/**********************************************************************
 * main
 *
 *
 *
 **********************************************************************/
int main(int argc, char *argv[])
{
	FILE *stimfp = NULL, *datafp = NULL, *dsumfp = NULL;
	char *stimfname = NULL;
	char hour [16];

	char  buf[128], stimexm[128],
		timebuff[64];
	int stim_class, subjectid, box,
		i, stimtemp;//, stim_rv, stim_rd;
	int stimoff_sec, stimoff_usec, respwin_sec, respwin_usec, resp_sec, resp_usec;  /* debugging variables */

	int n_stimulus_classes;
	char *consts_string  = calloc (512, sizeof(char));

	int min_substims_per_stim = 0;
	int max_substims_per_stim = 0;

	//gsl_permutation * gslperm;
	//gsl_combination * gslcomb;
	//size_t gslcomb_i;
	//size_t gslperm_i;
	
	int nsubstims = 0; // e.g. # of lines in .stim file.
	
	char *boxstring = calloc (1024, sizeof(char));
	char *stimulus_regexp = NULL;
	char *stimulus_regexp2 = NULL;

	char *foo = NULL;

	srand(time (0) );

	memset(g_substimuli, 0, MAXSTIM * sizeof (substimulus));
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
	

	consts_init("mccgng");
	min_substims_per_stim = consts_int("MIN_SUBSTIMS_PER_STIM");
	max_substims_per_stim = consts_int("MAX_SUBSTIMS_PER_STIM");
	stimulus_regexp = consts_getstring("STIMULUS_REGEXP");
	stimulus_regexp2 = consts_getstring("STIMULUS_REGEXP2");
	fprintf(stderr, "stimulus_regexp='%s'\n", stimulus_regexp);
	fprintf(stderr, "stimulus_regexp2='%s'\n", stimulus_regexp2);
	
	/* get the reward_probs, reward_times, stimulus_class_probs */
	memset(reward_probs, 0, MAX_STIMULI_CLASSES *sizeof(int));
	memset(reward_times, 0, MAX_STIMULI_CLASSES *sizeof(int));
	memset(stimulus_class_probs, 0, MAX_STIMULI_CLASSES *sizeof(int));
	n_stimulus_classes = consts_int("NUM_STIMULUS_CLASSES");
	stim_class_stats = calloc(n_stimulus_classes, sizeof(stimulus_class_stats));
	total_stim_class_stats = calloc(n_stimulus_classes, sizeof(stimulus_class_stats));

	for (i=0; i < n_stimulus_classes; i++) {
		memset(consts_string, 0, 256*sizeof(char));
		sprintf(consts_string, "REWARD_PROB_%d", i);
		reward_probs[i] = consts_int(consts_string);

		memset(consts_string, 0, 256*sizeof(char));
		sprintf(consts_string, "REWARD_TIME_%d", i);
		reward_times[i] = consts_int(consts_string);

		memset(consts_string, 0, 256*sizeof(char));
		sprintf(consts_string, "STIMULUS_CLASS_PROB_%d", i);
		stimulus_class_probs[i] = consts_int(consts_string);
	}
	
	/* Parse the command line */
	command_line_parse(argc, argv, &box_id, &subjectid, &startH, &stopH, &setup, &stimfname);
	fprintf(stderr, "command_line_parse(): box_id=%d, subjectid=%d, startH=%d, stopH=%d, setup=%d\n",box_id, subjectid, startH, stopH, setup );

	// actually connect to soundserver.. don't do this unless we know what box is cool to connect to
	fprintf(stderr, "connecting to soundserver\n");
	sprintf(boxstring, "/dev/box%d", box_id);
	if ((dsp_fd = connect_to_soundserver("singin.uchicago.edu", boxstring)) == -1) {
		perror("FAILED connection to soundserver");
		exit (-1);
	}                                                       /*assign sound device*/
	fprintf(stderr, "yo connected to soundserver OK I think...\n");
	

	// based on value of "setup" from command_line_parse(), set "hopper", "peck", and "light"
	if (setup == 1){
		hopper = LFTFEED;
		peck = CENTERPECK;
	}
	else if (setup == 2) {
		hopper = LFTFEED;
		peck = LEFTPECK;
	}
	else if (setup == 3) {
		hopper = RGTFEED;
		peck = RIGHTPECK;
	}
	
	if (stopH <= startH){
	  fprintf(stderr, "\tTERMINAL ERROR: exp start-time must be greater than stop-time\n");
	  exit(-1);
	} 
	if (box_id <= 0) {
	    fprintf(stderr, "\tYou must enter a box ID!: %s \n", argv[i]); 
	    fprintf(stderr, "\tERROR: try 'gonogo2 -help' for help\n");
	    exit(-1);
	}
	/* Initialize box */
	else  {
	   printf("Initializing box #%d...", box_id);
	   operant_init();
	   operant_clear(box_id);
	   printf("done\n");
	}

	fprintf(stderr, "Loading stimulus file '%s' for box '%d' session\n", stimfname, box_id); 
	fprintf(stderr, "Subject ID number: %i\n", subjectid);


/* Read in the list of exmplars from stimulus file */
// XXX: these are all now SUBstimuli.

	nsubstims = 0;

	if ((stimfp = fopen(stimfname, "r")) != NULL) {
	    while (fgets(buf, sizeof(buf), stimfp)) {
			nsubstims++;
		}
		fprintf(stderr, "Found %d substimulus exemplars in '%s'\n", nsubstims, stimfname);
		rewind(stimfp);

	      
	    for (i = 0; i < nsubstims; i++) {
			fgets(buf, 128, stimfp);
			sscanf(buf, "%d\%s", &stimtemp, g_substimuli[i].exemplar);
			g_substimuli[i].motif_class = stimtemp;
			if (load2soundserver(dsp_fd, i, g_substimuli[i].exemplar) != 0 ) {
				printf("Error loading substimulus file: %s\n",  g_substimuli[i].exemplar);
				close_soundserver(dsp_fd);
				exit(0);
			}
			if(DEBUG){printf("stimulus file: %s\t", g_substimuli[i].exemplar);}
			if(DEBUG){printf("stimulus motif class: %i\n", g_substimuli[i].motif_class);}
//		if(DEBUG){printf("reward probability: %i\t", stimulus[i].rv*10);}
//		if(DEBUG){printf("reward duration: %i tenths of a second\n", stimulus[i].rd);}
			
			//resp[i].stimclass=stimulus[i].class; // XXX WTF will break
			//tot_resp[i].stimclass=stimulus[i].class; // XXX WTF will break
		}
	}
	else {
	    printf("Error opening stimulus input file!\n");
	    close_soundserver(dsp_fd);
	    exit(0);	  
	}
        
	fclose(stimfp);
	if(DEBUG){printf("flag: done reading in stims\n");}


	// this is recursive and exits when level=max_substims_per_stim
	//generate_all_possible_stimuli(&stimuli, nsubstims, 0, max_substims_per_stim);

	int total_stims = (int) pow((double)nsubstims, max_substims_per_stim);
	fprintf(stderr, "total_stims=%d\n", total_stims);
	stimuli = calloc(total_stims, sizeof(stimulus));
	int curr_stimulus = 0;
	int ii=0, jj=0, kk=0, ll=0;
	for (ii=0; ii<total_stims; ii++) {
		stimuli[ii].substimuli = calloc(4, sizeof(int));
	}
	for (ii=0; ii<nsubstims;ii++) {
		for (jj=0; jj<nsubstims;jj++) {
			for (kk=0; kk<nsubstims;kk++) {
				for (ll=0; ll<nsubstims;ll++) {
//					fprintf(stderr, "doing %d\n", curr_stimulus);
					assert(curr_stimulus < total_stims);
					stimuli[curr_stimulus].substimuli[0] = ii;
					stimuli[curr_stimulus].substimuli[1] = jj;
					stimuli[curr_stimulus].substimuli[2] = kk;
					stimuli[curr_stimulus].substimuli[3] = ll;
					curr_stimulus++;
				}
			}
		}
	}

	regex_t regex;
	regex_t regex_BBAA;
	regcomp(&regex, stimulus_regexp, 0);
	regcomp(&regex_BBAA, stimulus_regexp2, 0);
	
	for (ii=0; ii< total_stims; ii++) {
		stimuli[ii].stimulus_class = STIMULUS_CLASS_S_MINUS;
	}
	
	char *curr_regexp = calloc(4, sizeof(char));
	for (ii=0; ii< total_stims; ii++) {
		curr_regexp[0] = (char) (stimuli[ii].substimuli[0]) + (int) 'a'; // XXX retarded
		curr_regexp[1] = (char) (stimuli[ii].substimuli[1]) + (int) 'a'; // XXX retarded
		curr_regexp[2] = (char) (stimuli[ii].substimuli[2]) + (int) 'a'; // XXX retarded
		curr_regexp[3] = (char) (stimuli[ii].substimuli[3]) + (int) 'a'; // XXX retarded

//		printf("dude: %s\n", curr_regexp);
		
		if (regexec(&regex, curr_regexp, 0, NULL, 0) == 0) {
			fprintf(stderr, "stimuli[%d].substimuli = [%d][%d][%d][%d]=valid\n", ii,
		           stimuli[ii].substimuli[0], stimuli[ii].substimuli[1], stimuli[ii].substimuli[2], stimuli[ii].substimuli[3]);	
			stimuli[ii].stimulus_class = STIMULUS_CLASS_S_PLUS;
		}
		
	}

	// now eliminate repeats arbitrarily
	for (ii=0; ii< total_stims; ii++) {
		if ((stimuli[ii].substimuli[0] == stimuli[ii].substimuli[1]) || 
			(stimuli[ii].substimuli[0] == stimuli[ii].substimuli[2]) ||
			(stimuli[ii].substimuli[0] == stimuli[ii].substimuli[3]) ||
			(stimuli[ii].substimuli[1] == stimuli[ii].substimuli[2]) || 
			(stimuli[ii].substimuli[1] == stimuli[ii].substimuli[3]) ||
			(stimuli[ii].substimuli[2] == stimuli[ii].substimuli[3])) {
			stimuli[ii].stimulus_class =STIMULUS_CLASS_INVALID ;
		}
	}

	// now eliminate BBAA From S_MINUS ONLY 
	for (ii=0; ii< total_stims; ii++) {
		if (stimuli[ii].stimulus_class == STIMULUS_CLASS_S_MINUS) {
			curr_regexp[0] = (char) (stimuli[ii].substimuli[0]) + (int) 'a'; // XXX retarded
			curr_regexp[1] = (char) (stimuli[ii].substimuli[1]) + (int) 'a'; // XXX retarded
			curr_regexp[2] = (char) (stimuli[ii].substimuli[2]) + (int) 'a'; // XXX retarded
			curr_regexp[3] = (char) (stimuli[ii].substimuli[3]) + (int) 'a'; // XXX retarded
			if (regexec(&regex_BBAA, curr_regexp, 0, NULL, 0) == 0) {
				stimuli[ii].stimulus_class = STIMULUS_CLASS_INVALID;
			}
		}
	}
	
//	int n_stimuli_splus = 0;
//	int n_stimuli_sminus = 0;
	for (ii=0; ii< total_stims; ii++) {
		if (stimuli[ii].stimulus_class==STIMULUS_CLASS_S_PLUS) {
//			fprintf(stderr, "SPLUS stimuli[%d].substimuli = [%d][%d][%d][%d]=valid\n", ii, stimuli[ii].substimuli[0], stimuli[ii].substimuli[1], stimuli[ii].substimuli[2], stimuli[ii].substimuli[3]);
			n_stimuli_splus++;
		}
	}

	for (ii=0; ii< total_stims; ii++) {
		if (stimuli[ii].stimulus_class==STIMULUS_CLASS_S_MINUS) {
//			fprintf(stderr, "SMINUS stimuli[%d].substimuli = [%d][%d][%d][%d]=valid\n", ii, stimuli[ii].substimuli[0], stimuli[ii].substimuli[1], stimuli[ii].substimuli[2], stimuli[ii].substimuli[3]);

			n_stimuli_sminus++;
		}
	}

	stimuli_splus = calloc(n_stimuli_splus, sizeof(stimulus));
	stimuli_sminus = calloc(n_stimuli_sminus, sizeof(stimulus));

	int curr_stimuli_splus = 0;
	int curr_stimuli_sminus = 0;
	fprintf(stderr, "TOTAL SPLUS: %d, TOTAL SMINUS:%d\n", n_stimuli_splus, n_stimuli_sminus);
	// TOTAL SPLUS: 3136, TOTAL SMINUS:37408 (so far so good)

	for (ii=0; ii< total_stims; ii++) {
		if (stimuli[ii].stimulus_class==STIMULUS_CLASS_S_PLUS) {
			memcpy(&(stimuli_splus[curr_stimuli_splus++]), &stimuli[ii], sizeof(stimulus));
		} else if (stimuli[ii].stimulus_class==STIMULUS_CLASS_S_MINUS) {
			memcpy(&(stimuli_sminus[curr_stimuli_sminus++]), &stimuli[ii], sizeof(stimulus));
		}
	}
	
	setup_logs(stimfname, &datafp, &dsumfp, subjectid);
	
	do_trial(stimuli_splus, stimuli_sminus, datafp, dsumfp, n_stimulus_classes, subjectid);
	

	time_t curr_tt;
	struct tm *loctime;
	curr_tt = time(NULL);
	loctime = (struct tm*) localtime (&curr_tt); // XXX WTF "warning: assignment makes pointer from integer without a cast" WHERE?
	strftime (hour, 16, "%H", loctime);
	if (DEBUG){printf("atoi(hour) at loop start: %d \n", atoi(hour));}



	curr_tt = time(NULL);
	
	
	/*  Cleanup */
	
	close_soundserver(dsp_fd);
	
	if (datafp != NULL) { fclose(datafp); }
	if (dsumfp != NULL) { fclose(dsumfp); }
	return 0;
}                      


/**********************************************************************
 * feed
 *
 *
 *
 **********************************************************************/
int feed(int rv, int d)
{
  int feed_me;
  int reinf_val = rv;
  long feed_duration = d*100000;
  if(DEBUG){fprintf(stderr,"feed-> feed duration= %d\t", feed_duration);}
  if(DEBUG){fprintf(stderr,"reinf value= %d\t", reinf_val);}
  feed_me = ( 10.0*rand()/(RAND_MAX+0.0) ); 
  if(DEBUG){fprintf(stderr,"feed_me = %d\t resp_sel = %d\t dual_hopper = %d\n", feed_me, resp_sel, dual_hopper);}
  
  if (feed_me < reinf_val){
    operant_write(box_id, hopper, 1);
    usleep(feed_duration);
    operant_write(box_id, hopper, 0);
    if(DEBUG){fprintf(stderr,"feed left\n");}
    return(1);
  }
  else{return (0);}
}


/**********************************************************************
 * timeout
 *
 *
 *
 **********************************************************************/
int timeout(void)
{
  operant_write(box_id, HOUSELT, 0);
  usleep(timeout_duration);
  operant_write(box_id, HOUSELT, 1);
  return (0);
}

