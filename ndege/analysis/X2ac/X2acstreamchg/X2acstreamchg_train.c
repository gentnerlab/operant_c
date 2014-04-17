/****************************************************************************
**    X2ac.c -- use to analyse data from any 2ac operant routine 
*****************************************************************************
**
**  12/16/99  TQG  handles initial parsing and analysis of the data files 
**                 output from 2chioce_bsl and 2choice_flash anf 2choice_bsl+tone.  
**                 (1) Blocks the data by a specified block size, and for each block provides 
**                 the number of correct, incorrect and no-responses for 
**                 each stimulus class, or exemplar  
**
**            Note: This version does NOT include non-responses or correction trials
**                   in the derived performace values for each block.  However,             
**                   it counts both corection and non-correction trials toward the 
**                   trials per block number.             
**
**  1-27-01  TQG  added '-p' flag which will create an output file that gnuplot can read
**  
**
**  2-01-01 TQG  Now handles data files containing more than 2 stimulus classes
**  4-19-01 TQG  Now parses by stimulus class or exemplar 
**  5-01-01 TQG  added command line flag to control counting the last block
**
**  3-14-01 TQG added '-x' flag which will cause only reinforced non-correction trials to
**                 add to the cumulative trials/block.  i.e. a block of 50 trials, contains 
**                 50 non-correction trials, nothing else.  The default (w/o -x is to count the
**                 correction trials towards the trials/block, but not in performance on the block.    
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


#define DEBUG 0

#define BLOCK_SIZE  100                  /* default number of trials per block */
#define PLOT_OUTPUT  0                   /* default '0' for no gnuplot output */
#define NMAXSTIMS    4096                  /* default max number of stimulus classes or exemplars*/
#define LAST_BLOCK 0                     /* default '0' , don't count the trials in the last (incomplete) block */
#define CORRECTION 0                     /* default '0', don't count data from correction trials */

int blocksize = BLOCK_SIZE;
int plot_out = PLOT_OUTPUT;
int lastblock = LAST_BLOCK;
int subjectid = 0;
int correction = CORRECTION;

