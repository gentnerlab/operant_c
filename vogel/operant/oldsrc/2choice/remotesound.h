/*
**  Remotesaber.h    Functions for talking to saber's remote_thread.  DWC
**
**    The functions themselves are in remotesound.c
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
**     That stuff catches SIGPIPE signals so that if the soundserver dies, your
**  remote program will not also die.
**
**     Then, 
**
**    if ((client_fd = connect_to_soundserver("modeln","/dev/dsp0")) == -1) { perror("HELP!"); }
**
**  will make client_fd a file descriptor pointing to a socket connected to a running
**  soundserver process.  Connect_to_soundserver will hang until the soundserver connects.
**
**
**  NOTE:  You need to explicitly kill the server process you're talking to,
**  by calling close_soundserver(client_fd), when you're done with it.
*/

extern int load2soundserver (int client_fd, int stim_number, char *stimfilename);
extern int play2soundserver (int client_fd, int stim_number);
extern int connect_to_soundserver(char *rhost, char *snd_dev_name);
extern int close_soundserver(int client_fd);
