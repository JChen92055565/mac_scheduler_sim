CC      := gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -pedantic -Iinc
LDFLAGS := -lm

SRC     := src/main.c src/sim.c src/scheduler.c src/metrics.c src/phy.c
OBJ     := $(SRC:.c=.o)
TARGET  := bin/l1sched

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

# compile any .c in src/ into .o
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET) --ttis 2000 --rb 100 --ues 32 --arrival 0.2 --deadline 8 --seed 42

clean:
	rm -rf bin src/*.o