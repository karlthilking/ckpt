ARCH    ?= arm64
BUILD   ?= .

CC := clang
CXX := clang++
CFLAGS := -std=c17 -Wall -Wno-deprecated-declarations \
	  -DDEVELOPMENT=1 -g3 -O0 -arch $(ARCH) \
	  -iquote . -iquote ./include
CXXFLAGS := -std=c++20 -Wall -Wno-deprecated-declarations \
	    -DDEVELOPMENT=1 -g3 -O0 -arch $(ARCH) \
	    -iquote . -iquote ./include

LIBXND_WRAPPERS := \
        xnd/wrappers/time_wrappers.c \
        xnd/wrappers/stdlib_wrappers.c \
        xnd/wrappers/pthread_wrappers.c \
        xnd/wrappers/file_wrappers.c \
        xnd/wrappers/signal_wrappers.c \
        xnd/pid/pid_wrappers.c

LIBXND_SOURCES := \
        xnd/xnd_lib.c \
        xnd/pac.c \
        xnd/vm_common.c \
        xnd/vm_checkpoint.c \
        xnd/writeckpt.c \
        xnd/shared_cache.c \
        xnd/thread_info.c \
        xnd/ckptfile.c \
        xnd/util/log.c \
        xnd/util/path.c \
        xnd/util/debug.c \
        xnd/util/io.c \
        xnd/platform/signal.c \
        xnd/platform/ucontext/_setcontext.s \
        xnd/platform/ucontext/setcontext.c \
        xnd/platform/exe.c \
        xnd/pid/pid_table.cpp \
        xnd/coordinator/xnd_coord_api.c \
        xnd/coordinator/xnd_coord_client.c \
        $(LIBXND_WRAPPERS)

LIBXND_OBJECTS := \
        $(BUILD)/xnd_lib.o \
        $(BUILD)/pac.o \
        $(BUILD)/vm_common.o \
        $(BUILD)/vm_checkpoint.o \
        $(BUILD)/writeckpt.o \
        $(BUILD)/shared_cache.o \
        $(BUILD)/thread_info.o \
        $(BUILD)/ckptfile.o \
        $(BUILD)/log.o \
        $(BUILD)/path.o \
        $(BUILD)/debug.o \
        $(BUILD)/signal.o \
        $(BUILD)/_setcontext.o \
        $(BUILD)/setcontext.o \
        $(BUILD)/pid_table.o \
        $(BUILD)/io.o \
        $(BUILD)/exe.o \
        $(BUILD)/xnd_coord_api.o \
        $(BUILD)/xnd_coord_client.o \
        $(BUILD)/time_wrappers.o \
        $(BUILD)/stdlib_wrappers.o \
        $(BUILD)/signal_wrappers.o \
        $(BUILD)/pthread_wrappers.o \
        $(BUILD)/file_wrappers.o \
        $(BUILD)/pid_wrappers.o

XND_RESTART_SOURCES := \
        xnd/restart.c \
        xnd/pac.c \
        xnd/vm_common.c \
        xnd/vm_restore.c \
        xnd/readckpt.c \
        xnd/ckptfile.c \
        xnd/shared_cache.c \
        xnd/util/log.c \
        xnd/util/path.c \
        xnd/util/io.c

XND_RUN_SOURCES := \
        xnd/xnd_run.c \
        xnd/ckptfile.c \
        xnd/platform/exe.c \
        xnd/util/path.c \
        xnd/shared_cache.c \
        xnd/util/io.c \
        xnd/util/log.c

XND_COMMAND_SOURCES := \
        xnd/xnd_command.c \
        xnd/util/log.c \
        xnd/util/path.c \
        xnd/util/io.c \
        xnd/platform/exe.c \
        xnd/coordinator/xnd_coord_api.c

XND_COORD_SOURCES := \
        xnd/util/io.c \
        xnd/util/log.c \
        xnd/util/path.c \
        xnd/coordinator/xnd_coord.c \

ALL := \
        $(BUILD)/xnd_run \
        $(BUILD)/xnd_print \
        $(BUILD)/xnd_restart \
        $(BUILD)/xnd_command \
        $(BUILD)/xnd_coordinator \
        $(BUILD)/libxnd.dylib

all: $(ALL)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: xnd/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: xnd/util/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: xnd/platform/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: xnd/platform/ucontext/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: xnd/platform/ucontext/%.s | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: xnd/coordinator/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: xnd/pid/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: xnd/pid/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: xnd/wrappers/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libxnd.dylib: $(LIBXND_OBJECTS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -dynamiclib -fPIC -o $@ $^
	dsymutil $@

__TEXT          := 0x500000000000
__DATA          := 0x500000008000
__DATA_CONST    := 0x50000000c000
__LINKEDIT      := 0x500000010000
STACKADDR       := 0x500000110000
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

$(BUILD)/xnd_command: $(XND_COMMAND_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/xnd_coordinator: $(XND_COORD_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

test:	
	$(MAKE) -C test all

clean:
	rm -rf *.dSYM
	rm -f $(ALL) *.dylib *.o *.ckpt *.dat *.xnd
	rm -f xnd.log xnd-debug.sh

.PHONY: all clean test
.INTERMEDIATE: $(LIBXND_OBJECTS)
