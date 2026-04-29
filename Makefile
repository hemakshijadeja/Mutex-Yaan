# Satellite Network — Makefile
# Builds: bin/server  bin/client

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -Wshadow -g
LDFLAGS := -lpthread -lrt

# ---------- directories ----------
BIN_DIR  := bin
LOG_DIR  := logs

# ---------- source lists ----------
COMMON_SRC := common/shared_mem.c

SERVER_SRC := server/server.c        \
              server/auth.c          \
              server/satellite.c     \
              server/ground_station.c \
              server/command_handler.c \
              server/telemetry_log.c  \
              server/signal_handler.c  \
              ipc/debris_monitor.c   \
              $(COMMON_SRC)

CLIENT_SRC := client/client.c

# ---------- targets ----------
.PHONY: all server client clean run-server run-client

all: $(BIN_DIR)/server $(BIN_DIR)/client

$(BIN_DIR)/server: $(SERVER_SRC) | $(BIN_DIR) $(LOG_DIR)
	$(CC) $(CFLAGS) -I. -o $@ $(SERVER_SRC) $(LDFLAGS)
	@echo "[MAKE] server binary built → $@"

$(BIN_DIR)/client: $(CLIENT_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -I. -o $@ $(CLIENT_SRC) $(LDFLAGS)
	@echo "[MAKE] client binary built → $@"

# create output directories if they don't exist
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(LOG_DIR):
	mkdir -p $(LOG_DIR)

clean:
	rm -rf $(BIN_DIR)
	rm -f $(LOG_DIR)/telemetry.csv $(LOG_DIR)/orbit_log.csv
	@echo "[MAKE] cleaned."

# convenience targets — open two terminals and run these
run-server: $(BIN_DIR)/server
	./$(BIN_DIR)/server

run-client: $(BIN_DIR)/client
	./$(BIN_DIR)/client
