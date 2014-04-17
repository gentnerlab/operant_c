/*****************************************************************
 * Operant Test
 * 
 * Interface application to operantIO
 *
 * 8-22-2001: Initial version released
 *
 * 8-27-2001: Changing boxes now clears the box immediately.
 * now runs as operant_test with automatic test routine for the box
 * 
*****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "../operantio/operantio.c"


int active_box=0;

void test_loop();
void go_interactive();
void print_usage()
{
  printf("Correct Usage: operant_monitor <boxid>\n");
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
  operant_clear(active_box);
  go_interactive();
  operant_close();
  return 0;
}


void go_interactive()
{
  char input[80];
  do{

    printf("%d >", active_box);
    scanf("%s", input);

    if(!strcmp(input, "test"))
      {
	printf("Running test loop for box %d\n", active_box);
	test_loop();
      }
    else if(!strcmp(input, "box"))
      {
	scanf("%d", &active_box);
	operant_clear(active_box);
      }
    else if(!strcmp(input, "clear"))
      {
	operant_clear(active_box);
	printf("Cleared\n");
      }


  }while(strcmp(input, "quit"));

}

void test_loop()
{
  int i=0, j= 0, testint=3, check=0, loop=0;
  //struct timespec rsi = { 0,5000000};
 printf("testing inputs for box %d\n", active_box);
  for (i=1; i<4;i++){
    printf("activate input %d\n", i); 
    do{
    usleep(10000);
    check=operant_read(active_box, i);
    ++loop;
    } while (check == 0);
    }
  printf("testing outputs for box %d\n", active_box);
  for (j=1;j<6;j++){
    operant_write(active_box, j, 1);
    printf("chan %d on\t", j);
    sleep(testint);
    operant_write(active_box, j, 0);
    printf("chan %d off\n", j);
  }
}




