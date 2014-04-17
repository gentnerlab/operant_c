#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <math.h>
#include <signal.h>
#include <sys/time.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include "/usr/local/src/operantio/operantio.c"
#include <sunrise.h>

void fillvec(short *vecp, int n)
{    
  SNDFILE *sf;
  short *op;
  int i;

  op = (short *) malloc(sizeof(int)*n);


  i = 0;

}

int main(void)
{
  short vec[256]; 
  int i, n = 20;

  printf("\nold: ");
  for (i = 0; i < n; i++){
    vec[i] = (int) floor(rand());
    printf("%d ", vec[i]);
  }
  
  fillvec(vec, n);
  
  printf("\nnew: ");
  for (i = 0; i < n; i++){
    printf("%d ", vec[i]);
  }
  return vec[1];
}

    


 
