/****************************************************************************
**    Xfemxpeck.c -- use to analyse data from femxpeck procedures
*****************************************************************************
**
**
**
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define BLOCK_SIZE  20                  /* default number of trials per block */
#define PLOT_OUTPUT 0                    /* default '0' for no gnuplot output */
#define MAXSTIMCLASS 64                  /* default max number of stimulus classes */
#define LAST_BLOCK 0                     /* default '0' , don't count the trials in the last (incomplete) block */
#define CORRECTION 0                     /* default '0', don't count data from correction trials */
#define NMAXSTIMS 256                     /* default max number of stimuli */

#define DEBUG 0

int blocksize = BLOCK_SIZE;
int plot_out = PLOT_OUTPUT;
int lastblock = LAST_BLOCK;
int subjectid = 0;
int correction = CORRECTION;

int main (int ac, char *av[])
{
  int sess_num, tot_trls, ses_trls, stim_class, block_num, offset, swap;
  int Lresp, Cresp, Rresp, resp_tod, resp_date, freq, rate, duration;
  int trial_rep, reinf_val, i,j, class, n_stimclass, nstims,docsv=0;
  int printstimcount=0, bystim=0, ngroups=0,stim_group=0;
  FILE *outfp = NULL, *infp = NULL, *plotfp = NULL, *stimfp = NULL;
  char *stimfname = NULL;
  char infilename[128], outfname[128], plotfname[128], line[256], exemplar[64];
  char stimf[128], stimfline[256], stimfroot [128];

  struct stim {
    char exem[128];
    int class;
  }stimlist[NMAXSTIMS];
  
  struct outdata{
    int left;
    int center;
    int right;
    int trls;
  } block[NMAXSTIMS], total[NMAXSTIMS];


/* Parse the command line */
	
	for (i = 1; i < ac; i++)
	  {
	    if (*av[i] == '-')
	      {
		if (strncmp(av[i], "-S", 2) == 0)
		  {
		    sscanf(av[++i], "%d", &subjectid);
		  }
		else if (strncmp(av[i], "-b", 2) == 0)
		  {
		    sscanf(av[++i], "%d", &blocksize); 
		  }
		else if (strncmp(av[i], "-c", 2) == 0)
		  {
		   lastblock = 1; 
		  }
		else if (strncmp(av[i], "-Z", 4) == 0)
		  {
		   fprintf(stderr, "docsv: %d\n", docsv);
		   docsv = 1; 
		   fprintf(stderr, "docsv: %d\n", docsv);
		  }
		else if (strncmp(av[i], "-v", 2) == 0)
		  {
		   printstimcount = 1; 
		  }
		else if (strncmp(av[i], "-p", 2) == 0)
		  {
		    plot_out = 1;
		    fprintf(stderr, "\nGenerating output file for gnuplot\n");
		  }
		else if (strncmp(av[i], "-m", 2) == 0)
                  {
                    bystim = 1;
                    fprintf(stderr, "Sorting by stimulus exemplar\n");
                  }
		else if (strncmp(av[i], "-h", 2) == 0)
		  {
		    fprintf(stderr, "Xfemxpeck usage:\n");
		    fprintf(stderr, "    Xfemxpeck [-h][-p][-c][-Z][-v][-s][-b <value>] [-S <value>] <stimfile>\n\n");
		    fprintf(stderr, "     -h          = show this help message\n");
		    fprintf(stderr, "     -p          = generate output for gnuplot\n");
		    fprintf(stderr, "     -c          = count the trials in the last (even if it is incomplete) block\n");
		    fprintf(stderr, "     -Z          = output XDAT as comma separated variable file\n");
		    fprintf(stderr, "     -v          = output to screen the total number of trials with each stimulus group\n");
		    fprintf(stderr, "     -m          = sort the data by stimulus exemplars\n\n");
		    fprintf(stderr, "     -b int      = specify  trial block size (default = 20)\n\n");
		    fprintf(stderr, "     -S int      = specify the subject ID number (required)\n");
		    fprintf(stderr, "     stimfile    = specify stimulus filename (required)\n");
		    exit(-1);
		  }
		else
		  {
		    fprintf(stderr, "Unknown option: %s.  Use '-h' for help.\n", av[i]);
		    exit (-1);
		  }
	      }
	    else
	      {
		stimfname = av[i];
		if(DEBUG){fprintf(stderr, "stimfname: %s\n", stimfname);}
		strcpy(stimfroot, stimfname);
		stimfroot[ strlen(stimfname) - 5] = '\0';
		if(DEBUG){fprintf(stderr, "stimfroot: %s\n", stimfroot);}
	      }
	  }
	if (subjectid == 0)
	  {
	    fprintf(stderr, "ERROR: You must specify the subject ID (-S 999)\n");
	    exit(-1);
	  }
	if (stimfname == NULL)
	  {
	    fprintf(stderr, "ERROR: You must specify the stimulus file name (ex: 'test')\n");
	    exit(-1);
	  }

	//printf("%d\n%s\n", subjectid, stimfname);
	sprintf (infilename, "%d_%s.femxpeck_rDAT", subjectid, stimfroot);
	//printf("%s\n", infilename);
	fprintf(stderr, "\n\nAnalyzing data in '%s'\n", infilename);
	fprintf (stderr, "Number of trials per block = %d\n", blocksize);


/* determine number of stimulus classes */
	if ((stimfp = fopen(stimfname, "r")) == NULL)
	  {
	    fprintf(stderr, "ERROR!: problem opening stimulus file!: %s\n", stimfname);
	    exit (-1);
	  }
	else
	  {
	    class = n_stimclass = nstims = 0;
	    while (fgets (stimfline, sizeof(line), stimfp) != NULL )
	      {
		sscanf(stimfline, "%d%s%d%d", &class, stimf, &freq, &rate);
		stimlist[nstims].class = class;
		strcpy(stimlist[nstims].exem,  stimf);
		nstims++;
		if (class > n_stimclass) {n_stimclass = class;}
	      }
	  }

	
 /*set ngroups for the subsequent analysis */
	if (bystim){ngroups = nstims;}
	else {ngroups = n_stimclass;}
	
	if  ( (nstims > NMAXSTIMS) || (n_stimclass > NMAXSTIMS) ) {
	  fprintf(stderr, "ERROR!: number of stimulus exemplars or classes in '%s' exceeds the default maximum\n", stimfname);
	  exit(-1);}
	
	if  (nstims == 0){
	  fprintf(stderr, "ERROR!: no stimuli detected in the input file '%s'\n", stimfname);
	  exit(-1);}
	
	fprintf (stderr, "Number of stimulus classes = %d\n", n_stimclass);
	fprintf (stderr, "Number of stimulus exemplars = %d\n", nstims);
	fprintf (stderr, "Number of stimulus groups = %d\n", ngroups);

	if(DEBUG){
	  for (i = 0; i < nstims; i++){
	    fprintf(stderr, "stimulus '%d' is '%s'\n", i, stimlist[i].exem);
	  }
	}
/* setup output files */

	if (bystim){
	  sprintf(outfname, "%i_%s_bystim.femxpeck_XDAT", subjectid, stimfroot);
	  if(DEBUG){
	    for  (i=0; i<nstims; i++)
	      fprintf(stderr, "stimulus file '%d': %s\n", i+1, stimlist[i].exem);
	  }
	}
	else{
	  sprintf(outfname, "%i_%s.femxpeck_XDAT", subjectid, stimfroot);
	}
	if ((outfp = fopen(outfname, "w")) == NULL)
	  { 
	    fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", outfname);
	    fclose(outfp);
	    exit (-1);
	  }
	if (plot_out == 1){
	  if (bystim) {sprintf(plotfname, "%i_%s_bystim.plot_out", subjectid, stimfroot);}
          else {sprintf(plotfname, "%i_%s.plot_out", subjectid, stimfroot);}
	  if ((plotfp = fopen(plotfname, "w")) == NULL)
	    { 
	      fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", outfname);
	      fclose(plotfp);
	      exit (-1);
	    }
	  fprintf(plotfp, "#gnuplot output file for %d with %s\n", subjectid, stimfname);
	}

     /* open input file */

	if ((infp = fopen(infilename, "r")) != NULL)
	  {
	    fprintf(stderr, "Working....");
	  }
	else
	  {
	    fprintf(stderr, "ERROR!: problem opening data input file!: %s\n", infilename);
	    exit (-1);
	  }
	

 /* crunch the trial data */

 for (i = 0; i<ngroups; i++)                 /*zero all the output variables*/
   {
     block[i].left = block[i].center = block[i].right = block[i].trls = 0;
     total[i].left = total[i].center = total[i].right = total[i].trls = 0;
   }

 trial_rep = block_num = 0;

 if(DEBUG){printf("trial vars cleared\n");}

 while (fgets (line, sizeof(line), infp) != NULL )                /* read in a line from infile */
   {
     sess_num = tot_trls = ses_trls = stim_class = reinf_val = swap = 0;
     offset = duration = Lresp = Cresp = Rresp = resp_tod = resp_date = 0;
     if(DEBUG){printf("%s", line);}

     if (sscanf (line, "%d%d%d%d%s%d%d%d%d%d%d%d%d%d", 
		 &sess_num, &tot_trls, &ses_trls, &swap, exemplar, 
		 &offset, &duration, &stim_class, &Lresp, &Cresp, 
		 &Rresp, &reinf_val, &resp_tod, &resp_date) == 14)
       {

	 if(DEBUG){printf("found correctly formatted data\n");}

	 if(bystim == 1){
	   j=0;
	   while(strcmp(stimlist[j].exem, exemplar) != 0 )
	     j++; 
	   stim_group=j; /* set the index value for the stim on this trial */
	   if(DEBUG){ //spot check trials
	     if(trial_rep%20==0){
	       printf("exem: %s\tstimlist: %s\t j:%i\tstim_group:%i\n", 
		      exemplar, stimlist[stim_group].exem, j, stim_group);}
	   }
	 }
	 else{
	   stim_group = stim_class-1;
	 }
	
	 switch (stim_class){ 
	 case 1:                  /*left key trial*/
	   block[stim_group].left += Lresp;
	   block[stim_group].center += Cresp;
	   block[stim_group].right += Rresp;
	   ++block[stim_group].trls;
	   ++trial_rep;
	   break;
	 case 2:                   /*right key trial*/
	   block[stim_group].left += Lresp;
	   block[stim_group].center += Cresp;
	   block[stim_group].right += Rresp;
	   ++block[stim_group].trls;
	   ++trial_rep;
	   break;
	 default:
	   fprintf(stderr, "ERROR: Illegal stim class '%d' detected on trial number %d\n", stim_class, tot_trls);
	   break;
	 }
	
	 if(DEBUG){printf("end\n\n");}

	 if (trial_rep == blocksize)
	   {
	     block_num++;
	    
	     /* output block data */
	     for (i=0; i<ngroups; i++){/*print the non-correction trial data */
	       if(docsv==0)
		 fprintf(outfp, "%d\t%d\t%d\t%d\t", 
			 block[i].trls, block[i].left, block[i].center, block[i].right);
	       else
		 fprintf(outfp, "%d,%d,%d,%d,", 
			 block[i].trls, block[i].left, block[i].center, block[i].right);
	     }
	     fprintf(outfp, "\n");
	     
	     fflush(outfp);
	     
	     if(plot_out == 1){
	       fprintf(plotfp, "%d\t", block_num);
	       for (i=0; i<ngroups; i++)
		 fprintf(plotfp, "%f\t", (float)block[i].trls/(float)blocksize);
	       fprintf(plotfp, "%.4f\n", (float)(resp_tod)/19000.0 );
	       fflush(plotfp);
	     }
	     
	     /* reset block variables */
	 
	     for (i=0; i<ngroups; i++){                 /*zero all the output variables*/
	       total[i].trls += block[i].trls;
	       total[i].left += block[i].left;
	       total[i].center += block[i].center;
	       total[i].right += block[i].right;
	       block[i].left = block[i].center = block[i].right = block[i].trls = 0;
	     }
	     trial_rep = 0;
	 
	   }  
       }
   }                                                /* EOF loop */
 
 /* output last block of data */
 if (lastblock){
   if (trial_rep != 0){
     block_num++;
     for (i=0; i<ngroups; i++){                 /*update the totals*/
       total[i].trls += block[i].trls;
       total[i].left += block[i].left;
       total[i].center += block[i].center;
       total[i].right += block[i].right;
     }
     /* output block data */
     for (i=0; i<ngroups; i++){/*print the non-correction trial data */
       if(docsv==0)
	 fprintf(outfp, "%d\t%d\t%d\t%d\t", 
		 block[i].trls, block[i].left, block[i].center, block[i].right);
       else
	 fprintf(outfp, "%d,%d,%d,%d,", 
		 block[i].trls, block[i].left, block[i].center, block[i].right);
     }
     fprintf(outfp, "\n");
     
     fflush(outfp);
     
     if(plot_out == 1){
       fprintf(plotfp, "%d\t", block_num);
       for (i=0; i<ngroups; i++)
	 fprintf(plotfp, "%f\t", (float)block[i].trls/(float)blocksize);
       fprintf(plotfp, "%.4f\n", (float)(resp_tod)/19000.0 );
       fflush(plotfp);
     }
   }
 }
 
 
 fclose (outfp);
 fclose (infp);
 if(plot_out==1){fclose (plotfp);}
 fprintf(stderr, "....Done\n");
 printf("Total Number of Blocks: %d\n", block_num);
 if (lastblock){
   if (blocksize!=trial_rep){
     printf("LAST BLOCK IS INCOMPLETE: contains %d trials\n", trial_rep);} 
   else{
     printf("LAST BLOCK IS COMPLETE\n");}
 }
 else{
   if (blocksize!=trial_rep){
     printf("LAST BLOCK IS COMPLETE; but the last %d trials not counted\n", trial_rep);}
   else{
     printf("LAST BLOCK IS COMPLETE: all trials counted\n");} 
 }
 printf("last trial run at %d %d\n", resp_tod , resp_date);
 
 if (printstimcount==1){
   printf("\nNumber of trials per stimulus group\n");
   if(bystim){
     for (i=0; i<ngroups; i++)  
	 fprintf(stderr, "stimulus %s \t%d\n", stimlist[i].exem, total[i].trls);
   }
   else{
     for (i=0; i<ngroups; i++)  
       fprintf(stderr, "class: %i \t%d\n", i+1, total[i].trls);
   }
 }

 return 0;
 
}




