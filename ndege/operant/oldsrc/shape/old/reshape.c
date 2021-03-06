/************************************************************************************
**  RESHAPE.C   same as shape.c, but without block1 and only 20 trial of blocks 2-4 
*************************************************************************************
**
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
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
#include <dio96.h>

#define SIMPLEIOMODE 0

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
#define FEEDER		231 
#define ALLOFF		255        

/* --------- OPERANT VARIABLES ---------- */
#define INTER_TRIAL_INTERVAL     2.0           /* iti in seconds */
#define RESPONSE_SAMPLE_INTERVAL 500000        /* input polling rate in nanoseconds */
#define EXP_START_TIME           6            /* hour(0-24) session will start (cannot be greater than the stop time)*/
#define EXP_END_TIME             20           /* hour(0-24) session will stop (cannot be less that the start time)*/
#define SLEEP_TIME               30           /* night loop polling interval (in sec) */
#define BLOCK_2_MAX              20          /* max number of block 2 trials */
#define BLOCK_3_MAX              20          /* max number of block 3 trials */
#define BLOCK_4_MAX              20          /* max number of block 4 trials */

int startH = EXP_START_TIME; 
int stopH = EXP_END_TIME;int sleep_interval = SLEEP_TIME;
int ctrkylt = CTRKEYLT;
int rgtkylt = RGTKEYLT;
int lftkylt = LFTKEYLT;
int hslt_only = HOUSELT;
int feed = FEEDER; 
int all_off = ALLOFF;
int block2_trial_max = BLOCK_2_MAX;
int block3_trial_max = BLOCK_3_MAX;
int block4_trial_max = BLOCK_4_MAX;


static char *bit_inA = "/dev/dio96/portaa";
static char *bit_inB = "/dev/dio96/portac";
static char *bit_inC = "/dev/dio96/portbb";
static char *bit_inD = "/dev/dio96/portca";
static char *bit_inE = "/dev/dio96/portcc";
static char *bit_inF = "/dev/dio96/portdb";

static char *bit_outA = "/dev/dio96/portab";
static char *bit_outB = "/dev/dio96/portba";
static char *bit_outC = "/dev/dio96/portbc";
static char *bit_outD = "/dev/dio96/portcb";
static char *bit_outE = "/dev/dio96/portda";
static char *bit_outF = "/dev/dio96/portdc";

static char *boxa = "A";
static char *boxb = "B";
static char *boxc = "C";
static char *boxd = "D";
static char *boxe = "E";
static char *boxf = "F";

char *bit_dev = NULL;
char *bit_out = NULL;
char *box = NULL;

unsigned char obit;

struct timespec iti = { INTER_TRIAL_INTERVAL, 0};
struct timespec rsi = { 0, RESPONSE_SAMPLE_INTERVAL};

