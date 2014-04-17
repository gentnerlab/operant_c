#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <math.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#define MAXFILESIZE 5292000    /* max samples allowed in soundfile */

#ifndef SQR
#define SQR(a)  ((a) * (a))
#endif

typedef struct{
  char exemplar[128];
  int class;
  int reinf;
  int punish;
  int freq;
  unsigned long int dur; 
  int num;
}stim;



snd_pcm_t *handle;

int setup_pcmdev(char *pcm_name)
{
  snd_pcm_hw_params_t *params;
  snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK; 
  int rate = 44100, dir;
  snd_pcm_uframes_t persize, persize2;

  int maxrate, minrate;
  unsigned int pertime, perTmin, perTmax;
  snd_pcm_uframes_t bufsize, perSmin, perSmax, bufSmin, bufSmax;
  

  /* Allocate the snd_pcm_hw_params_t structure on the stack. */ 
  snd_pcm_hw_params_alloca(&params);
  
  /* Open PCM device for playback. */
  if (snd_pcm_open(&handle, pcm_name, stream, 0) < 0) {
    fprintf(stderr, "Error opening PCM device %s\n", pcm_name);
    return(-1);
  }
  
  /* Init params with full configuration space */
  if (snd_pcm_hw_params_any(handle, params) < 0) {
    fprintf(stderr, "Can not configure this PCM device.\n");
    return(-1);
  }
  
  /* set interleaved mode */
  if (snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
    fprintf(stderr, "Error setting access.\n");
    return(-1);
  }
  
  /* Set sample format */
  if (snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE) < 0) {
    fprintf(stderr, "Error setting format.\n");
    return(-1);
  }
  
  /* Set sample rate.*/ 
  if (snd_pcm_hw_params_set_rate_near(handle, params, &rate, 0) < 0) {
    fprintf(stderr, "Error setting rate.\n");
    return(-1);
  }
  
  /* Set number of channels */
  if (snd_pcm_hw_params_set_channels(handle, params, 1) < 0) {
    fprintf(stderr, "Error setting channels.\n");
    return(-1);
  }
  
  /* Set period size to n frames (samples). */
  persize = 1024; dir=0;
  if (snd_pcm_hw_params_set_period_size_near(handle,params, &persize, &dir)< 0) {
    fprintf(stderr, "Error setting period size to %d.\n", (int) persize);
    return(-1);
  }
    
  
  /* Apply HW parameter settings to PCM device */
  if (snd_pcm_hw_params(handle, params) < 0) {
    fprintf(stderr, "Error setting HW params.\n");
    return(-1);
  }

  /*get some inof about the hardware*/
  
  // printf("\n ---------- hardware parameters ------------ \n");
  snd_pcm_hw_params_get_rate_min (params, &minrate, &dir);
  //printf("min rate: %d samples per sec\n",minrate);
  snd_pcm_hw_params_get_rate_max (params, &maxrate, &dir);
  //printf("max rate: %d samples per sec\n",maxrate);
  snd_pcm_hw_params_get_period_time (params, &pertime, &dir);
  //printf("period: %d microseconds\n",pertime);
  snd_pcm_hw_params_get_period_time_min (params, &perTmin, &dir);
  snd_pcm_hw_params_get_period_time_min (params, &perTmax, &dir);
  //printf("min period time: %d microseconds\n",perTmin);
  //printf("max period time: %d microseconds\n",perTmax);
  snd_pcm_hw_params_get_period_size (params, &persize2, &dir);
  //printf("period: %d frames\n",(int) persize2);
  snd_pcm_hw_params_get_period_size_min (params, &perSmin, &dir);
  snd_pcm_hw_params_get_period_size_min (params, &perSmax, &dir);
  //printf("min period size: %d frames\n",(int) perSmin);
  //printf("max period size: %d frames\n",(int) perSmax);
  snd_pcm_hw_params_get_buffer_size (params, &bufsize);
  //printf("buffer size: %d frames\n",(int) bufsize);
  snd_pcm_hw_params_get_buffer_size_min (params, &bufSmin);
  snd_pcm_hw_params_get_buffer_size_min (params, &bufSmax);
  //printf("min buffer size: %d frames\n",(int) bufSmin);
  //printf("max buffer size: %d frames\n",(int) bufSmax);
  
  return (double) persize2;
}



/********************************************************
 *  int playwav (char * sound_file_name, double period) *
 *                                                      *
 * returns: 1 on successful play                        *
 *          -1 when soundfile does not play             *
 *          0 when soundfile plays with under.over run  *
 *******************************************************/
