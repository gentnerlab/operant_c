all:
	gcc5 -O2 -fno-strength-reduce -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -Wall -g2 -DUSE_LPR_PRINT_CMD   -I/usr/local/src/aplot/dataio -I/usr/src/dio96 -I/usr/X11R6/include  -Dlinux -D__i386__ -D_POSIX_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE -DX_LOCALE  -DNO_MESSAGE_CATALOG  -DFUNCPROTO=15 -DNARROWPROTO -c femx.c -o femx.o
	gcc5 -O2 -fno-strength-reduce -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -Wall -g2 -DUSE_LPR_PRINT_CMD   -I/usr/local/src/aplot/dataio -I/usr/src/dio96 -I/usr/X11R6/include  -Dlinux -D__i386__ -D_POSIX_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE -DX_LOCALE  -DNO_MESSAGE_CATALOG  -DFUNCPROTO=15 -DNARROWPROTO     -c remotesound.c -o remotesound.o
	gcc5 -o femx -O2 -fno-strength-reduce -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -Wall -g2 -DUSE_LPR_PRINT_CMD -L/usr/X11R6/lib femx.o remotesound.o /usr/local/src/aplot/dataio/libdataio.a -lm -Wl,-rpath,/usr/X11R6/lib -lcomedi

clean:
	rm -rf *.o femx

