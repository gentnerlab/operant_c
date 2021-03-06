/*
**  HISTORY:
**  09/16/97 PJ  Extensive modifications to support multiple channels and either soundcard
**               or chorus output
**  03/11/99 ASD Cleanup/rewrite.  Changed pin assignments.  Made possible
**               to run two behave's simultaneously (on different soundcards)
**               Eliminate chorus output option, related cruft.  Nicer
**		 command line arguments.
**  04/99    TQG  Adapted from behave.c to run 2 3key/1hopper operant panels on 1-interval yes/no
**               auditory discrimination task. Uses 2 soundblaster cards and the dio96 I/O card.
**                  
**               trial sequence: select stim at random, wait for center key peck, play stim, wait for left or 
**               right key peck, consequate response, log trial data in output file, wait for iti, loop for
**               correction trials, select new stim for non-correction trials.
**               the session ends when the lights go off.  A new session begins when the lights come on the
**               following day.  A block of sessions ends when the pre determined number of trials have been run.  
**
** 11/11/99 TQG Further adapted from ivr5.c to run a shaping routine in the operant chamber that will teach an 
**              to peck the center key to hear a stimulus, the peck one of the side keys for reward.
**              training sequence:
**                 Block 1:  Hopper comes up on VI (stays up for 5 s) for the first day 
**                           that the animal is in the apparatus. Center key flashes for 5 sec, prior 
**                           to the hopper access. If the center key is pressed while flashing, then 
**                           the hopper comes up and then the session jumps to block 2 immediately.
**
**                 Block 2:  The center key flashes until pecked.  When pecked the hopper comes up for 
**                           4 sec. Run 100 trials.
**
**                 Block 3:  The center key flashes until pecked, then either the right or left (p = .5)
**                           key flashes until pecked, then the hopper comes up for 3 sec. Run 100 trials. 
**      
**                 Block 4:  Wait for peck to non-flashing center key, then right or left key flashes 
**                           until pecked, then food for 2.5 sec.   Run 100 trials.
**
** 9-14-01 TQG Modified to accomodate go/nogo terminal procedure along with one or two hopper 2choice procedures
**             Go/Nogo shaping works like this:
**                 Block 1:  Hopper comes up on VI (stays up for 5 s) for the first day 
**                           that the animal is in the apparatus. Center key flashes for 5 sec, prior 
**                           to the hopper access. If the center key is pressed while flashing, then 
**                           the hopper comes up and then the session jumps to block 2 immediately.
**                 Block 2:  The center key flashes until pecked.  When pecked the hopper comes up for 
**                           4 sec. Run 100 trials.
**                 Block 3:  Wait for a peck to non-flashing center key, when you get it, the hopper 
**                           comes up for 2.5 sec. Run 100 trials. 
**
**                 NOTE: when you run the go/nog procedure in a 2 hopper apparatus, it uses only the 
**                       right hand key and hopper.  If you do this often, you may want to add the
**                       facility for use of the left hand key and hopper.       
*/


#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <math.h>
#include <time.h>

#include "/usr/local/src/operantio/operantio.c"

#define DEBUG 0

#define timersub(a, b, result)                                               \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                          \
    if ((result)->tv_usec < 0) {                                              \
      --(result)->tv_sec;                                                     \
      (result)->tv_usec += 1000000;                                           \
    }                                                                         \
  } while (0)

/* --- I/O CHANNELS ---*/
#define LEFTPECK   1
#define CENTERPECK 2
#define RIGHTPECK  3

#define LFTKEYLT  1  
#define CTRKEYLT  2  
#define RGTKEYLT  3  
#define HOUSELT   4   
#define FEEDER    5

 

/* --------- OPERANT VARIABLES ---------- */
#define RESPONSE_SAMPLE_INTERVAL 5000000      /* input polling rate in nanoseconds */
#define EXP_START_TIME           6            /* hour(0-24) session will start (cannot be greater than the stop time)*/
#define EXP_END_TIME             20           /* hour(0-24) session will stop (cannot be less that the start time)*/
#define SLEEP_TIME               30           /* night loop polling interval (in sec) */
#define BLOCK_2_MAX              100          /* max number of block 2 trials */
#define BLOCK_3_MAX              100          /* max number of block 3 trials */
#define BLOCK_4_MAX              100          /* max number of block 4 trials */

