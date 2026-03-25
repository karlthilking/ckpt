CC=clang
CFLAGS=-g3 -O0

SOURCE  = ./src
INCLUDE = ./include
TEST    = ./test

build: libckpt.dylib ckpt readckpt count

%.dylib: $(SOURCE)/%.c
	$(CC) $(CFLAGS) -dynamiclib -o $@ $<

%: $(SOURCE)/%.c
	$(CC) $(CFLAGS) -o $@ $<

%: $(TEST)/%.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf *.dat *.dSYM *.dylib ckpt readckpt count 
