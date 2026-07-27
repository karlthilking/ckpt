ARCH            ?= arm64
BUILD           ?= .
TIMING          ?= 0
DEVELOPMENT     ?= 1
DEBUG           ?= 0

CC      := clang
CXX     := clang++

ifeq ($(DEBUG),1)
OPTFLAGS := -O0
else
OPTFLAGS := -O2
endif

CFLAGS := \
	-std=c17 -Wall -Wno-deprecated-declarations \
	-D_XOPEN_SOURCE \
	-DDEVELOPMENT=$(DEVELOPMENT) -DTIMING=$(TIMING) -DDEBUG=$(DEBUG) \
	-g $(OPTFLAGS) -arch $(ARCH) -iquote . -iquote ./include

CXXFLAGS := \
	-std=c++20 -Wall -Wno-deprecated-declarations \
	-D_XOPEN_SOURCE \
	-DDEVELOPMENT=$(DEVELOPMENT) -DTIMING=$(TIMING) -DDEBUG=$(DEBUG) \
	-g $(OPTFLAGS) -arch $(ARCH) -iquote . -iquote ./include

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
        $(BUILD)/fd_table.o \
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
        xnd/util/io.c \
        xnd/platform/ucontext/setcontext.c \
        xnd/platform/ucontext/_setcontext.s

XND_LAUNCH_OBJECTS := \
        $(BUILD)/xnd_launch.o \
        $(BUILD)/ckptfile.o \
        $(BUILD)/exe.o \
        $(BUILD)/macho.o \
        $(BUILD)/path.o \
        $(BUILD)/shared_cache.o \
        $(BUILD)/io.o \
        $(BUILD)/log.o \
        $(BUILD)/env.o \
        $(BUILD)/xnd_coord_api.o

XND_COMMAND_OBJECTS := \
	$(BUILD)/xnd_command.o \
	$(BUILD)/log.o \
	$(BUILD)/path.o \
	$(BUILD)/io.o \
	$(BUILD)/exe.o \
	$(BUILD)/xnd_coord_api.o

XND_COORD_OBJECTS := \
        $(BUILD)/shared_cache.o \
        $(BUILD)/ckptfile.o \
        $(BUILD)/io.o \
        $(BUILD)/log.o \
        $(BUILD)/fd.o \
        $(BUILD)/env.o \
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

XND_PRINT_OBJECTS := \
        $(BUILD)/xnd_print.o \
        $(BUILD)/compress.o \
        $(BUILD)/log.o \
        $(BUILD)/path.o \
        $(BUILD)/io.o

XND_MACHO_PARSE_OBJECTS := \
	$(BUILD)/xnd_macho_parse.o \
	$(BUILD)/io.o \
	$(BUILD)/log.o \
	$(BUILD)/path.o \
	$(BUILD)/macho.o

ALL := \
        $(BUILD)/xnd_launch \
        $(BUILD)/xnd_print \
        $(BUILD)/xnd_restart \
        $(BUILD)/xnd_restart_internal \
        $(BUILD)/xnd_command \
        $(BUILD)/xnd_coordinator \
        $(BUILD)/libxnd.dylib \
	$(BUILD)/xnd_macho_parse

all: $(ALL)

debug:
	$(MAKE) clean
	$(MAKE) DEBUG=1 all

release:
	$(MAKE) clean
	$(MAKE) DEBUG=0 all

$(BUILD):
	mkdir -p $@

vpath %.c xnd xnd/util xnd/platform xnd/platform/ucontext xnd/coordinator xnd/pid xnd/wrappers
vpath %.cpp xnd xnd/pid
vpath %.s xnd/platform/ucontext

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libxnd.dylib: $(LIBXND_OBJECTS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -dynamiclib -fPIC -lz -o $@ $^
	dsymutil $@

RESTART_TEXT		:= 0x500000000000
RESTART_DATA		:= 0x50000000c000
RESTART_DATA_CONST	:= 0x500000010000
RESTART_LINKEDIT	:= 0x500000014000
RESTART_STACK_SIZE	:= 0x000000100000
RESTART_STACK		:= 0x500000118000

$(BUILD)/xnd_restart_internal: $(XND_RESTART_INTERNAL_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -fno-stack-protector \
	-DXND_RESTART_STACK=$(RESTART_STACK) \
	-DXND_RESTART_STACK_SIZE=$(RESTART_STACK_SIZE) \
	-DXND_RESTART_TEXT=$(RESTART_TEXT) \
	-Wl,-segaddr,__TEXT,$(RESTART_TEXT) \
	-Wl,-segaddr,__DATA,$(RESTART_DATA) \
	-Wl,-segaddr,__DATA_CONST,$(RESTART_DATA_CONST) \
	-Wl,-segaddr,__LINKEDIT,$(RESTART_LINKEDIT) \
	-Wl,-ld_classic \
	-o $@ $^

$(BUILD)/xnd_restart: $(XND_RESTART_OBJECTS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -lz -o $@ $^
	dsymutil $@

$(BUILD)/xnd_launch: $(XND_LAUNCH_OBJECTS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^
	dsymutil $@

$(BUILD)/xnd_print: $(XND_PRINT_OBJECTS) | $(BUILD)
	$(CC) $(CFLAGS) -lz -o $@ $^
	dsymutil $@

$(BUILD)/xnd_command: $(XND_COMMAND_OBJECTS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^
	dsymutil $@

$(BUILD)/xnd_coordinator: $(XND_COORD_OBJECTS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $^
	dsymutil $@

$(BUILD)/xnd_macho_parse: $(XND_MACHO_PARSE_OBJECTS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^
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
	$(XND_COORD_OBJECTS) \
	$(XND_PRINT_OBJECTS) \
	$(XND_MACHO_PARSE_OBJECTS) \
	$(XND_LAUNCH_OBJECTS) \
	$(XND_COMMAND_OBJECTS)