int startH = EXP_START_TIME; 
int stopH = EXP_END_TIME;int sleep_interval = SLEEP_TIME;
int block2_trial_max = BLOCK_2_MAX;
int block3_trial_max = BLOCK_3_MAX;
int block4_trial_max = BLOCK_4_MAX;

const char exp_name[] = "SHAPE";

int box_id = -1;
int dual_hopper = 0;

/* trial block functions */
int gng_block_one(void);
int gng_block_two(void);
int gng_block_three(void);
int twochoice__block_one(void);
int twochoice__block_two(void);
int twochoice_block_three(void);
int twochoice_block_four(void);

struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};

FILE *datafp = NULL;
char datafname[128], hour [16];
char timebuff[64], choice[128];

int trial_num, block_num, hopx, shunt_seed, shunt, iti;
int left = 0, right= 0, center = 0;

long feed_duration, loop;
double duration;

struct timeval time0, time1, dur;
time_t curr_tt, prev_tt;
struct tm * loctime;
    
    
int main(int ac, char *av[])
{
 if(DEBUG){printf("into main\n");}

int i, subjectid, t_proc = -1;
srand ( time (0) );	

  /* Parse the command line */
	
  for (i = 1; i < ac; i++)
    {
      if (*av[i] == '-')
	{
	  if (strncmp(av[i], "-B", 2) == 0){ 
	    sscanf(av[++i], "%i", &box_id);
	    if(DEBUG){printf("box number = %d\n", box_id);}
	  }
	  else if (strncmp(av[i], "-S", 2) == 0){
	    sscanf(av[++i], "%i", &subjectid);
	    if(DEBUG){printf("subject ID number = %d\n", subjectid);}
	  }
	  else if (strncmp(av[i], "-T", 2) == 0){
	    sscanf(av[++i], "%i", &t_proc);
	    if(DEBUG){printf("t_proc = %d\n", t_proc);}
	  }
	  else if (strncmp(av[i], "-h", 2) == 0){
	    fprintf(stderr, "shape usage:\n");
	    fprintf(stderr, "    shape [-h] [-B x] [-T x] [-S x] \n\n");
	    fprintf(stderr, "        -h           = show this help message\n");
	    fprintf(stderr, "        -B x         = use '-B 1' '-B 2' ... '-B 12' for box 1-12\n");
	    fprintf(stderr, "                       Note: shape cannot run in box 6\n");
	    fprintf(stderr, "        -S x         = specifies the subject ID number\n");
	    fprintf(stderr, "        -T x         = specifies the terminal procedure\n");
	    fprintf(stderr, "                     use: '-T 1' for go/nogo or '-T 2' for 2choice\n");
	    exit(-1);
	    }
	  else
	    {
	      fprintf(stderr, "Unknown option: %s\n", av[i]);  
	      fprintf(stderr, "Try '2choice -h' for help\n");
	    }
	}
    }
  if (box_id < 0)
    {
      fprintf(stderr, "\tYou must enter a box ID!: %s \n", av[i]); 
      fprintf(stderr, "\tERROR: try '2choice -help' for help\n");
      exit(-1);
    }
  if (t_proc < 0)
    {
      fprintf(stderr, "\tERROR: INVALID TERMINAL PROCEDURE\n\ttry 'shape -h' for help\n");
      exit(-1);
    }

/* Initialize box */
  else
    {
      printf("Initializing box #%d...", box_id);
      if (operant_open()!=0){
	fprintf(stderr, "Problem opening IO interface\n");
	exit (-1);
      }
      operant_clear(box_id);
      printf("done\n");
    }

  fprintf(stderr, "Shaping session started:\n"); 
  fprintf(stderr, "   Subject ID: %d\n", subjectid);
  fprintf(stderr, "          Box: %d\n", box_id);
  if (t_proc == 1){
    fprintf(stderr, "          GO/NOGO terminal procedure\n\n");}
  if (t_proc == 2){
    fprintf(stderr, "          2CHOICE terminal procedure\n\n");}


  /*Open & setup data logging files */
  
  time (&curr_tt);
  loctime = localtime (&curr_tt); 
  strftime (timebuff, 64, "%d%b%y", loctime);
  sprintf(datafname, "%i_%s.shapeDAT", subjectid, timebuff);
  datafp = fopen(datafname, "a");
        
  if (datafp==NULL) {
    fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", datafname);
    fclose(datafp);
    operant_close();
    exit(-1); }

  /* Write data file header info */
  fprintf (datafp, "File name: %s\n", datafname);
  fprintf (datafp, "Start time: %s", asctime(loctime));
  fprintf (datafp, "Subject ID: %d\n\n", subjectid);



  /*Run the appropriate shaping sequence */

  if(DEBUG){printf("start sequence list\n");}	

  if (t_proc == 1){   /* run the go/nogo shaping sequence */
    gng_block_one(); 
    gng_block_two();
    gng_block_three();

  }
  if (t_proc == 2){   /* run the 2choice shaping sequence */
    twochoice_block_one();
    twochoice_block_two();
    twochoice_block_three();
    twochoice_block_four();
  }
  if (t_proc == 3){   /* run the last go/nogo shaping sequence forever*/
    block3_trial_max = 100000;
    gng_block_three();

  }

  /*Cleanup */
  time(&curr_tt);
  printf ("SHAPING IN BOX '%d' COMPLETE!\t%s\n", box_id, ctime(&curr_tt) );
  fprintf(datafp,"SHAPING COMPLETE!\t%s\n", ctime(&curr_tt) );
  operant_clear(box_id);
  fclose(datafp);
  return 0;
}


