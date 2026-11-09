# #Used source to help develop this file: https://makefiletutorial.com/

# USE_AESD_CHAR_DEVICE ?= 1

# override CFLAGS += -DUSE_AESD_CHAR_DEVICE=$(USE_AESD_CHAR_DEVICE) -g -Wall -O0
# CC ?= $(CROSS_COMPILE)gcc
# #writer: writer.c
# #	cc writer.c -o writer

# #default target
# all: packet_processor

# packet_processor: packet_processor.o
# 	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
# clean:
# 	rm -f packet_processor packet_processor.o

 # the compiler: gcc for C program, define as g++ for C++

objects = packet_processor.o circular-buffer.o

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -I /usr/include/libnl3
LDFLAGS ?= -lnl-3 -lnl-genl-3 -lm
all: packet_processor

packet_processor: $(objects)
	$(CC) $^ -o packet_processor $(LDFLAGS)

packet_processor.o: 
	$(CC) $(CFLAGS) -c packet_processor.c -o packet_processor.o

circular-buffer.o:
	$(CC) $(CFLAGS) -c circular-buffer.c -o circular-buffer.o

clean:
	rm -f circular-buffer.o packet_processor.o