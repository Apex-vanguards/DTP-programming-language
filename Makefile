CXX      := g++
CXXFLAGS := -std=c++17 -O3 -march=native -Wall -Wextra -Iinclude -ffast-math
TARGET   := dtpc
SRC      := src/dtpc.cpp

STDLIB_DIR    := stdlib
INSTALL_BIN   := /usr/local/bin/dtpc
INSTALL_LIB   := /usr/local/share/dtp/stdlib
HOME_LIB      := $(HOME)/.dtp/stdlib

.PHONY: all clean install install-user examples

all: $(TARGET)

$(TARGET): $(SRC) $(wildcard include/*.hpp)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) -lstdc++fs
	@echo "Built: ./$(TARGET)"

install: $(TARGET)
	install -d $(INSTALL_LIB)
	install -m 755 $(TARGET) $(INSTALL_BIN)
	cp -r $(STDLIB_DIR)/. $(INSTALL_LIB)/
	@echo "Installed dtpc -> $(INSTALL_BIN)"
	@echo "Stdlib       -> $(INSTALL_LIB)"

install-user: $(TARGET)
	mkdir -p $(HOME_LIB)
	cp $(TARGET) $(HOME)/.dtp/dtpc
	cp -r $(STDLIB_DIR)/. $(HOME_LIB)/
	@echo "Installed dtpc -> $(HOME)/.dtp/dtpc"
	@echo "Stdlib       -> $(HOME_LIB)"
	@echo ""
	@echo "Add to PATH: export PATH=\"$(HOME)/.dtp:\$$PATH\""

examples: $(TARGET)
	@echo "--- Fibonacci ---"
	./$(TARGET) examples/fib.dtp -o /tmp/fib && /tmp/fib
	@echo "\n--- Sorting ---"
	./$(TARGET) examples/sort.dtp -o /tmp/sort && /tmp/sort
	@echo "\n--- Structs ---"
	./$(TARGET) examples/structs.dtp -o /tmp/structs && /tmp/structs

clean:
	rm -f $(TARGET) /tmp/__dtp_*.c
