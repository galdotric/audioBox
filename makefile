# Makefile for sound_seg module

# rule to build sound_seg.o for submission:
# this rule is used by the marking script with 'make sound_seg.o'
sound_seg.o: sound_seg.c sound_seg.h
	gcc -c sound_seg.c -o sound_seg.o -fPIC -Wall -Wextra

# Rule to build test executable for local testing with ASAN and debugging
test: test.c sound_seg.c sound_seg.h
	gcc -g -fsanitize=address -Wall -Wextra test.c sound_seg.c -o test

# Clean up generated files to avoid pushing binaries to the repository
clean:
	rm -f sound_seg.o test