int playwav(char *sfname, double period)
{
  
  SNDFILE *sf;
  SF_INFO *sfinfo;
  short *obuff=NULL;
  sf_count_t incount;
  double inframes, padded;
  long pad = 0;
  int outcount;
  snd_pcm_uframes_t outframes;

  /* memory for SF_INFO structures */
  sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));

  //fprintf(stderr, "trying to open '%s'\n", sfname); 
  /* open input file*/
  if(!(sf = sf_open(sfname,SFM_READ,sfinfo))){
    fprintf(stderr,"error opening input file %s\n",sfname);
    return -1;
  }
  /*print out some info about the file you just openend */
  //fprintf(stderr,"\n ---------- Stimulus parameters from playwav ------------ \n");
  //fprintf (stderr, "    Samples: %d\n", (int)sfinfo->frames) ;
  //fprintf (stderr, "Sample Rate: %d\n", sfinfo->samplerate) ;
  //fprintf (stderr, "   Channels: %d\n", sfinfo->channels) ;
  
  
  /* check that some assumptions are met */
  /* this should be done by the operant code*/
  if (sfinfo->frames > MAXFILESIZE){
    fprintf(stderr,"File is too large!\n");
    sf_close(sf);
    return -1;
  }
  if (sfinfo->samplerate != 44100){
    fprintf(stderr, "Sample rate for %s is not equal to 44.1 KHz!\n", sfname);
    sf_close(sf);
    return -1;
  }
  if (sfinfo->channels != 1){
    fprintf(stderr, "Sound file %s is not mono!\n", sfname);
    sf_close(sf);
    return -1;
  }
  
  /* make sure format is WAV */
  if((sfinfo->format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV){
    
    /* pad the file size up to next highest period count*/
    inframes=(int)sfinfo->frames;
    pad = (period * ceil( inframes/ period))-inframes;
    padded=inframes+pad;

    /* allocate buffer memory */
    obuff = (short *) malloc(sizeof(int)*padded);
    if (obuff==NULL){
      fprintf (stderr, "AUDOUT.C: couldn't allocate memory for obuff \n");
      sf_close(sf);
      free(sfinfo);
      free(obuff);
    }

    /* read the data */
    incount = sf_readf_short(sf, obuff, inframes);
    fprintf(stderr, "got %d samples when I tried for %d to sf_readf_short(%s)\n",(int)incount, (int)inframes,sfname);
  }
  else {
    printf("Not a WAV file\n");
    sf_close(sf);
    free(sfinfo);
    free(obuff);
    return -1;
  }
   
  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use\n");
    sf_close(sf);
    free(sfinfo);
    free(obuff);
    return -1;
  }
  if (snd_pcm_reset(handle)<0){
    fprintf (stderr, "cannot reset audio interface for use\n");
    sf_close(sf);
    free(sfinfo);
    free(obuff);
    return -1;
  }
  outframes = padded;
  fprintf(stderr, "outframes %d \n", (int)outframes);
  outcount = snd_pcm_writei(handle, obuff, outframes);
  
  fprintf(stderr,"outcount %d \n", outcount);
  if (outcount == -EPIPE) {
    fprintf(stderr, "overrun while playing %s\n",sfname);
    snd_pcm_prepare(handle);
    sf_close(sf);
    free(sfinfo);
    free(obuff);
    return 0;
  } 
  else if (outcount < 0){
    fprintf(stderr,"error from write: %s\n",snd_strerror(outcount));
    sf_close(sf);
    free(sfinfo);
    free(obuff);
    return 0;
  }
  else if (outcount != outframes) {
    fprintf(stderr, "oops! tried to write %d samples but only did %d: off by %d \n", 
	    (int) outframes, outcount, outcount-(int) outframes);
    sf_close(sf);
    free(sfinfo);
    free(obuff);
    return 0;
  }
 
  
  /*free up resources*/
  fprintf(stderr, "success! wrote %d frames with %d extra frames\n", (int) outframes, (int) outframes-outcount);
  sf_close(sf);
  free(sfinfo);
  free(obuff);
  obuff=NULL;
  return 1;
}


/**************************************************************************************
 *  int playwav2 (char * sound_file_name, double period, int stimdur, int stimoffset) *
 *   'duration' is the total number of msec to output, use '-1' for whole soundfile   *
 *    'stimoffset' is the number of msec between the beginning of the soundfile and   *
 *       the start of the stimulus                                                    *   
 * playwav2 adds a 50ms linear ramp to the beginning and end of the excised stimulus  * 
 * playwav returns: 1 on successful play                                              *
 *                  -1 when soundfile does not play                                   *
 *                   0 when soundfile plays with under.over run                       *
 *************************************************************************************/

