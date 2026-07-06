ARCH            ?= arm64
BUILD           ?= .
TIMING          ?= 0
DEVELOPMENT     ?= 1

CC      := clang
CXX     := clang++

CFLAGS := \
	-std=c17 -Wall -Wno-deprecated-declarations \
	-DDEVELOPMENT=$(DEVELOPMENT) -DTIMING=$(TIMING) \
	-g3 -O0 -arch $(ARCH) -iquote . -iquote ./include

CXXFLAGS := \
	-std=c++20 -Wall -Wno-deprecated-declarations \
	-DDEVELOPMENT=$(DEVELOPMENT) -DTIMING=$(TIMING) \
        -g3 -O0 -arch $(ARCH) -iquote . -iquote ./include

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
        $(BUILD)/pid_table_common.o \
        $(BUILD)/io.o \
        $(BUILD)/exe.o \
        $(BUILD)/xnd_coord_api.o \
        $(BUILD)/xnd_coord_client.o \
        $(BUILD)/time_wrappers.o \
        $(BUILD)/stdlib_wrappers.o \
        $(BUILD)/signal_wrappers.o \
        $(BUILD)/pthread_wrappers.o \
        $(BUILD)/file_wrappers.o \
        $(BUILD)/pid_wrappers.o \
        $(BUILD)/fd.o \
        $(BUILD)/env.o \
        $(BUILD)/compress.o

XND_RESTART_INTERNAL_SOURCES := \
        xnd/xnd_restart_internal.c \
        xnd/pac.c \
        xnd/vm_common.c \
        xnd/vm_restore.c \
        xnd/readckpt.c \
        xnd/ckptfile.c \
        xnd/shared_cache.c \
        xnd/util/log.c \
        xnd/util/path.c \
        xnd/util/io.c

XND_LAUNCH_SOURCES := \
        xnd/xnd_launch.c \
        xnd/ckptfile.c \
        xnd/platform/exe.c \
        xnd/util/path.c \
        xnd/shared_cache.c \
        xnd/util/io.c \
        xnd/util/log.c \
        xnd/util/env.c \
        xnd/coordinator/xnd_coord_api.c

XND_COMMAND_SOURCES := \
        xnd/xnd_command.c \
        xnd/util/log.c \
        xnd/util/path.c \
        xnd/util/io.c \
        xnd/platform/exe.c \
        xnd/coordinator/xnd_coord_api.c

XND_COORD_OBJECTS := \
        $(BUILD)/shared_cache.o \
        $(BUILD)/ckptfile.o \
        $(BUILD)/io.o \
        $(BUILD)/log.o \
        $(BUILD)/fd.o \
        $(BUILD)/path.o \
        $(BUILD)/pid_table_common.o \
        $(BUILD)/proc_list.o \
        $(BUILD)/xnd_coord.o \

XND_RESTART_OBJECTS := \
        $(BUILD)/xnd_restart.o \
        $(BUILD)/exe.o \
        $(BUILD)/path.o \
        $(BUILD)/io.o \
        $(BUILD)/env.o \
        $(BUILD)/xnd_coord_api.o \
        $(BUILD)/shared_cache.o \
        $(BUILD)/log.o \
        $(BUILD)/ckptfile.o \
        $(BUILD)/compress.o

ALL := \
        $(BUILD)/xnd_launch \
        $(BUILD)/xnd_print \
        $(BUILD)/xnd_restart \
        $(BUILD)/xnd_restart_internal \
        $(BUILD)/xnd_command \
        $(BUILD)/xnd_coordinator \
        $(BUILD)/libxnd.dylib

all: $(ALL)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: xnd/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: xnd/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

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
	$(CXX) $(CXXFLAGS) -dynamiclib -fPIC -lz -o $@ $^
	dsymutil $@

__TEXT          := 0x500000000000
__DATA          := 0x500000018000
__DATA_CONST    := 0x500000020000
__LINKEDIT      := 0x500000028000
STACKADDR       := 0x500000128000
$(BUILD)/xnd_restart_internal: $(XND_RESTART_INTERNAL_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -fno-stack-protector \
        -DXND_RESTART_STACKADDR=$(STACKADDR) \
        -Wl,-segaddr,__TEXT,$(__TEXT) \
        -Wl,-segaddr,__DATA,$(__DATA) \
        -Wl,-segaddr,__DATA_CONST,$(__DATA_CONST) \
        -Wl,-segaddr,__LINKEDIT,$(__LINKEDIT) \
        -Wl,-ld_classic \
        -o $@ $^

$(BUILD)/xnd_restart: $(XND_RESTART_OBJECTS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -lz -o $@ $^
	dsymutil $@

$(BUILD)/xnd_launch: $(XND_LAUNCH_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/xnd_print: xnd/xnd_print.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/xnd_command: $(XND_COMMAND_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/xnd_coordinator: $(XND_COORD_OBJECTS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $^
	dsymutil $@

test:	
	$(MAKE) -C test all

clean:
	rm -rf *.dSYM *-checkpoints
	rm -f $(ALL) *.dylib *.o *.ckpt *.dat *.xnd
	rm -f xnd.log xnd-debug.sh xnd_coordinator_v0

.PHONY: all clean test
.INTERMEDIATE: \
        $(LIBXND_OBJECTS) \
        $(XND_RESTART_OBJECTS) \
        $(XND_COORD_OBJECTS)
