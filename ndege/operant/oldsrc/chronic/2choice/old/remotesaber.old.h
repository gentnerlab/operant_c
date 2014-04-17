/*
**  Remotesaber.h    Functions for talking to saber's remote_thread.  DWC
**
**    The functions themselves are in remotesaber.c
**
**  You will probably want something like the following in your program:
** 
**    int client_fd = -1;
**
**    static void sig_pipe(int signo)
**    {
**            printf("SIGPIPE caught\n");
**            client_fd = -1;
**    }
**
**                         [...]
**
**    if (signal(SIGPIPE, sig_pipe) == SIG_ERR)
**            {
**                    perror("error installing signal handler for SIG_PIPE");
**                    return 0;
**            }
**
**     That stuff catches SIGPIPE signals so that if saber dies, your
**  remote program will not also die.
**
**     Then, 
**
**    if ((client_fd = wait_for_remote_saber_connection()) == -1) { perror("HELP!"); }
**
**  will make client_fd a file descriptor pointing to a socket connected to a running
**  saber process.  Wait_for_remote_saber_connection will hang until saber connects,
**  i.e. until someone give saber the "remote" command in sclient or the command line
**  interface.
**
**
**     NOTES:  This is all a hideous hack, largely reflecting my imperfect understanding
**  of saber.  Since we're starting to need to pass a bunch of commands to saber, it
**  would probably be much better to make this interface a stripped-down program-friendly
**  version of sclient, and move the "load" and "play" commands into the stims interface.
**  Then all of this stuff could alternately be done by hand from sclient's command line,
**  which makes much more sense anyway.  I may spend some time working on documentation for
**  "the saber API".
*/

// extern int socket_checkconn(int fd, int *newfd_p);  // maybe we don't need to export these
// extern int socket_prepare(unsigned short portnum);

extern int log2saber (int client_fd, char *logmsg);
extern int trig2saber (int client_fd, int trig_on, int channel, int prefix);
extern int load2saber (int client_fd, int stim_number, char *stimfilename);
extern int play2saber (int client_fd, int stim_number);
int wait_for_remote_saber_connection();