int playwav2(char *sfname, double period, int stimdur, int stimoffset)
{
  
  SNDFILE *sf;
  SF_INFO *sfinfo;
  short *obuff;
  sf_count_t incount;
  double inframes, padded, maxframes;
  long pad = 0;
  int outcount, tmp;
  snd_pcm_uframes_t outframes;
  sf_count_t offset;

  //printf("PLAYWAV2:trying to playwav2 with '%s', offset:%d  stimdur:%d\n",sfname,stimoffset,stimdur);

  /* memory for SF_INFO structures */
  sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));
  
  /* open input file*/
  if(!(sf = sf_open(sfname,SFM_READ,sfinfo))){
    fprintf(stderr,"PLAYWAV2: error opening input file %s\n",sfname);
    free(sfinfo);
    return -1;
  }
  /*print out some info about the file you just openend */
  //printf("\n ---------- Stimulus parameters ------------ \n");
  //printf ("Samples : %d\n", (int)sfinfo->frames) ;
  //printf ("Sample Rate : %d\n", sfinfo->samplerate) ;
  //printf ("Channels    : %d\n", sfinfo->channels) ;
  
  
  /* check that some assumptions are met */
  /* this should be done by the operant code*/
  if (sfinfo->frames > MAXFILESIZE){
    fprintf(stderr,"PLAYWAV2: File is too large!\n");
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  if (sfinfo->samplerate != 44100){
    fprintf(stderr, "PLAYWAV2: Sample rate for %s is not equal to 44.1 KHz!\n", sfname);
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  if (sfinfo->channels != 1){
    fprintf(stderr, "PLAYWAV2: Sound file %s is not mono!\n", sfname);
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  
  /* make sure format is WAV */
  if((sfinfo->format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV){
    printf("PLAYWAV2: Not a WAV file\n");
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  
  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "PLAYWAV2: cannot prepare audio interface for use\n");
    sf_close(sf);
    free(sfinfo);
    return -1;
  }


  /* extract the stimulus to play */
  if (stimoffset< 0){
    fprintf (stderr, "PLAYWAV2: Stimulus offset value must be greater than or equal to zero\n");
    sf_close(sf);
    free(sfinfo);
    return -1;
  }

  offset = (int) rint((float)sfinfo->samplerate*(float)stimoffset/1000.0);
  //printf("PLAYWAV2:offset  is %d msec, which is %d samples\n", stimoffset, (int) offset );
  tmp = sf_seek(sf, offset, SEEK_SET);
  //printf("PLAYWAV2:trying to seek by %d samples; sf_seek returned %d \n", (int)offset, tmp);

  maxframes = (int)sfinfo->frames;
  if (stimdur==-1) /* play the whole soundfile (less any offset)*/
    inframes = maxframes - (int)offset;
  else if(stimdur>0)/* play the define stimulus duration */
    inframes = (int) rint((float)sfinfo->samplerate*(float)stimdur/1000.0);
  else{
    fprintf (stderr, "PLAYWAV2: Invalid stimulus duration '%d'\n",stimdur);
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  
  //printf("stimdur  is %d msec, so I want %d samples after the offset\n", stimdur, (int) inframes );
  
  /* pad the file size up to next highest period count*/
  pad = (period * ceil( inframes/ period))-inframes;
  padded=inframes+pad;
  //printf("pad: %lu\n", pad);
  //printf("period: %d\n",(int) period);
  
  /* allocate buffer memory */
  obuff = (short *) malloc(sizeof(int)*padded);

  /* read the data */
  if( ( maxframes - (offset + inframes) ) <0){
    fprintf (stderr, "PLAYWAV2: The sum of the stimulus duration and offset exceeds the soundfile length\n");
    sf_close(sf);
    free(sfinfo);
    free(obuff);
    return -1;
  }
  //printf("trying to sf_readf %d frames\n",(int)inframes); 
  incount = sf_readf_short(sf, obuff, inframes);
  //printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount, (int)inframes);  
  
  /* ramp the first (and last) 50 ms from zero to normal amplitude*/
  int i;
  int ramp_dur= .05 * sfinfo->samplerate; //number of samples for ramp
  for (i = 0; i<ramp_dur; i++) {
    obuff[i] = obuff[i]*((float)i/(float)ramp_dur);
  }
  for (i = (incount-ramp_dur); i<=incount; i++) {
    obuff[i] = obuff[i]* ((float)(incount-i)/(float)ramp_dur);
  }

  outframes = padded;
  //printf("outframes %d \n", (int)outframes);
  outcount = snd_pcm_writei(handle, obuff, outframes);
  //printf("outcount %d \n", outcount);
  if (outcount == -EPIPE) {/* EPIPE means overrun */
    fprintf(stderr, "PLAYWAV2: overrun while playing %s\n",sfname);
    snd_pcm_prepare(handle);
    return 0;
  } 
  else if (outcount < 0){
    fprintf(stderr,"PLAYWAV2: error from write: %s\n",snd_strerror(outcount));
    return 0;
  }
  else if (outcount != outframes) {
    fprintf(stderr, "PLAYWAV2: oops! tried to write %d samples but only did %d: off by %d \n", 
	    (int) outframes, outcount, outcount-(int) outframes);
    return 0;
  }

  //printf("wrote %d samples when I tried for %d\n", (int) outframes, outcount);
  
  /*free up resources*/
  sf_close(sf);
  free(sfinfo);
  free(obuff);
  return 1;
}


/*****************************************************************************
 *   PLAYBACK UTILITIES:Underrun and suspend recovery
 *                      wait_for_poll
 *****************************************************************************/
/*
static int xrun_recovery(snd_pcm_t *handle, int err)
{
  if (err == -EPIPE) {
    err = snd_pcm_prepare(handle);
    if (err < 0)
      printf("Can't recovery from underrun, prepare failed: %s\n", snd_strerror(err));
    return 0;
  } else if (err == -ESTRPIPE) {
    while ((err = snd_pcm_resume(handle)) == -EAGAIN)
      sleep(1);       
    if (err < 0) {
      err = snd_pcm_prepare(handle);
      if (err < 0)
	printf("Can't recovery from suspend, prepare failed: %s\n", snd_strerror(err));
    }
    return 0;
  }
  return err;
}
static int wait_for_poll(snd_pcm_t *handle, struct pollfd *ufds, unsigned int count)
{
  unsigned short revents;
  while (1) {
    poll(ufds, count, -1);
    snd_pcm_poll_descriptors_revents(handle, ufds, count, &revents);
    if (revents & POLLERR)
      return -EIO;
    if (revents & POLLOUT)
      return 0;
  }
}
*/

/***************************************************************************************
****************************************************************************************
**  int play_and_count(char *sfname, double period, int box_id ,struct PECK * trl)    **
**                                                                                    **
** playabck routine that works the same as playwav except that it uses non-blocking   **
** and can count the number of pecks (and the peck times) to each key during          **
** the stimulus presentation                                                          **
**                                                                                    **
** Returns: 1 and fills the struct PECK on successful play                           **
**          -1 when soundfile does not play                                           **
**          0 when soundfile plays with under.over run                                **
****************************************************************************************
****************************************************************************************/
/*int play_and_count(char *sfname, double period, int box_id ,struct PECK * trl)
  {
  //  TODO:   get this to return a struct that holds 3 arrays of times for pecks at each key

  SNDFILE *sf;
  SF_INFO *sfinfo;
  short *obuff;
  unsigned short *ptr;
  sf_count_t incount;
  double inframes, maxframes;
  int tmp;
  snd_pcm_uframes_t outframes, totframesout;
  int i, err, count, init;
  
  struct pollfd *ufds;
  struct PECK old, new;

  old.left=trl->left;
  old.right=trl->right;
  old.center=trl->center;


  sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));
  

  if(!(sf = sf_open(sfname,SFM_READ,sfinfo))){
  fprintf(stderr,"error opening input file %s\n",sfname);
  free(sfinfo);
  return -1;
  }
  if (snd_pcm_prepare (handle) < 0) {
  fprintf (stderr, "cannot prepare audio interface for use\n");
  sf_close(sf);
  free(sfinfo);
  return -1;
  }
 
  obuff = (short *) malloc(sizeof(int)*inframes);
  incount = sf_readf_short(sf, obuff, inframes);
  if(DEBUG){printf("got %d samples when I tried for %d from sf_readf_short()\n",
  (int)incount, (int)inframes);}  
  outframes = inframes;
  if(DEBUG){printf("I'll try to write %d frames\n", (int)outframes);}
  
  snd_pcm_nonblock(handle,1); 

  count = snd_pcm_poll_descriptors_count (handle);
  if (count <= 0) {
  printf("Invalid poll descriptors count\n");
  return count;
  }
  ufds = malloc(sizeof(struct pollfd) * count);
  if (ufds == NULL) {
  printf("Not enough memory\n");
  return -ENOMEM;
  }
  if ((err = snd_pcm_poll_descriptors(handle, ufds, count)) < 0) {
  printf("Unable to obtain poll descriptors for playback: %s\n", snd_strerror(err));
  return err;
  }
  init = 1;
  if (!init) {
  err = wait_for_poll(handle, ufds, count);
  if (err < 0) {
  if (snd_pcm_state(handle) == SND_PCM_STATE_XRUN ||
  snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
  err = snd_pcm_state(handle) == SND_PCM_STATE_XRUN ? -EPIPE : -ESTRPIPE;
  if (xrun_recovery(handle, err) < 0) {
  printf("Write error: %s\n", snd_strerror(err));
  exit(EXIT_FAILURE);
  }
  init = 1;
  } else {
  printf("Wait for poll failed\n");
  return err;
  }
  }
  }
  totframesout=0;
  ptr=obuff;
  gettimeofday(&stimstart, NULL);

  while (outframes > 0) {
  err = snd_pcm_writei(handle,ptr, outframes);
  if (err < 0) {
  if (xrun_recovery(handle, err) < 0) {
  printf("Write error: %s\n", snd_strerror(err));
  exit(EXIT_FAILURE);
  }
  init = 1;
  break;  
  }
  if (snd_pcm_state(handle) == SND_PCM_STATE_RUNNING)
  init = 0;
  
  new.right = operant_read(box_id, RIGHTPECK);    
  new.left = operant_read(box_id, LEFTPECK);      
  new.center = operant_read(box_id, CENTERPECK);  
  gettimeofday(&resp_lag, NULL);
    

  if(new.right > old.right){
  timersub (&resp_lag, &stimstart, &resp_rt);  
  trl->right++;
  }
  else if(new.left > old.left){
  timersub (&resp_lag, &stimstart, &resp_rt);  
  trl->left++;
  }
  else if (new.center > old.center){
  timersub (&resp_lag, &stimstart, &resp_rt);  
  trl->center++;
  }
  old=new;

  totframesout += err; 
  ptr += err * channels;
  outframes -= err;

  if (outframes == 0){
  if(DEBUG){printf("outframes is zero so I'll break\n");}
  break;
  }

  err = wait_for_poll(handle, ufds, count);
  if (err < 0) {
  if (snd_pcm_state(handle) == SND_PCM_STATE_XRUN ||
  snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
  err = snd_pcm_state(handle) == SND_PCM_STATE_XRUN ? -EPIPE : -ESTRPIPE;
  if (xrun_recovery(handle, err) < 0) {
  printf("Write error: %s\n", snd_strerror(err));
  exit(EXIT_FAILURE);
  }
  init = 1;
  } else {
  printf("Wait for poll failed\n");
  return err;
  }
  }
  }
  if(DEBUG){
  printf("exited while writei loop, so what am I waiting for?\n");
  printf("frames not played: %d \n", (int)outframes);
  printf("frames played: %d \n", (int) totframesout);
  }
  sf_close(sf);
  free(sfinfo);
  sfinfo=NULL;
  free(obuff);
  obuff=NULL;
  free(ufds);
  ufds=NULL;
  return (1);
  }
*/

/*************************************************************************************************
**************************************************************************************************
**  int playmaskedwav(int wndur, int wnspl, char *sfname, int lag, int sfspl,  double period)   **
**                                                                                              **
**  use this to play wav file embedded in white noise with a onset lag                          **
**  user sets the level of nosie and  target in dB, and noise duration                          **
**                                                                                              **
**  Returns: 1 on successful play                                                               **
**          -1 when soundfile does not play                                                     **
**           0 when soundfile plays with under.over run                                         **
**************************************************************************************************
*************************************************************************************************/
int playmaskedwav(int wndur, int wnspl, char *sf1name, int lag, int target_spl,  double period)
{
  SNDFILE *sf1;
  SF_INFO *sf1info;
  short *obuff1, *obuff2;
  sf_count_t incount1;
  double inframes1, nnoiseframes, lagoffset, LLrms, targetSR, padded;
  long pad=0, nsamps;
  float foo;

  int outcount;
  //snd_pcm_uframes_t outframes;
 
  int ramp_dur = 10; /*noise onset & offset ramp in msec*/
  int DEBUG =1 ;
  int nbits, bw, j, k, i, max;
  signed int total;

  float dcoff1, dcoff2, rms1, rms2, db1, db2, newrms, scale,peakrms, peak_db;

  printf("PLAYMASKEDWAV:trying to play '%s'at %d dB in a %d msec %d dB noise with lag of %d msec\n",sf1name,target_spl,wndur, wnspl,lag);

  /* get memory for SF_INFO structures */
  sf1info = (SF_INFO *) malloc(sizeof(SF_INFO));
    
  /* open input file1*/
  if(!(sf1 = sf_open(sf1name,SFM_READ,sf1info))){
    fprintf(stderr,"PLAYMASKEDWAV: error opening input file %s\n",sf1name);
    free(sf1info);
    return -1;
  }

  inframes1=(int)sf1info->frames;
  pad = (period * ceil( inframes1/ period))-inframes1;
  padded=inframes1+pad;

  /* allocate buffer memory */
  obuff1 = (short *) malloc(sizeof(int)*padded);
    
  /* read the data */
  incount1 = sf_readf_short(sf1, obuff1, inframes1);
  if(DEBUG){printf("inframes1 is %d\n",(int)inframes1);}

  if(incount1!=inframes1){
    fprintf(stderr, "got %d samples when I tried for %d from sf_readf_short()\n",(int)incount1, (int)inframes1);
    sf_close(sf1);
    free(sf1info);
    free(obuff1);
    return(-1);
  }

  /*check some things and prepare the dac handle*/
  if(inframes1 > MAXFILESIZE){
    fprintf(stderr,"PLAYMAKEDWAV: One or both of the soundfiles is too large!\n");
    sf_close(sf1);
    free(sf1info);
    free(obuff1);
    return -1;
  }
  if (sf1info->samplerate != 44100){
    fprintf(stderr, "PLAYMASKEDWAV: The sample rate for '%s' is not equal to 44.1 kHz\n", sf1name);
    sf_close(sf1);
    free(sf1info);
    free(obuff1);
    return -1;
  }
  if (sf1info->channels != 1){
    fprintf(stderr, "PLAYMASKEDWAV: '%s' is not a mono sound file\n", sf1name);
    sf_close(sf1);
    free(sf1info);
    free(obuff1);
    return -1;
  }
  if((sf1info->format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV){
    printf("PLAYMASKEDWAV: '%s' is not a WAV file\n", sf1name);
    sf_close(sf1);
    free(sf1info);
    free(obuff1);
    return -1;
  }
  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "PLAY2WAVS: cannot prepare audio interface for use\n");
    sf_close(sf1);
    free(sf1info);
    free(obuff1);
    return -1;
  }

  /*make some noise*/
  pad=nnoiseframes=0;
  targetSR=(double)sf1info->samplerate;
  nsamps = (int) rint((float)targetSR*(float)wndur/1000.0);  
  pad = (period * ceil( nsamps/ period))-nsamps;
  nnoiseframes = nsamps+pad;

  if(DEBUG){printf("nnoiseframes is %d \n", (int)nnoiseframes);}
  obuff2 = (short *) malloc(sizeof(int)*nnoiseframes);
  srand(time(0));

  for (i=0;i<nsamps;i++)
  obuff2[i]= (int) (32767*2*(float) rand()/RAND_MAX+0.0)-32767 ;
   
  for(i=nsamps;i<nnoiseframes;i++)
    obuff2[i]=0; /*make sure your pad is silent*/    

  /*ramp the start and end of the noise*/
  ramp_dur = (int) rint((float)targetSR*(float)ramp_dur/1000); //number of samples for ramp
  for (i = 0; i<ramp_dur; i++) 
    obuff2[i] = obuff2[i]*((float)i/(float)ramp_dur);
  for (i = (nsamps-ramp_dur); i<=nsamps; i++) 
    obuff2[i] = obuff2[i]* ((float)(nsamps-i)/(float)ramp_dur);
  
  /*make sure the noise is longer than target+lag*/
  lagoffset = (int) rint((float)sf1info->samplerate*(float)lag/1000.0);
  if(DEBUG){printf("PLAYMASKEDWAV:stim 2 onset lag  is %d msec, which is %d samples\n", lag, (int) lagoffset );}
  if(nnoiseframes <(inframes1 + lagoffset)){
    fprintf (stderr, "PLAYMASKEDWAV: the duration of sthe target plus the lag must be less than the duration of soundfile 1\n");
    sf_close(sf1);
    free(sf1info);
    free(obuff1);free(obuff2);
    return -1;
  }	

  /*get the levels for the target soundfile*/
  nbits = 16;
  bw=6.0206*nbits;  /*we assume 16bit soundfiles for now*/
  
  total=0;
  for (j=0; j < inframes1; j++)
    total += obuff1[j];
  dcoff1 = (float) (total * 1.0 / inframes1);
  if(DEBUG){printf("DC offset for '%s' is %f\n", sf1name, dcoff1);}

  total=0;
  for (k=0; k < nnoiseframes; k++)
    total += obuff2[k];
  dcoff2 = (float) (total * 1.0 / nnoiseframes);
  if(DEBUG){printf("DC offset for the masker is %f\n",dcoff2);}

  LLrms=0.0;
  for (j=0; j<inframes1; j++)
    LLrms += SQR(obuff1[j] - dcoff1);
  rms1 = sqrt(LLrms / (double)inframes1) / (double) pow(2,15);
  db1 = bw + (20.0 * log10(rms1) );
  if(DEBUG){printf("rms for '%s' is %f dB SPL\n", sf1name, db1);}
  
  LLrms=0.0;
  for (k=0; k < nnoiseframes; k++)
    LLrms += SQR(obuff2[k] - dcoff2);
  rms2 = sqrt(LLrms / (double)nnoiseframes)/ (double) pow(2,15);
  db2 = bw + (20.0 * log10(rms2) );
  if(DEBUG){printf("rms for the masker is %f dB SPL\n", db2);}
  
  /*scale sfs to the new levels */
  newrms=scale=0.0;
  if(target_spl!=0){
    foo=(target_spl-bw)/20.0;
    newrms = pow(10,foo);
    scale = newrms/rms1;
  }
  if(DEBUG){printf("newrms:%g, tmp: %g, rms1: %g, scale: %g\n", newrms, foo, rms1, scale);}
  for (j=0; j<inframes1; j++)	
    obuff1[j] = scale * obuff1[j];
  
  newrms=scale=0.0;
  foo=(wnspl-bw)/20.0;
  newrms = pow(10, foo);
  scale = newrms/rms2;
  if(DEBUG){printf("newrms:%g, tmp: %g, rms1: %g, scale: %g\n", newrms, foo, rms1, scale);}
  
  for (j=0; j<nnoiseframes; j++)
    obuff2[j] = scale * obuff2[j];
  
  if(DEBUG){
    LLrms=0.0;
    for (j=0; j<inframes1; j++)
      LLrms += SQR(obuff1[j] - dcoff1);
    rms1 = sqrt(LLrms / (double)inframes1) / (double) pow(2,15);
    db1 = bw + (20.0 * log10(rms1) );
    printf("scaled rms for '%s' is %f dB SPL\n", sf1name, db1);
    
    LLrms=0.0;
    for (k=0; k < nnoiseframes; k++)
      LLrms += SQR(obuff2[k] - dcoff2);
    rms2 = sqrt(LLrms / (double)nnoiseframes) /(double) pow(2,15);
    db2 = bw + (20.0 * log10(rms2) );
    printf("scaled rms for the masker is %f dB SPL\n", db2);
  }
  
  
  /*add sf1 to the noise masker starting at the lag+1 sample*/
  for (i = 0; i<inframes1; i++)
    obuff2[i+(int)lagoffset] = obuff2[i+(int)lagoffset] + obuff1[i];
  
  /*peak check*/
  for(i=0; i<nnoiseframes;i++){
    if( max < SQR(obuff1[j]) )
      max = SQR(obuff1[j]);
  }
  
  peakrms=sqrt(max);
  peak_db = bw + (20*log10(peakrms));
  if(peak_db>90){
    fprintf(stderr, "PLAYMASKERWAV: SPL in the combined soundfile exceeds 90 dB, SETTING THE MAX TO 90 dB \n");
    scale = 90/peakrms;
    for(i=0; i<inframes1;i++)
      obuff1[j] = scale * obuff1[j];
  }
  
  /*play the combined soundfile (obuff1) */

  printf("nnoiseframes %d \n", (int)nnoiseframes);
  outcount = snd_pcm_writei(handle, obuff2, nnoiseframes);
  printf("outcount %d \n", outcount);
  if (outcount == -EPIPE) {/* EPIPE means overrun */
    fprintf(stderr, "PLAYMAKEDWAV: overrun while playing masked %s\n",sf1name);
    snd_pcm_prepare(handle);
    return 0;
  } 
  else if (outcount < 0){
    fprintf(stderr,"PLAYMASKEDWAV: error from write: %s\n",snd_strerror(outcount));
    return 0;
  }
  else if (outcount != nnoiseframes) {
    fprintf(stderr, "PLAYMASKEDWAV: oops! tried to write %d samples but only did %d: off by %d \n", 
	    (int) nnoiseframes, outcount, outcount-(int) nnoiseframes);
    return 0;
  }
  
  printf("wrote %d samples when I tried for %d\n", (int) nnoiseframes, outcount);
  
  /*free up resources*/
  sf_close(sf1);
  free(sf1info);
  free(obuff1);free(obuff2);
  return 1;
}
  
/********************************************************
 *  int playwav (char * sound_file_name, double period) *
 *                                                      *
 * returns: 1 on successful play                        *
 *          -1 when soundfile does not play             *
 *          0 when soundfile plays with under.over run  *
 *******************************************************/
int playstreamchg_train(char *sf1name, char *sf2name, double period, float trialdur, float distdur, int trialtype)
{
  
  SNDFILE *sf1, *sf2;
  SF_INFO *sf1info, *sf2info;
  short *obuff1, *obuff2, *obuff3;
  sf_count_t incount1, incount2;
  double inframes1, padded, inframes2;
  long pad = 0;
  int outcount, fs = 44100, nsamps, distsamps, i;
  snd_pcm_uframes_t outframes;
  div_t divres;

  int DEBUG = 0;
      

  /* memory for SF_INFO structures */
  sf1info = (SF_INFO *) malloc(sizeof(SF_INFO));
  sf2info = (SF_INFO *) malloc(sizeof(SF_INFO));

  //fprintf(stderr, "trying to open '%s'\n", sfname); 
  /* open input file*/
  if(!(sf1 = sf_open(sf1name,SFM_READ,sf1info))){
    fprintf(stderr,"error opening input file %s\n",sf1name);
    return -1;
  }
  if(!(sf2 = sf_open(sf2name,SFM_READ,sf2info))){
    fprintf(stderr,"error opening input file %s\n",sf2name);
    return -1;
  }
  /*print out some info about the file you just openend */
  //fprintf(stderr,"\n ---------- Stimulus parameters from playwav ------------ \n");
  //fprintf (stderr, "    Samples: %d\n", (int)sfinfo->frames) ;
  //fprintf (stderr, "Sample Rate: %d\n", sfinfo->samplerate) ;
  //fprintf (stderr, "   Channels: %d\n", sfinfo->channels) ;
  
  
  /* check that some assumptions are met */
  /* this should be done by the operant code*/

  /* checking target stim */
  if (sf1info->frames > MAXFILESIZE){
    fprintf(stderr,"File is too large!\n");
    sf_close(sf1);
    sf_close(sf2);
    return -1;
  }
  if (sf1info->samplerate != fs){
    fprintf(stderr, "Sample rate for %s is not equal to %d!\n", sf1name, fs);
    sf_close(sf1);
    sf_close(sf2);
    return -1;
  }
  if (sf1info->channels != 1){
    fprintf(stderr, "Sound file %s is not mono!\n", sf1name);
    sf_close(sf1);
    sf_close(sf2);
    return -1;
  }
  if((sf1info->format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV){
    printf("Not a WAV file\n");
    sf_close(sf1);
    sf_close(sf2);
    return -1;
  }

  /* checking distracter stim */
  if (sf2info->frames > MAXFILESIZE){
    fprintf(stderr,"File is too large!\n");
    sf_close(sf1);
    sf_close(sf2);
    return -1;
  }
  if (sf2info->samplerate != fs){
    fprintf(stderr, "Sample rate for %s is not equal to %d!\n", sf2name, fs);
    sf_close(sf1);
    sf_close(sf2);
    return -1;
  }
  if (sf2info->channels != 1){
    fprintf(stderr, "Sound file %s is not mono!\n", sf2name);
    sf_close(sf1);
    sf_close(sf2);
    return -1;
  }
  if((sf2info->format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV){
    printf("Not a WAV file\n");
    sf_close(sf1);
    sf_close(sf2);
    return -1;
  }
  
  /* read the data */
  inframes1 = (int)sf1info->frames;
  obuff1 = (short *) malloc(sizeof(int)*inframes1);
  incount1 = sf_readf_short(sf1, obuff1, inframes1);
  if(DEBUG){printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount1, (int)inframes1);}

  inframes2 = (int)sf2info->frames;
  obuff2 = (short *) malloc(sizeof(int)*inframes2);
  incount2 = sf_readf_short(sf2, obuff2, inframes2);
  if(DEBUG){printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount2, (int)inframes2);}

  /* determine number of desired samples */
  nsamps = (int)(fs*trialdur);
  distsamps = (int)(fs*distdur);
  pad = (period * ceil(nsamps/period))-nsamps;
  padded = nsamps + pad;
  obuff3 = (short *) malloc(sizeof(int)*padded);

  if(DEBUG){printf("padded = %d, nsamps = %d, distsamps = %d\n",(int)padded, nsamps, distsamps);}

  /* create outbuffer */
  if (trialtype == 1){
    for (i = 0; i < nsamps; i++){
      divres = div(i,inframes1);
      obuff3[i] = obuff1[divres.rem];
    }
  }
  else if (trialtype == 2){
    for (i = 0; i < nsamps; i++){
      if (i < (nsamps - distsamps)){
	divres = div(i, inframes1);
	/*if(DEBUG){printf(" %d", divres.rem);}*/
	obuff3[i] = obuff1[divres.rem];
      }
      else {
	divres = div(i, inframes2);
	obuff3[i] = obuff2[divres.rem];
      }
    }
  }
   
  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use\n");
    sf_close(sf1);
    free(sf1info);
    free(obuff1);
    sf_close(sf2);
    free(sf2info);
    free(obuff2);
    free(obuff3);
    return -1;
  }
  if (snd_pcm_reset(handle)<0){
    fprintf (stderr, "cannot reset audio interface for use\n");
    sf_close(sf1);
    free(sf1info);
    free(obuff1);
    sf_close(sf2);
    free(sf2info);
    free(obuff2);
    free(obuff3);
    return -1;
  }
  outframes = padded;
  if(DEBUG){printf("\noutframes %d \n", (int)outframes);}
  outcount = snd_pcm_writei(handle, obuff3, outframes);
  if(DEBUG){printf("outcount %d \n", (int)outcount);}
  if (outcount == -EPIPE) {
    fprintf(stderr, "overrun while playing %s\n",sf1name);
    snd_pcm_prepare(handle);
    sf_close(sf1);
    free(sf1info);
    free(obuff1);
    sf_close(sf2);
    free(sf2info);
    free(obuff2);
    free(obuff3);
    return 0;
  } 
  else if (outcount < 0){
    fprintf(stderr,"error from write: %s\n",snd_strerror(outcount));
    sf_close(sf1);
    free(sf1info);
    free(obuff1);
    sf_close(sf2);
    free(sf2info);
    free(obuff2);
    free(obuff3);
    return 0;
  }
  else if (outcount != outframes) {
    fprintf(stderr, "oops! tried to write %d samples but only did %d: off by %d \n", 
	    (int) outframes, outcount, outcount-(int) outframes);
    sf_close(sf1);
    free(sf1info);
    free(obuff1);
    sf_close(sf2);
    free(sf2info);
    free(obuff2);
    free(obuff3);
    return 0;
  }
 
  
  /*free up resources*/
  sf_close(sf1);
  free(sf1info);
  free(obuff1);
  sf_close(sf2);
  free(sf2info);
  free(obuff2);
  free(obuff3);
  return 1;
}

 
  

/********************************************************
 *  int playwav (char * sound_file_name, double period) *
 *                                                      *
 * returns: 1 on successful play                        *
 *          -1 when soundfile does not play             *
 *          0 when soundfile plays with under.over run  *
 *******************************************************/
/* int playstreamchg(stim **stimulus, int stim_number, double period, char *STIMPATH, float trialdur, float trialtype) */
/* { */
  
/*   SNDFILE *sf; */
/*   SF_INFO *sfinfo; */
/*   short *obuff; */
/*   sf_count_t incount; */
/*   double inframes, padded; */
/*   long pad = 0; */
/*   int outcount; */
/*   char stimexm[128], sfname[256]; */
/*   snd_pcm_uframes_t outframes; */
      
/*   strcpy (stimexm, stimulus[stim_number].exemplar);                       /\* get exemplar filename *\/ */
/*   sprintf(sfname,"%s%s", STIMPATH, stimexm);                                /\* add full path to file name *\/ */

/*   /\* memory for SF_INFO structures *\/ */
/*   sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO)); */

/*   //fprintf(stderr, "trying to open '%s'\n", sfname);  */
/*   /\* open input file*\/ */
/*   if(!(sf = sf_open(sfname,SFM_READ,sfinfo))){ */
/*     fprintf(stderr,"error opening input file %s\n",sfname); */
/*     return -1; */
/*   } */
/*   /\*print out some info about the file you just openend *\/ */
/*   //fprintf(stderr,"\n ---------- Stimulus parameters from playwav ------------ \n"); */
/*   //fprintf (stderr, "    Samples: %d\n", (int)sfinfo->frames) ; */
/*   //fprintf (stderr, "Sample Rate: %d\n", sfinfo->samplerate) ; */
/*   //fprintf (stderr, "   Channels: %d\n", sfinfo->channels) ; */
  
  
/*   /\* check that some assumptions are met *\/ */
/*   /\* this should be done by the operant code*\/ */
/*   if (sfinfo->frames > MAXFILESIZE){ */
/*     fprintf(stderr,"File is too large!\n"); */
/*     sf_close(sf); */
/*     return -1; */
/*   } */
/*   if (sfinfo->samplerate != 44100){ */
/*     fprintf(stderr, "Sample rate for %s is not equal to 44.1 KHz!\n", sfname); */
/*     sf_close(sf); */
/*     return -1; */
/*   } */
/*   if (sfinfo->channels != 1){ */
/*     fprintf(stderr, "Sound file %s is not mono!\n", sfname); */
/*     sf_close(sf); */
/*     return -1; */
/*   } */
  
/*   /\* make sure format is WAV *\/ */
/*   if((sfinfo->format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV){ */
    
/*     /\* pad the file size up to next highest period count*\/ */
/*     inframes=(int)sfinfo->frames; */
/*     pad = (period * ceil( inframes/ period))-inframes; */
/*     padded=inframes+pad; */

/*     /\* allocate buffer memory *\/ */
/*     obuff = (short *) malloc(sizeof(int)*padded); */
    
/*     /\* read the data *\/ */
/*     incount = sf_readf_short(sf, obuff, inframes); */
/*     //printf("got %d samples when I tried for %d from sf_readf_short()\n",(int)incount, (int)inframes); */
/*   } */
/*   else { */
/*     printf("Not a WAV file\n"); */
/*     sf_close(sf); */
/*     return -1; */
/*   } */
   
/*   if (snd_pcm_prepare (handle) < 0) { */
/*     fprintf (stderr, "cannot prepare audio interface for use\n"); */
/*     sf_close(sf); */
/*     free(sfinfo); */
/*     free(obuff); */
/*     return -1; */
/*   } */
/*   if (snd_pcm_reset(handle)<0){ */
/*     fprintf (stderr, "cannot reset audio interface for use\n"); */
/*     return -1; */
/*   } */
/*   outframes = padded; */
/*   //fprintf(stderr, "outframes %d \n", (int)outframes); */
/*   outcount = snd_pcm_writei(handle, obuff, outframes); */
  
/*   //fprintf(stderr,"outcount %d \n", outcount); */
/*   if (outcount == -EPIPE) { */
/*     fprintf(stderr, "overrun while playing %s\n",sfname); */
/*     snd_pcm_prepare(handle); */
/*     return 0; */
/*   }  */
/*   else if (outcount < 0){ */
/*     fprintf(stderr,"error from write: %s\n",snd_strerror(outcount)); */
/*     return 0; */
/*   } */
/*   else if (outcount != outframes) { */
/*     fprintf(stderr, "oops! tried to write %d samples but only did %d: off by %d \n",  */
/* 	    (int) outframes, outcount, outcount-(int) outframes); */
/*     return 0; */
/*   } */
 
  
/*   /\*free up resources*\/ */
/*   sf_close(sf); */
/*   free(sfinfo); */
/*   free(obuff); */
/*   return 1; */
/* } */
  
/*************************getsoundvec*******************************/
int getsoundvec(char *sfname, double nidealsamps,short *svoutp, float dB, double period)
{

  SNDFILE *sf;
  SF_INFO *sfinfo;
  double inframes, padded; 
  float fs;
  long pad=0;
  sf_count_t incount;
  short *obuff;
  int i,j;

  signed int total;
  float dcoff, db_orig, newrms, rms, scale, foo;
  double LLrms;
  int nbits, bw; 

  nbits = 16;
  bw=6.0206*nbits;  /*we assume 16bit soundfiles for now*/

  /* memory for SF_INFO structures */
  sfinfo = (SF_INFO *) malloc(sizeof(SF_INFO));

  //fprintf(stderr, "trying to open '%s'\n", sfname); 
  /* open input file*/
  if(!(sf = sf_open(sfname,SFM_READ,sfinfo))){
    fprintf(stderr,"error opening input file %s\n",sfname);
    free(sfinfo);
    return -1;
  }
  
  /* check that some assumptions are met */
  /* this should be done by the operant code*/
  if (sfinfo->frames > MAXFILESIZE){
    fprintf(stderr,"File is too large!\n");
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  if (sfinfo->samplerate != 44100){
    fprintf(stderr, "Sample rate for %s is not equal to 44.1 KHz!\n", sfname);
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  if (sfinfo->channels != 1){
    fprintf(stderr, "Sound file %s is not mono!\n", sfname);
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  if((sfinfo->format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV){
    printf("GETSOUNDVEC: '%s' is not a WAV file\n", sfname);
    sf_close(sf);
    free(sfinfo);
    return -1;
  }
  

  fs = (float)sfinfo->samplerate;
  inframes=(int)sfinfo->frames;
  pad = (long) (period * ceil(nidealsamps/ period))-inframes;
  padded=inframes+pad;

  obuff = (short *) malloc(sizeof(int)*padded);
  incount = sf_readf_short(sf, obuff, inframes);

  /*fprintf(stderr, "nidealsamps = %d, period = %d, pad = %d, padded = %d, got %d samples when I tried for %d\n",(int) nidealsamps, (int) period, (int) pad, (int) padded, (int)incount, (int)inframes);*/
  if(incount!=inframes){
    sf_close(sf);
    free(sfinfo);
    free(obuff);
    return -1;
  }
   
  total=0;
  for (j=0; j < inframes; j++)
    total += obuff[j];
  dcoff = (float) (total * 1.0 / inframes);
  /* {printf("DC offset for '%s' is %f\n", sfname, dcoff);}*/

  LLrms=0.0;
  for (j=0; j<inframes; j++)
    LLrms += SQR(obuff[j] - dcoff);
  rms = sqrt(LLrms / (double)inframes) / (double) pow(2,15);
  db_orig = bw + (20.0 * log10(rms) );
  /*{printf("rms for '%s' is %.4f dB SPL, adjusting to %.4f dB SPL\n", sfname, db_orig, dB);}*/
  
  /*scale sfs to the new levels */
  newrms=scale=0.0;
  foo=(dB-bw)/20.0;
  newrms = pow(10,foo);
  scale = newrms/rms;
 
  for (j=0; j<inframes; j++)
    obuff[j] = scale * obuff[j];

  /* ramp the first (and last) 50 ms from zero to normal amplitude to avoid popping/clicking*/
  int ramp_dur= .05 * sfinfo->samplerate; //number of samples for ramp
  for (i = 0; i<ramp_dur; i++) {
    obuff[i] = obuff[i]*((float)i/(float)ramp_dur);
  }
  for (i = (incount-ramp_dur); i<=incount; i++) {
    obuff[i] = obuff[i]* ((float)(incount-i)/(float)ramp_dur);
  }

  j=0;
  while(j<((int) inframes)){
    *svoutp= *(obuff+j);
    svoutp++;
    j++;
  }

  sf_close(sf);
  free(sfinfo);
  free(obuff);
  /*{printf("exiting properly\n");}*/
  
  return 1;
}


/*********************************************************************
 ********************************************************************/
int playsoundvec (short *obuff, double ntotalsamps, double period )
{ 
  
  long pad;
  double padded;
  int outcount;
  snd_pcm_uframes_t outframes;


  if (snd_pcm_prepare (handle) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use\n");
    free(obuff);
    return -1;
  }
  if (snd_pcm_reset(handle)<0){
    fprintf (stderr, "cannot reset audio interface for use\n");
    return -1;
  }

  pad = (long) (period * ceil(ntotalsamps/ period))-ntotalsamps;
  padded=ntotalsamps+pad;

 
  outframes = padded;
  //fprintf(stderr, "outframes %d \n", (int)outframes);
  outcount = snd_pcm_writei(handle, obuff, outframes);
  
  //fprintf(stderr,"outcount %d \n", outcount);
  if (outcount == -EPIPE) {
    fprintf(stderr, "overrun while playing soundfile");
    snd_pcm_prepare(handle);
    return 0;
  } 
  else if (outcount < 0){
    fprintf(stderr,"error from write: %s\n",snd_strerror(outcount));
    return 0;
  }
  else if (outcount != outframes) {
    fprintf(stderr, "oops! tried to write %d samples but only did %d: off by %d \n", 
	    (int) outframes, outcount, outcount-(int) outframes);
    return 0;
  }
 
  return 1;
}