/*2choice block 1 */
int twochoice_block_one(void)
{
  int vi; 
  
  block_num = 1;
  trial_num = 1;
  feed_duration = 5000000;
  fprintf(datafp,"\nBLOCK 1\n\n");
  fprintf(datafp,"Blk #\tTrl #\tAcces Time\n");
  
  operant_write(box_id, HOUSELT, 1);
 
  do
    {
      if(DEBUG){printf("trial number = %d\n", trial_num);}
      vi =  ( (30.0)*rand()/(RAND_MAX+0.0) ) + 10 ; 
      sleep (vi);
      gettimeofday(&time0, NULL);
      operant_write(box_id, CTRKEYLT, 1);
      loop = 0;  center = 0;
      do                                         
	{
	  nanosleep(&rsi, NULL);	               	       
	  center = operant_read(box_id, CENTERPECK);   /*get value at center peck position*/ 
	  if(DEBUG==2){printf("flag: bits value read = %d\n", center);}	 
	  ++loop;
	  if ( loop % 25 == 0 )
	    {
	      if ( loop % 50 == 0 )
		{operant_write(box_id, CTRKEYLT, 1); }
	      else {operant_write(box_id, CTRKEYLT, 0); }
	    }
	  gettimeofday(&time1, NULL);
	  timersub (&time1, &time0, &dur); 
	  if (DEBUG==2){printf("dur_tt=[%d/%d]\n", dur.tv_sec, dur.tv_usec);}
	} while ( (center == 0) && (dur.tv_sec < 5) );  
	    
      operant_write(box_id, CTRKEYLT, 0);
      time(&curr_tt);
      loctime = localtime (&curr_tt);
      if(DEBUG){printf("time: %s\n", ctime(&curr_tt));} 
      strftime (hour, 16, "%H", loctime);
      

      /*raise the hopper*/
      operant_write(box_id, FEEDER, 1);
      usleep(feed_duration);
      operant_write(box_id, FEEDER, 0);
      

      /*output to data file */
      fprintf(datafp, "%d\t%d\t%s\n", block_num, trial_num, ctime(&curr_tt) );
      fflush (datafp);
      
      ++trial_num;
      
    } while ( (center==0) && (atoi(hour) >= startH) && (atoi(hour) < stopH) );
  
  while ((atoi(hour) >= stopH) || (atoi(hour) < startH) )
    {
      operant_clear(box_id);
      sleep (sleep_interval);
      time(&curr_tt);
      loctime = localtime (&curr_tt);
      strftime (hour, 16, "%H", loctime);
      if(DEBUG){printf("sleep loop - curr_time: %s - hour: %s\n" , ctime(&curr_tt), hour );}
    }  
  operant_write(box_id, HOUSELT, 1);
  return(1);
}



