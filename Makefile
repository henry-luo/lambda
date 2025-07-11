# Jubily Project Makefile
# Utilizes compile.sh for build operations with standard make targets

# Project configuration
PROJECT_NAME = jubily
COMPILE_SCRIPT = ./compile.sh
DEFAULT_CONFIG = build_lambda_config.json
RADIANT_CONFIG = build_radiant_config.json

# Build configurations
BUILD_DIR = build
BUILD_DEBUG_DIR = build_debug
BUILD_WINDOWS_DIR = build_windows

# Output executables
LAMBDA_EXE = lambda.exe
RADIANT_EXE = radiant.exe
WINDOW_EXE = window.exe

# Auto-detect number of jobs for parallel compilation
NPROCS := 1
OS := $(shell uname -s)
ifeq ($(OS),Darwin)
	NPROCS := $(shell sysctl -n hw.ncpu)
else ifeq ($(OS),Linux)
	NPROCS := $(shell nproc)
endif

# Limit parallel jobs to reasonable maximum
JOBS := $(shell echo $$(($(NPROCS) > 8 ? 8 : $(NPROCS))))

# Default target
.DEFAULT_GOAL := build

# Phony targets (don't correspond to actual files)
.PHONY: all build clean debug release rebuild test run help install uninstall \
        lambda radiant window cross-compile format lint check docs \
        build-windows build-debug build-release clean-all distclean \
        verify-windows test-windows

# Help target - shows available commands
help:
	@echo "$(PROJECT_NAME) - Available Make Targets:"
	@echo ""
	@echo "Build Targets:"
	@echo "  build         - Build lambda project (incremental, default target)"
	@echo "  debug         - Build with debug symbols"
	@echo "  release       - Build optimized release version"
	@echo "  rebuild       - Force complete rebuild"
	@echo "  lambda        - Build lambda project specifically"
	@echo "  radiant       - Build radiant project"
	@echo "  window        - Build window project"
	@echo "  all           - Build all projects"
	@echo ""
	@echo "Cross-compilation:"
	@echo "  cross-compile - Cross-compile for Windows"
	@echo "  build-windows - Same as cross-compile"
	@echo ""
	@echo "Maintenance:"
	@echo "  clean         - Remove build artifacts"
	@echo "  clean-all     - Remove all build directories"
	@echo "  distclean     - Complete cleanup (build dirs + executables)"
	@echo ""
	@echo "Development:"
	@echo "  test          - Run tests (if available)"
	@echo "  verify-windows - Verify Windows cross-compiled executable with Wine"
	@echo "  test-windows  - Run CI tests for Windows executable"
	@echo "  run           - Build and run the default executable"
	@echo "  check         - Run static analysis and checks"
	@echo "  format        - Format source code"
	@echo "  lint          - Run linter on source files"
	@echo ""
	@echo "Options:"
	@echo "  JOBS=N        - Set number of parallel compilation jobs (default: $(JOBS))"
	@echo "  CONFIG=file   - Use specific configuration file"
	@echo ""
	@echo "Examples:"
	@echo "  make build JOBS=4         # Build with 4 parallel jobs"
	@echo "  make debug                # Debug build with AddressSanitizer"
	@echo "  make rebuild              # Force complete rebuild"

# Main build target (incremental)
build:
	@echo "Building $(PROJECT_NAME) (incremental)..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --jobs=$(JOBS)

$(LAMBDA_EXE): build

# Debug build
debug:
	@echo "Building debug version..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --debug --jobs=$(JOBS)

# Release build (optimized)
release: build-release

build-release:
	@echo "Building release version..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --jobs=$(JOBS)

# Force rebuild (clean + build)
rebuild:
	@echo "Force rebuilding $(PROJECT_NAME)..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --force --jobs=$(JOBS)

# Specific project builds
lambda:
	@echo "Building lambda project..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --jobs=$(JOBS)

radiant:
	@echo "Building radiant project..."
	$(COMPILE_SCRIPT) $(RADIANT_CONFIG) --jobs=$(JOBS)

window: radiant

# Build all projects
all: lambda radiant
	@echo "All projects built successfully."

# Cross-compilation for Windows
cross-compile: build-windows

build-windows:
	@echo "Cross-compiling for Windows..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --platform=windows --jobs=$(JOBS)

# Debugging builds with specific directories
build-debug:
	@echo "Building with debug configuration..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --debug --jobs=$(JOBS)

