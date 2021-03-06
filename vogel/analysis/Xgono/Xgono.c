/****************************************************************************
**    X2gono.c -- use to analyse data from gonogo1 gonog2 procedure
*****************************************************************************
**
** 9-20-01 TQG: Adapted from most recent version of X2choice.c
**
**
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>

#define BLOCK_SIZE  100                  /* default number of trials per block */
#define PLOT_OUTPUT 0                    /* default '0' for no gnuplot output */
#define MAXSTIMCLASS 64                  /* default max number of stimulus classes */
#define LAST_BLOCK 0                     /* default '0' , don't count the trials in the last (incomplete) block */
#define CORRECTION 0                     /* default '0', don't count data from correction trials */
#define NMAXSTIMS 256                     /* default max number of stimuli */
#define DEBUG 0

int maxblocksize = 10000000;
int blocksize = BLOCK_SIZE;
int plot_out = PLOT_OUTPUT;
int lastblock = LAST_BLOCK;
int subjectid = 0;
int correction = CORRECTION;

int main (int ac, char *av[])
{
  int sess_num, trl_num, trl_type, stim_class, block_num, do_transpose=0;
  int resp_sel, resp_acc, resp_tod, resp_date, freq, rate, total;
  int trial_rep, reinf_val, i,j, class, n_stimclass, nstims,docsv=0;
  int printstimcount=0, bystim=0, ngroups=0,stim_group=0;
  float resp_rt;
  FILE *outfp = NULL, *infp = NULL, *plotfp = NULL, *stimfp = NULL;
  char *stimfname = NULL, *stimfroot=NULL,  *stimtmp;
  char infilename[128], outfname[128], plotfname[128], line[256], exemplar[64];
  char stimf[128], stimfline[256];

  struct stim {
    char exem[128];
    int class;
  }stimlist[NMAXSTIMS];
  
  struct stimdat{
    int go;
    int no;
  }Rgroup[NMAXSTIMS];
  

  struct outdata{
    int go;
    int no;
    float goRT;
    float meanRT;
    float pGo;
  } resp[NMAXSTIMS], resp_CX[NMAXSTIMS];


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
		else if (strncmp(av[i], "-X", 2) == 0)
		  {
		   correction = 1;    
		   fprintf(stderr, "\nCounting the data from correction trials!\n");
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
		else if (strncmp(av[i], "-T", 2) == 0)
                  {
                    do_transpose = 1;
		    blocksize=maxblocksize;
		  }
		else if (strncmp(av[i], "-h", 2) == 0)
		  {
		    fprintf(stderr, "Xgono usage:\n");
		    fprintf(stderr, "    Xgono [-hpcZXvs] [-b <value>] [-S <value>] <stimfile>\n\n");
		    fprintf(stderr, "     -h          = show this help message\n");
		    fprintf(stderr, "     -p          = generate output for gnuplot\n");
		    fprintf(stderr, "     -c          = count the trials in the last (even if it is incomplete) block\n");
		    fprintf(stderr, "     -Z       = output XDAT as comma separated variable file\n");
		    fprintf(stderr, "     -X          = count the data from correction trials (if there are any)\n");
		    fprintf(stderr, "     -v          = output to screen the total number of trials with each stimulus group\n");
		    fprintf(stderr, "     -m          = sort the data by stimulus exemplar\n");
		    fprintf(stderr, "     -T          = set the block size to INF and output data in rows by stimulus group\n");
		    fprintf(stderr, "     -b int      = specify  trial block size (default = 100)\n\n");
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
		stimfname = strdup(av[i]);
		stimtmp = strdup(stimfname);
		if(DEBUG){fprintf(stderr, "stimfname: %s\n", stimfname);}
		if(DEBUG){fprintf(stderr, "stimtmp: %s\n", stimtmp);}
		//strcpy(stimfroot, stimfname);
		//stimfroot[ strlen(stimfname) - 5] = '\0';
		stimfroot = strsep(&stimtmp, ".");
		if(DEBUG){fprintf(stderr, "stimfroot: %s\n", stimfroot);}
		if(DEBUG){fprintf(stderr, "stimfname: %s\n", stimfname);}
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
	sprintf (infilename, "%d_%s.gonogo_rDAT", subjectid, stimfroot);
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
	  sprintf(outfname, "%i_%s_bystim.GONO_XDAT", subjectid, stimfroot);
	  if(DEBUG){
	    for  (i=0; i<nstims; i++)
	      fprintf(stderr, "stimulus file '%d': %s\n", i+1, stimlist[i].exem);
	  }
	}
	else{
	  sprintf(outfname, "%i_%s.GONO_XDAT", subjectid, stimfroot);
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
     resp[i].go = resp[i].no = 0;
     resp[i].goRT = resp[i].meanRT = 0.0;
     resp_CX[i].go = resp_CX[i].no = 0;
     resp_CX[i].goRT = resp_CX[i].meanRT = 0.0;
    }

 for (i=0; i<ngroups; i++)                 /*zero all the output variables*/
   Rgroup[i].go = Rgroup[i].no = 0;
 
 
 trial_rep = block_num = 0;

 if(DEBUG){printf("trial vars cleared\n");}

 while (fgets (line, sizeof(line), infp) != NULL )                /* read in a line from infile */
   {
     sess_num = trl_num = trl_type = stim_class =  0;
     resp_sel = resp_acc = resp_rt = resp_tod = resp_date = 0;

     if (sscanf (line, "%d%d%d%s%d%d%d%f%d%d%d", &sess_num, &trl_num, &trl_type, exemplar, 
		 &stim_class, &resp_sel, &resp_acc, &resp_rt, &reinf_val, &resp_tod, &resp_date) == 11)
       {

	 if(DEBUG==2){printf("start\n");}

	 if(bystim == 1){
	   j=0;
	   while((strcmp(stimlist[j].exem, exemplar) != 0 ) && (j<=ngroups) )
	     j++; 
	   stim_group=j; /* set the index value for the stim on this trial */
	  	   
	   if(DEBUG){
	     if(trial_rep%20==0){printf("exem: %s\tstimlist: %s\t j:%i\tstim_group:%i\n", exemplar, stimlist[stim_group].exem, j, stim_group);}
	   }
	 }
	 else{
	   stim_group = stim_class-1;
	 }

	 /*do the sorting */
	 if(stim_group> ngroups){
	   fprintf(stderr, "WARNING: Unknown stimulus '%s' found on trial %d\n", exemplar, trl_num);
	   if(DEBUG){fprintf(stderr, "skipped trial %d\n", trl_num);}
	 }
	 else{
	   if (trl_type == 1){                                    /* non-correction trial */
	     switch (resp_sel){ 
	     case 0:                  /*no resp*/
	       ++resp[stim_group].no;
	       ++trial_rep;
	       break;
	     case 1:                   /* go resp */
	       ++resp[stim_group].go;                               
	       resp[stim_group].goRT += resp_rt;  /* add rt to running tally */
	       ++trial_rep;
	       break;
	     default:
	       fprintf(stderr, "ERROR: Illegal response selection '%d' detected on trial number %d\n", resp_sel, trl_num);
	       break;
	     }
	   }
	   else if (trl_type == 0){                                 /* correction trial */
	     if (correction == 1){
	       switch (resp_sel){ 
	       case 0:                  /*no resp*/
		 ++resp_CX[stim_group].no;
		 break;
	       case 1:                   /* goresp */
		 ++resp_CX[stim_group].go;                               
		 resp_CX[stim_group].goRT += resp_rt;  /* add rt to running tally */
		 break;
	       default:
		 fprintf(stderr, "ERROR: Illegal response selection '%d' detected on trial number %d\n", resp_sel, trl_num);
		 break;
	       }
	     }
	   }
	 }

	 if(DEBUG==2){printf("end\n\n");}

	 if (trial_rep == blocksize)
	   {
	     block_num++;
	    
	     /* generate some numbers for the block */  
	     for (i=0; i<ngroups; i++) 
	       {
		 Rgroup[i].no += resp[i].no;
		 Rgroup[i].go += resp[i].go;
		 if  (resp[i].goRT != 0) {resp[i].meanRT = resp[i].goRT/resp[i].go;}
		 if  ((correction ==1) & (resp_CX[i].goRT != 0)) {resp_CX[i].meanRT = resp_CX[i].goRT/resp_CX[i].go;}
	       }
	     
	     /* output block data */
	     if(do_transpose==0){
	       for (i=0; i<ngroups; i++)/*print the non-correction trial data */
		 {
		   if(docsv==0)
		     fprintf(outfp, "%d\t%d\t%.4f\t", resp[i].go, resp[i].no, resp[i].meanRT);
		   else
		     fprintf(outfp, "%d,%d,%.4f,", resp[i].go, resp[i].no, resp[i].meanRT);
		 }
	       if(correction ==1){	    
		 for (i=0; i<ngroups; i++)/*print the correction trial data */
		   {
		     if(docsv==0)
		       fprintf(outfp, "%d\t%d\t%.4f\t", resp_CX[i].go, resp_CX[i].no, resp_CX[i].meanRT);	
		     else
		       fprintf(outfp, "%d,%d,%.4f,", resp_CX[i].go, resp_CX[i].no, resp_CX[i].meanRT); 
		   }
		 if(docsv==0)
		   fprintf(outfp, "%.4f",(float)(resp_tod)/19000.0 );
		 else
		   fprintf(outfp, "%.4f",(float)(resp_tod)/19000.0 );
	       }
	     }
	     else{ /*output with new line for each group*/
	        for (i=0; i<ngroups; i++){/*print the non-correction trial data */
		  if(docsv==0)
		    fprintf(outfp, "%d\t%d\t%.4f\n", resp[i].go, resp[i].no, resp[i].meanRT);
		  else
		    fprintf(outfp, "%d,%d,%.4f\n", resp[i].go, resp[i].no, resp[i].meanRT);
		}
	     }
	     fprintf(outfp, "\n");
	     
	     
	     fflush(outfp);
	     
	     if(plot_out == 1)
	       {
		 fprintf(plotfp, "%d\t", block_num);
		 for (i=0; i<ngroups; i++)
		  {
		    resp[i].pGo = (float) resp[i].go /( (float) (resp[i].go+resp[i].no) );
		    fprintf(plotfp, "%f\t", resp[i].pGo );
		  }
		 resp[ngroups].pGo = (float) resp[ngroups].go /( (float) (resp[ngroups].go+resp[ngroups].no) );
		 fprintf(plotfp, "%.4f\n", (float)(resp_tod)/19000.0 );
		 fflush(plotfp);
	       }
	     
	    /* reset block variables */
	 
	     for (i=0; i<ngroups; i++)                 /*zero all the output variables*/
	       {
		 resp[i].go = resp[i].no = 0;
		 resp[i].goRT = resp[i].meanRT = 0.0;
		 resp_CX[i].go = resp_CX[i].no = 0;
		 resp_CX[i].goRT = resp_CX[i].meanRT = 0.0;
	       }
	     trial_rep = 0;
	   }
       }
   }                                                /* EOF loop */
 
 /* output last block of data */
 if (lastblock){
   if (trial_rep != 0){
     block_num++;
     for (i=0; i<ngroups; i++) 
       {
	 Rgroup[i].no += resp[i].no;
	 Rgroup[i].go += resp[i].go;
	 if  (resp[i].goRT != 0) {resp[i].meanRT = resp[i].goRT/resp[i].go;}
	 if  ((correction ==1) & (resp_CX[i].goRT != 0)) {resp_CX[i].meanRT = resp_CX[i].goRT/resp_CX[i].go;}
       }
     if(do_transpose==0){
       for (i=0; i<ngroups; i++)/*print the non-correction trial data */
	 {
	   if(docsv==0)
	     fprintf(outfp, "%d\t%d\t%.4f\t", resp[i].go, resp[i].no, resp[i].meanRT);
	   else
	     fprintf(outfp, "%d,%d,%.4f,", resp[i].go, resp[i].no, resp[i].meanRT);
	 }
       if(correction ==1){	    
	 for (i=0; i<ngroups; i++)/*print the correction trial data */
	   {
	     if(docsv==0)
	       fprintf(outfp, "%d\t%d\t%.4f\t", resp_CX[i].go, resp_CX[i].no, resp_CX[i].meanRT);	
	     else
	       fprintf(outfp, "%d,%d,%.4f,", resp_CX[i].go, resp_CX[i].no, resp_CX[i].meanRT);
	     
	   }
	 if(docsv==0) 
	   fprintf(outfp, "%.4f", (float)(resp_tod)/19000.0 );
	 else
	   fprintf(outfp, "%.4f", (float)(resp_tod)/19000.0 );
       }
     }
     else{/*output with new line for each group*/
       for (i=0; i<ngroups; i++){/*print the non-correction trial data */
	 if(docsv==0)
	   fprintf(outfp, "%d\t%d\t%.4f\n", resp[i].go, resp[i].no, resp[i].meanRT);
	 else
	   fprintf(outfp, "%d,%d,%.4f\n", resp[i].go, resp[i].no, resp[i].meanRT);
       }
     }
     fprintf(outfp, "\n");
       
     fflush(outfp);
	     
     
     if(plot_out == 1){
       fprintf(plotfp, "%d\t", block_num );
       for (i = 1; i == ngroups-1; i++){
	 resp[i].pGo = (float) resp[i].go /( (float) (resp[i].go+resp[i].no) );
	 fprintf(plotfp, "%f\t", resp[i].pGo );
       }
       resp[ngroups].pGo = (float) resp[ngroups].go /( (float) (resp[ngroups].go+resp[ngroups].no) );
       fprintf(plotfp, "%f\t%.4f\n", resp[ngroups].pGo, (float)(resp_tod)/19000.0 );
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
       {
	 total = Rgroup[i].go + Rgroup[i].no; 
	 fprintf(stderr, "stimulus %s \t%d\n", stimlist[i].exem, total);
       }    
   }
   else{
     for (i=0; i<ngroups; i++)  
       {
	 total = Rgroup[i].go + Rgroup[i].no; 
	 fprintf(stderr, "class: %i \t%d\n", i+1, total);
       }    
   }
 }
 // printf("stimlist[0]: %s \t stimlist[max]: %s\n", stimlist[0].exem,stimlist[ngroups-1].exem);
 if(DEBUG){
   for (i=0; i<ngroups; i++)
     printf("i: %d \tRgroup[i].go: %i \t Rgroup[i].no: %i\n", i,Rgroup[i].go,Rgroup[i].no);
 }
 return 0;
 
}




