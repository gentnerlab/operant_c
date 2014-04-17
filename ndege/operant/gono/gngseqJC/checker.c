#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]){

FILE *infile;
infile = fopen("999.summaryDAT", "r");
  char str_buf[100];
  if (infile == NULL) {
    printf("No summaryDAT file");
  } else {
    fgets(str_buf, 10, infile);    
  }
    int n = atoi(&str_buf[0]);
    printf("%i", n);

return 0;
}