# Clean targets
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)/*.o
	@rm -rf $(BUILD_DEBUG_DIR)/*.o
	@rm -rf $(BUILD_WINDOWS_DIR)/*.o
	@rm -f $(BUILD_DIR)/*.compile_log
	@rm -f $(BUILD_DIR)/*.compile_status
	@rm -f $(BUILD_DEBUG_DIR)/*.compile_log
	@rm -f $(BUILD_DEBUG_DIR)/*.compile_status
	@rm -f $(BUILD_WINDOWS_DIR)/*.compile_log
	@rm -f $(BUILD_WINDOWS_DIR)/*.compile_status
	@echo "Build artifacts cleaned."

clean-all:
	@echo "Removing all build directories..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(BUILD_DEBUG_DIR)
	@rm -rf $(BUILD_WINDOWS_DIR)
	@echo "All build directories removed."

distclean: clean-all
	@echo "Complete cleanup..."
	@rm -f $(LAMBDA_EXE)
	@rm -f lambda-windows.exe
	@rm -f $(WINDOW_EXE)
	@rm -f _transpiled.c
	@rm -f *.exe
	@echo "Complete cleanup finished."

# Development targets
test:
	@echo "Running tests..."
	@if [ -d "test" ]; then \
		echo "Looking for test files..."; \
		find test -name "*.ls" -exec echo "Found test: {}" \; 2>/dev/null || true; \
		if [ -f "test_simple.ls" ]; then \
			echo "Running simple test..."; \
			./$(LAMBDA_EXE) test_simple.ls 2>/dev/null || echo "Test file found but execution failed"; \
		else \
			echo "No test files found. Create test/*.ls files to enable testing."; \
		fi; \
	else \
		echo "No test directory found. Tests not available."; \
	fi

# Windows verification targets
verify-windows:
	@echo "Verifying Windows cross-compiled executable..."
	@if [ ! -f "lambda-windows.exe" ]; then \
		echo "Windows executable not found. Building..."; \
		$(MAKE) build-windows; \
	fi
	@if [ ! -f "test/verify-windows-exe.sh" ]; then \
		echo "Error: Windows verification script not found at test/verify-windows-exe.sh"; \
		exit 1; \
	fi
	@cd test && ./verify-windows-exe.sh

test-windows:
	@echo "Running CI tests for Windows executable..."
	@if [ ! -f "lambda-windows.exe" ]; then \
		echo "Windows executable not found. Building..."; \
		$(MAKE) build-windows; \
	fi
	@if [ ! -f "test/test-windows-exe-ci.sh" ]; then \
		echo "Error: Windows CI test script not found at test/test-windows-exe-ci.sh"; \
		exit 1; \
	fi
	@cd test && ./test-windows-exe-ci.sh

run: build
	@echo "Running $(LAMBDA_EXE)..."
	@if [ -f "$(LAMBDA_EXE)" ]; then \
		./$(LAMBDA_EXE); \
	else \
		echo "Executable $(LAMBDA_EXE) not found. Build first with 'make build'"; \
		exit 1; \
	fi

# Code quality targets
# check:
# 	@echo "Running static analysis..."
# 	@echo "Checking for common issues in source files..."
# 	@find lambda -name "*.c" -o -name "*.h" | xargs -I {} sh -c 'echo "Checking: {}"; grep -n "TODO\|FIXME\|XXX" {} || true' 2>/dev/null
# 	@echo "Static analysis complete."

# format:
# 	@echo "Formatting source code..."
# 	@if command -v clang-format >/dev/null 2>&1; then \
# 		find lambda -name "*.c" -o -name "*.h" | xargs clang-format -i; \
# 		echo "Code formatted with clang-format."; \
# 	else \
# 		echo "clang-format not found. Install with: brew install clang-format"; \
# 	fi

lint:
	@echo "Running linter..."
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck --enable=warning,style,performance --std=c11 lambda/ 2>/dev/null || true; \
	else \
		echo "cppcheck not found. Install with: brew install cppcheck"; \
	fi

# Installation targets (optional)
install: build
	@echo "Installing $(PROJECT_NAME)..."
	@mkdir -p /usr/local/bin
	@cp $(LAMBDA_EXE) /usr/local/bin/$(PROJECT_NAME)
	@echo "$(PROJECT_NAME) installed to /usr/local/bin/$(PROJECT_NAME)"

uninstall:
	@echo "Uninstalling $(PROJECT_NAME)..."
	@rm -f /usr/local/bin/$(PROJECT_NAME)
	@echo "$(PROJECT_NAME) uninstalled."

# Documentation generation (if docs exist)
# docs:
# 	@echo "Generating documentation..."
# 	@if [ -f "README.md" ]; then \
# 		echo "README.md found."; \
# 		if command -v pandoc >/dev/null 2>&1; then \
# 			pandoc README.md -o README.html; \
# 			echo "Generated README.html"; \
# 		fi; \
# 	fi

# Advanced targets for development workflow
quick: 
	@echo "Quick build (minimal checks)..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --jobs=$(JOBS)

parallel:
	@echo "Maximum parallel build..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --jobs=8 --force

# Configuration-specific builds
config-debug:
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --debug --jobs=$(JOBS)

config-release:
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --jobs=$(JOBS)

# Utility targets
info:
	@echo "Project Information:"
	@echo "  Name: $(PROJECT_NAME)"
	@echo "  Build Script: $(COMPILE_SCRIPT)"
	@echo "  Default Config: $(DEFAULT_CONFIG)"
	@echo "  Parallel Jobs: $(JOBS)"
	@echo "  Build Directory: $(BUILD_DIR)"
	@echo "  Target Executable: $(LAMBDA_EXE)"
	@echo ""
	@echo "Available Configurations:"
	@ls -1 *.json 2>/dev/null | grep -E "(config|build)" | sed 's/^/  /' || echo "  No configuration files found"

# Dependency tracking (show what would be built)
what-will-build:
	@echo "Checking what files need compilation..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --jobs=1 | head -20

# Performance targets
time-build:
	@echo "Timing build performance..."
	@time $(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --force --jobs=$(JOBS)

benchmark:
	@echo "Build benchmark (3 runs)..."
	@for i in 1 2 3; do \
		echo "Run $$i:"; \
		time $(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --force --jobs=$(JOBS) >/dev/null; \
		echo ""; \
	done
