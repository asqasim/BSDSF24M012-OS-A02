CC = gcc
CFLAGS = -Wall -Wextra -g
SRC = src/ls-v1.0.0.c
TARGET = bin/ls

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET) obj/*.o

.PHONY: all clean
