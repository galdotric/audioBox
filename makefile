# Compiler to use
CC = gcc

# Compiler flags for the object file
CFLAGS = -Wall -Werror -fPIC -std=c99

# Target object file
OBJ = sound_seg.o

# Source file
SRC = sound_seg.c

# Default target
all: $(OBJ)

# Rule to build sound_seg.o
$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

# Clean up
clean:
	rm -f $(OBJ)

# Phony targets
.PHONY: all clean