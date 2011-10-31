CFLAGS += -ggdb3 -O0 -Wall -Werror
CPPFLAGS +=
LDFLAGS +=
CC = gcc
#APP = evhttp
#EXT = c

all: $(APP)

$(APP): $(APP).$(EXT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $(APP) $(APP).$(EXT) $(LDFLAGS)

run: $(APP)
	./$(APP) $(OPT) $(OPTS) $(PARM) $(PARMS)

clean:
	rm -f $(APP)

