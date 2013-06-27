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
 *  8-28-08 TQG modified to run only a single card
 *  8-29-08 MB: many changes for ni_6509 card. All 
 *  		channels operate on subdevice 2.
 **********************************************************/

#include <stdio.h>
#include <comedilib.h>
#include <stdlib.h>


/* INPUT Chans (Basically just STUPID offsets) */ 
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

comedi_t *operant_comedi_device =NULL; 


/* FD for Amish S. Dave's isa driver */
/*int asd_infd=0, asd_outfd = 0;
unsigned char asd_mask = 0; 
*/
int operant_initialized=0;



/* Initialize box by selecting corresponding file descriptor 
 * This will overwrite the values of file discriptors if 
 * called more than once. 
 */
void operant_write(int, int , int);

int operant_init(){
  int  j , m, n;
  operant_comedi_device=comedi_open(CARD0);
  printf (" fd  = %p",operant_comedi_device);
  if(operant_comedi_device==NULL){
	printf("\n device initalization failed\n");
	exit(-1);
	}
	else{
	printf("\ndevices initialized\n");
 	 operant_initialized=1;
  }
    for (j=0; j<=7; j++)
      comedi_dio_config(operant_comedi_device,2, j,COMEDI_INPUT );
    for (j=8; j<=23; j++)
      comedi_dio_config(operant_comedi_device,2, j,COMEDI_OUTPUT );
    for (j=24; j<=31; j++)
      comedi_dio_config(operant_comedi_device,2, j,COMEDI_INPUT );
    for (j=32; j<=47; j++)
      comedi_dio_config(operant_comedi_device,2, j,COMEDI_OUTPUT );
    for (j=48; j<=55; j++)
      comedi_dio_config(operant_comedi_device,2, j,COMEDI_INPUT );
    for (j=56; j<=71; j++)
      comedi_dio_config(operant_comedi_device,2, j,COMEDI_OUTPUT );
    for (j=72; j<=79; j++)
      comedi_dio_config(operant_comedi_device,2, j,COMEDI_INPUT );
    for (j=80; j<=96; j++)
      comedi_dio_config(operant_comedi_device,2, j,COMEDI_OUTPUT );

  for (m=1; m<=8; m++){
    for(n=1; n<=8; n++) 
      operant_write(m, n, 0);
  }    
  return 0;
}

int operant_open(){
  operant_comedi_device=comedi_open(CARD0);
  printf (" fd  = %p",operant_comedi_device);
  //operant_comedi_device2=comedi_open(CARD1);
  //printf (" fd2 = %p",operant_comedi_device2);
  //printf("\ndevices initialized\n");
  operant_initialized=1;
  if( operant_comedi_device==NULL) /*|| operant_comedi_device2==NULL)*/
    return -1;
  else{
    operant_initialized=1;
    return 0;
  }
}
  
void operant_clear(int box_id)
{
  int i;
  if(operant_initialized)
    for(i=1; i<=8; i++)
      operant_write(box_id, i, 0);
  else
    fprintf(stderr,"Attempted to clear box #%d without initializing\n", box_id);
}

int operant_close()
{
  int foo;
  foo = comedi_close(operant_comedi_device);
  /*foo += comedi_close(operant_comedi_device2); */
  printf("\ndevices closed\n");
  if(foo==0)
    return 0;
  else
    return -1;
}

/* This function contains the mappings of chan <--> cage */
int set_base_io_chans(int *indev, int *input, int *outdev, unsigned int *output, int boxid)
{ 
  switch (boxid)
    {
    case 0:
      *input  = 0;
      *output = 8;
      *indev  = 2;
      *outdev = 2;
      break;
    case 1:
      *input  = 4;
      *output = 16;
      *indev  = 2;
      *outdev = 2;
      break;
    case 2:
      *input  = 24;
      *output = 32;
      *indev  = 2;
      *outdev = 2;
      break;
    case 3:
      *input  = 28;
      *output = 40;
      *indev  = 2;
      *outdev = 2;
      break;
    case 4:
      *input  = 48;
      *output = 56;
      *indev  = 2;
      *outdev = 2;
      break;
    case 5:
      *input  = 52;
      *output = 64;
      *indev  = 2;
      *outdev = 2;
      break;
    case 6:
      *input  = 72;
      *output = 80;
      *indev  = 2;
      *outdev = 2;
      break;
    case 7:
      *input  = 76;
      *output = 88;
      *indev  = 2;
      *outdev = 2;
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
  int indev, in, outdev;
unsigned int out;
  comedi_t *devname;
  if(channel > 8 || channel <1)
      fprintf(stderr, "operant_write error: Output channel #%d doesn't exist!\n", channel);
  else{
    channel--; 
    if (box_id<=8 && box_id>0)
      devname=operant_comedi_device;
    else
      fprintf(stderr,"operant_write error: Box number %d does not exist!\n", box_id);
    
    box_id--;
    set_base_io_chans(&indev, &in, &outdev, &out, box_id);
    
    if (state)
      comedi_dio_write(devname,outdev,(out+channel), 0);
    else
      comedi_dio_write(devname,outdev,(out+channel), 1);
  }
}

/* Input Wrapper */
int operant_read(int box_id, int channel)
{
  int indev, in, outdev;
	unsigned int out;
  comedi_t *devname;

  if(channel > 4 || channel <1)
    {
      fprintf(stderr, "operant_read error: Input channel #%d doesn't exist!\n", channel);
      return -1;
    }
  else{
    channel--;
    if (box_id<=8 && box_id>0)
      devname=operant_comedi_device;
    else{
      fprintf(stderr, "operant_read error: Box number %d does not exist!\n", box_id);
      return -1;
    }
    
    box_id--;
    set_base_io_chans(&indev, &in, &outdev, &out, box_id);
    comedi_dio_read(devname,indev,(in+channel), &out );
    return !out;
  }
}


