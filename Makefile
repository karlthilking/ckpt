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
	$(BUILD)/tls.o \
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
        xnd/shared_cache.c \
        xnd/util/log.c \
        xnd/util/path.c \
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
	$(BUILD)/xnd_macho_parse \
	$(BUILD)/segpatch

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

# FIXME: xnd_restart_internal's memory segment sizes can fluctuate
# with different optimization levels or other compile options.
# xnd_restart_internal's memory size definitions should be discovered
# dynamically, or use generous padding to between segments so changes
# to optimization level won't effect any code assuming a fixed size.
#
# If changes to xnd_restart_internal.c or any of the other compilation
# units it links against force one of xnd_restart_internal's segments
# to exceed their defined size, update the Make variable so the linker
# flags reserve enough space for each segment.
# (otool -lv xnd_restart_internal and look at vmsize of each segment)

RESTART_TEXT_SIZE := 0x8000
RESTART_TEXT := 0x500000000000

RESTART_DATA_SIZE := 0x4000
RESTART_DATA := $(shell printf "0x%X" \
	$$(( $(RESTART_TEXT)+$(RESTART_TEXT_SIZE) )))

RESTART_DATA_CONST_SIZE := 0x4000
RESTART_DATA_CONST := $(shell printf "0x%X" \
	$$(( $(RESTART_DATA)+$(RESTART_DATA_SIZE) )))

RESTART_LINKEDIT_SIZE := 0x4000
RESTART_LINKEDIT := $(shell printf "0x%X" \
	$$(( $(RESTART_DATA_CONST)+$(RESTART_DATA_CONST_SIZE) )))

# With DYLD_SHARED_REGION=private, /usr/lib/dyld will map itself above
# the main executable instead of residing in the dyld shared cache.
# As of macOS 26.5.2, dyld will occupy 80 16KB pages in virtual memory
# above the main executable, so to be on the safe side, reserve 100 pages
# of memory for dyld between xnd_restart_internal's __LINKEDIT and the
# bottom of the restart stack.

DYLD_RESERVE_SIZE := 0x190000
DYLD_START := $(shell printf "0x%X" \
	$$(( $(RESTART_LINKEDIT)+$(RESTART_LINKEDIT_SIZE) )))
DYLD_END := $(shell printf "0x%X" \
	$$(( $(DYLD_START)+$(DYLD_RESERVE_SIZE) )))

RESTART_STACK_SIZE := 0x100000
RESTART_STACK_BOTTOM := $(DYLD_END)
RESTART_STACK_TOP := $(shell printf "0x%X" \
	$$(( $(RESTART_STACK_BOTTOM)+$(RESTART_STACK_SIZE) )))

GUARD_ADDR := 0x100000000
GUARD_SIZE := 0x7b500000
GUARD_PROT := ---
GUARD_MAXPROT := rwx

RESTART_DEFS := \
	-DXND_RESTART_STACK=$(RESTART_STACK_BOTTOM)ULL \
	-DXND_RESTART_STACK_SIZE=$(RESTART_STACK_SIZE)ULL \
	-DXND_RESTART_TEXT=$(RESTART_TEXT)ULL \
	-DXND_RESTART_TEXT_SIZE=$(RESTART_TEXT_SIZE)ULL \
	-DXND_RESTART_DATA=$(RESTART_DATA)ULL \
	-DXND_RESTART_DATA_SIZE=$(RESTART_DATA_SIZE)ULL \
	-DXND_RESTART_DATA_CONST=$(RESTART_DATA_CONST)ULL \
	-DXND_RESTART_DATA_CONST_SIZE=$(RESTART_DATA_CONST_SIZE)ULL \
	-DXND_RESTART_LINKEDIT=$(RESTART_LINKEDIT)ULL \
	-DXND_RESTART_LINKEDIT_SIZE=$(RESTART_LINKEDIT_SIZE)ULL \
	-DXND_GUARD_ADDR=$(GUARD_ADDR)ULL \
	-DXND_GUARD_SIZE=$(GUARD_SIZE)ULL \
	-DDYLD_RESERVE_SIZE=$(DYLD_RESERVE_SIZE)ULL

# This assumes that macOS 15.x.x introduced the Memory Tag 22 region
# (supposedly for the unified buffer cache) that requires patching
# the restart binary with a guard segment after compiling.
MACOS_MAJOR := $(shell sw_vers --productVersion | sed s'/\.[0-9]*//g')
ifeq ($(shell test $(MACOS_MAJOR) -ge 15; echo $$?),0)
	SEGPATCH_ARGV := \
		$(BUILD)/segpatch $(BUILD)/xnd_restart_internal \
		--vmaddr $(GUARD_ADDR) --vmsize $(GUARD_SIZE) \
		--segname __XND --sectname __xnd \
		--prot $(GUARD_PROT)/$(GUARD_MAXPROT)
else
	SEGPATCH_ARGV :=
endif

$(BUILD)/xnd_restart_internal: $(XND_RESTART_INTERNAL_SOURCES) | $(BUILD)/segpatch
	$(CC) $(CFLAGS) -fno-stack-protector $(RESTART_DEFS) \
	-Wl,-segaddr,__TEXT,$(RESTART_TEXT) \
	-Wl,-segaddr,__DATA,$(RESTART_DATA) \
	-Wl,-segaddr,__DATA_CONST,$(RESTART_DATA_CONST) \
	-Wl,-segaddr,__LINKEDIT,$(RESTART_LINKEDIT) \
	-Wl,-ld_classic -o $@ $^
	$(SEGPATCH_ARGV)

$(BUILD)/vm_checkpoint.o: vm_checkpoint.c | $(BUILD)
	$(CC) $(CFLAGS) \
	-DXND_RESTART_BASE=$(RESTART_TEXT)ULL \
	-DXND_RESTART_END=$(RESTART_STACK_TOP)ULL \
	-c $< -o $@

$(BUILD)/libxnd.dylib: $(LIBXND_OBJECTS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -dynamiclib -fPIC -lz -o $@ $^
	dsymutil $@

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

$(BUILD)/segpatch: xnd/segpatch.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

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
