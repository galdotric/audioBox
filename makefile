# Compiler to use
CC = gcc

# Compiler flags
CFLAGS = -Wall -g

# Target executable name
TARGET = sound_test

# Source files
SOURCES = sound_seg.c test.c

# Object files
OBJECTS = sound_seg.o test.o

# Header files
HEADERS = sound_seg.h

# Default target
all: $(TARGET)

# Link object files to create executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET)

# Compile sound_seg.c to object file
sound_seg.o: sound_seg.c $(HEADERS)
	$(CC) $(CFLAGS) -c sound_seg.c

# Compile test.c to object file
test.o: test.c $(HEADERS)
	$(CC) $(CFLAGS) -c test.c

# Run the program
run: $(TARGET)
	./$(TARGET)

# Clean up
clean:
	rm -f $(OBJECTS) $(TARGET)

# Phony targets
.PHONY: all run clean