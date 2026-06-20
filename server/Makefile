CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= 

all: $(TARGET)

default: $(TARGET)

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) aesdsocket.c $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o
