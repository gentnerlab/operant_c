/**********************************************************
 *  Operant I/O library
 *  
 *  Copyright 2001 Donour Sizemore & University of Chicago
 *
 *  6-28-01 : Initial version completed.   
 *
 *  8-20-01 : Modified the library to work with BOTH comedi
 *            driver and Amish's ISA-DIO96 driver.  Is able
 *            to read and write with single bit granularity
 *            but it's ugly.
 **********************************************************/

#include <stdio.h>
#include <comedilib.h>
#include <stdlib.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include "pcmio.h"


/* INPUT Chans (Basically just STUPID offsets) */ 
#define  IN_8      7
#define  IN_7      6
#define  IN_6      5
#define  IN_5      4
#define  IN_4      3
#define  IN_3      2
#define  IN_2      1
#define  IN_1      0

/* OUTPUT Chans (again) */
#define  OUT_8      7
#define  OUT_7      6
#define  OUT_6      5
#define  OUT_5      4
#define  OUT_4      3
#define  OUT_3      2
#define  OUT_2      1
#define  OUT_1      0


#define CARD0  "/dev/comedi0"
#define CARD1  "/dev/comedi1"

comedi_t *operant_comedi_device =NULL; 
comedi_t *operant_comedi_device2=NULL; 


/* FD for Amish S. Dave's isa driver */
int asd_infd=0, asd_outfd = 0;
unsigned char asd_mask = 0; 
int operant_initialized=0;


/* Initialize box by selecting corresponding file discriptor 
 * This will overwrite the values of file discriptors if 
 * called more than once. 
 */
void operant_write(int, int , int);
int operant_init(){

  operant_comedi_device=comedi_open(CARD0);
  printf (" fd  = %p",operant_comedi_device);
  operant_comedi_device2=comedi_open(CARD1);
  printf (" fd2 = %p",operant_comedi_device2);
  
  operant_initialized=1;
  return 0;
}

void operant_clear(int box_id)
{
  int i;
  if(operant_initialized)
    for(i=0; i<=7; i++)
      operant_write(box_id, i, 0);
  else
    printf("Attempted to clear box #%d without initializing\n", box_id);
}

void operant_close()
{
  comedi_close(operant_comedi_device);
  comedi_close(operant_comedi_device2);
}


/* This function contains the mappings of chan <--> cage */
int set_base_io_chans(int *indev, int *input, int *outdev, int *output, int boxid)
{ 
  switch (boxid)
    {
    case 6:   
    case 0:
      *input  = 0;
      *output = 8;
      *indev  = 0;
      *outdev = 0;
      break;
    case 7:
    case 1:
      *input  = 16;
      *output = 0;
      *indev  = 0;
      *outdev = 1;
      break;
    case 8:
    case 2:
      *input  = 8;
      *output = 16;
      *indev  = 1;
      *outdev = 1;
      break;
    case 9:
    case 3:
      *input  = 0;
      *output = 8;
      *indev  = 2;
      *outdev = 2;
      break;
    case 10:
    case 4:
      *input  = 16;
      *output = 0;
      *indev  = 2;
      *outdev = 3;
      break;
    case 11:
    case 5:
      *input  = 8;
      *output = 16;
      *indev  = 3;
      *outdev = 3;
      break;
    default:
      printf("Error!: box %d not valid\n", boxid);
      *indev=*outdev=*input=*output=0;
      break;
    }
  return 0;
}


/* Output wrapper */
void operant_write(int box_id, int channel, int state)
{
  int indev, in, outdev,out;
  if(channel > 7 || channel <0)
      printf("Channel #%d doesn't exist!\n", channel);
  else{
    box_id--;
    set_base_io_chans(&indev, &in, &outdev, &out, box_id);
    if(box_id < 6)
      if (state)
	comedi_dio_write(operant_comedi_device,outdev,(out+channel), 0);
      else
	comedi_dio_write(operant_comedi_device,outdev,(out+channel), 1);
    else
      if (state){
	//	printf ("This should turn the second box on\n");
	comedi_dio_write(operant_comedi_device2,outdev,(out+(7-channel)), 0);
      }
      else
	comedi_dio_write(operant_comedi_device2,outdev,(out+(7-channel)), 1);
      
  }
}

/* Input Wrapper */
int operant_read(int box_id, int channel)
{
  int indev, in, outdev,out;
  box_id--;
  if(channel > 7 || channel <0)
    {
      printf("Channel #%d doesn't exist!\n", channel);
      return -1;
    }
  else{
    set_base_io_chans(&indev, &in, &outdev, &out, box_id);
    if(box_id < 6)
      comedi_dio_read(operant_comedi_device,indev,in+channel, &out );
    else
      comedi_dio_read(operant_comedi_device2,indev,in+(7-channel), &out );
    return !out;
  }
}














