CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -g
SERVER_TARGET = retail_server
CLIENT_TARGET = retail_client
SERVER_SRC = src/retail_server.c
CLIENT_SRC = src/retail_client.c src/fort.c

all: $(SERVER_TARGET) $(CLIENT_TARGET)

run: $(SERVER_TARGET)
	./$(SERVER_TARGET)

run-server: $(SERVER_TARGET)
	./$(SERVER_TARGET)

run-client: $(CLIENT_TARGET)
	./$(CLIENT_TARGET)

demo: $(SERVER_TARGET) $(CLIENT_TARGET)
	bash ./demo_watch_full.sh

$(SERVER_TARGET): $(SERVER_SRC)
	$(CC) $(CFLAGS) -pthread -o $@ $^

$(CLIENT_TARGET): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f src/*.o retail_store $(SERVER_TARGET) $(CLIENT_TARGET)
