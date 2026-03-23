CC=clang
CFLAGS=-g3 -O0

build: ctx

%: %.c
	$(CC) $(CFLAGS) $< -o $@ 

clean:
	rm -f ctx *.dSYM
