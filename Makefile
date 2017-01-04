CC ?= gcc-6
CFLAGS ?=
CFLAGS += -Wall -Wextra
LDFLAGS ?=
node_exp_INC ?= -I/usr/include/libnl3
node_exp_LIBS ?= -lmicrohttpd -lnl-3 -lnl-genl-3

.PHONY: all
all: node_exp
	env

node_exp: node_exp.o
	env
	$(CC) $(COPTS) $(node_exp_LIBS) $(LDFLAGS) node_exp.o -o node_exp

.PHONY: clean
clean:
	rm -f *.o node_exp
.c.o:
	env
	$(CC) $(node_exp_INC) $(CFLAGS) -c -o $@ $<
