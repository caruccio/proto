#APP = appname
CFLAGS += -ggdb3 -O0 -Wall -Werror
CPPFLAGS +=
LDFLAGS +=
CC ?= gcc
EXT ?= c
RUN = ./$(APP) $(OPT) $(OPTS) $(PARM) $(PARMS)
OBJS = $(SRCS:.$(EXT)=.o)

all: $(APP)

$(APP): $(OBJS)
	$(CC) $(CFLAGS) -o $(APP) $(OBJS) $(LDFLAGS)

run: $(APP)
	$(RUN)

gdb: $(APP)
	gdb --args $(RUN)

strace: $(APP)
	strace $(SOPT) $(SOPTS) $(SPARM) $(SPARMS) $(RUN)

clean: clean-$(APP)
	rm -f $(APP) *.o
