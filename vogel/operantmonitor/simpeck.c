
/*****************************************************************
 * Operant Monitor
 * Copyright Donour Sizemore and University of Chicago 2001
 *
 * Interface application to operantIO
 *
 * 8-22-2001: Initial version released
 *
 * 8-27-2001: Changing boxes now clears the box immediately.
 * Changing to simulate quick pecks for testing code remotely.
 *****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "/usr/local/src/operantio/operantio_6509.c"

int active_box=0;

void go_interactive();
void print_usage()
{
  printf("Correct Usage: simpeck <boxid>\n");
}

int main(int argc, char **argv)
{
  /* Parse command line */

  if(argc<2){
    print_usage();
    exit(-1);
  }
  active_box = atoi(argv[1]);
  operant_open();
  go_interactive();
  operant_close();
  return 0;
}


void go_interactive()
{
  char input[80];
  int chan;
  do{

    printf("%d >", active_box);
    scanf("%s", input);

    if(!strcmp(input, "l"))
      {
	printf("Left peck\n");
	operant_write(active_box, chan, 0);
      }
    else if(!strcmp(input, "c"))
      {
	scanf("%d", &chan);
	printf("Turn on %d\n", chan);
	operant_write(active_box, chan, 1);
      }
    else if(!strcmp(input, "r"))
      {
	scanf("%d", &chan);
	printf("Channel %d -- %d \n",chan, operant_read(active_box, chan));
      }
    else if(!strcmp(input, "box"))
      {
	scanf("%d", &active_box);
      }
    else if(!strcmp(input, "clear"))
      {
	operant_clear(active_box);
	printf("Cleared\n");
      }


  }while(strcmp(input, "quit"));

}






