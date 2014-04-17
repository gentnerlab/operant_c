all:
	gcc -Wall -g2 -c chron_gng.c -o chron_gng.o
	gcc -Wall -g2 -c operantio.c -o operantio.o
	gcc -o chron_gng -g2 chron_gng.o operantio.o -ldataio -lm -lcomedi

clean:
	rm -rf *.o chron_gng

