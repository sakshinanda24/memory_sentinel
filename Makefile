# MemorySentinel - Top-level Makefile
# Targets:
#   all            Build kernel module + userspace binaries (default)
#   userspace      Build only userspace binaries (no kernel headers needed)
#   kernel         Build only the kernel module
#   install        Install binaries system-wide and load the kernel module
#   uninstall      Remove installed binaries and unload the kernel module
#   clean          Remove all build artifacts
#   help           Show this message

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -I./include
LDFLAGS  := -lpthread

DAEMON_SRC := userspace/sentinel_daemon.cpp
CLI_SRC    := userspace/sentinel_cli.cpp
DAEMON_BIN := sentinel-daemon
CLI_BIN    := sentinel

.PHONY: all kernel userspace clean install uninstall help

all: kernel userspace

kernel:
	$(MAKE) -C kernel/

userspace: $(DAEMON_BIN) $(CLI_BIN)

$(DAEMON_BIN): $(DAEMON_SRC) include/sentinel_proto.h
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

$(CLI_BIN): $(CLI_SRC) include/sentinel_proto.h
	$(CXX) $(CXXFLAGS) -o $@ $<

install: all
	sudo cp $(DAEMON_BIN) /usr/local/bin/
	sudo cp $(CLI_BIN)    /usr/local/bin/
	sudo $(MAKE) -C kernel/ install
	@echo "Done. Run: sudo sentinel-daemon &"

uninstall:
	-./$(CLI_BIN) stop 2>/dev/null || true
	sudo rm -f /usr/local/bin/$(DAEMON_BIN) /usr/local/bin/$(CLI_BIN)
	-sudo $(MAKE) -C kernel/ uninstall

clean:
	$(MAKE) -C kernel/ clean
	rm -f $(DAEMON_BIN) $(CLI_BIN)

help:
	@echo "MemorySentinel build targets:"
	@echo "  make all        - Build kernel module + userspace (default)"
	@echo "  make userspace  - Build only daemon + CLI (no kernel headers)"
	@echo "  make kernel     - Build only the kernel module"
	@echo "  make install    - Install system-wide + load module"
	@echo "  make uninstall  - Remove installation + unload module"
	@echo "  make clean      - Remove build artifacts"
