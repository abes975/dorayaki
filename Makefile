INCLUDE=.
CC=gcc
CFLAGS=-g -O3 -Wall -I$(INCLUDE)
#CFLAGS=-g -O3 -Wall -I$(INCLUDE) -DDORAYAKI_DEBUG

#DIRS=. ./tests

LIBS=

DEPS = socket_pool.h 

OBJ = socket_pool.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

dorayaki: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f *.o *~ core $(INCLUDE)/*~ 
