CC = gcc

# Compiler flags
CFLAGS = -Iinclude

# Linker flags
LDFLAGS = -lcurl -lcjson

# Source files
SRC_MAIN = src/main.c
SRC_MESSAGE = src/message_compilator.c
SRC_CONNECTION = src/connection_handler.c
SRC_COMMUNICATION = src/communication_handler.c

# Object files
OBJ_MAIN = $(SRC_MAIN:.c=.o)
OBJ_MESSAGE = $(SRC_MESSAGE:.c=.o)
OBJ_CONNECTION = $(SRC_CONNECTION:.c=.o)
OBJ_COMMUNICATION = $(SRC_COMMUNICATION:.c=.o)

# Output file
TARGET = client

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJ_MAIN) $(OBJ_MESSAGE) $(OBJ_CONNECTION) $(OBJ_COMMUNICATION)
	$(CC) $(OBJ_MAIN) $(OBJ_MESSAGE) $(OBJ_CONNECTION) $(OBJ_COMMUNICATION) -o $@ $(LDFLAGS)


install-libraries:
	apt update
	apt install libcurl4-openssl-dev libcjson-dev build-essential -y

install-libraries-and-llama:
	apt update
	apt install libcurl4-openssl-dev libcjson-dev build-essential curl -y
	curl -fsSL https://ollama.com/install.sh | sh
	until curl -s http://localhost:11434 | grep "Ollama is running" ; do \
		sleep 1; \
	done
	ollama run llama3.2 &
