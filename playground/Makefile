CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
LDLIBS ?= -lm

COMMON_SRCS = fixed_point_logadd.c
MCMC_SRCS = mcmc.c mcmc_core.c $(COMMON_SRCS)

.PHONY: all test clean

all: mcmc fixed_point_experiment test_fixed_point_logadd

mcmc: $(MCMC_SRCS) mcmc_core.h fixed_point_logadd.h
	$(CC) $(CFLAGS) -o $@ $(MCMC_SRCS) $(LDLIBS)

fixed_point_experiment: fixed_point_experiment.c $(COMMON_SRCS) fixed_point_logadd.h
	$(CC) $(CFLAGS) -o $@ fixed_point_experiment.c $(COMMON_SRCS) $(LDLIBS)

test_fixed_point_logadd: test_fixed_point_logadd.c $(COMMON_SRCS) fixed_point_logadd.h
	$(CC) $(CFLAGS) -o $@ test_fixed_point_logadd.c $(COMMON_SRCS) $(LDLIBS)

test: test_fixed_point_logadd
	./test_fixed_point_logadd

clean:
	$(RM) fixed_point_experiment test_fixed_point_logadd
