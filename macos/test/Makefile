CC=clang
CFLAGS=-g3 -O0

build: ctx maps pac count

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf count ctx maps pac *.dSYM
