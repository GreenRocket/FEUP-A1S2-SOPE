# Executable name
PROG := forensic

# Project folders
SRC_DIR := ./src
INC_DIR := ./include
OBJ_DIR := ./obj

# Compiler
CC := gcc

# Compiler flags
CFLAGS := -I$(INC_DIR)
CFLAGSW := -Wall -Wextra -Werror

SRC_FILES := $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC_FILES))

# Compile source into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(CFLAGSW) -c -o $@ $<

# Link object files into executable file
$(PROG): $(OBJ_FILES)
	$(CC) $(CFLAGS) $(CFLAGSW) -o $@ $^ -lm


# GNUMake feature: Prevent confusing with files called all, clean or run
.PHONY: all clean run

all: $(PROG)

clean:
	rm -f $(PROG)
	rm -r -f $(OBJ_DIR)

run: all
	./$(PROG) ${ARGS}