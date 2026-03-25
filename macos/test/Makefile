CC=clang
CFLAGS=-g3 -O0

build: ctx maps pac

pac: pac.c
	$(CC) $(CFLAGS) -o $@ $<

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf ctx maps pac *.dSYM
