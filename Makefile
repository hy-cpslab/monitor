SRCS = monitor.c

CC = gcc
CFLAGS = -g -Wall
LIBS = -lvirt -lncurses

TARGET = monitor

all : $(TARGET)

$(TARGET) : $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LIBS)

clean :
	rm -rf $(TARGET) core

new :
	$(MAKE) clean
	$(MAKE)
