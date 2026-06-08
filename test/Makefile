CC      := clang
CFLAGS  := -Wall -g3 -O0 -arch arm64

TESTS := \
        01_conjugate_gradient \
        02_heat_diffusion

all: $(TESTS)

01_conjugate_gradient: 01_conjugate_gradient.c
	$(CC) $(CFLAGS) -o $@ $<

OPENMP_INCLUDE  := /opt/homebrew/opt/libomp/include
OPENMP_LIB      := /opt/homebrew/opt/libomp/lib
OPENMP_FLAGS	:= -Xpreprocessor -fopenmp -lomp \
		   -I$(OPENMP_INCLUDE) -L$(OPENMP_LIB)

02_heat_diffusion: 02_heat_diffusion.c
	$(CC) -arch arm64 -g -O2 $(OPENMP_FLAGS) -o $@ $<

clean:
	rm -rf $(TESTS) *.dSYM *.o