/* 2choice block 2 */
int twochoice_block_two(void)
{
  iti = 2;
  block_num = 2;
  trial_num = 1;
  feed_duration = 4000000;
  fprintf(datafp,"\nBLOCK 2\n\n");
  fprintf(datafp,"Blk #\tTrl #\tResponse Time\n");
  
  operant_write(box_id, HOUSELT, 1);

  do{
    do{
      loop = 0;
      operant_write (box_id, CTRKEYLT, 1);
      center = 0;

      do{
	nanosleep(&rsi, NULL);	               	       
	center = operant_read(box_id, CENTERPECK);
	++loop;
	if ( loop % 25 == 0 )
	  {
	    if ( loop % 50 == 0 )
	      {operant_write(box_id, CTRKEYLT, 1); }
	    else {operant_write(box_id, CTRKEYLT, 0); }
	  }
      } while (center == 0);   
      
      time(&curr_tt);
      loctime = localtime (&curr_tt);
      if(DEBUG){printf("time: %s\n" , ctime(&curr_tt));} 
      strftime (hour, 16, "%H", loctime);
      operant_write(box_id, CTRKEYLT, 0);

      /*raise a hopper */	
      operant_write(box_id, FEEDER, 1);
      usleep(feed_duration);
      operant_write(box_id, FEEDER, 0);
     
      
      /*output trial data */
      fprintf(datafp, "%d\t%d\t%s\n", block_num, trial_num, ctime(&curr_tt) );
      fflush (datafp);
      sleep(iti);
      ++trial_num;
      
    } while ( (trial_num <= block2_trial_max) && (atoi(hour) >= startH) && (atoi(hour) < stopH) );
	     
    while ( (atoi(hour) < startH) || (atoi(hour) >= stopH) )
      {
	operant_clear(box_id);
	sleep (sleep_interval);
	time(&curr_tt);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
      }  
    operant_write(box_id, HOUSELT, 1);
  } while (trial_num <= block2_trial_max);

  return(1);
}


