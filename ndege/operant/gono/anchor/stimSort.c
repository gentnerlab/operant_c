#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>
#include <sndfile.h>

#include "/usr/local/src/operantio/operantio.c"
#include "/usr/local/src/audioio/audout.c"

#define TONES_PER_TRIAL 2
#define ANCHOR_SENTINEL 100

void anchorSeries();
int coinToss();

int heads = 0;
int tails = 0;

int anchorStore[TONES_PER_TRIAL][ANCHOR_SENTINEL]; //create gloabl array to temporarily store anchored trial sequences
int anchorPos = 0; //establish position of anchor

int main(int argc, char* argv[]) {
  printf("hi");
  srand(time(NULL));
  int anchorArray[5]  = {1,2,3,4,5}; //possible anchors in set
  anchorSeries(anchorArray[anchorPos]);
  printf("%d and %d", heads, tails);
  return 1;
}

void anchorSeries (int anchor) {
  int trial = 0;
  do {
    coinToss();
   
    trial++;
  } while(trial < ANCHOR_SENTINEL); // sentinel value for anchor trials run per block
}

int coinToss () {
  int i = rand() % 2;

  if (i == 0) {
    heads++;
    return 1;
  } else { tails ++; return 0;}
}
