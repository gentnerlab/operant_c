all:
	gcc -Wall -g2 -c chron2choice.c -o chron2choice.o
	gcc -Wall -g2 -c operantio.c -o operantio.o
	gcc -o chron2choice -g2 chron2choice.o operantio.o -ldataio -lm -lcomedi

clean:
	rm -rf *.o chron2choice

