CC=clang
CFLAGS=-g3 -O0

build: ctx maps

%: %.c
	$(CC) $(CFLAGS) $< -o $@ 

clean:
	rm -rf ctx maps *.dSYM
