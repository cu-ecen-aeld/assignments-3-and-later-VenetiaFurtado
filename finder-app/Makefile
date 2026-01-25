################################################################################
# Assignment 2: File Operations and Cross Compiler
# ECEN 5713 | Spring 2026
# Author: Venetia Furtado
# GNU makefile for writer application
# Resources used: https://makefiletutorial.com/
################################################################################

# Compiler to use CROSS_COMPILE allows cross compilation with aarch64-none-linux-gnu-
# else resolves to gcc
CC := $(CROSS_COMPILE)gcc

# Compiler flags:
# -Wall: enable compiler warnings
# -Werror: treat all warnings as errors
CFLAGS := -Wall -Werror

# Name of the final executable
TARGET := writer

# Source files
SRC := writer.c

# Replaces .c files with .o (writer.c -> writer.o)
OBJ := $(SRC:.c=.o)

# Default target: running `make` builds the target
all: $(TARGET)

# Compilation step
# $< -> source file (first prerequisite)
# $@ -> object file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Linker step
# Build the executable from object files
# $@ -> target name (writer)
# $^ -> all prerequisites (writer.o)
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^


# Clean target
# Removes generated files
clean:
	rm -f $(TARGET) $(OBJ)

# Declare phony targets
# Ensures these targets run even if files named 'all' or 'clean' exist
.PHONY: all clean
