export SRC_PATH ?= $(realpath ../src)
export UTILS_PATH ?= $(realpath ../utils)

PYTHON ?= python3
CC = gcc
CPPFLAGS = -I$(UTILS_PATH)
CFLAGS = -fPIC -Wall -Wextra -g
LDFLAGS = -L$(SRC_PATH)
LDLIBS = -losmem

SNIPPETS_SRC = $(sort $(wildcard snippets/*.c))
SNIPPETS = $(patsubst %.c,%,$(SNIPPETS_SRC))

.PHONY: all src snippets clean_src clean_snippets check check-fast test-custom lint clean

all: src snippets

src:
	$(MAKE) -C $(SRC_PATH)

snippets: $(SNIPPETS)

clean_snippets:
	rm -rf $(SNIPPETS) custom_tests

clean_src:
	$(MAKE) -C $(SRC_PATH) clean

clean: clean_src clean_snippets

custom_tests: custom_tests.c src
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

test-custom: custom_tests
	LD_LIBRARY_PATH=$(SRC_PATH) ./custom_tests

check:
	$(MAKE) clean_src clean_snippets src snippets
	$(PYTHON) run_tests.py

check-fast:
	$(MAKE) clean_src clean_snippets src snippets
	$(PYTHON) run_tests.py -d

lint:
	-cd .. && checkpatch.pl -f src/*.c tests/src/*.c
	-cd .. && checkpatch.pl -f checker/*.sh
	-cd .. && cpplint --recursive src/ tests/ checker/
	-cd .. && shellcheck checker/*.sh
#	-cd .. && pylint tests/*.py

snippets/%: snippets/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
