CC = gcc
CFLAGS = -Wall -Wextra -I./include -g
LDFLAGS = -lws2_32

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

APP_SRCS = $(wildcard $(SRC_DIR)/*.c)
APP_OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(APP_SRCS))

TARGET = server

all: clean $(TARGET)

$(TARGET): $(APP_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir $(OBJ_DIR)

clean:
	rmdir /s /q $(OBJ_DIR) 2>nul || del /q $(TARGET).exe 2>nul

.PHONY: all clean
