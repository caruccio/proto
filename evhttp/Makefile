CPPFLAGS = -I/usr/include/libev -I./http-parser
LDFLAGS = -lev -lrt
SRCS = evhttp.c client.c timer.c http-parser/http_parser.c http.c be.c
APP = evhttp

ifdef DEBUG
CPPFLAGS += -DDEBUG
else
CPPFLAGS += -DNDEBUG
endif

#ifdef BE_ECHO
#SRCS += be_echo.c
#CPPFLAGS += -DBE_ECHO
#endif
#
#ifdef BE_BRIDGE
#SRCS += be_bridge.c
#CPPFLAGS += -DBE_BRIDGE
#endif

ifdef ($(BACKEND),echo)
SRCS += be_echo.c
CPPFLAGS += -DBE_ECHO=1
endif

ifeq ($(BACKEND),bridge)
SRCS += be_bridge.c
CPPFLAGS += -DBE_BRIDGE=1
endif

include ../Makefile

clean-$(APP):
	rm -f http-parser/http_parser.o
