/*
**  Some functions to control soundserver remotely.
*/

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
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include "pcmio.h"


#define PORTNUMBER 14001                       /* connection to soundserver */
#define STIM_NETBUFSIZE 256
#define DEBUG 0

/*************************************************************
*************************************************************/
static int open_connection(char *hostname, u_short portnum)
{
        struct hostent *hp;
        struct sockaddr_in sin;
        int sock;

        if ((hp = gethostbyname(hostname)) != NULL)
        {
                if ((sock = socket(hp->h_addrtype, SOCK_STREAM, 0)) >= 0)
                {
                        sin.sin_family = hp->h_addrtype;
                        memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);
                        sin.sin_port = htons(portnum);
                        if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) >= 0)
                                return sock;
                        else fprintf(stderr, "remotesound: connect(): %s\n", strerror(errno));
                }
                else fprintf(stderr, "remotesound: socket(): %s\n", strerror(errno));
        }
        else fprintf(stderr, "remotesound: gethostname(): %s\n", strerror(errno));
        return -1;
}


/* 
**  load2soundserver-- tell soundserver to load a stimulus file for playback at
**    a time to be named later.   DWC
**
**  We need to load a stimulus file and
**  then later, play it in response to an external trigger (when the bird
**  pecks a key).
**
**    This function takes an array index (stim_number) and a file name; 
**  the stimulus data will be loaded into a buffer and later may be 
**  retrieved using the specified array index.  Soundserver will happily 
**  overwrite (actually replace) data in an array index that's already been 
**  used.
**
**  Note that every time you connect to soundserver, it forks off a new
**  process to talk to you.  Each new process created in this way has its
**  own array of stimulus data with its own indices.  The way I have set
**  up these functions, each process will talk to a different sound device;
**  this means that you must load the stimulus you want to play separately
**  for each sound device you want to play to.
*/
int load2soundserver (int client_fd, int stim_number, char *stimfilename) {
  char *msg;
  int r;
  msg = (char *)malloc(STIM_NETBUFSIZE*sizeof(char));
  sprintf(msg,"+LOAD %d %s\n",stim_number,stimfilename);

  if (write(client_fd, msg, strlen(msg)) == -1)
    {
      close(client_fd);
      client_fd= -1;
      perror("remote (load2soundserver): write(client_fd) ");
      return(-1);
    }
  if (read(client_fd,msg,STIM_NETBUFSIZE) < 0) {
    perror("remote (load2soundserver): read(client_fd) ");
    close(client_fd);
    client_fd=-1;
    return(-1);
  }
  if (DEBUG) {
    fprintf(stderr,msg);
  }
  free(msg);
  return(0);
}

/*
**  Play2soundserver-- play back a previously-loaded stimulus file.  DWC
**
**  The important argument is stim_number, the array index used with the
**  previous load2soundserver command which opened the stimulus file.  If you
**  haven't already opened the file with load2soundserver, play2soundserver 
**  will fail.
*/

int play2soundserver (int client_fd, int stim_number) {
  char *msg;
  int r;
  msg = (char *)malloc(STIM_NETBUFSIZE*sizeof(char));
  sprintf(msg,"+PLAY %d\n",stim_number);

  if (write(client_fd, msg, strlen(msg)) == -1)
    {
      close(client_fd);
      client_fd= -1;
      perror("remote (play2soundserver): write(client_fd) ");
      return(-1);
    } 
  if (read(client_fd,msg,STIM_NETBUFSIZE) < 0) {
    perror("remote (play2soundserver): read(client_fd) ");
    close(client_fd);
    client_fd=-1;
    return(-1);
  }
  if (DEBUG) {
    fprintf(stderr,msg);
  }
  free(msg);
  return(0);
}

/*  Try to connect to soundserver process, then try to open device
**  file snd_dev_name.
 */
int connect_to_soundserver(char *rhost, char *snd_dev_name) {
 
  /* Open our socket (to accept incoming connections) */
  int client_fd = -1;
  int n,i,r;
  char *message_buf;
  message_buf = (char *)malloc(STIM_NETBUFSIZE*sizeof(char));

  if ((client_fd = open_connection(rhost,PORTNUMBER)) == -1)
    {
      perror("open_connection");
      return(-1);
    } else {
      r=read(client_fd,message_buf,STIM_NETBUFSIZE);
      if (DEBUG) {
	fprintf(stderr,message_buf);
      }
      /* initialize the socket connection-- send a message across it */
      sprintf(message_buf,"+OPEN %s\n",snd_dev_name);
      if (write(client_fd, message_buf, strlen(message_buf)) == -1)
	{
	  close(client_fd);
	  client_fd= -1;
	} 
    }
  if (read(client_fd,message_buf,STIM_NETBUFSIZE)<0) {
    perror("remote (connect_to_soundserver): read(client_fd) ");
    close(client_fd);
    client_fd=-1;
    return(-1);
  }
  if (DEBUG) {
    fprintf(stderr,message_buf);
  }
  free(message_buf);
  return(client_fd);
}

int close_soundserver (int client_fd) {
  char *msg;
  int r;
  msg = (char *)malloc(STIM_NETBUFSIZE*sizeof(char));
  sprintf(msg,"+DIE\n");

  if (write(client_fd, msg, strlen(msg)) == -1)
    {
      close(client_fd);
      client_fd= -1;
      perror("remote (play2saber): write(client_fd) ");
      return(-1);
    } 
  if (read(client_fd,msg,STIM_NETBUFSIZE) < 0) {
    perror("remote (close_soundserver): read(client_fd) ");
    close(client_fd);
    client_fd=-1;
    return(-1);
  }
  if (DEBUG) {
    fprintf(stderr,msg);
  }
  free(msg);
  close(client_fd);
  return(0);
}














