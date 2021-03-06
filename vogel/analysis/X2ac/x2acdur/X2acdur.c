/****************************************************************************
**    X2acdur.c -- use to analyse data from 2acdur procedure 
**     THIS ONLY WORKS FOR 2ACDUR !!!
*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#define BLOCK_SIZE  80                  /* default number of trials per block */
#define PLOT_OUTPUT 0                   /* default '0' for no gnuplot output */
#define MAXSTIMCLASS 64                 /* default max number of stimulus classes */
#define LAST_BLOCK 0                    /* default '0' , don't count the trials in the last (incomplete) block */
#define NMAXSTIMS 256                   /* default max number of stimuli */
#define MAXOBINS 32                    /* max number of offset bins */
#define MAXDBINS 32                    /* max number of duration bins */

#define DEBUG 2


/*inverse normal function*/
/* Coefficients in rational approximations. */
static const double a[] ={	
  -3.969683028665376e+01,
   2.209460984245205e+02,
  -2.759285104469687e+02,
   1.383577518672690e+02, 
  -3.066479806614716e+01,
   2.506628277459239e+00 
};
static const double b[]={
  -5.447609879822406e+01,
   1.615858368580409e+02, 
  -1.556989798598866e+02, 
   6.680131188771972e+01, 
  -1.328068155288572e+01
};
static const double c[]={
  -7.784894002430293e-03,			          
  -3.223964580411365e-01, 
  -2.400758277161838e+00, 
  -2.549732539343734e+00, 
   4.374664141464968e+00,
   2.938163982698783e+00
};  
static const double d[] = { 
  7.784695709041462e-03,
  3.224671290700398e-01,
  2.445134137142996e+00, 
  3.754408661907416e+00
 }; 
 
#define LOW 0.02425
#define HIGH 0.97575 

double norminv(double p){ 
  double q, r;  
  errno = 0;  
  if (p<0||p>1){ 	
    errno = EDOM; 	
    return 0.0;
  } 
  else if (p==0){ 	
    errno = ERANGE; 	
    return -HUGE_VAL /* minus "infinity" */;
  }
  else if (p==1){
    errno = ERANGE;
    return HUGE_VAL /* "infinity" */; 
  }
  else if (p<LOW){
    /* Rational approximation for lower region */ 	
    q = sqrt(-2*log(p));
    return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
      ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);
  } 	
  else if (p>HIGH){
    /* Rational approximation for upper region */ 		
    q  = sqrt(-2*log(1-p)); 		
    return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) / 
      ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1); 	
  }
  else{
    /* Rational approximation for central region */     		
    q = p - 0.5;     	
    r = q*q; 	
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q / 
      (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1); 	
  } 
}
/*end of invnorm*/


/* set some globals*/
int blocksize = BLOCK_SIZE;
int plot_out = PLOT_OUTPUT;
int lastblock = LAST_BLOCK;
int subjectid = 0;


