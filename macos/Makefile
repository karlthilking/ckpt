CC=clang
CFLAGS=-g3 -O0

SOURCE  = ./src
INCLUDE = ./include
TEST    = ./test

build: libckpt.dylib ckpt restart readckpt count

%.dylib: $(SOURCE)/%.c
	$(CC) $(CFLAGS) -dynamiclib -o $@ $<

restart: $(SOURCE)/restart.c
	$(CC) $(CFLAGS) \
                -Wl,-segaddr,__TEXT,0x200000000 \
                -Wl,-segaddr,__DATA,0x200800000 \
                -Wl,-segaddr,__DATA_CONST,0x200900000 \
                -Wl,-segaddr,__LINKEDIT,0x200A00000 \
                -o $@ $<

%: $(SOURCE)/%.c
	$(CC) $(CFLAGS) -o $@ $<

%: $(TEST)/%.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf *.dSYM *.dylib ckpt restart readckpt count 
