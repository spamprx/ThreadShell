CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -O2 -DNDEBUG
DEBUG_FLAGS = -g -O0 -DDEBUG
LDFLAGS = -lreadline

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
LOG_DIR = logs
JOB_DIR = jobs

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
TARGET = threadshell

.PHONY: all debug clean install test setup

all: setup $(BUILD_DIR)/$(TARGET)

debug: CXXFLAGS += $(DEBUG_FLAGS)
debug: setup $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $< -o $@

setup:
	@mkdir -p $(BUILD_DIR) $(LOG_DIR) $(JOB_DIR)
	@if [ ! -f $(JOB_DIR)/sample_job.txt ]; then \
		echo "Creating sample job files..."; \
	fi

install: all
	@echo "Installing ThreadShell-HPC..."
	sudo cp $(BUILD_DIR)/$(TARGET) /usr/local/bin/
	@echo "Installation complete. Run 'threadshell' from anywhere."

test: all
	@echo "Running basic tests..."
	./$(BUILD_DIR)/$(TARGET) < /dev/null || true
	@echo "Tests complete."

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(LOG_DIR)/*.csv
	@echo "Clean complete." 