hearder := ./include

CFLAGS := -I $(hearder) -std=c99 -Wall -fsanitize=address

SRC := $(shell find src -maxdepth 1 -name "*.c")
EXE := Clox

${EXE}: ${SRC}
	@echo "Building..."
	@echo "Flags: ${CFLAGS}"
	@echo "Source: ${SRC}"
	@clang  $(CFLAGS) -o $@ $(SRC)
	@echo "Done!"

build: ${EXE}

run: ${EXE}
	@echo "Running..."
	@./${EXE}

test: ${EXE}
	@echo "Testing..."
	@./${EXE} test

clean:
	@echo "Cleaning..."
	@rm -f ${EXE}
	@echo "Done!"

.PHONY: build run clean

# CC = clang
# CFLAGS = -I $(hearder) -std=c99 

# main: $(obj)
# 	$(CC) $(CFLAGS) -o $@ $^

# # clean: 
# # 	rm -f *.o
