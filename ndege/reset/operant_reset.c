/*****************************************************************
 * Operant Reset
 *
 * Interface application to operantIO
 *
 * Initializes all IO ports (as per opernatio.c) and turns all outputs off 
 *****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "/usr/local/src/operantio/operantio.c"

int main(void)
{
  operant_init();
  return 0;
}







