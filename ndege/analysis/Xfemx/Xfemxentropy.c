/****************************************************************************
**    Xfemxentropy.c -- use to analyse data from femxentropy procedures
*****************************************************************************/

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
  int sess_num, tot_trls, ses_trls, stim_class, block_num, swap;
  int resp_tod, resp_date, freq;
  int trial_rep, i, class, n_stimclass, nstims, docsv=0;
  int printstimcount=0, bysession=0, ngroups=0,stim_group=0;
  int last_session, sess_count=0;
  FILE *outfp = NULL, *infp = NULL, *plotfp = NULL, *stimfp = NULL;
  char *stimfname = NULL;
  char infilename[128], outfname[128], plotfname[128], line[256], exemplar[64];
  char stimf[128], stimfline[256], stimfroot [128];
  float Sduration = 0.0, Tduration =0.0, blockstimsecs=0.0, blockperchsecs=0.0;
  //  float sess_marker=0.0;

  struct stim {
    char exem[128];
    int class;
  }stimlist[NMAXSTIMS];
  
  struct outdata{
    int Resp;
    float D_stim;
    float D_trl;
  } block[NMAXSTIMS], total[NMAXSTIMS];


/* Parse the command line */
	
  for (i = 1; i < ac; i++){
    
    if (*av[i] == '-'){
      if (strncmp(av[i], "-S", 2) == 0)
	sscanf(av[++i], "%d", &subjectid);
      else if (strncmp(av[i], "-b", 2) == 0)
	sscanf(av[++i], "%d", &blocksize); 
      else if (strncmp(av[i], "-c", 2) == 0)
	lastblock = 1; 
      else if (strncmp(av[i], "-Z", 4) == 0){
	fprintf(stderr, "docsv: %d\n", docsv);
	docsv = 1; 
	fprintf(stderr, "docsv: %d\n", docsv);
      }
      else if (strncmp(av[i], "-v", 2) == 0)
	printstimcount = 1; 
      else if (strncmp(av[i], "-s", 2) == 0)
	bysession = 1; 
      else if (strncmp(av[i], "-p", 2) == 0){
	plot_out = 1;
	fprintf(stderr, "\nGenerating output file for gnuplot\n");
      }
      else if (strncmp(av[i], "-h", 2) == 0){
	fprintf(stderr, "Xfemxentropy usage:\n");
	fprintf(stderr, "    Xfemxentropy [-h][-p][-c][-Z][-v][-s][-b <value>] [-S <value>] <stimfile>\n\n");
	fprintf(stderr, "     -h          = show this help message\n");
	fprintf(stderr, "     -p          = generate output for gnuplot\n");
	fprintf(stderr, "     -c          = count the trials in the last (even if it is incomplete) block\n");
	fprintf(stderr, "     -Z          = output XDAT as comma separated variable file\n");
	fprintf(stderr, "     -v          = output to screen the total number of trials with each stimulus group\n");
	fprintf(stderr, "     -m          = sort the data by stimulus exemplars\n\n");
	fprintf(stderr, "     -s          = start a new block when the session changes\n\n");
	fprintf(stderr, "     -b int      = specify number of stimulus trials/block (default = 20)\n");
	fprintf(stderr, "     -S int      = specify the subject ID number (required)\n");
	fprintf(stderr, "     stimfile    = specify stimulus filename (required)\n");
	exit(-1);
      }
      else{
	fprintf(stderr, "Unknown option: %s.  Use '-h' for help.\n", av[i]);
	exit (-1);
      }
    }
    else{
      stimfname = av[i];
      if(DEBUG){fprintf(stderr, "stimfname: %s\n", stimfname);}
      strcpy(stimfroot, stimfname);
      stimfroot[ strlen(stimfname) - 5] = '\0';
      if(DEBUG){fprintf(stderr, "stimfroot: %s\n", stimfroot);}
    }
  }
  if (subjectid == 0){
    fprintf(stderr, "ERROR: You must specify the subject ID (-S 999)\n");
    exit(-1);
  }
  if (stimfname == NULL){
    fprintf(stderr, "ERROR: You must specify the stimulus file name (ex: 'test')\n");
    exit(-1);
  }
  
  sprintf (infilename, "%d_%s.femxentropy_rDAT", subjectid, stimfroot);
  fprintf(stderr, "\n\nAnalyzing data in '%s'\n", infilename);
  fprintf (stderr, "Number of trials per block = %d\n", blocksize);
  
  
  /* determine number of stimulus classes */
  if ((stimfp = fopen(stimfname, "r")) == NULL){
    fprintf(stderr, "ERROR!: problem opening stimulus file!: %s\n", stimfname);
    exit (-1);
  }
  else{
    class = n_stimclass = nstims = 0;
    while (fgets (stimfline, sizeof(line), stimfp) != NULL ){
      sscanf(stimfline, "%d%s%d", &class, stimf, &freq);
      stimlist[nstims].class = class;
      strcpy(stimlist[nstims].exem,  stimf);
      nstims++;
      if (class > n_stimclass) {n_stimclass = class;}
    }
    ++n_stimclass; /*add one for the 'silent' perch*/
  }
  
  
  /*set ngroups for the subsequent analysis */
  ngroups = n_stimclass;
  
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
  sprintf(outfname, "%i_%s.femxentropy_XDAT", subjectid, stimfroot);
  
  if ((outfp = fopen(outfname, "w")) == NULL)
    { 
      fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", outfname);
      fclose(outfp);
      exit (-1);
    }
  if (plot_out == 1){
    sprintf(plotfname, "%i_%s.plot_out", subjectid, stimfroot);
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
  
  
  for (i = 0; i<ngroups; i++)                 /*zero all the output variables*/
    {
      block[i].Resp = 0;
      block[i].D_stim = block[i].D_trl = 0.0;
      blockstimsecs = blockperchsecs = 0.0;
      total[i].Resp = 0;
      total[i].D_stim = total[i].D_trl = 0.0;
    }
  
  trial_rep = block_num = 0;
  
  if(DEBUG){printf("trial vars cleared\n");}
  
  
  last_session=1;
  //sess_marker=0.025;

  while (fgets (line, sizeof(line), infp) != NULL ){                /* read in a line from infile */
    sess_num = tot_trls = ses_trls = stim_class = swap = 0;
    Sduration = Tduration = resp_tod = resp_date = 0;
    if(DEBUG==3){printf("%s", line);}
    
    if (sscanf (line, "%d%d%d%d%s%f%d%f%d%d", 
		&sess_num, &tot_trls, &ses_trls, &swap, exemplar, 
		&Sduration, &stim_class, &Tduration, 
		&resp_tod, &resp_date) == 10){
      
      
      if ( (trial_rep == blocksize) || ( (bysession) && (sess_num>last_session) )){ 
	block_num++;
	
	/* output the previous block of data */
	fprintf(outfp, "%d\t", sess_num);
	for (i=0; i<ngroups; i++){/*print the non-correction trial data */
	  if(docsv==0)
	    fprintf(outfp, "%d\t%.4f\t%.4f\t",block[i].Resp, block[i].D_stim, block[i].D_trl);
	  else
	    fprintf(outfp, "%d,%.4f,%.4f,",block[i].Resp, block[i].D_stim, block[i].D_trl);
	}
	fprintf(outfp, "\n");
	if(DEBUG){
	  fprintf(stderr,"block done\n");	
	  for (i=0; i<ngroups; i++){/*print the non-correction trial data */
	    fprintf(stderr,"%d\t%.4f\t%.4f\t",block[i].Resp, block[i].D_stim, block[i].D_trl);
	  }
	}
	fflush(outfp);
	
	if(plot_out == 1){
	  fprintf(plotfp, "%d\t", block_num);
	  for (i=0; i<ngroups; i++)
	    fprintf(plotfp, "%d\t%f\t%f\t%f\t%f\t", 
		    block[i].Resp, block[i].D_trl, block[i].D_stim,  
		    block[i].D_trl/blockperchsecs, block[i].D_stim/blockstimsecs);
	  fprintf(plotfp, "%.3f\n", (float)(resp_tod)/19000.0 );
	  fflush(plotfp);
	}
	
	/* reset block variables */ 
	for (i=0; i<ngroups; i++){                 /*zero all the output variables*/
	  total[i].Resp += block[i].Resp;
	  total[i].D_stim += block[i].D_stim;
	  total[i].D_trl += block[i].D_trl;
	  block[i].Resp = 0; 
	  block[i].D_stim = block[i].D_trl= 0.0;
	  blockperchsecs=blockstimsecs=0.0;
	}
	trial_rep = 0;
	if(sess_num>last_session){
	  sess_count++;
	  // if(sess_marker==0.05)
	  //sess_marker=0.025;
	  //else
	  //sess_marker=0.05;
	}
	last_session=sess_num;
      }
      
      if(Tduration<Sduration)
	Sduration=Tduration;  /*can't listen to stim for longer than the trial duration*/
      
     if(DEBUG){
       if((Sduration!=Tduration)&&(Sduration!=0.0))
	 printf("found correctly formatted data; Tdur:%f; Sdur:%f\n",Tduration,Sduration);
     }
     
     stim_group = stim_class;

     
     /*do the work*/
     // if(DEBUG){printf("stimclass is %d\n", stim_class);}
     switch (stim_class){ 
     case 0:
       ++block[stim_group].Resp; 
       block[stim_group].D_stim += Sduration;
       block[stim_group].D_trl += Tduration;
       blockstimsecs += Sduration;
       blockperchsecs += Tduration;
       break;
     case 1:                  /*perch 1 trial*/
       ++block[stim_group].Resp; 
       block[stim_group].D_stim += Sduration;
       block[stim_group].D_trl += Tduration; 
       ++trial_rep;
       blockstimsecs += Sduration;
       blockperchsecs += Tduration;
       break;
     case 2:                   /*perch 2 trial*/
       ++block[stim_group].Resp; 
       block[stim_group].D_stim += Sduration;
       block[stim_group].D_trl += Tduration;
       ++trial_rep;
       blockstimsecs += Sduration;
       blockperchsecs += Tduration;
       break;
     default:
       fprintf(stderr, "ERROR: Illegal stim class '%d' detected on trial number %d\n", stim_class, tot_trls);
       break;
     }
     // if(DEBUG){printf("resp0:%d; resp1:%d; resp2:%d\n", block[stim_group].Resp);}
    }  
  }                                                /* EOF loop */
 

 
 /* output final block of data */
 if (lastblock){
   if (trial_rep != 0){
     block_num++;
     for (i=0; i<ngroups; i++){                 /*update the totals*/
       total[i].Resp += block[i].Resp;
       total[i].D_stim += block[i].D_stim;
       total[i].D_trl += block[i].D_trl;
     }
     /* output block data */ 
     fprintf(outfp, "%d\t", sess_num);
     for (i=0; i<ngroups; i++){/*print the non-correction trial data */
       if(docsv==0)
	 fprintf(outfp, "%d\t%.4f\t%.4f\t",block[i].Resp, block[i].D_stim, block[i].D_trl);
       else
	 fprintf(outfp, "%d,%.4f,%.4f,", block[i].Resp, block[i].D_stim, block[i].D_trl);
     }
     fprintf(outfp, "\n");
     fflush(outfp);
     
     if(plot_out == 1){
       fprintf(plotfp, "%d\t", block_num);
       for (i=0; i<ngroups; i++)
	 fprintf(plotfp, "%d\t%f\t%f\t%f\t%f\t", 
		 block[i].Resp, block[i].D_trl, block[i].D_stim,  
		 block[i].D_trl/blockperchsecs,  block[i].D_stim/blockstimsecs);
       fprintf(plotfp, "%.3f\n", (float)(resp_tod)/19000.0 );
       fflush(plotfp);
     }
   }
 }
 
 
 fclose (outfp);
 fclose (infp);
 if(plot_out==1){fclose (plotfp);}
 fprintf(stderr, "....Done\n");
 printf("Total Number of Blocks: %d\n", block_num);
 printf("Total Number of Sessions: %d\n", sess_count);
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
   for (i=0; i<ngroups; i++)  
     fprintf(stderr, "class: %i \t%d\n", i+1, total[i].Resp);
 }
 
 return 0;
 
}




