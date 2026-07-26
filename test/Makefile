CC		:= clang
CXX		:= clang++
CFLAGS		:= -Wall -g -O0 -arch arm64
CXXFLAGS	:= -std=c++20 -Wall -g -O0 -arch arm64
BUILD		:= bin

TESTS := \
	01_conjugate_gradient \
	02_heat_diffusion \
	03_nbody \
	04_jacobi

ALL := $(addprefix $(BUILD)/,$(TESTS))

all: $(ALL)

$(BUILD):
	mkdir -p $@

OPENMP_INCLUDE	:= /opt/homebrew/opt/libomp/include
OPENMP_LIB	:= /opt/homebrew/opt/libomp/lib
OPENMP_FLAGS	:= -Xpreprocessor -fopenmp -lomp \
		   -I$(OPENMP_INCLUDE) -L$(OPENMP_LIB)

$(BUILD)/02_heat_diffusion: 02_heat_diffusion.c | $(BUILD)
	$(CC) -arch arm64 -g -O2 $(OPENMP_FLAGS) -o $@ $<

$(BUILD)/%: %.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/%: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(ALL)
	rm -rf $(ALL:%=%.dSYM)
	rm -rf *-checkpoints xnd.log __pycache__

.PHONY: clean all