/* 2choice block 3 */
int twochoice_block_three(void)
{
  iti = 2;
  block_num = 3;
  trial_num = 1;
  feed_duration = 3000000;
  fprintf(datafp,"\nBLOCK 3\n\n");
  fprintf(datafp,"Blk #\tTrl #\tResp Key\tResp Time\n");
  
  operant_write(box_id, HOUSELT, 1);

  do{
    do{
      loop = 0;
      center = 0;
      operant_write (box_id, CTRKEYLT, 1);
      
      do{
	nanosleep(&rsi, NULL);	               	       
	center = operant_read(box_id, CENTERPECK);
	++loop;
	if ( loop % 25 == 0 )
	  {
	    if ( loop % 50 == 0 )
	      {operant_write(box_id, CTRKEYLT, 1); }
	    else {operant_write(box_id, CTRKEYLT, 0); }
	  }
      } while (center == 0);   
      operant_write(box_id, CTRKEYLT, 0);
	   

      shunt_seed = (10.0)*rand()/(RAND_MAX+0.0);
      shunt = shunt_seed % 2;
      if (shunt == 0)
	{
	  strcpy (choice, "right");
	  loop = 0;
	  operant_write (box_id, RGTKEYLT, 1); 
	  right = 0;

	  do{
	    nanosleep(&rsi, NULL);	               	       
	    right = operant_read(box_id, RIGHTPECK);		 	       
	    ++loop;
	    if ( loop % 25 == 0 )
	      {
		if ( loop % 50 == 0 )
		  {operant_write(box_id, RGTKEYLT, 1); }
		else {operant_write(box_id, RGTKEYLT, 0); }
	      }
	  } while (right==0);
	  operant_write(box_id, RGTKEYLT, 0);

	  time(&curr_tt);
	  loctime = localtime (&curr_tt);
	  if(DEBUG){printf("time: %s\n" , ctime(&curr_tt));} 
	  strftime (hour, 16, "%H", loctime);
	  
	  operant_write(box_id, FEEDER, 1);
	  usleep(feed_duration);
	  operant_write(box_id, FEEDER, 0);
	 
	}
      else
	{
	  strcpy (choice, "left");
	  loop = 0;
	  operant_write (box_id, LFTKEYLT, 1);
	  left = 0;

	  do{
	    nanosleep(&rsi, NULL);	               	       
	    left = operant_read(box_id, LEFTPECK);   
	    ++loop;
	    if ( loop % 25 == 0 )
	      {
		if ( loop % 50 == 0 )
		  {operant_write(box_id, LFTKEYLT, 1); }
		else {operant_write(box_id, LFTKEYLT, 0); }
	      }
	  } while (left==0);
	  operant_write(box_id, LFTKEYLT, 0);

	  time(&curr_tt);
	  loctime = localtime (&curr_tt);
	  if(DEBUG){printf("time: %s\n", ctime(&curr_tt));} 
	  strftime (hour, 16, "%H", loctime);
	  
	  operant_write(box_id, FEEDER, 1);
	  usleep(feed_duration);
	  operant_write(box_id, FEEDER, 0);
	}
 
      /* output trial data */
      fprintf(datafp, "%d\t%d\t%s\t%s\n", block_num, trial_num, choice, ctime(&curr_tt) );
      fflush (datafp);
      sleep(iti);
      ++trial_num;
      
    } while ( (trial_num <= block3_trial_max) && (atoi(hour) >= startH) && (atoi(hour) < stopH) );
	     
    while ( (atoi(hour) < startH) || (atoi(hour) >= stopH) )
      {
	operant_clear(box_id);
	sleep (sleep_interval);
	time(&curr_tt);
	loctime =  localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
      }  
    operant_write(box_id, HOUSELT, 1);
  } while (trial_num <= block3_trial_max);

  return(1);
}


/* 2choice block 4 */
int twochoice_block_four(void)
{
  iti = 2;
  block_num = 4;
  trial_num = 1;
  feed_duration = 2500000;
  fprintf(datafp,"\nBLOCK 4\n\n");
  fprintf(datafp,"Blk #\tTrl #\tResp Key\tResp Time\n");

  operant_write(box_id, HOUSELT, 1);

  do{
    do{
      center = 0;
      do{                                         
	nanosleep(&rsi, NULL);	               	       
	center = operant_read(box_id, CENTERPECK);
      } while (center == 0);   
      
      shunt_seed = (10.0)*rand()/(RAND_MAX+0.0);
      shunt = shunt_seed % 2 ;
      if (shunt == 0)
	{
	  strcpy (choice, "right");
	  loop = 0;
	  operant_write (box_id, RGTKEYLT, 1); 
	  right = 0;

	  do{
	    nanosleep(&rsi, NULL);	               	       
	    right = operant_read(box_id, RIGHTPECK);		 	       
	    ++loop;
	    if ( loop % 25 == 0 )
	      {
		if ( loop % 50 == 0 )
		  {operant_write(box_id, RGTKEYLT, 1); }
		else {operant_write(box_id, RGTKEYLT, 0); }
	      }
	  } while (right==0);
	  operant_write(box_id, RGTKEYLT, 0);

	  time(&curr_tt);
	  loctime = localtime (&curr_tt);
	  if(DEBUG){printf("time: %s\n" , ctime(&curr_tt));} 
	  strftime (hour, 16, "%H", loctime);
      	 
	  operant_write(box_id, FEEDER, 1);
	  usleep(feed_duration);
	  operant_write(box_id, FEEDER, 0);
	}
      else
	{
	  strcpy (choice, "left");
	  loop = 0;
	  operant_write (box_id, LFTKEYLT, 1);
	  left = 0;
	  
	  do{
	    nanosleep(&rsi, NULL);	               	       
	    left = operant_read(box_id, LEFTPECK);   
	    ++loop;
	    if ( loop % 25 == 0 )
	      {
		if ( loop % 50 == 0 )
		  {operant_write(box_id, LFTKEYLT, 1); }
		else {operant_write(box_id, LFTKEYLT, 0); }
	      }
	  } while (left==0);
	  operant_write(box_id, LFTKEYLT, 0);

	  time(&curr_tt);
	  loctime = localtime (&curr_tt);
	  if(DEBUG){printf("time: %s\n" , ctime(&curr_tt));} 
	  strftime (hour, 16, "%H", loctime);

	  operant_write(box_id, FEEDER, 1);
	  usleep(feed_duration);
	  operant_write(box_id, FEEDER, 0);
	}     

      /*output trial data */
      fprintf(datafp, "%d\t%d\t%s\t%s\n", block_num, trial_num, choice, asctime (loctime) );
      fflush (datafp);
      sleep(iti);
      ++trial_num;
      
    } while ( (trial_num <= block4_trial_max) && (atoi(hour) >= startH) && (atoi(hour) < stopH) );
	     
    while ( (atoi(hour) < startH) || (atoi(hour) >= stopH) )
      {
	operant_clear(box_id);
	sleep (sleep_interval);
	time(&curr_tt);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
      }  
    operant_write(box_id, HOUSELT, 1);
  } while (trial_num <= block4_trial_max);

  return (1);
}


