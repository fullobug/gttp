SRC_DIR   := src
BUILD_DIR := build
EXE 	  := $(BUILD_DIR)/gttp
GCC_FLAGS := -g -Wall 
LD_FLAGS := -L/usr/lib/x86_64-linux-gnu -lsqlite3 -ldl -lpthread 
OBJ_FILES := $(BUILD_DIR)/$(wildcard *.o)

# build/gttp : src/gttp.c src/sqlite3.c

$(EXE) : $(BUILD_DIR)/gttp.o 
	gcc $^ $(GCC_FLAGS) $(LD_FLAGS) -o $(EXE) 
	
build/gttp.o : src/gttp.c src/gttp.h
	gcc -c $(GCC_FLAGS) $< -o build/gttp.o 

#$(EXE) : $(SRC_DIR)/*.c
#	gcc $(GCC_FLAGS) -o $@ $^

clean:
	rm -rf $(BUILD_DIR)/*
	
