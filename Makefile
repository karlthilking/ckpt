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

TESTS           := $(BUILD)/test/02_thread_id

BINARIES        := $(BUILD)/ckpt $(BUILD)/printckpt $(TESTS)
ALL             := $(BINARIES) $(BUILD)/restart $(BUILD)/libckpt.dylib

all: $(ALL)

$(BUILD):
	mkdir -p $@
	mkdir $(BUILD)/test

$(BUILD)/libckpt.dylib: $(LIBCKPT_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -I$(INCLUDE) -dynamiclib -fPIC -o $@ $^

__TEXT          := 0x300000000
__DATA          := 0x300004000
__LINKEDIT      := 0x300008000
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

$(BUILD)/test/%: $(TEST)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/test/%: $(TEST)/%.cpp | $(BUILD)
	clang++ -std=c++20 -g3 -O0 -arch $(ARCH) -o $@ $<

clean:
	rm -rf build/
	rm -rf *.dSYM *.dylib *.o *.ckpt *.dat
