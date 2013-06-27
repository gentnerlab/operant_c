#ifndef __AUDOUT_H
#define __AUDOUT_H

#define MAXPECKS 1024

struct PECK{
  int left;
  int center;
  int right;
};

struct PECKTIME{
  float pecktime[MAXPECKS];
  char peckkey[MAXPECKS];
};

#endif
