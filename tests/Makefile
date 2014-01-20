INCLUDE=.
CC=gcc
CFLAGS=-g -O3 -Wall -I$(INCLUDE) -DDORAYAKI_DEBUG
DIRS=. 

LIBS=-lcheck -lrt -lm -lpthread

OBJ = socket_pool_test.o ../socket_pool.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

socket_pool_test: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f *.o *~ core $(INCLUDE)/*~ 
