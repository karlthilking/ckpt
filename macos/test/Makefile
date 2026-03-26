CC=clang
CFLAGS=-g3 -O0

build: ctx maps pac count fp

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf count ctx maps pac fp *.dSYM
