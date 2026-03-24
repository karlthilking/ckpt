CC=clang
CFLAGS=-g3 -O0

build: src/maps

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f src/maps *.dSYM *.so
