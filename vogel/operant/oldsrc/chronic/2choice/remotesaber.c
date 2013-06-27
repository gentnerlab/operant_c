/*
**  Some functions to control saber remotely.
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
#include "dio96.h"



#define PORTNUMBER 13003                       /* connection to saber-- sclient port */

/* Stuff after here is cribbed from saber or aremote, or was otherwise written
   just to pass logging messages to saber.  -DWC
*/

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
                        if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) 
>= 0)
                                return sock;
                        else fprintf(stderr, "remotesound: connect(): %s\n", strerror(errno));
                }
                else fprintf(stderr, "remotesound: socket(): %s\n", strerror(errno));
        }
        else fprintf(stderr, "remotesound: gethostname(): %s\n", strerror(errno));
        return -1;
}




/*  Make a pretty message and send it to saber.           --DWC
 *  This function prepends the string "+TRIG " (which saber is written to log)
 *  and a timestamp (taken from the system clock) to whatever message is passed to
 *  it.  Then it sends the whole bundle off to saber, where the remote_thread should
 *  catch it and put it in any open explog file.
 */

int log2saber (int client_fd, char *logmsg) {
  struct timeval tv;
  time_t curtime;
  char *msg;
  char *msstr;
  char *ch;
  msg = (char *)malloc(256*sizeof(char));
  msstr = (char *)malloc(256*sizeof(char));
  sprintf(msg,"+NOTE ");
		/*
		** Make log entry w/ ~ exact start time
		*/
  gettimeofday(&tv, NULL);
  curtime = (time_t)tv.tv_sec;
  strftime(msstr, 23, "%d-%b-%Y:%X", localtime((time_t *)&curtime));
  strcat(msg,msstr);
  sprintf(msstr, "%0.2f", (float)tv.tv_usec / 1000000.0);
  strncat(msg + strlen(msg), msstr + 1, 3);
  for (ch=msg + strlen(msg); ch < msg + 25; ch++) if (*ch == '\0') *ch = ' ';
  strcat(msg, " ");
  strcat(msg, logmsg);

  if (write(client_fd, msg, strlen(msg)) == -1)
    {
      close(client_fd);
      client_fd= -1;
      perror("remote: write(client_fd) ");
    } 
  return(0);
}

int trig2saber (int client_fd, int trig_on, int channel, int prefix) {
  char *msg;
  msg = (char *)malloc(256*sizeof(char));
  if (trig_on) {
    if (prefix) {
      sprintf(msg,"+TRIG_ON %d %d\n",channel,prefix);
    } else {
      sprintf(msg,"+TRIG_ON %d\n",channel);      
    }
  } else {
    sprintf(msg,"+TRIG_OFF %d\n",channel);
  }

  if (write(client_fd, msg, strlen(msg)) == -1)
    {
      close(client_fd);
      client_fd= -1;
      perror("remote: write(client_fd) ");
    } 
  return(0);
}

/* 
**  load2saber-- tell saber to load a stimulus file for playback at
**    a time to be named later.   DWC
**
**    Saber boasts a rich stimulus playback interface, but unfortunately
**  the time at which its stimuli are to be played is fixed at the same
**  time stimulus files are loaded.  We need to load a stimulus file and
**  then later, play it in response to an external trigger (when the bird
**  pecks a key).
**  
**    Currently, I have added a hideous hack to saber's remote_thread
**  to support maintaining an array of stimulus data buffers whose
**  contents are loaded and then later played in response to asynchronous
**  commands to remote_thread.  This hack should ultimately be replaced
**  by some minor modifications to saber's stims command; however, this
**  external interface will probably remain the same.
**
**    This function takes an array index (stim_number) and a file name; 
**  the stimulus data will be loaded into a buffer and later may be 
**  retrieved using the specified array index.  Saber will happily overwrite 
**  (actually replace) data in an array index that's already been used.
*/
int load2saber (int client_fd, int stim_number, char *stimfilename) {
  char *msg;
  msg = (char *)malloc(256*sizeof(char));
  sprintf(msg,"+LOAD %d %s\n",stim_number,stimfilename);

  if (write(client_fd, msg, strlen(msg)) == -1)
    {
      close(client_fd);
      client_fd= -1;
      perror("remote (load2saber): write(client_fd) ");
    } 
  return(0);
}

/*
**  Play2saber-- play back a previously-loaded stimulus file.  DWC
**
**  The important argument is stim_number, the array index used with the
**  previous load2saber command which opened the stimulus file.  If you
**  haven't already opened the file with load2saber, play2saber will fail.
*/

int play2saber (int client_fd, int stim_number) {
  char *msg;
  msg = (char *)malloc(256*sizeof(char));
  sprintf(msg,"+PLAY %d\n",stim_number);

  if (write(client_fd, msg, strlen(msg)) == -1)
    {
      close(client_fd);
      client_fd= -1;
      perror("remote (play2saber): write(client_fd) ");
    } 
  return(0);
}

/*  Block until saber agrees to talk to us.
 */
int wait_for_remote_saber_connection() {
 
  /* Open our socket (to accept incoming connections) */
  int client_fd = -1;
  int net_fd,n,i;
  char *message_buf;
  message_buf = (char *)malloc(255*sizeof(char));

  while (1) {
    /* Check for socket connection */
    if ((client_fd = open_connection("sturnus", PORTNUMBER)) == -1) {      
      perror("open_connection");
      return(-1);
    } else {
      /* initialize the socket connection-- send a message across it */
      n = strlen(message_buf);
      for (i=0;i<n;i++) {
	message_buf[i]='\0';
      }
      sprintf(message_buf,"+INIT Hi there!\n");
      if (write(client_fd, message_buf, strlen(message_buf)) == -1)
	{
	  close(client_fd);
	  close(net_fd);
	  client_fd= -1;
	} 
      break;
    }
  }
  return(client_fd);
}




