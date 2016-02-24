PROG_NAME=snake

CC=gcc
CFLAGS=-Os -Wall
LDFLAGS=-lwiringPi -lssd1306 -ltftgfx

.PHONY:clean rebuild exec debug

all:$(PROG_NAME)

clean:
	@echo "Cleaning workspace.........."
	-rm ./*.o ./$(PROG_NAME)

rebuild:clean all


exec:all
	./$(PROG_NAME)

debug:CFLAGS+=-g
debug:rebuild
	gdb ./$(PROG_NAME)

$(PROG_NAME):snake.o 
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

