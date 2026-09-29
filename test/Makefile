# Desktop tests of micro-MADS (Linux/macOS, BSD socket port).
#   make            build and run the unit tests
#   make live       run the agent against a broker (BROKER, PORT, NAME, SECONDS)
CC ?= cc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -g -O1 \
         -Wall -Wextra -fsanitize=address,undefined -I../inc -I../src
LIB = ../src/mm_agent.c ../src/mm_fsm.c ../src/mm_ini.c ../src/mm_zmtp.c \
      ../port/mm_tcp_posix.c

BROKER ?= 127.0.0.1
PORT ?= 9092
NAME ?= stm32_board
SECONDS ?= 5

all: test_units test_broker
	./test_units

test_units: test_units.c ../src/mm_ini.c ../src/mm_zmtp.c
	$(CC) $(CFLAGS) $^ -o $@

test_broker: test_broker.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@

live: test_broker
	./test_broker $(BROKER) $(PORT) $(NAME) $(SECONDS)

clean:
	rm -f test_units test_broker

.PHONY: all live clean
