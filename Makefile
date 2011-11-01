CFLAGS += -ggdb3 -O0 -Wall -Werror
CPPFLAGS +=
LDFLAGS +=
CC = gcc
RUN = ./$(APP) $(OPT) $(OPTS) $(PARM) $(PARMS)
#APP = evhttp
#EXT = c

all: $(APP)

$(APP): $(APP).$(EXT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $(APP) $(APP).$(EXT) $(LDFLAGS)

run: $(APP)
	$(RUN)

gdb: $(APP)
	gdb --args $(RUN)

clean:
	rm -f $(APP)

