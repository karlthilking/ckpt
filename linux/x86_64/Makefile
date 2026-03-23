CC = gcc
CFLAGS = -g3 -O0

build: libckpt.so ckpt testlib.so pth

%: %.c
	$(CC) $(CFLAGS) -lpthread -o $@ $<

%.so: %.c
	$(CC) $(CFLAGS) -shared -fPIC -ldl -o $@ $<

clean:
	rm -f libckpt.so ckpt open
