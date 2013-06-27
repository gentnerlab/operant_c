extern int pcm_errno;
extern int sys_nerr;

#ifdef linux
#define MADV_SEQUENTIAL
#define madvise(addr,len,flags)
#endif

#define PCMIOMMAP 1
#define PCMIOMALLOC 2
#define PCMIOINCENTRY 3
#define PCMIODECENTRY 4
#define PCMIOSETTIME 5
#define PCMIOSETSR 6
#define PCMIOGETSIZE 7				/* Get the size in samples - shortcut to using pcm_stat() - arg = int* */
#define PCMIOGETSR 8				/* Get the samplerate in Hz - shortcut to using pcm_stat() - arg = int* */
#define PCMIOGETENTRY 9				/* Get the entry - shortcut to using pcm_stat() - arg = int* */
#define PCMIOGETTIME 10				/* Get the timestamp - shortcut to using pcm_stat() - arg = (long *) */
#define PCMIOGETCAPS 11				/* Get the capabilities of this file format... Includes MULTENTRY */
#define PCMIOGETNENTRIES 12			/* Get the number of entries in this file */

#define PCMIOCAP_MULTENTRY 1		/* This file format can hold >1 entry... */
#define PCMIOCAP_SAMPRATE 2			/* This file format stores samplerate info... */

typedef struct pcmfilestruct
{
	char *name;
	int flags;
	int entry;
	int fd;
	void *addr;
	int len;
	short *bufptr;
	int buflen;
	int memalloctype;
	int (*open)();
	void (*close)();
	int (*read)();
	int (*write)();
	int (*seek)();
	int (*ctl)();
	int (*stat)();
	char *tempnam;
#ifdef USING_ESPS
	void *header;				/* For ESPS */
	FILE *espsfp;				/* For ESPS */
#endif
	int samplerate;
	int timestamp;
	int nentries;
	void *p2file;				/* For PCMSEQ2 */
	int pcmseq3_entrysize;		/* For PCMSEQ2 */
	char pcmseq3_key[29];		/* For PCMSEQ2 */
	long pcmseq3_cursamp;		/* For PCMSEQ2 */
	long pcmseq3_poscache;		/* For PCMSEQ2 */
	int entrystarted;			/* For PCMSEQ2 */
	FILE *outfp;
} PCMFILE;

struct pcmstat
{
	int	entry;
	int	nsamples;
	int	samplerate;
	int timestamp;
	int capabilities;
	int nentries;
};

PCMFILE *pcm_open(char *, char *);
void pcm_close(PCMFILE *);
int pcm_read(PCMFILE *fp, short **buf_p, int *nsamples_p);
int pcm_seek(PCMFILE *fp, int entry);
int pcm_ctl(PCMFILE *fp, int request, void *arg);
int pcm_stat(PCMFILE *fp, struct pcmstat *buf);
int pcm_write(PCMFILE *fp, short *buf, int nsamples);

int pcmraw_recognizer(PCMFILE *);
int pcmraw_open(PCMFILE *);
void pcmraw_close(PCMFILE *fp);
int pcmraw_read(PCMFILE *fp, short **buf_p, int *nsamples_p);
int pcmraw_seek(PCMFILE *fp, int entry);
int pcmraw_ctl(PCMFILE *fp, int request, void *arg);
int pcmraw_stat(PCMFILE *fp, struct pcmstat *buf);
int pcmraw_write(PCMFILE *fp, short *buf, int nsamples);

int pcmfix_recognizer(PCMFILE *);
int pcmfix_open(PCMFILE *);
void pcmfix_close(PCMFILE *fp);
int pcmfix_read(PCMFILE *fp, short **buf_p, int *nsamples_p);
int pcmfix_seek(PCMFILE *fp, int entry);
int pcmfix_ctl(PCMFILE *fp, int request, void *arg);
int pcmfix_stat(PCMFILE *fp, struct pcmstat *buf);
int pcmfix_write(PCMFILE *fp, short *buf, int nsamples);

int pcmwav_recognizer(PCMFILE *);
int pcmwav_open(PCMFILE *);
void pcmwav_close(PCMFILE *fp);
int pcmwav_read(PCMFILE *fp, short **buf_p, int *nsamples_p);
int pcmwav_seek(PCMFILE *fp, int entry);
int pcmwav_ctl(PCMFILE *fp, int request, void *arg);
int pcmwav_stat(PCMFILE *fp, struct pcmstat *buf);
int pcmwav_write(PCMFILE *fp, short *buf, int nsamples);

int pcmseq_recognizer(PCMFILE *);
int pcmseq_open(PCMFILE *);
void pcmseq_close(PCMFILE *fp);
int pcmseq_read(PCMFILE *fp, short **buf_p, int *nsamples_p);
int pcmseq_seek(PCMFILE *fp, int entry);
int pcmseq_ctl(PCMFILE *fp, int request, void *arg);
int pcmseq_stat(PCMFILE *fp, struct pcmstat *buf);
int pcmseq_write(PCMFILE *fp, short *buf, int nsamples);

int pcmfeasd_recognizer(PCMFILE *);
int pcmfeasd_open(PCMFILE *);
void pcmfeasd_close(PCMFILE *fp);
int pcmfeasd_read(PCMFILE *fp, short **buf_p, int *nsamples_p);
int pcmfeasd_seek(PCMFILE *fp, int entry);
int pcmfeasd_ctl(PCMFILE *fp, int request, void *arg);
int pcmfeasd_stat(PCMFILE *fp, struct pcmstat *buf);

int expidx_recognizer(PCMFILE *);
int expidx_open(PCMFILE *);
void expidx_close(PCMFILE *fp);
int expidx_read(PCMFILE *fp, short **buf_p, int *nsamples_p);
int expidx_seek(PCMFILE *fp, int entry);
int expidx_ctl(PCMFILE *fp, int request, void *arg);
int expidx_stat(PCMFILE *fp, struct pcmstat *buf);
int expidx_write(PCMFILE *fp, short *buf, int nsamples);

