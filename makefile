CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -g -D_DEFAULT_SOURCE -I.
SAN     = -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

TESTS   = tests/test_blackbox tests/test_legacy

.PHONY: all demo test test-asan output clean

all: $(TESTS) demo/audiobox

# The command-line program: run it with no arguments for the demo.
demo/audiobox: demo/audiobox.c sound_seg.c sound_seg.h
	$(CC) $(CFLAGS) demo/audiobox.c sound_seg.c -lm -o $@

demo: demo/audiobox
	./demo/audiobox

tests/%: tests/%.c sound_seg.c sound_seg.h tests/harness.h
	$(CC) $(CFLAGS) $< sound_seg.c -o $@

tests/%_asan: tests/%.c sound_seg.c sound_seg.h tests/harness.h
	$(CC) $(CFLAGS) $(SAN) $< sound_seg.c -o $@

# Run both suites.
test: $(TESTS)
	@status=0; for t in $(TESTS); do ./$$t || status=1; echo; done; exit $$status

# Run both suites with AddressSanitizer and UBSan (memory errors show as CRASH).
test-asan: $(TESTS:=_asan)
	@status=0; for t in $(TESTS:=_asan); do ./$$t || status=1; echo; done; exit $$status

# Regenerate tests/output.txt from a sanitizer run.
output: $(TESTS:=_asan)
	@$(MAKE) -s test-asan > tests/output.txt; cat tests/output.txt

clean:
	rm -f $(TESTS) $(TESTS:=_asan) demo/audiobox *.o
	rm -rf demo/out
