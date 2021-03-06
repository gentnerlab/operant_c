/****************************************************************************
**    X2acmot.c -- use to analyse data from 2acmot procedure
*****************************************************************************
**
**
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define BLOCK_SIZE  100                  /* default number of trials per block */
#define PLOT_OUTPUT 0                    /* default '0' for no gnuplot output */
#define MAXSTIMCLASS 64                  /* default max number of stimulus classes */
#define LAST_BLOCK 0                     /* default '0' , don't count the trials in the last (incomplete) block */
#define NMAXSTIMS 256                     /* default max number of stimuli */

#define DEBUG 0

int blocksize = BLOCK_SIZE;
int plot_out = PLOT_OUTPUT;
int lastblock = LAST_BLOCK;
int subjectid = 0;

int main (int ac, char *av[])
{
  int sess_num, trl_num, trl_type, nummots, stim_class, block_num, maxmots;
  int resp_sel, resp_acc, resp_tod, resp_date, total, nstims=0;
  int trial_rep, reinf_val, i,docsv=0, class=0, nclasses=0;
  int printstimcount=0, bystim=0, ngroups=0,stim_group=0;
  float resp_rt;
  FILE *outfp = NULL, *infp = NULL, *plotfp = NULL, *stimfp = NULL;
  char *stimfname = NULL;
  char infilename[128], outfname[128], plotfname[128], line[256], exemplar[64];
  char stimfroot [128], stimf[128], stimfline[256];

  struct stim {
    char exem[128];
    int class;
  }stimlist[NMAXSTIMS];
  
  struct stimdat{
    int left;
    int right;
    int no;
  }Rgroup[NMAXSTIMS];
  

  struct outdata{
    int left;
    int right;
    int no;
    float lRT;
    float rRT;
    float lRTmean;
    float rRTmean;
    float pleft;
  } resp[NMAXSTIMS];// resp_CX[NMAXSTIMS];


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
      else if (strncmp(av[i], "-M", 2) == 0){
	sscanf(av[++i], "%d", &maxmots);    
        fprintf(stderr, "\nMax number of motifs in a sequence set to %d\n", maxmots);
      }
      else if (strncmp(av[i], "-p", 2) == 0){
	plot_out = 1;
	fprintf(stderr, "\nGenerating output file for gnuplot\n");
      }
      else if (strncmp(av[i], "-h", 2) == 0){
	fprintf(stderr, "X2acmot usage:\n");
	fprintf(stderr, "    X2acmot [-hpcZXvs] [-b <value>] [-S <value>] <stimfile>\n\n");
	fprintf(stderr, "     -h          = show this help message\n");
	fprintf(stderr, "     -p          = generate output for gnuplot\n");
	fprintf(stderr, "     -c          = count the trials in the last (even if it is incomplete) block\n");
	fprintf(stderr, "     -Z          = output XDAT as comma separated variable file\n");
	fprintf(stderr, "     -v          = output to screen the total number of trials with each stimulus group\n");
	fprintf(stderr, "     -M          = specify the max number of motifs in a sequence (required)\n\n");
	fprintf(stderr, "     -b int      = specify  trial block size (default = 100)\n\n");
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
  
  sprintf (infilename, "%d_%s.2ac_rDAT", subjectid, stimfroot);
  fprintf(stderr, "\n\nAnalyzing data in '%s'\n", infilename);
  fprintf (stderr, "Number of trials per block = %d\n", blocksize);
  
  /* set the number of STIM GROUPS we'll use for the analysis */
  ngroups = (2*maxmots)+2; /*maxmots is the number of different sequence lengths */
  fprintf (stderr, "Number of stimulus groups = %d\n", ngroups);
 
  /* find out how many classes there are in the stim file */
  if ((stimfp = fopen(stimfname, "r")) == NULL){
    fprintf(stderr, "ERROR!: problem opening stimulus file!: %s\n", stimfname);
    exit (-1);
  }
  else{
    class = nclasses = nstims = 0;
    while (fgets (stimfline, sizeof(line), stimfp) != NULL ){
      sscanf(stimfline, "%d%s", &class, stimf);
      //    stimlist[nstims].class = class;
      //strcpy(stimlist[nstims].exem,  stimf);
      nstims++;
      if (class > nclasses) {nclasses = class;}
    }
  }
  fprintf(stderr, "found %d stim classes in '%s'\n", nclasses, stimfname);  
  
  /*set up the output file*/
  sprintf(outfname, "%i_%s.2AC_XDAT", subjectid, stimfroot);
  if ((outfp = fopen(outfname, "w")) == NULL){ 
    fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", outfname);
    fclose(outfp);
    exit (-1);
  }
  if (plot_out == 1){
    sprintf(plotfname, "%i_%s.plot_out", subjectid, stimfroot);
    if ((plotfp = fopen(plotfname, "w")) == NULL){ 
      fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", outfname);
      fclose(plotfp);
      exit (-1);
    }
    fprintf(plotfp, "#gnuplot output file for %d with %s\n", subjectid, stimfname);
  }
  
  /* open input file */
  if ((infp = fopen(infilename, "r")) != NULL)
    fprintf(stderr, "Working....");
  else{
    fprintf(stderr, "ERROR!: problem opening data input file!: %s\n", infilename);
    exit (-1);
  }
  
  /* crunch the trial data */
  for (i = 0; i<ngroups; i++){                 /*zero all the output variables*/
    resp[i].left = resp[i].right = resp[i].no = 0;
    resp[i].lRT = resp[i].rRT = resp[i].rRTmean = resp[i].lRTmean = 0.0;
  }
  
  for (i=0; i<ngroups; i++)                 /*zero all the output variables*/
    Rgroup[i].left = Rgroup[i].right = Rgroup[i].no = 0;
  
  trial_rep = block_num = 0;
  
  if(DEBUG){printf("trial vars cleared\n");}
  
  while (fgets (line, sizeof(line), infp) != NULL ){               /* read in a line from infile */
    sess_num = trl_num = nummots = stim_class =  0;
    resp_sel = resp_acc = resp_rt = resp_tod = resp_date = 0;
    
    if (sscanf (line, "%d%d%d%d%s%d%d%d%f%d%d%d", &sess_num, &trl_num, &trl_type, &nummots, exemplar, 
		&stim_class, &resp_sel, &resp_acc, &resp_rt, &reinf_val, &resp_tod, &resp_date) == 12){
      
      if(trl_type==1){
	//	if(DEBUG){printf("start\n");}
      
	/* we assume there 2 baseline classes (1 & 2) and then some number of motifs in a given probe trial*/
	/* this is an ugly hack since there is no convention for numbering probe classes aligned with go or nogo bsl stims*/
	if(nummots==0)
	  stim_group = stim_class-1;
	else{
	  if(nclasses==4){ 
	    switch (stim_class){
	    case 3:
	      stim_group = nummots+1;
	      break;
	    case 4:
	      stim_group = maxmots+nummots+1;
	      break;
	    default:
	      fprintf(stderr, "ERROR: Illegal stimulus class '%d' detected on t %d\n", stim_class, trl_num);
	    }
	  }
	  else{
	    switch (stim_class){
	    case 3:
	    case 4:
	    case 5:
	    case 6:
	    case 7:
	    case 8:
	    case 9:
	    case 10:
	      stim_group = nummots+1;
	      break;
	    case 11:
	    case 12:
	    case 13:
	    case 14:
	    case 15:
	    case 16:
	    case 17:
	    case 18:
	      stim_group = maxmots+nummots+1;
	      break;
	    default:
	      fprintf(stderr, "ERROR: Illegal stimulus class '%d' detected on t %d\n", stim_class, trl_num);
	    }
	  }
	}
	
	switch (resp_sel){ /*1=left, 2=right,0=none*/ 
	case 0:                  /*no response*/
	  ++resp[stim_group].no;
	  ++trial_rep;
	  break;
	case 1:                   /* left key resp */
	  ++resp[stim_group].left;
	  resp[stim_group].lRT += resp_rt;  /* add rt to running tally */
	  ++trial_rep;
	  break;
	case 2:                   /* right key resp */
	  ++resp[stim_group].right;                               
	  resp[stim_group].rRT += resp_rt;  /* add rt to running tally */
	  ++trial_rep;
	  break; 
	default:
	  fprintf(stderr, "ERROR: Illegal response selection '%d' detected on trial number %d\n", resp_sel, trl_num);
	  break;
	}
	//if(DEBUG){printf("end\n\n");}
      }
      if (trial_rep == blocksize){
	block_num++;
	

	/* generate some numbers for the block */  
	for (i=0; i<ngroups; i++) {
	  Rgroup[i].no += resp[i].no;
	  Rgroup[i].left += resp[i].left;
	  Rgroup[i].right += resp[i].right;
	  if  (resp[i].lRT != 0) {resp[i].lRTmean = resp[i].lRT/resp[i].left;}
	  if  (resp[i].rRT != 0) {resp[i].rRTmean = resp[i].rRT/resp[i].right;}
	}

	if(DEBUG){ 
	  fprintf(stderr, "BLOCK %d TOTALS\n", block_num);
	  for (i=0; i<ngroups; i++) {
	    fprintf(stderr, "%d\tleft:%d\tright:%d\tno:%d\tl-RT:%.4f\tr-RT:%.4f\n", i+1,
		    resp[i].left, resp[i].right, resp[i].no, resp[i].lRTmean, resp[i].rRTmean);
	  }
	  fprintf(stderr,"\n");
	}
	
	/* output block data */
	for (i=0; i<ngroups; i++){     /*print the non-correction trial data */
	  fprintf(outfp, "%d\t%d\t%d\t%.4f\t%.4f\t", resp[i].left, resp[i].right, resp[i].no, resp[i].lRTmean, resp[i].rRTmean);
	}
	
	fprintf(outfp, "\n");
	
	fflush(outfp);
	
	if(plot_out == 1){
	  fprintf(plotfp, "%d\t", block_num);
	  for (i=0; i<ngroups; i++){
	    resp[i].pleft = (float) resp[i].left /( (float) (resp[i].left + resp[i].right) );
	    fprintf(plotfp, "%f\t", resp[i].pleft );
	  }
	  resp[ngroups].pleft = (float) resp[ngroups].left /( (float) (resp[ngroups].left+resp[ngroups].right) );
	  fprintf(plotfp, "%f\t%.4f\n", resp[ngroups].pleft,(float)(resp_tod)/19000.0 );
	  fflush(plotfp);
	}
	
	/* reset block variables */
	for (i=0; i<ngroups; i++){                 /*zero all the output variables*/
	  resp[i].left = resp[i].right = resp[i].no = 0;
	  resp[i].lRT = resp[i].rRT = resp[i].lRTmean = resp[i].rRTmean = 0.0;
	}
	trial_rep = 0;
      }
    }
  }                                                /* EOF loop */
 
  
  
  
  /* output last block of data */
  if (lastblock){
    if (trial_rep != 0){
      block_num++;

      for (i=0; i<ngroups; i++) {
	Rgroup[i].no += resp[i].no;
	Rgroup[i].left += resp[i].left;
	Rgroup[i].right += resp[i].right;
	if  (resp[i].lRT != 0) {resp[i].lRTmean = resp[i].lRT/resp[i].left;}
	if  (resp[i].rRT != 0) {resp[i].rRTmean = resp[i].rRT/resp[i].right;}
      }
      
      /* output block data */
      for (i=0; i<ngroups; i++){     /*print the non-correction trial data */
	fprintf(outfp, "%d\t%d\t%d\%.4f\t%.4f\t", 
		resp[i].left, resp[i].right, resp[i].no, resp[i].lRTmean, resp[i].rRTmean);
      }
      
      fprintf(outfp, "\n");
      fflush(outfp);
      
      if(plot_out == 1){
	fprintf(plotfp, "%d\t", block_num );
	for (i = 1; i == ngroups-1; i++){
	  resp[i].pleft = (float) resp[i].left /( (float) (resp[i].left + resp[i].right) );
	  fprintf(plotfp, "%f\t", resp[i].pleft );
	}
	resp[ngroups].pleft = (float) resp[ngroups].left /( (float) (resp[ngroups].left + resp[ngroups].right) );
	fprintf(plotfp, "%f\t%.4f\n", resp[ngroups].pleft, (float)(resp_tod)/19000.0 );
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
	  total = Rgroup[i].left + Rgroup[i].right; 
	  fprintf(stderr, "stimulus %s \t%d\n", stimlist[i].exem, total);
	}    
    }
    else{
      for (i=0; i<ngroups; i++)  
	{
	  total = Rgroup[i].left + Rgroup[i].right; 
	  fprintf(stderr, "class: %i \t%d\n", i+1, total);
	}    
    }
  }

  if(DEBUG){
    for (i=0; i<ngroups; i++)
      printf("i: %d \tRgroup[i].left: %i \t Rgroup[i].right: %i \t Rgroup[i].no: %i\n", 
	     i,Rgroup[i].left, Rgroup[i].right, Rgroup[i].no);
  }
  return 0;
  
}