int main(int ac, char *av[])
{
	FILE *datafp = NULL;
	char datafname[128], hour [16];
	char timebuff[64], choice[128];
	int i, dinfd, doutfd,  subjectid;

	int trial_num, block_num, vi, shunt_seed, shunt;

	long feed_duration, loop;

	double duration;

	time_t prev_tt, curr_tt;
	struct tm *loctime;
	unsigned char bits = 0;

	obit = 0;
	srand ( time (0) );	    
    


/* Parse the command line */
	
	for (i = 1; i < ac; i++)
	{
		if (*av[i] == '-')
		{
		  if (strncmp(av[i], "-A", 2) == 0)
		    {
		      bit_dev = bit_inA;                   /* assign input port for box A = port aa */
		      bit_out = bit_outA;                  /* assign output port for box A = port ab*/
		      box = boxa;		      
		      obit = 1;
		    }
		  else if (strncmp(av[i], "-B", 2) == 0)
		    {
		      bit_dev = bit_inB;                   /* assign input port for box B = ac */
		      bit_out = bit_outB;                  /* assign output port for box B  = ba */
		      box = boxb;
		      obit = 1;
		    }
		  else if (strncmp(av[i], "-C", 2) == 0)
		    {
		      bit_dev = bit_inC;                   /* assign input port for box C = bb */
		      bit_out = bit_outC;                  /* assign output port for box C  = bc */
		      box = boxc;
		      obit = 1;
		    }

		  else if (strncmp(av[i], "-D", 2) == 0)
		    {
		      bit_dev = bit_inD;                   /* assign input port for box D = ca */
		      bit_out = bit_outD;                  /* assign output port for box D  = cb */
		      box = boxd;
		      obit = 1;
		    }
		  else if (strncmp(av[i], "-E", 2) == 0)
		    {
		      bit_dev = bit_inE;                   /* assign input port for box E = cc */
		      bit_out = bit_outE;                  /* assign output port for box E  = da */
		      box = boxe;
		      obit = 1;
		    }
		  else if (strncmp(av[i], "-F", 2) == 0)
		    {
		      bit_dev = bit_inF;                   /* assign input port for box F = db */
		      bit_out = bit_outF;                  /* assign output port for box F  = dc */
		      box = boxf;
		      obit = 1;
		    }
		  else if (strncmp(av[i], "-S", 2) == 0)
		    {
		      sscanf(av[++i], "%i", &subjectid);
		    }
		  else if (strncmp(av[i], "-h", 2) == 0)
		    {
		      fprintf(stderr, "shape usage:\n");
		      fprintf(stderr, "    shape [-h] [-BOX_ID] [-Sxxx] \n\n");
		      fprintf(stderr, "        -h           = show this help message\n");
		      fprintf(stderr, "        -BOX_ID      = use box A, B, ...  Enter '-A', '-B', etc.\n");
		      fprintf(stderr, "        -Sxxx        = specifies the subject ID number\n");
		      exit(-1);
		    }
		  else
		    {
		      fprintf(stderr, "Unknown option: %s\n", av[i]);
		    }
		}
		
	}
	if (obit == 0)
	{
	  fprintf(stderr, "ERROR: You must specify the box (-A, -B ...), and subject ID (-Sxxx)!\n");
	  exit(-1);
	}

	fprintf(stderr, "Shaping session started:\n"); 
	fprintf(stderr, "   Subject ID: %i\n", subjectid);
	fprintf(stderr, "          Box: %s\n", box);


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

	//ioctl (dinfd, DIO_SETMODE, 0);		                 /* 0 == read, 1 == write */  
	//ioctl (doutfd, DIO_SETMODE, 1);

	write (dinfd, &all_off, 1);
	write (doutfd, &all_off, 1);			                 /* clear input/output ports */
        //printf("flag: dio96 I/O set up and cleared \n");

	
/*  Open & setup data logging files */

	curr_tt = time (NULL);
	loctime = localtime (&curr_tt);
	//printf("time: %s\n" , asctime (loctime));
	strftime (timebuff, 64, "%d%b%y", loctime);
        sprintf(datafname, "%i_%s.reshapeDAT", subjectid, timebuff);
	datafp = fopen(datafname, "a");
        
	if (datafp==NULL) {
          fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", datafname);
	  close(dinfd);
	  close(doutfd);
	  fclose(datafp);
	  exit(-1);
        }

/* Write data file header info */

	fprintf (datafp, "File name: %s\n", datafname);
        fprintf (datafp, "Start time: %s", asctime(loctime));
	fprintf (datafp, "Subject ID: %d\n\n", subjectid);




/*
** Block 2 **
*/

	 block_num = 2;
	 trial_num = 1;
	 feed_duration = 4000000;
	 fprintf(datafp,"\nBLOCK 2\n\n");
	 fprintf(datafp,"Blk #\tTrl #\tResponse Time\n");

	 write(doutfd, &hslt_only, 1);

	 do
	   {
	     do
	       {
		 loop = 0;
		 write (doutfd, &ctrkylt, 1);
		 do                                         
		   {
		     nanosleep(&rsi, NULL);	               	       
		     bits = 0;
		     read(dinfd, &bits, 1);		 	       
		     //printf("flag: bits value read = %d\n", bits);	 
		     
		     ++loop;
		       if ( loop % 7 == 0 )
			 {
			   if ( loop % 14 == 0 )
			     { write(doutfd, &ctrkylt, 1); }
			   else { write(doutfd, &hslt_only, 1); }
			 }
		   } while ( (bits != CENTERPECK) );  
		 
		 curr_tt = time (NULL);
		 loctime = localtime (&curr_tt);
		 //printf("time: %s\n" , asctime (loctime)); 
		 strftime (hour, 16, "%H", loctime);
		 
		 write(doutfd, &hslt_only, 1);
		 write(doutfd, &feed, 1);
		 usleep(feed_duration);
		 write(doutfd, &hslt_only, 1);
		 nanosleep(&iti, NULL);

		 fprintf(datafp, "%d\t%d\t%s\n", block_num, trial_num, asctime (loctime) );
             	 fflush (datafp);
		 
		 ++trial_num;
	       
	       } while ( (trial_num <= block2_trial_max) && (atoi(hour) >= startH) && (atoi(hour) < stopH) );
	     
	     while ( (atoi(hour) < startH) || (atoi(hour) >= stopH) )
	       {
		 write (doutfd, &all_off, 1);
		 sleep (sleep_interval);
		 curr_tt = time(NULL);
		 loctime = localtime (&curr_tt);
		 strftime (hour, 16, "%H", loctime);
	       }  
	     write(doutfd, &hslt_only, 1);
	   } while (trial_num <= block2_trial_max);


/*
** Block 3 **
*/

	 block_num = 3;
	 trial_num = 1;
	 feed_duration = 3000000;
	 fprintf(datafp,"\nBLOCK 3\n\n");
	 fprintf(datafp,"Blk #\tTrl #\tResp Key\tResp Time\n");

	 do
	   {
	     do
	       {
		 loop = 0;
		 write (doutfd, &ctrkylt, 1);
		 do                                         
		   {
		     nanosleep(&rsi, NULL);	               	       
		     bits = 0;
		     read(dinfd, &bits, 1);		 	       
		     //printf("flag: bits value read = %d\n", bits);	 
		     
		     ++loop;
		       if ( loop % 7 == 0 )
			 {
			   if ( loop % 14 == 0 )
			     { write(doutfd, &ctrkylt, 1); }
			   else { write(doutfd, &hslt_only, 1); }
			 }
		   } while ( (bits != CENTERPECK) );  
		 
		 
		 shunt_seed = (10.0)*rand()/(RAND_MAX+0.0);
		 shunt = shunt_seed % 2;
		 if (shunt == 0)
		   {
		     strcpy (choice, "right");
		     loop = 0;
		     write (doutfd, &rgtkylt, 1); 
		     do                                         
		       {
			 nanosleep(&rsi, NULL);	               	       
			 bits = 0;
			 read(dinfd, &bits, 1);		 	       
			 //printf("flag: bits value read = %d\n", bits);	 
			 
			 ++loop;
			   if ( loop % 7 == 0 )
			     {
			       if ( loop % 14 == 0 )
				 { write(doutfd, &rgtkylt, 1); }
			       else { write(doutfd, &hslt_only, 1); }
			     }
		       } while ( (bits != RIGHTPECK) );
		   }
		 else
		   {
		    strcpy (choice, "left");
		     loop = 0;
		     write (doutfd, &lftkylt, 1);
		       do                                         
			 {
			   nanosleep(&rsi, NULL);	               	       
			   bits = 0;
			   read(dinfd, &bits, 1);   
			   //printf("flag: bits value read = %d\n", bits);	 
	 			   ++loop;
			     if ( loop % 7 == 0 )
			       {
				 if ( loop % 14 == 0 )
				   { write(doutfd, &lftkylt, 1); }
				 else { write(doutfd, &hslt_only, 1); }
			       }
			 } while ( (bits != LEFTPECK) );
		   }
		 
		 curr_tt = time (NULL);
		 loctime = localtime (&curr_tt);
		 //printf("time: %s\n" , asctime (loctime)); 
		 strftime (hour, 16, "%H", loctime);
		 
		 write(doutfd, &hslt_only, 1);
		 write(doutfd, &feed, 1);
		 usleep(feed_duration);
		 write(doutfd, &hslt_only, 1);
		 nanosleep(&iti, NULL);

		 fprintf(datafp, "%d\t%d\t%s\t%s\n", block_num, trial_num, choice, asctime (loctime) );
		 fflush (datafp);
		 
		 ++trial_num;

	       } while ( (trial_num <= block3_trial_max) && (atoi(hour) >= startH) && (atoi(hour) < stopH) );
	     
	     while ( (atoi(hour) < startH) || (atoi(hour) >= stopH) )
	       {
		 write (doutfd, &all_off, 1);
		 sleep (sleep_interval);
		 curr_tt = time(NULL);
		 loctime = localtime (&curr_tt);
		 strftime (hour, 16, "%H", loctime);
	       }  
	     write(doutfd, &hslt_only, 1);
	   } while (trial_num <= block3_trial_max);



/*
** Block 4 **
*/

	 block_num = 4;
	 trial_num = 1;
	 feed_duration = 2500000;
	 fprintf(datafp,"\nBLOCK 4\n\n");
	 fprintf(datafp,"Blk #\tTrl #\tResp Key\tResp Time\n");

	 do
	   {
	     do
	       {
		 do                                         
		   {
		     nanosleep(&rsi, NULL);	               	       
		     bits = 0;
		     read(dinfd, &bits, 1);		 	       
		     //printf("flag: bits value read = %d\n", bits);	 
		   } while ( (bits != CENTERPECK) );  
		 
		 shunt_seed = (10.0)*rand()/(RAND_MAX+0.0);
		 shunt = shunt_seed % 2 ;
		 if (shunt == 0)
		   {
		     strcpy (choice, "right");
		     loop = 0;
		     write (doutfd, &rgtkylt, 1); 
		     do                                         
		       {
			 nanosleep(&rsi, NULL);	               	       
			 bits = 0;
			 read(dinfd, &bits, 1);		 	       
			 //printf("flag: bits value read = %d\n", bits);	 
			 
			 ++loop;
			   if ( loop % 7 == 0 )
			     {
			       if ( loop % 14 == 0 )
				 { write(doutfd, &rgtkylt, 1); }
			       else { write(doutfd, &hslt_only, 1); }
			     }
		       } while ( (bits != RIGHTPECK) );
		   }
		 else
		   {
		     strcpy (choice, "left");
		     loop = 0;
		     write (doutfd, &lftkylt, 1);
		       do                                         
			 {
			   nanosleep(&rsi, NULL);	               	       
			   bits = 0;
			   read(dinfd, &bits, 1);		 	       
			   //printf("flag: bits value read = %d\n", bits);	 
		     
			   ++loop;
			     if ( loop % 7 == 0 )
			       {
				 if ( loop % 14 == 0 )
				   { write(doutfd, &lftkylt, 1); }
				 else { write(doutfd, &hslt_only, 1); }
			       }
			 } while ( (bits != LEFTPECK) );
		   }
		 
		 curr_tt = time (NULL);
		 loctime = localtime (&curr_tt);
		 //printf("time: %s\n" , asctime (loctime)); 
		 strftime (hour, 16, "%H", loctime);
		 
		 write(doutfd, &hslt_only, 1);
		 write(doutfd, &feed, 1);
		 usleep(feed_duration);
		 write(doutfd, &hslt_only, 1);
		 nanosleep(&iti, NULL);

		 fprintf(datafp, "%d\t%d\t%s\t%s\n", block_num, trial_num, choice, asctime (loctime) );
		 fflush (datafp);

		 ++trial_num;

	       } while ( (trial_num <= block4_trial_max) && (atoi(hour) >= startH) && (atoi(hour) < stopH) );
	     
	     while ( (atoi(hour) < startH) || (atoi(hour) >= stopH) )
	       {
		 write (doutfd, &all_off, 1);
		 sleep (sleep_interval);
		 curr_tt = time(NULL);
		 loctime = localtime (&curr_tt);
		 strftime (hour, 16, "%H", loctime);
	       }  
	     write(doutfd, &hslt_only, 1);
	   } while (trial_num <= block4_trial_max);


	 curr_tt = time(NULL);
	 printf ("SHAPING IN BOX %s COMPLETE!\t%s\n", box, asctime (localtime(&curr_tt)) );
	 fprintf(datafp,"SHAPING COMPLETE!\t%s\n", asctime (localtime(&curr_tt)) );
	 write (doutfd, &all_off, 1);      
      

/*  Cleanup */

	 close(doutfd);
	 fclose(datafp);
	 return 0;
}






