int main (int ac, char *av[])
{
  float pC[NMAXSTIMS];
  int sess_num, trl_num, trl_type, stim_class, block_num, freq, Rrate, Prate;
  int resp_sel, resp_acc, resp_tod, resp_date, bystim=0, nstims, cx_src=0, stim_group;
  int trial_cnt, reinf_val, i, j, class, n_stimclass, ngroups, cnt_cx=1;
  int printstimcount=0;
  float resp_rt, Tdur, Ddur;
  FILE *outfp = NULL, *infp = NULL, *plotfp = NULL, *stimfp = NULL;
  char *stimfname = NULL;
  char infilename[128], outfname[128], plotfname[128], line[256], exemplar[64], distracter[64];
  char stimfline[256], stimf[64], stimfroot [128];

  struct stim {
    char exem[128];
    int class;
  }stimlist[NMAXSTIMS];
  
  struct stimdat{
    int C;
    int X;
    int N;
    int cC;
    int cX;
    int cN;
    float C_RT;
    float X_RT;
    float cC_RT;
    float cX_RT;
  }Rblock[NMAXSTIMS],Rtot[NMAXSTIMS];
  
  /* Parse the command line */
  for (i = 1; i < ac; i++){
    if (*av[i] == '-'){
      if (strncmp(av[i], "-S", 2) == 0)
	sscanf(av[++i], "%d", &subjectid);
      else if (strncmp(av[i], "-b", 2) == 0)
	sscanf(av[++i], "%d", &blocksize); 
      else if (strncmp(av[i], "-p", 2) == 0){
	plot_out = 1;
	fprintf(stderr, "\nGenerating output file for gnuplot\n");
      }
      else if (strncmp(av[i], "-c", 2) == 0)
	lastblock = 1; 
      else if (strncmp(av[i], "-v", 2) == 0)
	printstimcount = 1; 
      else if (strncmp(av[i], "-x", 2) == 0){
	cnt_cx = 0;  
	fprintf(stderr, "Not counting correction trials toward block totals\n");
      }
      else if (strncmp(av[i], "-m", 2) == 0){
	bystim = 1;
	fprintf(stderr, "Sorting by stimulus exemplar\n");
      }
      else if (strncmp(av[i], "-h", 2) == 0){
	fprintf(stderr, "X2acstreamchg_train usage:\n");
	fprintf(stderr, "    X2acstreamchg_train [-hpcxvm] [-b int] [-S int] <filename>\n\n");
	fprintf(stderr, "     -h          = show this help message\n");
	fprintf(stderr, "     -b int      = specify trial block size (default = 100)\n\n");
	fprintf(stderr, "     -p          = generate gnuplot output\n");
	fprintf(stderr, "     -c          = count the trials in the last (incomplete) block\n");
	fprintf(stderr, "     -x          = DO NOT count the correction trials toward block totals\n");
	fprintf(stderr, "     -v          = output to screen the total number of trials with each stimulus group\n");
	fprintf(stderr, "     -m          = sort the data by stimulus exemplars\n\n");
	fprintf(stderr, "     -S value    = specify the subject ID number (required)\n");
	fprintf(stderr, "     filename    = specify stimulus filename (required)\n");
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
  
  if(DEBUG==2){printf("%d\n%s\n", subjectid, stimfname);}
  sprintf (infilename, "%d_%s.SCtrain_rDAT", subjectid, stimfroot);
  if(DEBUG==2){printf("%s\n", infilename);}
  fprintf(stderr, "Analyzing data in '%s'\n", infilename);
  fprintf (stderr, "Number of trials per block = %d\n", blocksize);
  
  /* determine number of stimulus classes */
  if ((stimfp = fopen(stimfname, "r")) == NULL){
    fprintf(stderr, "ERROR!: problem opening stimulus file!: %s\n", stimfname);
    exit (-1);
  }
  else{
    class = n_stimclass = nstims = 0;
    while (fgets (stimfline, sizeof(line), stimfp) != NULL ){
      sscanf(stimfline, "%s%d%d%d", stimf, &freq, &Rrate, &Prate); /*NOTE FOR FUTURE USE - this line had to be changed from X2ac bc NO CLASS in STIMFILE*/
      stimlist[nstims].class = 1;
      strcpy(stimlist[nstims].exem,  stimf);
      nstims++;
      if (class > n_stimclass) {n_stimclass = class;}
    }
  }
  
  /*set ngroups for the subsequent analysis */
  if (bystim){ngroups = nstims;}
  else {ngroups = 2;}
  
  if  ( (nstims > NMAXSTIMS) || (n_stimclass > NMAXSTIMS) ) {
    fprintf(stderr, "ERROR!: number of stimulus exemplars or classes in '%s' exceeds the default maximum\n", stimfname);
    exit(-1);}
  
  if  (nstims == 0){
    fprintf(stderr, "ERROR!: no stimuli detected in the input file '%s'\n", stimfname);
    exit(-1);}
  
  fprintf (stderr, "Number of stimulus classes = %d\n", n_stimclass);
  fprintf (stderr, "Number of stimulus exemplars = %d\n", nstims);
  fprintf (stderr, "Number of trial groups = %d\n", ngroups);
  
  if(DEBUG){
    for (i = 0; i < nstims; i++){
      fprintf(stderr, "stimulus '%d' is '%s'\n", i, stimlist[i].exem);
    }
  }
  
  
  /* setup output files */
  if (bystim){
    sprintf(outfname, "%i_%s_bystim.SCtrain_XDAT", subjectid, stimfroot);
    for  (i=0; i<nstims; i++)
      fprintf(stderr, "stimulus file '%d': %s\n", i+1, stimlist[i].exem);
  }
  else
    sprintf(outfname, "%i_%s.SCtrain_XDAT", subjectid, stimfroot);
  
  if ((outfp = fopen(outfname, "w")) == NULL){
    fprintf(stderr, "ERROR!: problem opening data output file!: %s\n", outfname);
    fclose(outfp);
    exit (-1);
  }
  if (plot_out == 1){
    if (bystim)
      sprintf(plotfname, "%i_%s_bystim.plot_out", subjectid, stimfroot);
    else
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
    fprintf(stderr, "Working....\n");
  else{
    fprintf(stderr, "ERROR!: problem opening data input file!: %s\n", infilename);
    exit (-1);
  }
  
  
  
  /*zero all the output variables*/
  for (i = 0; i<ngroups; i++){     
    Rblock[i].C = Rblock[i].X = Rblock[i].N = 0;
    Rblock[i].cC = Rblock[i].cX = Rblock[i].cN = 0;
    Rblock[i].C_RT = Rblock[i].X_RT = 0.0;
    Rblock[i].cC_RT = Rblock[i].cX_RT = 0.0;

    Rtot[i].C = Rtot[i].X = Rtot[i].N = 0;
    Rtot[i].cC = Rtot[i].cX = Rtot[i].cN = 0;
    Rtot[i].C_RT = Rtot[i].X_RT = 0.0;
    Rtot[i].cC_RT = Rtot[i].cX_RT = 0.0;
  }
  trial_cnt = block_num = 0;

  /* crunch the trial data */
  while (fgets (line, sizeof(line), infp) != NULL ){                /* read in a line from infile */
    sess_num = trl_num = trl_type = stim_class =  0;
    resp_sel = resp_acc = resp_rt = resp_tod = resp_date = 0;
    if (sscanf (line, "%d%d%d%s%s%d%d%d%f%f%f%d%d%d", &sess_num, &trl_num, &trl_type, exemplar, distracter,
		&stim_class, &resp_sel, &resp_acc, &Tdur, &Ddur, &resp_rt, &reinf_val, &resp_tod, &resp_date) == 14){
      /*set the group index value */
      if(DEBUG==2){fprintf(stderr,"ntrialnum %d,cxn %d, %s, class %d, sel %d, acc %d, %.4f, %.4f\n", trl_num, trl_type, exemplar,
			 stim_class, resp_sel, resp_acc, Tdur, Ddur);
      }
/*       if(bystim == 1){ */
/* 	j=0; */
/* 	while(strcmp(stimlist[j].exem, exemplar) != 0 ) */
/* 	  j++; */
/* 	stim_group=j; /\* set the index value for the stim on this trial *\/ */
/* 	if(DEBUG) */
/* 	  if(trial_cnt%20==0){printf("exem: %s\tstimlist: %s\t j:%i\tstim_group:%i\n", exemplar, stimlist[stim_group].exem, j,stim_group);} */
/*       } */
/*       else */
	stim_group = stim_class-1;
      if(DEBUG==2){fprintf(stderr,"%d \t", stim_group);}

      /*sort the data*/
      if (trl_type == 1){                                      /* non-correction trial */
	if(DEBUG==2){printf("non-correction trial\t");}
	if (resp_sel == 0){                                              /* no resp */
	  Rblock[stim_group].N++;
	  cx_src = 2;                            /* correction trial source is no-resp*/
	}                                          
	else if (resp_acc == 1){                                         /* correct resp */
	  Rblock[stim_group].C++;                               
	  Rblock[stim_group].C_RT = Rblock[stim_group].C_RT  + resp_rt;  /* add to rt sum */
	  trial_cnt++; cx_src=0;
	}                                                  /* add to trials/block count */
	else if (resp_acc == 0){                                         /* incorrect resp */
	  Rblock[stim_group].X++;                                 
	  Rblock[stim_group].X_RT = Rblock[stim_group].X_RT + resp_rt;         
	  trial_cnt++; cx_src = 1;                                                   /* correction trial source is incorrect resp*/
	}  
	else if (resp_acc == 3){                                         /* probe resp */
          if(DEBUG==1){fprintf(stderr,"found a probe class %d on sess: %d trial:%d \n", stim_group, sess_num, trl_num);}
          if(resp_sel ==1 ){
            Rblock[stim_group].C++;
            Rblock[stim_group].C_RT = Rblock[stim_group].C_RT  + resp_rt; 
            trial_cnt++;
          }
          else if (resp_sel == 2){
            Rblock[stim_group].X++;
            Rblock[stim_group].X_RT = Rblock[stim_group].X_RT  + resp_rt; 
            trial_cnt++;
          }
        }                                    
      }
      else if (trl_type == 0){                                 /* correction trial */
	if(DEBUG==2){printf("correction trial\t");}
	if (cx_src==2){                   /*correction trial generated from no-resp on previous trial, so responses are valid */ 
	  if (resp_sel == 0)                                              /* no resp to correction trial*/
	    Rblock[stim_group].cN++;
	  else if (resp_acc == 1){
	    Rblock[stim_group].C++;                               
	    Rblock[stim_group].C_RT = Rblock[stim_group].C_RT  + resp_rt;
	    trial_cnt++;                                                     /* add to trials/block count */
	  }
	  else if (resp_acc == 0){      
	    Rblock[stim_group].X++;                                 
	    Rblock[stim_group].X_RT = Rblock[stim_group].X_RT + resp_rt;         
	    trial_cnt++;
	    cx_src = 1;/*change the correction trial source from NO-resp to X-resp */
	  }
	}
	else if(cx_src==1){               /*correction trial generated from incorrect resp on previous trial, so responses are not valid */ 
	  if (resp_sel == 0)                                              /* no resp to correction trial*/
	    Rblock[stim_group].cN++;
	  else if (resp_acc == 1){
	    Rblock[stim_group].cC++;                               
	    Rblock[stim_group].cC_RT = Rblock[stim_group].cC_RT + resp_rt;               /* add to rt sum */
	    if (cnt_cx==1) 
	      trial_cnt++;                                                 /* add to trials/block count (if we're counting cx trials)*/
	  }                
	  else if (resp_acc == 0){      
	    Rblock[stim_group].cX++;                                 
	    Rblock[stim_group].cX_RT = Rblock[stim_group].cX_RT + resp_rt;         
	    if (cnt_cx==1)
	      trial_cnt++;
	  }
	}
	else         /*correction trial following correct response, this is bad*/
	  fprintf(stderr,"WARNING!:: trial type '0' following correct response (trial:%d session:%d) you should investigate\n", trl_num, sess_num);
      }
    } 
    if (trial_cnt == blocksize){
      if(DEBUG){fprintf(stderr, "done with block: %d\n", block_num);}
      block_num++;

      if(DEBUG){
	for (i = 0; i < ngroups; i++){
	  fprintf(stderr, "%d\t%d\t%d\t%.4f\t%.4f\t", Rblock[i].C, Rblock[i].X, Rblock[i].N, Rblock[i].C_RT, Rblock[i].X_RT);}
	for (i = 0; i < ngroups; i++){
	  fprintf(stderr, "%d\t%d\t%d\t%.4f\t%.4f\t", Rblock[i].cC, Rblock[i].cX, Rblock[i].cN, Rblock[i].cC_RT, Rblock[i].cX_RT);}
	fprintf(stderr, "\n"); 
	fflush(stderr);
      } 

      /* figure mean RTs for the block */  
      for (i = 0; i< ngroups; i++){
	if  (Rblock[i].C > 0) Rblock[i].C_RT = Rblock[i].C_RT/(float)Rblock[i].C;
	if  (Rblock[i].X > 0) Rblock[i].X_RT = Rblock[i].X_RT/(float)Rblock[i].X;
	if  (Rblock[i].cC > 0) Rblock[i].cC_RT = Rblock[i].cC_RT/(float)Rblock[i].cC;
	if  (Rblock[i].cX > 0) Rblock[i].cX_RT = Rblock[i].cX_RT/(float)Rblock[i].cX;
      }
      /*ouput the block data */
      for (i = 0; i < ngroups; i++){
	fprintf(outfp, "%d\t%d\t%d\t%.4f\t%.4f\t", Rblock[i].C, Rblock[i].X, Rblock[i].N, Rblock[i].C_RT, Rblock[i].X_RT);}
      for (i = 0; i < ngroups; i++){
	fprintf(outfp, "%d\t%d\t%d\t%.4f\t%.4f\t", Rblock[i].cC, Rblock[i].cX, Rblock[i].cN, Rblock[i].cC_RT, Rblock[i].cX_RT);}
      fprintf(outfp, "\n");
      fflush(outfp);
      
      if(DEBUG){
	for (i = 0; i < ngroups; i++){
	  fprintf(stderr, "%d\t%d\t%d\t%.4f\t%.4f\t", Rblock[i].C, Rblock[i].X, Rblock[i].N, Rblock[i].C_RT, Rblock[i].X_RT);}
	for (i = 0; i < ngroups; i++){
	  fprintf(stderr, "%d\t%d\t%d\t%.4f\t%.4f\t", Rblock[i].cC, Rblock[i].cX, Rblock[i].cN, Rblock[i].cC_RT, Rblock[i].cX_RT);}
	fprintf(stderr, "\n"); 
	fflush(stderr);
      } 
     
      if(plot_out == 1){
	fprintf(plotfp, "%d\t", block_num );
	for (i = 0; i < ngroups; i++){
	  pC[i] = (float) Rblock[i].C /( (float) (Rblock[i].C + Rblock[i].X) );
	  fprintf(plotfp, "%f\t", pC[i] );
	}
	fprintf(plotfp, "%.4f\n",(float)(resp_tod)/19000.0 );
	fflush(plotfp);
      }
      if(DEBUG){
	fprintf(stderr, "%d\t", block_num );
	for (i = 0; i < ngroups; i++){
	  pC[i] = (float) Rblock[i].C /( (float) (Rblock[i].C + Rblock[i].X) );
	  fprintf(stderr, "%f\t", pC[i] );
	}
	fprintf(stderr, "%.4f\n",(float)(resp_tod)/19000.0 );
      }
	
      /*update your totals*/
      for (i = 0; i < ngroups; i++){    
	Rtot[i].C_RT += ((Rblock[i].C_RT * Rblock[i].C)+(Rtot[i].C_RT * Rtot[i].C))/(Rtot[i].C + Rblock[i].C);
	Rtot[i].X_RT += ((Rblock[i].X_RT * Rblock[i].X)+(Rtot[i].X_RT * Rtot[i].X))/(Rtot[i].X + Rblock[i].X);
	Rtot[i].cC_RT += ((Rblock[i].cC_RT * Rblock[i].cC)+(Rtot[i].cC_RT * Rtot[i].cC))/(Rtot[i].cC + Rblock[i].cC);
	Rtot[i].cX_RT += ((Rblock[i].cX_RT * Rblock[i].cX)+(Rtot[i].cX_RT * Rtot[i].cX))/(Rtot[i].cX + Rblock[i].cX);
	Rtot[i].C += Rblock[i].C;
	Rtot[i].X += Rblock[i].X;
	Rtot[i].N += Rblock[i].N;
	Rtot[i].cC += Rblock[i].cC;
	Rtot[i].cX += Rblock[i].cX;
	Rtot[i].cN += Rblock[i].cN;	
      }
      

      /* reset block variables */
      for (i = 0; i < ngroups; i++){             /*zero all the output variables*/
	Rblock[i].C = Rblock[i].X = Rblock[i].N = 0;
	Rblock[i].cC = Rblock[i].cX = Rblock[i].cN = 0;
	Rblock[i].C_RT = Rblock[i].X_RT = 0.0;
	Rblock[i].cC_RT = Rblock[i].cX_RT = 0.0;
	pC[i] = 0.0;
      }
      trial_cnt =0;
    }
  }                                                     /* EOF loop */
  if(DEBUG){fprintf(stderr, "finished t he file\n");}
  

  /* output last block of data */
  if (lastblock){
    if (trial_cnt != 0){
      block_num++;
      /*ouput the data */   
      for (i = 0; i< ngroups; i++){
	if  (Rblock[i].C > 0) Rblock[i].C_RT = Rblock[i].C_RT/(float)Rblock[i].C;
	if  (Rblock[i].X > 0) Rblock[i].X_RT = Rblock[i].X_RT/(float)Rblock[i].X;
	if  (Rblock[i].cC > 0) Rblock[i].cC_RT = Rblock[i].cC_RT/(float)Rblock[i].cC;
	if  (Rblock[i].cX > 0) Rblock[i].cX_RT = Rblock[i].cX_RT/(float)Rblock[i].cX;
      }

      /*ouput the block data */
      for (i = 0; i < ngroups; i++){
	fprintf(outfp, "%d\t%d\t%d\t%.4f\t%.4f\t", Rblock[i].C, Rblock[i].X, Rblock[i].N, Rblock[i].C_RT, Rblock[i].X_RT);}
      for (i = 0; i < ngroups; i++){
	fprintf(outfp, "%d\t%d\t%d\t%.4f\t%.4f\t", Rblock[i].cC, Rblock[i].cX, Rblock[i].cN, Rblock[i].cC_RT, Rblock[i].cX_RT);}
      //fseek(outfp, -1, SEEK_END); 
      fprintf(outfp, "\n");
      fflush(outfp);
     
      if(plot_out == 1){
	fprintf(plotfp, "%d\t", block_num );
	for (i = 0; i < ngroups; i++){
	  pC[i] = (float) Rblock[i].C /( (float) (Rblock[i].C + Rblock[i].X) );
	  fprintf(plotfp, "%f\t", pC[i] );
	}
	fprintf(plotfp, "%.4f\n",(float)(resp_tod)/19000.0 );
	fflush(plotfp);
      }

      /* update the totals */
      for (i = 0; i < ngroups; i++){    
	Rtot[i].C_RT += ((Rblock[i].C_RT * Rblock[i].C)+(Rtot[i].C_RT * Rtot[i].C))/(Rtot[i].C + Rblock[i].C);
	Rtot[i].X_RT += ((Rblock[i].X_RT * Rblock[i].X)+(Rtot[i].X_RT * Rtot[i].X))/(Rtot[i].X + Rblock[i].X);
	Rtot[i].cC_RT += ((Rblock[i].cC_RT * Rblock[i].cC)+(Rtot[i].cC_RT * Rtot[i].cC))/(Rtot[i].cC + Rblock[i].cC);
	Rtot[i].cX_RT += ((Rblock[i].cX_RT * Rblock[i].cX)+(Rtot[i].cX_RT * Rtot[i].cX))/(Rtot[i].cX + Rblock[i].cX);
	Rtot[i].C += Rblock[i].C;
	Rtot[i].X += Rblock[i].X;
	Rtot[i].N += Rblock[i].N;
	Rtot[i].cC += Rblock[i].cC;
	Rtot[i].cX += Rblock[i].cX;
	Rtot[i].cN += Rblock[i].cN;	
      }
    }
  }
 
  fclose (outfp);
  fclose (infp);
  if(plot_out)
    fclose(plotfp);
  
  fprintf(stderr, "\n\t....Done\n");

  printf("Total Number of Blocks: %d\n", block_num);
  if (lastblock){
    if (blocksize!=trial_cnt){
      printf("LAST BLOCK IS INCOMPLETE: contains %d trials\n", trial_cnt);} 
    else{
      printf("LAST BLOCK IS COMPLETE\n");}
  }
  else{
    if (blocksize!=trial_cnt){
      printf("LAST BLOCK IS COMPLETE; but the last %d trials not counted\n", trial_cnt);}
    else{
      printf("LAST BLOCK IS COMPLETE: all trials counted\n");} 
  }
  printf("last trial run at %d %d\n", resp_tod , resp_date);
  
  
  if (printstimcount==1){
    printf("\nNumber of trials per stimulus group\n");
    if(bystim){
      fprintf(stderr,"NON_CORRECION TRIALS\n");
      for (i=0; i<ngroups; i++)  
	fprintf(stderr, "\t'%s'\tCorr:%d\tIncorr:%d\t No-R:%d\t Total:%d\n", stimlist[i].exem, Rtot[i].C, Rtot[i].X, Rtot[i].N, Rtot[i].C+Rtot[i].X);
      fprintf(stderr,"CORRECION TRIALS\n");
      for (i=0; i<ngroups; i++)  
	fprintf(stderr, "\t'%s'\tCorr:%d\tIncorr:%d\t No-R:%d\t Total:%d\n", stimlist[i].exem, Rtot[i].cC, Rtot[i].cX, Rtot[i].cN, Rtot[i].cC+Rtot[i].cX);
     fprintf(stderr,"ALL TRIALS\n");
     for (i=0; i<ngroups; i++)  
       fprintf(stderr, "\t'%s'\tCorr:%d\tIncorr:%d\t No-R:%d\t Total:%d\n", 
	       stimlist[i].exem, Rtot[i].C+Rtot[i].cC, Rtot[i].X+Rtot[i].cX, Rtot[i].N+Rtot[i].cN, Rtot[i].C+Rtot[i].X+Rtot[i].cC+Rtot[i].cX);
    }
    else{
       fprintf(stderr,"NON_CORRECION TRIALS\n");
      for (i=0; i<ngroups; i++)  
	fprintf(stderr, "\tclass:%d\tCorr:%d\tIncorr:%d\t No-R:%d\t Total:%d\n", i+1, Rtot[i].C, Rtot[i].X, Rtot[i].N, Rtot[i].C+Rtot[i].X);
      fprintf(stderr,"CORRECION TRIALS\n");
      for (i=0; i<ngroups; i++)  
	fprintf(stderr, "\tclass:%d\tCorr:%d\tIncorr:%d\t No-R:%d\t Total:%d\n", i+1, Rtot[i].cC, Rtot[i].cX, Rtot[i].cN, Rtot[i].cC+Rtot[i].cX);
     fprintf(stderr,"ALL TRIALS\n");
     for (i=0; i<ngroups; i++)  
       fprintf(stderr, "\tclass:%d\tCorr:%d\tIncorr:%d\t No-R:%d\t Total:%d\n", 
	       i+1, Rtot[i].C+Rtot[i].cC, Rtot[i].X+Rtot[i].cX, Rtot[i].N+Rtot[i].cN, Rtot[i].C+Rtot[i].X+Rtot[i].cC+Rtot[i].cX);
    }
  }
  
  return 0;
  
}