/* go/nogo block 1 */
int gng_block_one(void)
{
  int vi, target, targetkeylt, targetpeck, targetfeed; 

  block_num = 1;
  trial_num = 1;
  feed_duration = 5000000;
  fprintf(datafp,"\nBLOCK 1\n\n");
  fprintf(datafp,"Blk #\tTrl #\tAcces Time\n");
    
  operant_write(box_id, HOUSELT, 1); 
    
  targetpeck = CENTERPECK;
  targetkeylt = CTRKEYLT;
  targetfeed = FEEDER;
  
    do{
    if(DEBUG){printf("trial number = %d\n", trial_num);}
    vi =  ( (30.0)*rand()/(RAND_MAX+0.0) ) + 10 ; 
    sleep (vi);
    loop = 0; target = 0;
    operant_write(box_id, targetkeylt, 1);
    gettimeofday(&time0, NULL);
    do{
      nanosleep(&rsi, NULL);	               	       
      target = operant_read(box_id, targetpeck);   /*get value at target peck position */ 
      ++loop;
      if(DEBUG==2){printf( "loop= %d\n", loop);}
      if ( loop % 25 == 0 )
	{
	  if ( loop % 50 == 0 )
	    {operant_write(box_id, targetkeylt, 1); }
	  else {operant_write(box_id, targetkeylt, 0); }
	}
      gettimeofday(&time1, NULL);
      timersub (&time1, &time0, &dur); 
      if (DEBUG==2){printf("dur_tt=[%d/%d]\n", dur.tv_sec, dur.tv_usec);}
    } while ( (target == 0) && (dur.tv_sec < 5) );  
	
    operant_write(box_id, targetkeylt, 0);
    time (&curr_tt);
    loctime = localtime (&curr_tt);
    if(DEBUG){printf("time: %s\n" , ctime(&curr_tt));}
    strftime (hour, 16, "%H", loctime);
    

    /*raise the hopper*/
    operant_write(box_id, targetfeed, 1);
    usleep(feed_duration);
    operant_write(box_id, targetfeed, 0);
  
    /*output to data file */
    fprintf(datafp, "%d\t%d\t%s\n", block_num, trial_num, asctime (loctime) );
    fflush (datafp);
    
    ++trial_num;
    
  } while ( (target==0) && (atoi(hour) >= startH) && (atoi(hour) < stopH) );
  
  while ((atoi(hour) >= stopH) || (atoi(hour) < startH) )
    {
      operant_clear(box_id);
      sleep (sleep_interval);
      time(&curr_tt);
      loctime = localtime (&curr_tt);
      strftime (hour, 16, "%H", loctime);
      if(DEBUG){printf("sleep loop - curr_time: %s - hour: %s\n" , asctime (loctime), hour );}
    }  
  operant_write(box_id, HOUSELT, 1);
  return(1);
}


