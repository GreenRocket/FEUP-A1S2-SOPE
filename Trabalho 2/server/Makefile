PROG := server

SRC := $(wildcard src/*.c) \
	   $(wildcard ../shared/src/*.c)

OBJ := $(SRC:.c=.o)
DEP := $(OBJ:.o=.d)  # one dependency file for each source

INC1 := include
INC2 := ../shared/include

CC := gcc
CFLAGS := -I$(INC1) -I$(INC2) -Wall -Wextra -Werror
LDFLAGS := -lpthread

$(PROG): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

-include $(DEP)   # include all dep files in the makefile

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	@$(CPP) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@

.PHONY: clean
clean:
	rm -f $(OBJ) $(DEP) $(PROG)