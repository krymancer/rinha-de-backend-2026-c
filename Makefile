# Rinha de Backend 2026 — C build. Static (scratch image), Haswell/AVX2, LTO.
CC      ?= cc
CFLAGS  ?= -O3 -march=haswell -mavx2 -std=gnu11 -D_GNU_SOURCE -Wall -Wextra -fno-plt -flto
LDFLAGS ?= -static -flto -s
LDLIBS  ?= -lm
SRC      = src
BIN      = bin

LIB = $(SRC)/vec.c $(SRC)/ivf.c $(SRC)/net.c $(SRC)/http.c

all: $(BIN)/lb $(BIN)/server $(BIN)/indexer $(BIN)/measure

$(BIN):
	mkdir -p $(BIN)

$(BIN)/lb: $(SRC)/lb.c $(SRC)/net.c $(SRC)/rinha.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/lb.c $(SRC)/net.c $(LDFLAGS)

$(BIN)/server: $(SRC)/server.c $(LIB) $(SRC)/rinha.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/server.c $(LIB) $(LDFLAGS) $(LDLIBS)

$(BIN)/indexer: $(SRC)/indexer.c $(SRC)/ivf.c $(SRC)/vec.c $(SRC)/net.c $(SRC)/rinha.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/indexer.c $(SRC)/ivf.c $(SRC)/vec.c $(SRC)/net.c $(LDFLAGS) $(LDLIBS)

$(BIN)/measure: $(SRC)/measure.c $(SRC)/vec.c $(SRC)/ivf.c $(SRC)/net.c $(SRC)/rinha.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/measure.c $(SRC)/vec.c $(SRC)/ivf.c $(SRC)/net.c $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf $(BIN)

.PHONY: all clean