/* go/nogo block 2 */
int gng_block_two(void)
{
  int target, targetkeylt, targetpeck, targetfeed;

  iti = 2;
  block_num = 2;
  trial_num = 1;
  feed_duration = 4000000;
  fprintf(datafp,"\nBLOCK 2\n\n");
  fprintf(datafp,"Blk #\tTrl #\tResponse Time\n");

  targetpeck = CENTERPECK;
  targetkeylt = CTRKEYLT;
  targetfeed = FEEDER;
  
  operant_write(box_id, HOUSELT, 1);

  do{
    do{
      loop = 0;
      operant_write (box_id, targetkeylt, 1);
      target = 0;
      
      do{
	nanosleep(&rsi, NULL);	               	       
	target = operant_read(box_id, targetpeck);
	++loop;
	if ( loop % 25 == 0 )
	  {
	    if ( loop % 50 == 0 )
	      {operant_write(box_id, targetkeylt, 1); }
	    else {operant_write(box_id,  targetkeylt, 0); }
	  }
      } while (target == 0);   

      operant_write(box_id, targetkeylt, 0);
      time (&curr_tt);
      loctime = localtime (&curr_tt);
      if(DEBUG){printf("time: %s\n" , ctime(&curr_tt));} 
      strftime (hour, 16, "%H", loctime);
      
      /*raise a hopper */	
      operant_write(box_id, targetfeed, 1);
      usleep(feed_duration);
      operant_write(box_id, targetfeed, 0);
            
      /*output trial data */
      fprintf(datafp, "%d\t%d\t%s\n", block_num, trial_num, asctime (loctime) );
      fflush (datafp);
      sleep(iti);
      ++trial_num;
      
    } while ( (trial_num <= block2_trial_max) && (atoi(hour) >= startH) && (atoi(hour) < stopH) );
	     
    while ( (atoi(hour) < startH) || (atoi(hour) >= stopH) )
      {
	operant_clear(box_id);
	sleep (sleep_interval);
	time(&curr_tt);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
      }  
    operant_write(box_id, HOUSELT, 1);
  } while (trial_num <= block2_trial_max);
  
  return(1);
}


/* go/nogo block 3 */
int gng_block_three(void)
{
  int target, targetkeylt, targetpeck, targetfeed;

  iti = 2;
  block_num = 3;
  trial_num = 1;
  feed_duration = 2500000;
  fprintf(datafp,"\nBLOCK 3\n\n");
  fprintf(datafp,"Blk #\tTrl #\tResponse Time\n");

  operant_write(box_id, HOUSELT, 1);

  targetpeck = CENTERPECK;
  targetkeylt = CTRKEYLT;
  targetfeed = FEEDER;
  
  do{
    do{
      
      target = 0;
      do{
	nanosleep(&rsi, NULL);	               	       
	target = operant_read(box_id, targetpeck);
      } while (target == 0);   
      
      time (&curr_tt);
      loctime = localtime (&curr_tt);
      if(DEBUG){printf("time: %s\n" , ctime (&curr_tt));} 
      strftime (hour, 16, "%H", loctime);
      
      /*raise the hopper*/
      operant_write(box_id, targetfeed, 1);
      usleep(feed_duration);
      operant_write(box_id, targetfeed, 0);
      
      
      /*output trial data */
      fprintf(datafp, "%d\t%d\t%s\n", block_num, trial_num, ctime (&curr_tt) );
      fflush (datafp);
      sleep(iti);
      ++trial_num;
      
    } while ( (trial_num <= block3_trial_max) && (atoi(hour) >= startH) && (atoi(hour) < stopH) );
	     
    while ( (atoi(hour) < startH) || (atoi(hour) >= stopH) )
      {
	operant_clear(box_id);
	sleep (sleep_interval);
	time(&curr_tt);
	loctime = localtime (&curr_tt);
	strftime (hour, 16, "%H", loctime);
      }  
    operant_write(box_id, HOUSELT, 1);
  } while (trial_num <= block3_trial_max);

  return(1);
}
