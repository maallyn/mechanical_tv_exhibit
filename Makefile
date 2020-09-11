CC=gcc
LIBS=-lm

CFLAGS=-g -I. -I ./include
LDFLAGS=-g -R/usr/local/lib -L/usr/local/lib -lgif -lpthread -lm -lX11 -lwiringPi

imager:	imager.o
	$(CC) -o imager imager.o $(LDFLAGS)
imager.o: imager.c
	$(CC) -c $(CFLAGS) imager.c 

test1:	test1.o
	$(CC) -o test1 test1.o $(LDFLAGS)
test1.o: test1.c
	$(CC) -c $(CFLAGS) test1.c 

all: imager test1
target = imager test1
objects = imager.o test1.o

.DEFAULT_GOAL := all
clean:
	rm -f $(objects) $(target)

.PHONY: all default clean
