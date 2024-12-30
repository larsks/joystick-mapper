SRCS = main.c
OBJS = $(SRCS:.c=.o)

all: jsmapper

jsmapper: $(OBJS)
	$(CC) -o $@ $(OBJS)