/*main*/
int main (int ac, char *av[])
{
  int sess_num, trl_num, trl_type, nummots, stim_class, block_num, ptype;
  int resp_sel, resp_acc, resp_tod, resp_date;
  int trial_rep, reinf_val, i, j;
  int printstimcount=0;

  int off_binsz=100, dur_binsz=100, offset_bins=0, duration_bins=0, offset=0, duration=0, obin=0, dbin=0;

  float resp_rt; 
  double dprimetest;

  FILE *outfp = NULL, *infp = NULL, *stimfp = NULL;
  char *stimfname = NULL;
  char infilename[128], outfname[128], line[256], exemplar[64];
  char stimfroot [128];

 

  struct responsedata{
    int left;
    int right;
    int no;
    float lRTmean;
    float rRTmean;
  }p3resp[MAXOBINS][MAXDBINS],p4resp[MAXOBINS][MAXDBINS],p3tmp[MAXOBINS][MAXDBINS],p4tmp[MAXOBINS][MAXDBINS],bsltmp[2],bsl[2];

  double d_prime[MAXOBINS][MAXDBINS],pcntcorr[MAXOBINS][MAXDBINS], H, F, hitrate, farate;;
 
  /*zero these matrices*/  
  for (i=1;i<=MAXOBINS;i++){
    for(j=1;j<=MAXDBINS;j++){
      p3resp[i][j].left =  p3resp[i][j].right =  p3resp[i][j].no = 0;
      p3resp[i][j].lRTmean =  p3resp[i][j].rRTmean = 0.0;
      p4resp[i][j].left =  p4resp[i][j].right =  p4resp[i][j].no = 0;
      p4resp[i][j].lRTmean =  p4resp[i][j].rRTmean = 0.0;
      p3tmp[i][j].left =  p3tmp[i][j].right =  p3tmp[i][j].no = 0;
      p3tmp[i][j].lRTmean =  p3tmp[i][j].rRTmean = 0.0;
      p4tmp[i][j].left =  p4tmp[i][j].right =  p4tmp[i][j].no = 0;
      p4tmp[i][j].lRTmean =  p4tmp[i][j].rRTmean = 0.0;
    }
  }
  for (i=1;i<=2; i++){
    bsltmp[i].left = bsltmp[i].right = bsltmp[i].no = 0;
    bsl[i].left = bsl[i].right=bsl[i].no =0;
    bsltmp[i].lRTmean = bsltmp[i].rRTmean = 0.0;
    bsl[i].lRTmean = bsl[i].rRTmean = 0.0;
  }
  
  /* Parse the command line */
  
  for (i = 1; i < ac; i++){
    if (*av[i] == '-'){
      if (strncmp(av[i], "-S", 2) == 0)
	sscanf(av[++i], "%d", &subjectid);
      else if (strncmp(av[i], "-B", 2) == 0)
	sscanf(av[++i], "%d:%d", &off_binsz, &dur_binsz); 
      else if (strncmp(av[i], "-c", 2) == 0)
	lastblock = 1; 
      else if (strncmp(av[i], "-v", 2) == 0)
	printstimcount = 1; 
      else if (strncmp(av[i], "-h", 2) == 0){
	fprintf(stderr, "X2acmot usage:\n");
	fprintf(stderr, "    X2acmot [-hpcZXvs] [-b <value>] [-S <value>] <stimfile>\n\n");
	fprintf(stderr, "     -h          = show this help message\n");
	//fprintf(stderr, "     -p          = generate output for gnuplot\n");
	fprintf(stderr, "     -c          = count the trials in the last (even if it is incomplete) block\n");
	fprintf(stderr, "     -Z          = output XDAT as comma separated variable file\n");
	fprintf(stderr, "     -B off:dur  = specify the bin size (in ms) for offsets and durations of probe stimuli\n");
 	fprintf(stderr, "     -v          = output to screen the total number of trials with each stimulus group\n");
	//fprintf(stderr, "     -b int      = specify  trial block size (default = 100)\n\n");
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
  
  /* set the number of GROUPS we'll use for the analysis */
  if(DEBUG){fprintf(stderr,"off_binsz=%d, dur_binsz=%d\n",off_binsz,dur_binsz);}
  offset_bins = ceil(12000/off_binsz);
  if(12000 % off_binsz > 0)
    fprintf(stderr, 
	    "WARNING: offset bin size does not fit evenly into the offset range, consider using a different bin size\n");
  duration_bins = ceil(12000/dur_binsz);
  if(12000 % dur_binsz > 0)
    fprintf(stderr, 
	    "WARNING: duration bin size does not fit evenly into the duration range, consider using a different bin size\n");
  if(DEBUG){fprintf(stderr, 
		    "using %d %d-ms offset bins, and %d %d-ms duration bins\n", 
		    offset_bins, off_binsz, duration_bins, dur_binsz);}
  
  /* find out how many classes there are in the stim file */
  if ((stimfp = fopen(stimfname, "r")) == NULL){
    fprintf(stderr, "ERROR!: problem opening stimulus file!: %s\n", stimfname);
    exit (-1);
  }
  
  /* open input file */
  if ((infp = fopen(infilename, "r")) != NULL)
    fprintf(stderr, "Working...\n");
  else{
    fprintf(stderr, "ERROR!: problem opening data input file!: %s\n", infilename);
    exit (-1);
  }
  
  /*zero the block and trial counters */
  trial_rep = block_num = 0;
  
  if(DEBUG==2){printf("trial vars cleared\n");}
  
  while (fgets (line, sizeof(line), infp) != NULL ){               /* read in a line from infile */
    sess_num = trl_num = nummots = stim_class =  0;
    resp_sel = resp_acc = resp_rt = resp_tod = resp_date = 0;
    
    if (sscanf (line, "%d%d%d%d%d%d%s%d%d%d%f%d%d%d", &sess_num, &trl_num, &trl_type, &ptype, &offset, &duration, exemplar, 
		&stim_class, &resp_sel, &resp_acc, &resp_rt, &reinf_val, &resp_tod, &resp_date) == 14){
      
      if(trl_type==1){ /* only look at the non-correction trials */
	if(DEBUG==2){printf("start\n");}
	
	switch(stim_class){
	case 1:
	case 2:
	  switch (resp_sel){ /*1=left, 2=right,0=none*/ 
	  case 0:                  /*no response*/
	    ++bsltmp[stim_class].no;
	    ++trial_rep;
	    break;
	  case 1:                   /* left key resp */
	    ++bsltmp[stim_class].left;
	    bsltmp[stim_class].lRTmean += resp_rt;  /* add rt to running tally */
	    ++trial_rep;
	    break;
	  case 2:                   /* right key resp */
	    ++bsltmp[stim_class].right;                               
	    bsltmp[stim_class].rRTmean += resp_rt;  /* add rt to running tally */
	    ++trial_rep;
	    break; 
	  default:
	    fprintf(stderr, "ERROR: Illegal response selection '%d' detected on trial number %d\n", resp_sel, trl_num);
	    break;
	  }
   	  break;
	case 3:
	  obin=ceil(offset/off_binsz);
	  dbin=ceil(duration/dur_binsz);
	  switch (resp_sel){ /* 0=none, 1=left, 2=right */ 
	  case 0:     
	    ++p3tmp[obin][dbin].no;
	    ++trial_rep;
	    break;
	  case 1:                   /* left key resp */
	    ++p3tmp[obin][dbin].left;
	    p3tmp[obin][dbin].lRTmean += resp_rt;  /* add rt to running tally */
	    ++trial_rep;
	    break;
	  case 2:                   /* right key resp */
	    ++p3tmp[obin][dbin].right;                               
	    p3tmp[obin][dbin].rRTmean += resp_rt;  /* add rt to running tally */
	    ++trial_rep;
	    break; 
	  default:
	    fprintf(stderr, "ERROR: Illegal response selection '%d' detected on trial number %d\n", resp_sel, trl_num);
	    break;
	  }
	  break;
	case 4:
	  obin=ceil(offset/off_binsz);
	  dbin=ceil(duration/dur_binsz);
	  switch (resp_sel){ /* 0=none, 1=left, 2=right */ 
	  case 0:     
	    ++p4tmp[obin][dbin].no;
	    ++trial_rep;
	    break;
	  case 1:                   /* left key resp */
	    ++p4tmp[obin][dbin].left;
	    p4tmp[obin][dbin].lRTmean += resp_rt;  /* add rt to running tally */
	    ++trial_rep;
	    break;
	  case 2:                   /* right key resp */
	    ++p4tmp[obin][dbin].right;                               
	    p4tmp[obin][dbin].rRTmean += resp_rt;  /* add rt to running tally */
	    ++trial_rep;
	    break; 
	  default:
	    fprintf(stderr, "ERROR: Illegal response selection '%d' detected on trial number %d\n", resp_sel, trl_num);
	    break;
	  }
	  break;
	default:
	   fprintf(stderr, "ERROR: Illegal stimulus class '%d' detected on trial number %d\n", resp_sel, trl_num);
	  break;
	}	  
	if(DEBUG==2){printf("end\n\n");}
      }

      if (trial_rep == blocksize){
	if(DEBUG==2){fprintf(stderr, ".");}
	block_num++;
	/* test to see if bsl performance was above criterion */
	H = (double) bsltmp[1].left/(bsltmp[1].left + bsltmp[1].right);
	F = (double) bsltmp[2].left/(bsltmp[2].left + bsltmp[2].right);
	if(DEBUG==2){fprintf(stderr, "H:%g \tF:%g, \tZ(H):%g \t z(F):%g\n",H,F,norminv(H),norminv(F));}
        dprimetest = norminv(H-0.001) - norminv(F+0.001);
	if(DEBUG==2){fprintf(stderr, "dprime test is %g\n", dprimetest);}
	/* if so, copy the data to your output arrays*/ 
	if(dprimetest>=1.0){
	  for (i=1;i<=2;i++){
	    bsl[i].left += bsltmp[i].left;
	    bsl[i].right += bsltmp[i].right;
	    bsl[i].no += bsltmp[i].no;
	    bsl[i].lRTmean += bsltmp[i].lRTmean;
	    bsl[i].rRTmean += bsltmp[i].rRTmean;
	  }
	  for (i=1; i<=offset_bins; i++){
	    for (j=1; j<=duration_bins; j++){
	      p3resp[i][j].left += p3tmp[i][j].left;
	      p3resp[i][j].right += p3tmp[i][j].right;
	      p3resp[i][j].no += p3tmp[i][j].no;
	      p3resp[i][j].lRTmean += p3tmp[i][j].lRTmean;
	      p3resp[i][j].rRTmean += p3tmp[i][j].rRTmean;

	      p4resp[i][j].left += p4tmp[i][j].left;
	      p4resp[i][j].right += p4tmp[i][j].right;
	      p4resp[i][j].no += p4tmp[i][j].no;
	      p4resp[i][j].lRTmean += p4tmp[i][j].lRTmean;
	      p4resp[i][j].rRTmean += p4tmp[i][j].rRTmean;

	    }
	  }
	}
	else{
	  fprintf(stderr, "dprime for block %d was %g so I wont count the data from it\n", block_num, dprimetest);  
	  fprintf(stderr, "bsl1-left:%d\tbsl1-right:%d\tbsl1-no:%d\n", bsltmp[1].left, bsltmp[1].right, bsltmp[1].no);
	  fprintf(stderr, "bsl2-left:%d\tbsl2-right:%d\tbsl2-no:%d\n", bsltmp[2].left, bsltmp[2].right, bsltmp[2].no);
	  fprintf(stderr, "\tH:%g \tF:%g, \tZ(H):%g \t z(F):%g\n",H,F,norminv(H),norminv(F));
	}
		
	/* reset block variables */
	for (i=1; i<=offset_bins; i++){
	  for (j=1; j<=duration_bins; j++){
	    p3tmp[i][j].left = p3tmp[i][j].right = p3tmp[i][j].no = 0;
	    p3tmp[i][j].lRTmean = p3tmp[i][j].rRTmean = 0.0;
	    
	    p4tmp[i][j].left = p4tmp[i][j].right = p4tmp[i][j].no = 0;
	    p4tmp[i][j].lRTmean = p4tmp[i][j].rRTmean = 0.0;
	  }
	}
	for (i=1;i<=2;i++){
	  bsltmp[i].left = bsltmp[i].right = bsltmp[i].no = 0;
	  bsltmp[i].lRTmean = bsltmp[i].rRTmean = 0.0;
	}
	trial_rep = 0;
      }
    }
  } /* EOF loop */
  
  /* check the last block of data */
  if ((lastblock!=0) && (trial_rep !=0)){
    block_num++;
    H = (double) bsltmp[1].left/(bsltmp[1].left + bsltmp[1].right);
    F = (double) bsltmp[2].left/(bsltmp[2].left + bsltmp[2].right);
    if(DEBUG==2){fprintf(stderr, "H:%g \tF:%g, \tZ(H):%g \t z(F):%g\n",H,F,norminv(H),norminv(F));}
    dprimetest = norminv(H-0.001) - norminv(F+0.001);
    if(DEBUG==2){fprintf(stderr, "dprime test is %g\n", dprimetest);}

    /* if its good, copy the data to your output arrays*/ 
    if(dprimetest>=1.0){
      for (i=1;i<=2;i++){
	bsl[i].left += bsltmp[i].left;
	bsl[i].right += bsltmp[i].right;
	bsl[i].no += bsltmp[i].no;
	bsl[i].lRTmean += bsltmp[i].lRTmean;
	bsl[i].rRTmean += bsltmp[i].rRTmean;
      }
      for (i=1; i<=offset_bins; i++){
	for (j=1; j<=duration_bins; j++){
	  p3resp[i][j].left += p3tmp[i][j].left;
	  p3resp[i][j].right += p3tmp[i][j].right;
	  p3resp[i][j].no += p3tmp[i][j].no;
	  p3resp[i][j].lRTmean += p3tmp[i][j].lRTmean;
	  p3resp[i][j].rRTmean += p3tmp[i][j].rRTmean;
	  
	  p4resp[i][j].left += p4tmp[i][j].left;
	  p4resp[i][j].right += p4tmp[i][j].right;
	  p4resp[i][j].no += p4tmp[i][j].no;
	  p4resp[i][j].lRTmean += p4tmp[i][j].lRTmean;
	  p4resp[i][j].rRTmean += p4tmp[i][j].rRTmean;
	}
      }
    }
  }
 
  /*calcuate the mean RTs*/
  for(i=1;i<=2;i++){
    bsl[i].lRTmean = bsl[i].lRTmean/bsl[i].left;
    bsl[i].rRTmean = bsl[i].rRTmean/bsl[i].right;
  }
  for (i=1; i<=offset_bins; i++){
    for (j=1; j<=duration_bins; j++){
      p3resp[i][j].lRTmean= p3resp[i][j].lRTmean/ p3resp[i][j].left;
      p3resp[i][j].rRTmean= p3resp[i][j].rRTmean/ p3resp[i][j].right;
      p4resp[i][j].lRTmean= p4resp[i][j].lRTmean/ p4resp[i][j].left;
      p4resp[i][j].rRTmean= p4resp[i][j].rRTmean/ p4resp[i][j].right;
    }
  }
  
  /*open bsl output files*/
  sprintf(outfname, "%i_%s.2AC_XDAT_bsl", subjectid, stimfroot);
  if ((outfp = fopen(outfname, "w")) == NULL){ 
    fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", outfname);
    fclose(outfp);exit (-1);
  }
  /*write bsl data*/
  for (i=1;i<=2;i++){
    fprintf(outfp, "%d\t%d\t%d\%g\t%g\n", bsl[i].left, bsl[i].right, bsl[i].no,bsl[i].lRTmean, bsl[i].rRTmean);
  }
  fclose(outfp);  
  

  /*compute the dprime values and percent correct for each offset x duration*/
  for (i=1; i<=offset_bins; i++){
    for (j=1; j<=duration_bins; j++){
      if(DEBUG==2){fprintf(stderr, "p3 left:%d \t p3 right:%d\n",p3resp[i][j].left,p3resp[i][j].right);}
      if(DEBUG==2){fprintf(stderr, "p4 left:%d \t p4 right:%d\n",p4resp[i][j].left,p4resp[i][j].right);}
      hitrate =(double) p3resp[i][j].left/(p3resp[i][j].left+p3resp[i][j].right);
      farate = (double) p4resp[i][j].left/(p4resp[i][j].left+p4resp[i][j].right);
      d_prime[i][j]=norminv(hitrate-0.001)-norminv(farate+0.001);
      pcntcorr[i][j]= (double)(p3resp[i][j].left + p4resp[i][j].right)/(p3resp[i][j].left+p3resp[i][j].right+p4resp[i][j].left+p4resp[i][j].right);
      if(DEBUG==2){fprintf(stderr, "hitrate:%g\t farate:%g\tdprime:%g\n", hitrate, farate, d_prime[i][j]);}
    }
  }

  fprintf(stderr, "here i am");
 /*open the d' output file*/ 
  sprintf(outfname, "%i_%s.2AC_XDAT_dprime", subjectid, stimfroot);
  if ((outfp = fopen(outfname, "w")) == NULL){ 
    fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", outfname);
    fclose(outfp);
    exit (-1);
  }
  /*spit out the response data*/
  for (i=1; i<=offset_bins; i++){
    for (j=1; j<=duration_bins; j++){
      fprintf(outfp, "%.4f\t", d_prime[i][j]);
    }
    fprintf(outfp, "\n");
  }
  fclose (outfp);

 /*open the percent correct output file*/ 
  sprintf(outfname, "%i_%s.2AC_XDAT_pcntcorr", subjectid, stimfroot);
  if ((outfp = fopen(outfname, "w")) == NULL){ 
    fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", outfname);
    fclose(outfp);
    exit (-1);
  }
  /*spit out the response data*/
  for (i=1; i<=offset_bins; i++){
    for (j=1; j<=duration_bins; j++){
      fprintf(outfp, "%.4f\t", pcntcorr[i][j]);
    }
    fprintf(outfp, "\n");
  }
  fclose (outfp);



  
  /*throw some data to the screen*/
  if (printstimcount==1){
    printf("\nNumber of responses to probe trials per offset x duration condition\n");
    for (i=1; i<=offset_bins; i++){
      for (j=1; j<=duration_bins; j++){
	fprintf(stderr, "%d\t", (p3resp[i][j].left+p3resp[i][j].right+p4resp[i][j].left+p3resp[i][j].right));
      }
      fprintf(stderr, "\n");
    }
  }

  /*clean up your open files */
  fclose (infp);

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
  
  return 0;
  
}
