SHELL = /bin/sh
CC = gcc

#INCS = -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include

# XXX: what does -D_GNU_SOURCE do? 
CFLAGS = -g -Wall $(INCS)  

LFLAGS = -L/usr/local/lib/saber
RFLAGS = 
LIBS = -lreadline -lncurses -lm -lcomedi


SRCS = 2AC_probe.c remotesound.c

OBJS = $(SRCS:.c=.o)

all: 2AC_probe

2AC_probe: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LFLAGS) $(RFLAGS) $(LIBS) 

depend:
	$(CPP) -M $(CFLAGS) $(SRCS) > .depend

cleaner:
	rm -f *.o *~

clean:
	rm -f *.o 2AC_probe

ifeq (.depend,$(wildcard .depend))
include .depend
endif








