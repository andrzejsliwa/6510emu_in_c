
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
DEBUG_CFLAGS = -Wall -Wextra -std=c11 -g -DDEBUG
LDFLAGS = -lm
SRC = 6510emu.c
TARGET = 6510emu
DEBUG_TARGET = emu_debug


all: $(TARGET) ## Build the 6510 emulator"

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

debug: $(DEBUG_TARGET) ## Build with debug symbols"

$(DEBUG_TARGET): $(SRC)
	$(CC) $(DEBUG_CFLAGS) -o $@ $^ $(LDFLAGS)

run: $(TARGET) ## Run the emulator

	./$(TARGET)

run_disasm: $(TARGET) ## Run with disassembly
	./$(TARGET) --disassemble

run_regs: $(TARGET) ## Run with disassembly and register display
	./$(TARGET) --disassemble --registers

run_fast: $(TARGET) ## Run at maximum speed
	./$(TARGET) --fast 

run_fast_disasm: $(TARGET) ## Run at maximum speed with disassembly
	./$(TARGET) --fast --disassemble

clean: ## Clean build artifacts

	rm -f $(TARGET) $(DEBUG_TARGET)

define print_help
	grep -E '^[a-zA-Z0-9_-]+:.*?## .*$$' $(1) | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36mmake %-20s\033[0m%s\n", $$1, $$2}'
endef

help:
	@printf "\033[36mHelp: \033[0m\n"
	@$(foreach file, $(MAKEFILE_LIST), $(call print_help, $(file));)


