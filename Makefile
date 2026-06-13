ARCH    ?= arm64
BUILD   ?= .

CC      := clang
CFLAGS  := -std=c17 -Wall -Wno-deprecated-declarations -DDEVELOPMENT=1 \
           -g3 -O0 -arch $(ARCH) -iquote . -iquote ./include

LIBXND_WRAPPERS := \
        xnd/wrappers/time_wrappers.c \
        xnd/wrappers/stdlib_wrappers.c \
        xnd/wrappers/pthread_wrappers.c \
        xnd/wrappers/file_wrappers.c \
        xnd/wrappers/signal_wrappers.c

LIBXND_SOURCES := \
        xnd/libckpt.c \
        xnd/pac.c \
        xnd/vm_common.c \
        xnd/vm_checkpoint.c \
        xnd/writeckpt.c \
        xnd/shared_cache.c \
        xnd/thread_info.c \
        xnd/util/log.c \
        xnd/util/path.c \
        xnd/util/debug.c \
        $(LIBXND_WRAPPERS)

XND_RESTART_SOURCES := \
        xnd/restart.c \
        xnd/pac.c \
        xnd/vm_common.c \
        xnd/vm_restore.c \
        xnd/readckpt.c \
        xnd/shared_cache.c \
        xnd/util/log.c \
        xnd/util/path.c

XND_RUN_SOURCES := \
        xnd/xnd_run.c \
        xnd/ckptfile.c \
        xnd/platform/exe.c \
        xnd/util/path.c

ALL := \
        $(BUILD)/xnd_run \
        $(BUILD)/xnd_print \
        $(BUILD)/xnd_restart \
        $(BUILD)/libxnd.dylib

all: $(ALL)

$(BUILD):
	mkdir -p $@

$(BUILD)/libxnd.dylib: $(LIBXND_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -dynamiclib -fPIC -o $@ $^

__TEXT          := 0x500000000000
__DATA          := 0x500000004000
__DATA_CONST    := 0x500000008000
__LINKEDIT      := 0x50000000c000
STACKADDR       := 0x50000010c000
$(BUILD)/xnd_restart: $(XND_RESTART_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) \
        -DXND_RESTART_STACKADDR=$(STACKADDR) \
	-Wl,-segaddr,__TEXT,$(__TEXT) \
	-Wl,-segaddr,__DATA,$(__DATA) \
        -Wl,-segaddr,__DATA_CONST,$(__DATA_CONST) \
	-Wl,-segaddr,__LINKEDIT,$(__LINKEDIT) \
        -Wl,-ld_classic \
	-o $@ $^

$(BUILD)/xnd_run: $(XND_RUN_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/xnd_print: xnd/xnd_print.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

test:	
	$(MAKE) -C test all

clean:
	rm -rf *.dSYM
	rm -f $(ALL) *.dylib *.o *.ckpt *.dat
	rm -f xnd.log xnd-debug.sh

.PHONY: all clean test
