CC		:= clang
CXX		:= clang++
CFLAGS		:= -Wall -g -O2 -arch arm64
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

OMP_INCLUDE 	:= /opt/homebrew/opt/libomp/include
OMP_LIB		:= /opt/homebrew/opt/libomp/lib
OMP_FLAGS	:= -Xpreprocessor -fopenmp -lomp \
		   -I$(OMP_INCLUDE) -L$(OMP_LIB)

$(BUILD)/02_heat_diffusion: 02_heat_diffusion.c | $(BUILD)
	$(CC) $(CFLAGS) $(OMP_FLAGS) -o $@ $<

$(BUILD)/%: %.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/%: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(ALL)
	rm -rf $(ALL:%=%.dSYM)
	rm -rf *-checkpoints xnd.log __pycache__

.PHONY: clean all
