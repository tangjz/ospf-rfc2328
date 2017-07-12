TARGET = simple-ospf

CC = gcc
CFLAGS = -std=gnu99 -Wall -Wextra -g
LIBS = -lpthread

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

.PRECIOUS: $(TARGET) $(OBJECTS)
.PHONY: clean

$(TARGET) : $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@ $(LIBS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm *.o $(TARGET)