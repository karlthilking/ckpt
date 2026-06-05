ARCH    ?= arm64
CC      := clang
CFLAGS  := -std=c17 -Wall -Wno-deprecated-declarations \
           -g3 -O0 -arch $(ARCH)

SRC     := ./src
TEST    := ./test
INCLUDE := ./include
BUILD   := ./build

LIBCKPT_WRAPPERS := $(SRC)/time_wrappers.c $(SRC)/stdlib_wrappers.c \
                    $(SRC)/pthread_wrappers.c $(SRC)/file_wrappers.c \
                    $(SRC)/signal_wrappers.c

LIBCKPT_SOURCES := $(SRC)/libckpt.c $(SRC)/pac.c $(SRC)/vm_common.c \
                   $(SRC)/vm_checkpoint.c $(SRC)/writeckpt.c \
                   $(SRC)/shared_cache.c $(SRC)/thread_info.c \
                   $(LIBCKPT_WRAPPERS)
                   
RESTART_SOURCES := $(SRC)/restart.c $(SRC)/pac.c $(SRC)/vm_common.c \
                   $(SRC)/vm_restore.c $(SRC)/readckpt.c \
                   $(SRC)/shared_cache.c

TESTS           := $(BUILD)/01_conjugate_gradient \
                   $(BUILD)/02_heat_diffusion \
                   $(BUILD)/03_monte_carlo $(BUILD)/04_stencil

BINARIES        := $(BUILD)/ckpt $(BUILD)/printckpt $(TESTS)
ALL             := $(BINARIES) $(BUILD)/restart $(BUILD)/libckpt.dylib

all: $(ALL)

$(BUILD):
	mkdir -p $@

$(BUILD)/libckpt.dylib: $(LIBCKPT_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -I$(INCLUDE) -dynamiclib -fPIC -o $@ $^

__TEXT          := 0x500000000000
__DATA          := 0x500000004000
__LINKEDIT      := 0x500000008000
$(BUILD)/restart: $(RESTART_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -I$(INCLUDE)  		\
	-Wl,-segaddr,__TEXT,$(__TEXT) 		\
	-Wl,-segaddr,__DATA,$(__DATA) 		\
	-Wl,-segaddr,__LINKEDIT,$(__LINKEDIT) 	\
	-o $@ $^

$(BUILD)/ckpt: $(SRC)/ckpt.c $(SRC)/repl.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(INCLUDE) -o $@ $^

$(BUILD)/%: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(INCLUDE) -o $@ $<

$(BUILD)/%: $(TEST)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

OPENMP_INCLUDE	:= /opt/homebrew/opt/libomp/include
OPENMP_LIB	:= /opt/homebrew/opt/libomp/lib
$(BUILD)/02_heat_diffusion: $(TEST)/02_heat_diffusion.c | $(BUILD)
	$(CC) -arch arm64 -g -O2 -Xpreprocessor -fopenmp -lomp \
        -I$(OPENMP_INCLUDE) -L$(OPENMP_LIB) -o $@ $<

clean:
	rm -rf build/
	rm -rf *.dSYM *.dylib *.o *.ckpt *.dat
