CC=clang
CFLAGS=-Wall -g3 -O0

SRC     = ./src
INCLUDE = ./include
TEST    = ./test

build: libckpt.dylib ckpt count restart printckpt

%.dylib: $(SRC)/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE)/ckpt.h -fPIC -dynamiclib -o $@ $<

TEXT_ADDR = 0x300000000
DATA_ADDR = 0x300800000
restart: $(SRC)/restart.c
	$(CC) $(CFLAGS) \
		-Wl,-segaddr,__TEXT,$(TEXT_ADDR) \
		-Wl,-segaddr,__DATA,$(DATA_ADDR) \
		-o $@ $<

%: $(SRC)/%.c
	$(CC) $(CFLAGS) -o $@ $<

%: $(TEST)/%.c
	$(CC) -g3 -O0 -o $@ $<

clean:
	rm -rf *.dSYM *.dylib *.o *.dat ckpt count printckpt restart
