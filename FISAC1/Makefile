CC = gcc
CFLAGS = -Wall -Wextra -I./include -g
LDFLAGS = -lws2_32

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

APP_SRCS = \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/server.c \
	$(SRC_DIR)/socket.c \
	$(SRC_DIR)/network.c \
	$(SRC_DIR)/websocket.c \
	$(SRC_DIR)/protocol.c \
	$(SRC_DIR)/http_client.c \
	$(SRC_DIR)/sha1.c \
	$(SRC_DIR)/base64.c \
	$(SRC_DIR)/cJSON.c
APP_OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(APP_SRCS))

TARGET = server.exe

all: $(TARGET)

$(TARGET): $(APP_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) server

.PHONY: all clean
