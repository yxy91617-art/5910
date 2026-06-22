CC = clang
CFLAGS = -g -Wall
TARGETS = penn-sh
SRCS = penn-sh.c command.c tokenizer.c
HEADERS = command.h tokenizer.h

.PHONY: all clean

default: penn-sh

all: $(TARGETS)

penn-sh: $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) $(SRCS) -o $@

clean:
	$(RM) -r penn-sh penn-sh.dSYM
