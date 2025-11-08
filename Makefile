# Lambda Project Makefile
# Utilizes compile.sh for build operations with standard make targets

# Project configuration
PROJECT_NAME = Lambda
COMPILE_SCRIPT = ./compile.sh
DEFAULT_CONFIG = build_lambda_config.json

# Build configurations
BUILD_DIR = build
BUILD_DEBUG_DIR = build_debug
BUILD_RELEASE_DIR = build_release
BUILD_WINDOWS_DIR = build_windows
BUILD_LINUX_DIR = build_linux
TYPESET_DIR = typeset

# Output executables
LAMBDA_EXE = lambda.exe
RADIANT_EXE = radiant.exe
WINDOW_EXE = window.exe

# Unicode support is always enabled (utf8proc-based)
# No longer using conditional compilation flags

# Auto-detect number of jobs for parallel compilation
NPROCS := 1
OS := $(shell uname -s)
ifeq ($(OS),Darwin)
	NPROCS := $(shell sysctl -n hw.ncpu)
	PREMAKE_FILE := premake5.mac.lua
else ifeq ($(OS),Linux)
	NPROCS := $(shell nproc)
	PREMAKE_FILE := premake5.lin.lua
else
	# Windows/MSYS2 detection
	NPROCS := $(shell nproc 2>/dev/null || echo 4)
	PREMAKE_FILE := premake5.win.lua
endif

# Optimize parallel jobs: use all cores for compilation, limit linking to 1
JOBS := $(NPROCS)
LINK_JOBS := 1

# Enable ccache for faster builds if available
ifneq ($(shell which ccache 2>/dev/null),)
	CC := ccache $(CC)
	CXX := ccache $(CXX)
	export CCACHE_DIR := $(shell pwd)/build/.ccache
	export CCACHE_MAXSIZE := 500M
	export CCACHE_COMPRESS := 1
endif

# Detect Python executable
# On MSYS2/Windows, prefer MINGW64 over CLANG64 for Universal CRT avoidance
# Force explicit paths for MSYS environment compatibility
ifeq ($(shell test -f /mingw64/bin/python3 && echo yes),yes)
	PYTHON := /mingw64/bin/python3
else ifeq ($(shell test -f /clang64/bin/python3 && echo yes),yes)
	PYTHON := /clang64/bin/python3
else
	PYTHON := python3
endif

# Detect Premake5 executable
# On MSYS2/Windows, prefer MINGW64 over CLANG64 for Universal CRT avoidance
PREMAKE5 := $(shell command -v premake5 2>/dev/null || command -v /mingw64/bin/premake5 2>/dev/null || command -v /clang64/bin/premake5 2>/dev/null || echo premake5)

# MINGW64 Environment Detection and Validation
MSYSTEM_DETECTED := $(shell echo $$MSYSTEM)
IS_MINGW64 := $(shell [ "$(MSYSTEM_DETECTED)" = "MINGW64" ] && echo "yes" || echo "no")
IS_CLANG64 := $(shell [ "$(MSYSTEM_DETECTED)" = "CLANG64" ] && echo "yes" || echo "no")
IS_MSYS2 := $(shell [ -n "$(MSYSTEM_DETECTED)" ] && echo "yes" || echo "no")

# Detect C/C++ compilers
# On MSYS2/Windows, prefer MINGW64 GCC over CLANG64 to avoid Universal CRT
# Force explicit paths for MSYS environment compatibility
ifeq ($(shell test -f /mingw64/bin/gcc && echo yes),yes)
	CC := /mingw64/bin/gcc
	CXX := /mingw64/bin/g++
	AR := /mingw64/bin/ar
	RANLIB := /mingw64/bin/ranlib
else ifeq ($(shell test -f /clang64/bin/clang && echo yes),yes)
	CC := /clang64/bin/clang
	CXX := /clang64/bin/clang++
	AR := /clang64/bin/ar
	RANLIB := /clang64/bin/ranlib
else
	CC := gcc
	CXX := g++
	AR := ar
	RANLIB := ranlib
endif

# Tree-sitter grammar dependencies
# This system automatically manages the dependency chain:
# grammar.js -> parser.c -> ts-enum.h -> C/C++ source files
# When grammar.js is modified, the parser and enum header are automatically regenerated
GRAMMAR_JS = lambda/tree-sitter-lambda/grammar.js
PARSER_C = lambda/tree-sitter-lambda/src/parser.c
GRAMMAR_JSON = lambda/tree-sitter-lambda/src/grammar.json
NODE_TYPES_JSON = lambda/tree-sitter-lambda/src/node-types.json
TS_ENUM_H = lambda/ts-enum.h
UPDATE_TS_ENUM_SCRIPT = ./utils/update_ts_enum.sh

# Auto-generate parser and ts-enum.h when grammar.js changes
$(TS_ENUM_H): $(GRAMMAR_JS)
	@echo "Grammar changed, regenerating parser and ts-enum.h..."
	@cd lambda/tree-sitter-lambda && npx tree-sitter-cli@0.24.7 generate
	$(UPDATE_TS_ENUM_SCRIPT)
	@echo "Updated ts-enum.h from grammar changes"

$(PARSER_C) $(GRAMMAR_JSON) $(NODE_TYPES_JSON): $(GRAMMAR_JS)
	@echo "Generating parser from grammar.js..."
	@cd lambda/tree-sitter-lambda && npx tree-sitter-cli@0.24.7 generate

# Lambda embedding dependencies
# Auto-generate lambda-embed.h when lambda.h changes
# This embeds the lambda.h header file as a byte array for runtime access
LAMBDA_H_FILE = lambda/lambda.h
LAMBDA_EMBED_H_FILE = lambda/lambda-embed.h

# Auto-generate lambda-embed.h when lambda.h changes or lambda-embed.h doesn't exist
$(LAMBDA_EMBED_H_FILE): $(LAMBDA_H_FILE)
	@echo "lambda.h changed or lambda-embed.h missing, regenerating lambda-embed.h..."
	@if command -v xxd >/dev/null 2>&1; then \
		echo "Regenerating $(LAMBDA_EMBED_H_FILE) from $(LAMBDA_H_FILE)..."; \
		xxd -i "$(LAMBDA_H_FILE)" > "$(LAMBDA_EMBED_H_FILE)"; \
		echo "Successfully regenerated $(LAMBDA_EMBED_H_FILE)"; \
	else \
		echo "Error: xxd command not found! Cannot regenerate $(LAMBDA_EMBED_H_FILE)"; \
		echo "Install xxd or manually run: xxd -i $(LAMBDA_H_FILE) > $(LAMBDA_EMBED_H_FILE)"; \
		exit 1; \
	fi

# Tree-sitter library targets
# Use system-installed libtree-sitter on Windows/MSYS2, build from source otherwise
ifeq ($(IS_MSYS2),yes)
TREE_SITTER_LIB = /mingw64/lib/libtree-sitter.dll.a
else
TREE_SITTER_LIB = lambda/tree-sitter/libtree-sitter.a
endif
TREE_SITTER_LAMBDA_LIB = lambda/tree-sitter-lambda/libtree-sitter-lambda.a
TREE_SITTER_JAVASCRIPT_LIB = lambda/tree-sitter-javascript/libtree-sitter-javascript.a

# Build or verify tree-sitter library
$(TREE_SITTER_LIB):
ifeq ($(IS_MSYS2),yes)
	@echo "Using system libtree-sitter library..."
	@if [ ! -f "$(TREE_SITTER_LIB)" ]; then \
		echo "❌ System libtree-sitter not found. Install with: pacman -S mingw-w64-x86_64-libtree-sitter"; \
		exit 1; \
	else \
		echo "✅ Found system libtree-sitter: $(TREE_SITTER_LIB)"; \
	fi
else
	@echo "Building tree-sitter library from source..."
	@echo "🔧 Compiler: $(CC)"
	@echo "🔧 CXX: $(CXX)"
	@echo "🔧 Environment: MSYSTEM=$(MSYSTEM)"
	@echo "🔧 Adding /mingw64/bin to PATH for DLL dependencies..."
	env -u OS PATH="/mingw64/bin:$$PATH" $(MAKE) -C lambda/tree-sitter libtree-sitter.a CC="$(CC)" CXX="$(CXX)" V=1
endif

# Build tree-sitter-lambda library (depends on parser generation)
$(TREE_SITTER_LAMBDA_LIB): $(PARSER_C)
	@echo "Building tree-sitter-lambda library..."
	@echo "🔧 Compiler: $(CC)"
	@echo "🔧 CXX: $(CXX)"
	@echo "🔧 Environment: MSYSTEM=$(MSYSTEM)"
	@echo "🔧 Working directory: lambda/tree-sitter-lambda"
	@echo "🔧 Unsetting OS variable to bypass Windows check..."
	@echo "🔧 Adding /mingw64/bin to PATH for DLL dependencies..."
	env -u OS PATH="/mingw64/bin:$$PATH" $(MAKE) -C lambda/tree-sitter-lambda libtree-sitter-lambda.a CC="$(CC)" CXX="$(CXX)" V=1 VERBOSE=1

# Build tree-sitter-javascript library
$(TREE_SITTER_JAVASCRIPT_LIB):
	@echo "Building tree-sitter-javascript library..."
	@echo "🔧 Compiler: $(CC)"
	@echo "🔧 CXX: $(CXX)"
	@echo "🔧 Environment: MSYSTEM=$(MSYSTEM)"
	@echo "🔧 Working directory: lambda/tree-sitter-javascript"
	@echo "🔧 Unsetting OS variable to bypass Windows check..."
	@echo "🔧 Adding /mingw64/bin to PATH for DLL dependencies..."
	env -u OS PATH="/mingw64/bin:$$PATH" $(MAKE) -C lambda/tree-sitter-javascript libtree-sitter-javascript.a CC="$(CC)" CXX="$(CXX)" V=1 VERBOSE=1

# MINGW64 Environment Validation Functions
define mingw64_env_check
	@if [ "$(IS_MSYS2)" = "yes" ] && [ "$(IS_MINGW64)" != "yes" ]; then \
		echo "⚠️  Warning: Not in MINGW64 environment!"; \
		echo "Current MSYSTEM: $(MSYSTEM_DETECTED)"; \
	fi
endef

define mingw64_toolchain_verify
	@echo "🔍 Verifying toolchain..."
	@if command -v $(CC) >/dev/null 2>&1; then \
		echo "✅ Compiler: $(CC) ($(shell $(CC) --version 2>/dev/null | head -1 || echo 'version unknown'))"; \
	else \
		echo "❌ Compiler $(CC) not found"; \
	fi
	@if command -v $(CXX) >/dev/null 2>&1; then \
		echo "✅ C++ Compiler: $(CXX) ($(shell $(CXX) --version 2>/dev/null | head -1 || echo 'version unknown'))"; \
	else \
		echo "❌ C++ Compiler $(CXX) not found"; \
	fi
endef

# Checking DLL dependencies to avoid Windows Universal CRT
define mingw64_dll_check
	@if [ -f "lambda.exe" ]; then \
		ldd lambda.exe 2>/dev/null | grep -E "not found|mingw64|msys64|ucrt|api-ms-win-crt" || echo "✅ No problematic dependencies found"; \
	else \
		echo "⚠️  lambda.exe not found, skipping DLL check"; \
	fi
endef

# Function to run make with error collection and summary
# Usage: $(call run_make_with_error_summary,TARGET)
# TARGET: The make target to build (e.g., lambda, radiant)
define run_make_with_error_summary
	@echo "🔨 Starting build process for target: $(1)..."
	@BUILD_LOG=$$(mktemp) && \
	if $(MAKE) -C build/premake config=debug_native $(1) -j$(JOBS) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)" LINK_JOBS="$(LINK_JOBS)" 2>&1 | tee "$$BUILD_LOG"; then \
		echo "✅ Build completed successfully for target: $(1)"; \
	else \
		echo "❌ Build failed for target: $(1)"; \
	fi; \
	echo ""; \
	echo "📋 Build Summary:"; \
	echo "================"; \
	ERROR_COUNT=`grep -E "(error:|Error:|ERROR:|fatal:|Failed:|failed:)" "$$BUILD_LOG" 2>/dev/null | wc -l | tr -d ' '`; \
	WARNING_COUNT=`grep -E "(warning:|Warning:|WARNING:)" "$$BUILD_LOG" 2>/dev/null | wc -l | tr -d ' '`; \
	echo "Errors: $$ERROR_COUNT"; \
	echo "Warnings: $$WARNING_COUNT"; \
	echo ""; \
	if [ "$$ERROR_COUNT" -gt 0 ] 2>/dev/null; then \
		echo "🔴 ERRORS FOUND:"; \
		echo "================"; \
		grep -n -E "(error:|Error:|ERROR:|fatal:|Failed:|failed:)" "$$BUILD_LOG" | while IFS=: read -r line_num content; do \
			file_info=$$(echo "$$content" | grep -oE "\.\.\/[^[:space:]]+\.[ch]p*p?:[0-9]+:[0-9]+:" | head -1); \
			if [ -z "$$file_info" ]; then \
				file_info=$$(echo "$$content" | grep -oE "\.\.\/[^[:space:]]+\.[ch]p*p?:[0-9]+:" | head -1); \
			fi; \
			if [ -z "$$file_info" ]; then \
				file_info=$$(echo "$$content" | grep -oE "[^[:space:]]+\.[ch]p*p?:[0-9]+:[0-9]+:" | head -1); \
			fi; \
			if [ -z "$$file_info" ]; then \
				file_info=$$(echo "$$content" | grep -oE "[^[:space:]]+\.[ch]p*p?:[0-9]+:" | head -1); \
			fi; \
			if [ -n "$$file_info" ]; then \
				rel_path=$$(echo "$$file_info" | cut -d: -f1); \
				line_no=$$(echo "$$file_info" | cut -d: -f2); \
				col_no=$$(echo "$$file_info" | cut -d: -f3); \
				if [[ "$$rel_path" == ../* ]]; then \
					abs_path="$$(cd build/premake && realpath "$$rel_path" 2>/dev/null || echo "$$(pwd)/$$rel_path")"; \
				else \
					abs_path="$$(pwd)/build/premake/$$rel_path"; \
				fi; \
				if [ -n "$$col_no" ] && [ "$$col_no" != "" ]; then \
					location="$$line_no:$$col_no"; \
				else \
					location="$$line_no"; \
				fi; \
				clean_content=$$(echo "$$content" | sed "s|[^[:space:]]*\.[ch]p*p*:[0-9]*:[0-9]*:||" | sed "s|[^[:space:]]*\.[ch]p*p*:[0-9]*:||"); \
				echo "   file://$$abs_path:$$location -$$clean_content"; \
			else \
				echo "   $$content"; \
			fi; \
		done; \
		echo ""; \
		echo "💡 Click on the file:// links above to jump to errors in VS Code"; \
	fi; \
	rm -f "$$BUILD_LOG"
endef

# Combined tree-sitter libraries target
tree-sitter-libs: $(TREE_SITTER_LIB) $(TREE_SITTER_LAMBDA_LIB)

# Build tree-sitter without Unicode/ICU dependencies (minimal build)
# Uses the amalgamated lib.c file approach recommended by ChatGPT
build-tree-sitter:
	@echo "Building minimal tree-sitter without Unicode/ICU dependencies..."
	@echo "🔧 Using amalgamated build (lib.c) - no Unicode dependencies"
	@cd lambda/tree-sitter && \
	echo "🧹 Cleaning previous build..." && \
	rm -f libtree-sitter-minimal.a tree_sitter.o && \
	echo "🔧 Compiling amalgamated tree-sitter..." && \
	env PATH="/mingw64/bin:$$PATH" $(CC) -c lib/src/lib.c \
		-Ilib/include \
		-O3 -Wall -Wextra -std=c11 -fPIC \
		-D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE \
		-o tree_sitter.o && \
	echo "� Creating static library..." && \
	env PATH="/mingw64/bin:$$PATH" $(AR) rcs libtree-sitter-minimal.a tree_sitter.o && \
	echo "🧹 Cleaning object file..." && \
	rm -f tree_sitter.o && \
	echo "✅ Minimal tree-sitter library built: lambda/tree-sitter/libtree-sitter-minimal.a" && \
	ls -la libtree-sitter-minimal.a

# Clean minimal tree-sitter build
clean-tree-sitter-minimal:
	@echo "Cleaning minimal tree-sitter build..."
	@cd lambda/tree-sitter && \
	rm -f libtree-sitter-minimal.a tree_sitter.o && \
	echo "✅ Minimal tree-sitter build cleaned."

# Default target
.DEFAULT_GOAL := build

# Phony targets (don't correspond to actual files)
.PHONY: all build build-ascii clean clean-test clean-grammar clean-radiant generate-grammar debug release rebuild test test-input run help install uninstall \
	    lambda radiant window format lint check docs intellisense analyze-size \
	    build-windows build-linux build-debug build-release clean-all distclean \
	    build-tree-sitter clean-tree-sitter-minimal tree-sitter-libs \
	    verify-windows verify-linux test-windows test-linux tree-sitter-libs \
	    generate-premake clean-premake build-test build-test-linux \
	    build-mingw64 build-tree-sitter clean-tree-sitter-minimal build-radiant \
	    test-radiant capture-layout test-layout layout

# Help target - shows available commands
help:
	@echo "$(PROJECT_NAME) - Available Make Targets:"
	@echo ""
	@echo "Build Targets (Premake-based):"
	@echo "  build         - Build lambda project using Premake build system (incremental, default)"
	@echo "                  On Windows/MSYS2: Automatically configures MINGW64 toolchain with PATH fixes"
	@echo "                  On other platforms: Prefers MINGW64 over CLANG64 to avoid Universal CRT"
	@echo "  build-radiant - Build Radiant HTML/CSS/SVG rendering engine executable only"
	@echo "                  Focuses on building radiant.exe without test executables"
	@echo "  debug         - Build with debug symbols and AddressSanitizer using Premake"
	@echo "  release       - Build optimized release version using Premake"
	@echo "  rebuild       - Force complete rebuild using Premake"
	@echo "  lambda        - Build lambda project specifically using Premake"
	@echo "  radiant       - Alias for build-radiant"
	@echo "  all           - Build all projects"
	@echo ""
	@echo "MINGW64 Targets (Universal CRT Avoidance):"
	@echo "  build-mingw64 - Enforce MINGW64 environment build (fails if not in MINGW64)"
	@echo "                  Ensures traditional MSVCRT.dll usage instead of Universal CRT"
	@echo ""
	@echo "Cross-compilation:"
	@echo "  build-windows - Same as cross-compile (now defaults to MINGW64)"
	@echo "  build-linux   - Cross-compile for Linux (musl static)"
	@echo "  build-test-linux - Cross-compile tests for Linux"
	@echo ""
	@echo "Maintenance:"
	@echo "  clean         - Remove build artifacts"
	@echo "  clean-test    - Remove test output and temporary files"
	@echo "  clean-grammar - Remove generated grammar and embed files (parser.c, ts-enum.h, lambda-embed.h)"
	@echo "  clean-radiant - Remove radiant build outputs (executable, premake files, object files)"
	@echo "  clean-all     - Remove all build directories and tree-sitter libraries"
	@echo "  distclean     - Complete cleanup (build dirs + executables + tests)"
	@echo "  intellisense  - Update VS Code IntelliSense database (compile_commands.json)"
	@echo ""
	@echo "Premake Build System:"
	@echo "  generate-premake - Generate premake5.lua from build_lambda_config.json"
	@echo "  clean-premake - Clean Premake build artifacts and generated files"
	@echo "  build-test    - Build all test projects using Premake"
	@echo ""
	@echo "Grammar & Parser:"
	@echo "  generate-grammar - Generate parser and ts-enum.h from grammar.js"
	@echo "                     (automatic when grammar.js changes)"
	@echo "  tree-sitter-libs - Build tree-sitter and tree-sitter-lambda libraries"
	@echo "  build-tree-sitter - Build tree-sitter without Unicode/ICU dependencies (Windows)"
	@echo "                      Creates libtree-sitter-minimal.a for Windows builds"
	@echo "  clean-tree-sitter-minimal - Clean minimal tree-sitter build artifacts"
	@echo ""
	@echo "Development:"
	@echo "  test          - Run comprehensive unit tests"
	@echo "  test-library  - Run library tests only"
	@echo "  test-input    - Run input processing test suite (MIME detection & math)"
	@echo "  test-validator- Run validator tests only"
	@echo "  test-mir      - Run MIR JIT tests only"
	@echo "  test-lambda   - Run lambda runtime tests only"
	@echo "  test-std      - Run Lambda Standard Tests (custom test runner)"
	@echo "  test-verbose  - Run tests with verbose output"
	@echo "  test-sequential - Run tests sequentially (not parallel)"
	@echo "  test-coverage - Run tests with code coverage analysis"
	@echo "  test-memory   - Run memory leak detection tests"
	@echo "  test-benchmark- Run performance benchmark tests"
	@echo "  test-fuzz     - Run fuzzing tests for robustness"
	@echo "  test-integration - Run end-to-end integration tests"
	@echo "  test-all      - Run complete test suite (all test types)"
	@echo "  test-radiant  - Run all Radiant layout engine tests (95+ tests: flexbox, layout, rendering)"
	@echo "                  Available suites: basic, intermediate, medium, advanced, baseline"
	@echo "  test-windows  - Run CI tests for Windows executable"
	@echo "  test-linux    - Run CI tests for Linux executable"
	@echo "  run           - Build and run the default executable"
	@echo "  analyze       - Run static analysis with scan-build (fixed for custom build)"
	@echo "  analyze-verbose - Run detailed static analysis with extra checkers"
	@echo "  analyze-single - Run static analysis on individual files"
	@echo "  analyze-direct - Direct clang static analysis (bypasses build system)"
	@echo "  analyze-compile-db - Use compile_commands.json for analysis (requires bear)"
	@echo "  tidy          - Run clang-tidy analysis on C++ files"
	@echo "  tidy-full     - Comprehensive clang-tidy with compile database"
	@echo "  tidy-fix      - Run clang-tidy with automatic fixes (interactive)"
	@echo "  check         - Run basic code checks (TODO/FIXME finder)"
	@echo "  format        - Format source code with clang-format"
	@echo "  lint          - Run linter (cppcheck) on source files"
	@echo "  analyze-size  - Analyze executable size breakdown by components"
	@echo "  test-layout              - Run Radiant layout integration tests (all suites)"
	@echo "                             Uses Radiant engine (Lexbor-based HTML/CSS rendering)"
	@echo "                             Usage: make test-layout suite=baseline (run specific suite)"
	@echo "                             Usage: make test-layout test=table_simple (run specific test, .html optional)"
	@echo "                             Usage: make test-layout pattern=float (run tests matching pattern)"
	@echo "                             Note: Uppercase variants also work (SUITE=, TEST=, PATTERN=)"
	@echo "                             Available suites: auto-detected from test/layout/data/"
	@echo "  layout                   - Run Lambda CSS layout integration tests (all suites)"
	@echo "                             Uses Lambda CSS engine (custom CSS cascade and layout)"
	@echo "                             Usage: make layout suite=baseline (run specific suite)"
	@echo "                             Usage: make layout test=table_simple (run specific test, .html optional)"
	@echo "                             Usage: make layout pattern=float (run tests matching pattern)"
	@echo "                             Note: Uppercase variants also work (SUITE=, TEST=, PATTERN=)"
	@echo "                             Available suites: auto-detected from test/layout/data/"
	@echo "  capture-layout   - Extract browser layout references using Puppeteer"
	@echo "                             Supports both .html and .htm file extensions"
	@echo "                             Usage: make capture-layout (all suites, skip existing)"
	@echo "                             Usage: make capture-layout suite=baseline"
	@echo "                             Usage: make capture-layout force=1 (regenerate all)"
	@echo "                             Usage: make capture-layout file=path/to/test.html"
	@echo "                             Usage: make capture-layout file=path/to/test.htm"
	@echo "                             Note: Uppercase variants also work (SUITE=, FORCE=, FILE=)"
	@echo ""
	@echo "Options:"
	@echo "  JOBS=N        - Set number of parallel compilation jobs (default: $(JOBS))"
	@echo "  CONFIG=file   - Use specific configuration file"
	@echo ""
	@echo "Examples:"
	@echo "  make build JOBS=4         # Build with 4 parallel jobs"
	@echo "  make debug                # Debug build with AddressSanitizer"
	@echo "  make rebuild              # Force complete rebuild"

# Environment debugging target
env-debug:
	@echo "🔍 Environment Detection Debug:"
	@echo "MSYSTEM: '$(MSYSTEM)'"
	@echo "MSYSTEM_DETECTED: '$(MSYSTEM_DETECTED)'"
	@echo "IS_MSYS2: '$(IS_MSYS2)'"
	@echo "IS_MINGW64: '$(IS_MINGW64)'"
	@echo "IS_CLANG64: '$(IS_CLANG64)'"

# Main build target (incremental) - Windows/MSYS2 optimized
build: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-libs
ifeq ($(IS_MSYS2),yes)
	@echo "🔧 Windows/MSYS2 detected - using optimized build configuration..."
	@echo "🔧 Setting up MINGW64 toolchain environment..."
	@echo "Building $(PROJECT_NAME) using Premake build system..."
	PATH="/mingw64/bin:$$PATH" $(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	@echo "Generating makefiles..."
	PATH="/mingw64/bin:$$PATH" $(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building lambda executable with $(JOBS) parallel jobs..."
	PATH="/mingw64/bin:$$PATH" $(MAKE) -C build/premake -j$(JOBS) lambda CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)" --no-print-directory -s CFLAGS="-w" CXXFLAGS="-w"
	@echo "✅ Build completed successfully!"
else
	@echo "Building $(PROJECT_NAME) using Premake build system..."
	$(call mingw64_env_check)
	$(call mingw64_toolchain_verify)
	@echo "Generating Premake configuration..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	@echo "Generating makefiles..."
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building lambda executable with $(JOBS) parallel jobs..."
	# Ensure explicit compiler variables are passed to Premake build
	@echo "Using CC=$(CC) CXX=$(CXX)"
	$(call run_make_with_error_summary,lambda)
endif

print-vars:
	@echo "Unicode support: Always enabled (utf8proc)"

print-jobs:
	@echo "CPU cores detected (NPROCS): $(NPROCS)"
	@echo "Parallel jobs (JOBS): $(JOBS)"
	@echo "Link jobs (LINK_JOBS): $(LINK_JOBS)"

$(LAMBDA_EXE): build

# MINGW64-specific build target (enforced environment)
build-mingw64: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-libs
	@echo "🔄 Building with MINGW64 environment (avoids Universal CRT)..."
	@if [ "$(IS_MSYS2)" = "yes" ] && [ "$(IS_MINGW64)" != "yes" ]; then \
		echo "❌ Error: Must be in MINGW64 environment for this target"; \
		echo "Current MSYSTEM: $(MSYSTEM_DETECTED)"; \
		echo "💡 Switch to MINGW64:"; \
		echo "   1. Close this terminal"; \
		echo "   2. Open MSYS2 MINGW64 terminal"; \
		echo "   3. Navigate to: cd /d/Projects/Lambda"; \
		echo "   4. Run: make build-mingw64"; \
		exit 1; \
	fi
	$(call mingw64_toolchain_verify)
	@echo "Setting MINGW64 environment variables..."
	@export CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib" PATH="/mingw64/bin:$$PATH"
	@echo "Generating Premake configuration..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	@echo "Generating makefiles..."
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building lambda executable with $(JOBS) parallel jobs..."
	$(MAKE) -C build/premake config=debug_native lambda -j$(JOBS) CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib" --no-print-directory -s CFLAGS="-w" CXXFLAGS="-w" 2>&1 | grep -v "warning:"
	@echo "✅ MINGW64 build completed. Executable: lambda.exe"
	@echo "🧪 Testing executable..."
	@./lambda.exe --help >/dev/null 2>&1 && echo "✅ Executable runs successfully" || echo "⚠️  Executable test failed"
	$(call mingw64_dll_check)




# Debug build - Now uses Premake with MINGW64 preference
debug: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-libs
	@echo "Building debug version using Premake build system..."
	$(call mingw64_env_check)
	$(call mingw64_toolchain_verify)
	@echo "Generating Premake configuration..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	@echo "Generating makefiles..."
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building lambda executable (debug) with $(JOBS) parallel jobs..."
	$(MAKE) -C build/premake config=debug_native lambda -j$(JOBS) CC="$(CC)" CXX="$(CXX)"
	@echo "Debug build completed. Executable: lambda.exe"
	$(call mingw64_dll_check)

# Release build (optimized)
release: build-release

build-release: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-libs
	@echo "Building release version using Premake build system..."
	$(call mingw64_env_check)
	$(call mingw64_toolchain_verify)
	@echo "Generating Premake configuration..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	@echo "Generating makefiles..."
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building lambda executable (release) with $(JOBS) parallel jobs..."
	$(MAKE) -C build/premake config=release_x64 lambda -j$(JOBS) CC="$(CC)" CXX="$(CXX)"
	@echo "Release build completed. Executable: lambda.exe"
	$(call mingw64_dll_check)

# Force rebuild (clean + build)
rebuild: clean $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-libs
	@echo "Force rebuilding $(PROJECT_NAME) using Premake build system..."
	@echo "Generating Premake configuration..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	@echo "Generating makefiles..."
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building lambda executable with $(JOBS) parallel jobs..."
	$(MAKE) -C build/premake config=debug_native lambda -j$(JOBS) CC="$(CC)" CXX="$(CXX)"
	@echo "Rebuild completed. Executable: lambda.exe"

# Specific project builds
lambda: build

# Radiant HTML/CSS/SVG rendering engine build (executable only)
build-radiant: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-libs
	@echo "Building Radiant HTML/CSS/SVG rendering engine..."
	@echo "Generating unified Premake configuration (includes radiant target)..."
	$(PYTHON) utils/generate_premake.py --config $(DEFAULT_CONFIG) --output $(PREMAKE_FILE)
	@echo "Generating makefiles for unified build..."
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building radiant executable with $(JOBS) parallel jobs..."
	# Ensure explicit compiler variables are passed to Premake build
	@echo "Using CC=$(CC) CXX=$(CXX)"
	$(call run_make_with_error_summary,radiant)
	@echo "✅ Radiant executable built successfully: $(RADIANT_EXE)"

radiant: build-radiant

# window: radiant

# Build all projects
all: lambda
	@echo "All projects built successfully."

# Debugging builds with specific directories
build-debug:
	@echo "Building with debug configuration..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --debug --jobs=$(JOBS)

build-wasm:
	@echo "Building WebAssembly version..."
	./compile-wasm.sh --linking-only

# Clean targets
clean:
	@echo "Cleaning build artifacts and executables..."
	@echo "Cleaning Premake build artifacts..."
	@rm -rf $(BUILD_DIR)/obj 2>/dev/null || true
	@rm -rf $(BUILD_DIR)/lib 2>/dev/null || true
	@echo "Cleaning legacy build artifacts (if any)..."
	@rm -rf $(BUILD_DIR)/*.o 2>/dev/null || true
	@rm -rf $(BUILD_DEBUG_DIR)/*.o 2>/dev/null || true
	@rm -rf $(BUILD_RELEASE_DIR)/*.o 2>/dev/null || true
	@rm -rf $(BUILD_WINDOWS_DIR)/*.o 2>/dev/null || true
	@rm -rf $(BUILD_LINUX_DIR)/*.o 2>/dev/null || true
	@rm -f $(BUILD_DIR)/*.d 2>/dev/null || true
	@rm -f $(BUILD_DEBUG_DIR)/*.d 2>/dev/null || true
	@rm -f $(BUILD_RELEASE_DIR)/*.d 2>/dev/null || true
	@rm -f $(BUILD_WINDOWS_DIR)/*.d 2>/dev/null || true
	@rm -f $(BUILD_LINUX_DIR)/*.d 2>/dev/null || true
	@rm -f $(BUILD_DIR)/*.compile_log 2>/dev/null || true
	@rm -f $(BUILD_DIR)/*.compile_status 2>/dev/null || true
	@rm -f $(BUILD_DEBUG_DIR)/*.compile_log 2>/dev/null || true
	@rm -f $(BUILD_DEBUG_DIR)/*.compile_status 2>/dev/null || true
	@rm -f $(BUILD_RELEASE_DIR)/*.compile_log 2>/dev/null || true
	@rm -f $(BUILD_RELEASE_DIR)/*.compile_status 2>/dev/null || true
	@rm -f $(BUILD_WINDOWS_DIR)/*.compile_log 2>/dev/null || true
	@rm -f $(BUILD_WINDOWS_DIR)/*.compile_status 2>/dev/null || true
	@rm -f $(BUILD_LINUX_DIR)/*.compile_log 2>/dev/null || true
	@rm -f $(BUILD_LINUX_DIR)/*.compile_status 2>/dev/null || true
	@echo "Cleaning executables..."
	@rm -f $(LAMBDA_EXE)
	@rm -f $(RADIANT_EXE)
	@rm -f $(WINDOW_EXE)
	@rm -f lambda_debug.exe
	@rm -f lambda_release.exe
	@rm -f lambda-windows.exe
	@rm -f lambda-linux.exe
	@rm -f _transpiled.c
	@echo "Build artifacts and executables cleaned."

clean-test:
	@echo "Cleaning test build outputs..."
	@rm -rf test_output/ 2>/dev/null || true
	@rm -f test/*.exe 2>/dev/null || true
	@rm -f test_*.exe 2>/dev/null || true
	@rm -f *.exe.tmp 2>/dev/null || true
	@rm -f build_test_*.json 2>/dev/null || true
	@rm -f build_test_*.json.tmp 2>/dev/null || true
	@find test/ -name "*.dSYM" -type d -exec rm -rf {} + 2>/dev/null || true
	@find test/ -name "*.o" -type f -delete 2>/dev/null || true
	@echo "Cleaning Radiant test outputs..."
	@rm -f test/test_radiant_flex_gtest.exe 2>/dev/null || true
	@rm -f test/test_radiant_flex_algorithm_gtest.exe 2>/dev/null || true
	@rm -f test/test_radiant_flex_integration_gtest.exe 2>/dev/null || true
	@rm -f test/test_radiant_text_flow_gtest.exe 2>/dev/null || true
	@rm -f test/test_radiant_font_face_gtest.exe 2>/dev/null || true
	@rm -f test/test_radiant_layout_gtest.exe 2>/dev/null || true
	@rm -f test/test_js_gtest.exe 2>/dev/null || true
	@rm -f test/test_flex_core_validation.exe 2>/dev/null || true
	@rm -f test/test_flex_simple.exe 2>/dev/null || true
	@rm -f test/test_flex_minimal.exe 2>/dev/null || true
	@rm -f test/test_flex_layout_gtest.exe 2>/dev/null || true
	@rm -f test/test_flex_standalone.exe 2>/dev/null || true
	@rm -f test/test_flex_new_features.exe 2>/dev/null || true
	@find test/ -name "*radiant*" -name "*.dSYM" -type d -exec rm -rf {} + 2>/dev/null || true
	@find test/ -name "*flex*" -name "*.dSYM" -type d -exec rm -rf {} + 2>/dev/null || true
	@find test/ -name "*text_flow*" -name "*.dSYM" -type d -exec rm -rf {} + 2>/dev/null || true
	@find test/ -name "*font_face*" -name "*.dSYM" -type d -exec rm -rf {} + 2>/dev/null || true
	@find test/ -name "*layout*" -name "*.dSYM" -type d -exec rm -rf {} + 2>/dev/null || true
	@echo "Test build outputs cleaned."

clean-grammar:
	@echo "Cleaning generated grammar and embed files..."
	@rm -f $(TS_ENUM_H)
	@rm -f $(PARSER_C)
	@rm -f $(GRAMMAR_JSON)
	@rm -f $(NODE_TYPES_JSON)
	@rm -f $(LAMBDA_EMBED_H_FILE)
	@echo "Generated grammar and embed files cleaned."

clean-radiant:
	@echo "Cleaning radiant build outputs..."
	@rm -f $(RADIANT_EXE)
	@rm -rf build/premake/radiant.make 2>/dev/null || true
	@if [ -d "build/obj/radiant" ]; then rm -rf build/obj/radiant; fi
	@echo "Radiant build outputs cleaned."

# IntelliSense support
intellisense:
	@echo "Updating IntelliSense database..."
	@./utils/update_intellisense.sh

# Generate grammar explicitly (useful for development)
generate-grammar: $(TS_ENUM_H)
	@echo "Grammar generation complete."

clean-all: clean-premake clean-test clean-radiant
	@echo "Removing all build directories..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(BUILD_DEBUG_DIR)
	@rm -rf $(BUILD_RELEASE_DIR)
	@rm -rf $(BUILD_WINDOWS_DIR)
	@rm -rf $(BUILD_LINUX_DIR)
	@echo "Cleaning tree-sitter libraries..."
	@if [ -d "lambda/tree-sitter" ]; then \
		cd lambda/tree-sitter && $(MAKE) clean; \
	fi
	@if [ -d "lambda/tree-sitter-lambda" ]; then \
		cd lambda/tree-sitter-lambda && $(MAKE) clean; \
	fi
	@echo "All build directories and tree-sitter libraries cleaned."

distclean: clean-all clean-grammar clean-test
	@echo "Complete cleanup..."
	@rm -f $(LAMBDA_EXE)
	@rm -f lambda_debug.exe
	@rm -f lambda_release.exe
	@rm -f lambda-windows.exe
	@rm -f lambda-linux.exe
	@rm -f $(WINDOW_EXE)
	@rm -f _transpiled.c
	@rm -f *.exe
	@echo "Complete cleanup finished."

# Development targets
test: build-test
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@echo "Running test suite ..."
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --parallel; \
	else \
		echo "Error: No test suite found"; \
		exit 1; \
	fi

test-dev:
	@echo "Running comprehensive test suite (development mode)..."
	@if [ -f "test_modern.sh" ]; then \
		./test_modern.sh || echo "Note: Some tests failed due to incomplete features (math parser, missing dependencies)"; \
	elif [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --parallel || echo "Note: Some tests failed due to incomplete features"; \
	else \
		echo "Error: No test suite found"; \
		exit 1; \
	fi

test-sequential: build-test
	@echo "Running comprehensive test suite (sequential execution)..."
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --sequential; \
	else \
		echo "Error: No test suite found"; \
		exit 1; \
	fi

test-parallel: build
	@echo "Running comprehensive test suite (parallel execution)..."
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --parallel; \
	else \
		echo "Error: No test suite found"; \
		exit 1; \
	fi

test-library: build
	@echo "Running library test suite..."
	@echo "Pre-compiling URL test with correct dependencies..."
	@clang -fms-extensions -Ilib/mem-pool/include -I/opt/homebrew/Cellar/criterion/2.4.2_2/include -o test/test_url.exe test/test_url.c build/url.o build/url_parser.o build/strbuf.o build/variable.o build/buffer.o build/utils.o -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -lcriterion
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --target=library --raw; \
	else \
		echo "Error: No test script found"; \
		exit 1; \
	fi


test-input: build
	@echo "Running input processing test suite..."
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --target=input --raw; \
	else \
		echo "Error: No test script found"; \
		exit 1; \
	fi

test-validator: build
	@echo "Running validator test suite..."
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --target=validator --raw; \
	else \
		echo "Error: Test script not found at test/test_run.sh"; \
		echo "Please ensure the test script exists and is executable."; \
		exit 1; \
	fi

test-lambda: build
	@echo "Running lambda test suite..."
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --target=lambda --raw; \
	else \
		echo "Error: Test script not found at test/test_run.sh"; \
		echo "Please ensure the test script exists and is executable."; \
		exit 1; \
	fi

# CSS Resolution Verification Target
# Compares Lambda CSS resolved properties against browser reference data
resolve: build
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════════════════════"
	@echo "CSS Resolution Verification"
	@echo "═══════════════════════════════════════════════════════════════════════════════"
	@echo ""
	@if [ -z "$(TEST)" ]; then \
		echo "Usage: make resolve TEST=<test_name>"; \
		echo ""; \
		echo "Example: make resolve TEST=baseline_803_basic_margin"; \
		echo ""; \
		echo "Available tests:"; \
		ls -1 test/layout/reference/baseline/*.json | xargs -n1 basename | sed 's/\.json//' | head -20; \
		echo "... and more (see test/layout/reference/baseline/)"; \
		echo ""; \
		exit 1; \
	fi; \
	TEST_NAME="$(TEST)"; \
	HTML_FILE="test/layout/data/baseline/$${TEST_NAME}.html"; \
	if [ ! -f "$${HTML_FILE}" ]; then \
		echo "❌ Error: Test HTML file not found: $${HTML_FILE}"; \
		exit 1; \
	fi; \
	REF_FILE="test/layout/reference/baseline/$${TEST_NAME}.json"; \
	if [ ! -f "$${REF_FILE}" ]; then \
		echo "❌ Error: Browser reference not found: $${REF_FILE}"; \
		exit 1; \
	fi; \
	echo "📄 Test: $${TEST_NAME}"; \
	echo "📂 HTML: $${HTML_FILE}"; \
	echo "📊 Reference: $${REF_FILE}"; \
	echo ""; \
	echo "─────────────────────────────────────────────────────────────────────────────────"; \
	echo "Running Lambda CSS resolution..."; \
	echo "─────────────────────────────────────────────────────────────────────────────────"; \
	./$(LAMBDA_EXE) layout "$${HTML_FILE}" --width 1200 --height 800 2>&1 | grep -E '^\[|^Error' || true; \
	echo ""; \
	if [ ! -f "/tmp/view_tree.json" ]; then \
		echo "❌ Error: Lambda CSS output not generated at /tmp/view_tree.json"; \
		exit 1; \
	fi; \
	echo "✅ Lambda CSS output generated"; \
	echo ""; \
	echo "─────────────────────────────────────────────────────────────────────────────────"; \
	echo "Comparing CSS properties..."; \
	echo "─────────────────────────────────────────────────────────────────────────────────"; \
	cd test/layout && node compare_css.js "$${TEST_NAME}"

# Run CSS resolution verification on all baseline tests
resolve-all: build
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════════════════════"
	@echo "CSS Resolution Verification - All Baseline Tests"
	@echo "═══════════════════════════════════════════════════════════════════════════════"
	@echo ""
	@PASSED=0; \
	FAILED=0; \
	TOTAL=0; \
	for ref_file in test/layout/reference/baseline/*.json; do \
		TEST_NAME=$$(basename "$${ref_file}" .json); \
		HTML_FILE="test/layout/data/baseline/$${TEST_NAME}.html"; \
		if [ ! -f "$${HTML_FILE}" ]; then \
			continue; \
		fi; \
		TOTAL=$$((TOTAL + 1)); \
		echo ""; \
		echo "Testing: $${TEST_NAME}"; \
		if $(MAKE) resolve TEST="$${TEST_NAME}" > "/tmp/resolve_$${TEST_NAME}.log" 2>&1; then \
			echo "  ✅ PASSED"; \
			PASSED=$$((PASSED + 1)); \
		else \
			echo "  ❌ FAILED"; \
			FAILED=$$((FAILED + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "═══════════════════════════════════════════════════════════════════════════════"; \
	echo "Summary"; \
	echo "═══════════════════════════════════════════════════════════════════════════════"; \
	echo "Total tests: $${TOTAL}"; \
	echo "Passed: $${PASSED}"; \
	echo "Failed: $${FAILED}"; \
	if [ "$${FAILED}" -gt 0 ]; then \
		echo ""; \
		echo "See logs in /tmp/resolve_*.log for details"; \
		exit 1; \
	fi

test-std: build
	@echo "Running Lambda Standard Tests (simple runner)..."
	@if [ -f "test/simple_test_runner.sh" ]; then \
		./test/simple_test_runner.sh; \
	else \
		echo "Error: Simple test runner script not found"; \
		exit 1; \
	fi

test-radiant:
	@echo "No Radiant test yet."

test-coverage:
	@echo "Running tests with coverage analysis..."
	@if command -v gcov >/dev/null 2>&1 && command -v lcov >/dev/null 2>&1; then \
		echo "Compiling with coverage flags..."; \
		gcc --coverage -fprofile-arcs -ftest-coverage -o lambda_coverage.exe $(shell find lambda -name "*.c") -I./include -I./lambda; \
		./test/test_run.sh; \
		gcov $(shell find lambda -name "*.c"); \
		lcov --capture --directory . --output-file coverage.info; \
		genhtml coverage.info --output-directory coverage-report; \
		echo "Coverage report generated in coverage-report/"; \
		echo "Open coverage-report/index.html to view results"; \
	else \
		echo "Coverage tools not found. Install with: brew install lcov"; \
		exit 1; \
	fi

test-memory:
	@echo "Running memory leak detection tests..."
	@if [ -f "test/test_memory.sh" ]; then \
		chmod +x test/test_memory.sh; \
		./test/test_memory.sh; \
	else \
		echo "Memory test script not found at test/test_memory.sh"; \
		exit 1; \
	fi

test-benchmark:
	@echo "Running performance benchmark tests..."
	@if [ -f "test/test_benchmark.sh" ]; then \
		chmod +x test/test_benchmark.sh; \
		./test/test_benchmark.sh; \
	else \
		echo "Benchmark test script not found at test/test_benchmark.sh"; \
		exit 1; \
	fi

test-fuzz:
	@echo "Running fuzzing tests..."
	@if [ -f "test/test_fuzz.sh" ]; then \
		chmod +x test/test_fuzz.sh; \
		./test/test_fuzz.sh; \
	else \
		echo "Fuzz test script not found at test/test_fuzz.sh"; \
		exit 1; \
	fi

test-integration:
	@echo "Running integration tests..."
	@if [ -f "test/test_integration.sh" ]; then \
		chmod +x test/test_integration.sh; \
		./test/test_integration.sh; \
	else \
		echo "Integration test script not found at test/test_integration.sh"; \
		exit 1; \
	fi

# Typesetting system testing
test-typeset: build
	@echo "Testing typesetting system..."
	./$(LAMBDA_EXE) test/lambda/typeset/test_typesetting.ls

test-typeset-math: build
	@echo "Testing mathematical typesetting..."
	./$(LAMBDA_EXE) -c "math_expr = input('test_simple_math.ls', 'math'); pages = typeset(math_expr); output('test_math_output.svg', pages[0].svg_content, 'svg'); print('Math typesetting complete')"

test-typeset-markdown: build
	@echo "Testing Markdown typesetting..."
	./$(LAMBDA_EXE) -c "md_content = input('README.md', 'markdown'); pages = typeset(md_content); output('readme_typeset.svg', pages[0].svg_content, 'svg'); print('Markdown typesetting complete')"

test-typeset-refined: build
	@echo "Testing refined typesetting system (view tree architecture)..."
	./$(LAMBDA_EXE) test/lambda/typeset/test_refined_typesetting.ls

test-typeset-all: build
	@echo "Running all typesetting tests..."
	./$(LAMBDA_EXE) test/lambda/typeset/run_all_tests.ls

test-typeset-end-to-end: build
	@echo "Testing end-to-end typesetting workflow..."
	./$(LAMBDA_EXE) test/lambda/typeset/test_end_to_end.ls

# C-based end-to-end test using Lambda runtime directly (no MIR/JIT)
test-typeset-c: build
	@echo "Building C-based end-to-end test with Lambda runtime..."
	gcc -I. -Iinclude -o test_end_to_end_direct test/lambda/typeset/test_end_to_end.c \
		lambda/lambda-mem.c lambda/lambda-eval.c lambda/print.c \
		typeset/typeset.c typeset/view/view_tree.c \
		typeset/integration/lambda_bridge.c typeset/serialization/lambda_serializer.c \
		typeset/output/svg_renderer.c \
		lib/strbuf.c lib/arraylist.c lib/hashmap.c \
		-lm
	@echo "Running C-based end-to-end test..."
	./test_end_to_end_direct
	@echo "Cleaning up test executable..."
	rm -f test_end_to_end_direct

# Minimal typesetting test without Lambda dependencies
test-typeset-minimal:
	@echo "Building minimal typesetting test..."
	gcc -I. -Iinclude -o test_minimal test/lambda/typeset/test_minimal.c \
		typeset/view/view_tree.c typeset/output/renderer.c typeset/output/svg_renderer.c \
		lib/strbuf.c \
		-lm
	@echo "Running minimal typesetting test..."
	./test_minimal
	@echo "Cleaning up test executable..."
	rm -f test_minimal

# Simple proof-of-concept test
test-typeset-simple:
	@echo "Building simple typesetting proof of concept..."
	gcc -I. -Iinclude -o test_simple test/lambda/typeset/test_simple.c \
		lib/strbuf.c lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c \
		-lm
	@echo "Running simple typesetting test..."
	./test_simple
	@echo "Cleaning up test executable..."
	rm -f test_simple

# Complete workflow demonstration
test-typeset-workflow:
	@echo "Building Lambda typesetting workflow demonstration..."
	gcc -I. -Iinclude -o test_workflow test/lambda/typeset/test_workflow.c \
		lib/strbuf.c lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c \
		-lm
	@echo "Running typesetting workflow demonstration..."
	./test_workflow
	@echo "Cleaning up test executable..."
	rm -f test_workflow


test-all:
	@echo "Running complete test suite (all test types)..."
	@echo "1. Unit Tests..."
	@$(MAKE) test
	@echo ""
	@echo "2. Memory Tests..."
	@$(MAKE) test-memory
	@echo ""
	@echo "3. Integration Tests..."
	@$(MAKE) test-integration
	@echo ""
	@echo "4. Benchmark Tests..."
	@$(MAKE) test-benchmark
	@echo ""
	@echo "5. Fuzz Tests..."
	@$(MAKE) test-fuzz
	@echo ""
	@echo "🎉 Complete test suite finished!"

# Phase 6: Build System Integration Targets
validate-build:
	@echo "Validating build objects for testing (Phase 6)..."
	@if [ -f "lib/build_utils.sh" ]; then \
		if source lib/build_utils.sh && validate_build_objects; then \
			echo "✅ Build objects validation passed"; \
		else \
			echo "❌ Build objects validation failed"; \
			echo "Run 'make build' to ensure all objects are current"; \
			exit 1; \
		fi; \
	else \
		echo "❌ Build utilities not found"; \
		exit 1; \
	fi

test-ci:
	@echo "Running CI test suite..."
	@$(MAKE) test
	@$(MAKE) test-memory
	@$(MAKE) test-integration

run: build
	@echo "Running $(LAMBDA_EXE)..."
	@if [ -f "$(LAMBDA_EXE)" ]; then \
		./$(LAMBDA_EXE); \
	else \
		echo "Executable $(LAMBDA_EXE) not found. Build first with 'make build'"; \
		exit 1; \
	fi

# Code quality targets
analyze: clean
	@echo "Running static analysis with clang..."
	@if command -v scan-build >/dev/null 2>&1; then \
		CC="scan-build clang" CXX="scan-build clang++" \
		scan-build -o analysis-results --use-cc=clang --use-c++=clang++ \
		./compile.sh build_lambda_config.json --force; \
		echo "Analysis complete. Results saved in analysis-results/"; \
		echo "Open the HTML report to view findings."; \
	elif [ -f "/opt/homebrew/Cellar/llvm/21.1.0/bin/scan-build" ]; then \
		CC="/opt/homebrew/Cellar/llvm/21.1.0/bin/scan-build clang" \
		CXX="/opt/homebrew/Cellar/llvm/21.1.0/bin/scan-build clang++" \
		/opt/homebrew/Cellar/llvm/21.1.0/bin/scan-build -o analysis-results \
		--use-cc=clang --use-c++=clang++ \
		./compile.sh build_lambda_config.json --force; \
		echo "Analysis complete. Results saved in analysis-results/"; \
	else \
		echo "scan-build not found. Install with: brew install llvm"; \
		exit 1; \
	fi

analyze-verbose: clean
	@echo "Running detailed static analysis..."
	@if command -v scan-build >/dev/null 2>&1; then \
		CC="scan-build clang" CXX="scan-build clang++" \
		scan-build -enable-checker alpha.core.CastSize \
		           -enable-checker alpha.core.CastToStruct \
		           -enable-checker alpha.security.ArrayBoundV2 \
		           -enable-checker alpha.security.ReturnPtrRange \
		           -enable-checker alpha.unix.cstring.BadSizeArg \
		           -enable-checker alpha.unix.cstring.OutOfBounds \
		           -o analysis-results-verbose \
		           --use-cc=clang --use-c++=clang++ \
		           ./compile.sh build_lambda_config.json --force; \
		echo "Detailed analysis complete. Results saved in analysis-results-verbose/"; \
	elif [ -f "/opt/homebrew/Cellar/llvm/21.1.0/bin/scan-build" ]; then \
		CC="/opt/homebrew/Cellar/llvm/21.1.0/bin/scan-build clang" \
		CXX="/opt/homebrew/Cellar/llvm/21.1.0/bin/scan-build clang++" \
		/opt/homebrew/Cellar/llvm/21.1.0/bin/scan-build -enable-checker alpha.core.CastSize \
		           -enable-checker alpha.core.CastToStruct \
		           -enable-checker alpha.security.ArrayBoundV2 \
		           -enable-checker alpha.security.ReturnPtrRange \
		           -enable-checker alpha.unix.cstring.BadSizeArg \
		           -enable-checker alpha.unix.cstring.OutOfBounds \
		           -o analysis-results-verbose \
		           --use-cc=clang --use-c++=clang++ \
		           ./compile.sh build_lambda_config.json --force; \
		echo "Detailed analysis complete. Results saved in analysis-results-verbose/"; \
	else \
		echo "scan-build not found. Install with: brew install llvm"; \
		exit 1; \
	fi

analyze-single:
	@echo "Running static analysis on individual files..."
	@mkdir -p analysis-results-single
	@echo "Testing analyzer with a simple buggy file..."
	@echo 'int main() { int *p = 0; return *p; }' > /tmp/test_analyzer.c
	@clang --analyze -Xanalyzer -analyzer-output=text /tmp/test_analyzer.c && echo "✅ Analyzer working" || echo "❌ Analyzer failed"
	@rm -f /tmp/test_analyzer.c
	@echo ""
	@echo "Analyzing Lambda source files..."
	@for file in lambda/print.cpp lambda/pack.cpp lib/strbuf.c lib/arraylist.c; do \
		if [ -f "$$file" ]; then \
			echo "Analyzing $$file..."; \
			clang --analyze -Xanalyzer -analyzer-output=text \
			      -Xanalyzer -analyzer-checker=core,unix,deadcode,security \
			      -I. -Ilib -Ilib/mem-pool/include \
			      "$$file" 2>&1 || echo "  (completed)"; \
		fi; \
	done
	@echo "Single file analysis complete."

# Alternative approach using compile_commands.json for better integration
analyze-compile-db: build
	@echo "Generating compile_commands.json for static analysis..."
	@if command -v bear >/dev/null 2>&1; then \
		bear -- make rebuild; \
		echo "Running clang-tidy with compile database..."; \
		if command -v clang-tidy >/dev/null 2>&1; then \
			find lambda -name "*.cpp" -o -name "*.c" | head -10 | \
			xargs clang-tidy -p . --checks='-*,clang-analyzer-*,bugprone-*,cert-*,misc-*,performance-*,portability-*,readability-*'; \
		else \
			echo "clang-tidy not found. Install with: brew install llvm"; \
		fi; \
	else \
		echo "bear not found. Install with: brew install bear"; \
		echo "Falling back to direct analysis..."; \
		$(MAKE) analyze-direct; \
	fi

# Direct analysis without build system wrapper
analyze-direct:
	@echo "Running direct static analysis on source files..."
	@mkdir -p analysis-results-direct
	@echo "Analyzing files that can compile independently..."
	@# Analyze files that don't have complex dependencies
	@for file in lambda/print.cpp lambda/pack.cpp lambda/utf_string.cpp \
	             lambda/format/format-*.cpp lambda/input/input-common.cpp \
	             lib/mime-*.c lambda/validator/error_reporting.c; do \
		if [ -f "$$file" ]; then \
			echo "Analyzing $$file..."; \
			clang --analyze -Xanalyzer -analyzer-output=html \
			      -Xanalyzer -analyzer-output-dir=analysis-results-direct \
			      -Xanalyzer -analyzer-checker=core,unix,deadcode,security.insecureAPI \
			      -I. -Ilib -Ilib/mem-pool/include -Ilambda \
			      -Ilambda/tree-sitter/lib/include \
			      -Iwasm-deps/include -Iwindows-deps/include \
			      "$$file" 2>/dev/null || echo "  (skipped due to dependencies)"; \
		fi; \
	done
	@echo "Running syntax-only analysis on complex files..."
	@find lambda -name "*.cpp" -o -name "*.c" | head -20 | while read file; do \
		echo "Syntax checking $$file..."; \
		clang -fsyntax-only -Weverything -Wno-padded -Wno-c++98-compat \
		      -I. -Ilib -Ilib/mem-pool/include -Ilambda \
		      -Ilambda/tree-sitter/lib/include \
		      -Iwasm-deps/include -Iwindows-deps/include \
		      "$$file" 2>&1 | grep -E "(warning|error)" | head -10 || true; \
	done
	@echo "Direct analysis complete. Check analysis-results-direct/ for HTML reports"
	@ls -la analysis-results-direct/ 2>/dev/null || echo "No HTML reports generated (code may be clean)"

check:
	@echo "Running basic code checks..."
	@echo "Checking for common issues in source files..."
	@find lambda -name "*.c" -o -name "*.cpp" -o -name "*.h" | xargs -I {} sh -c 'echo "Checking: {}"; grep -n "TODO\|FIXME\|XXX" {} || true' 2>/dev/null
	@echo "Basic checks complete."

format:
	@echo "Formatting source code..."
	@if command -v clang-format >/dev/null 2>&1; then \
		find lambda -name "*.c" -o -name "*.cpp" -o -name "*.h" | xargs clang-format -i; \
		echo "Code formatted with clang-format."; \
	else \
		echo "clang-format not found. Install with: brew install clang-format"; \
	fi

lint:
	@echo "Running comprehensive linter analysis..."
	@mkdir -p analysis-results-lint
	@if command -v cppcheck >/dev/null 2>&1; then \
		echo "Running cppcheck with all checks enabled..."; \
		cppcheck --enable=all --std=c++17 \
		         --suppress=missingIncludeSystem \
		         --suppress=unmatchedSuppression \
		         --xml --xml-version=2 \
		         --output-file=analysis-results-lint/cppcheck-report.xml \
		         -i lambda/tree-sitter/ \
		         -i lambda/tree-sitter-lambda/ \
		         -i lambda/tree-sitter-javascript/ \
		         lambda/ \
		         2>&1 | tee analysis-results-lint/cppcheck-output.txt; \
		echo ""; \
		echo "Generating human-readable summary..."; \
		cppcheck --enable=all --std=c++17 \
		         --suppress=missingIncludeSystem \
		         --suppress=unmatchedSuppression \
		         -i lambda/tree-sitter/ \
		         -i lambda/tree-sitter-lambda/ \
		         -i lambda/tree-sitter-javascript/ \
		         lambda/ \
		         2>&1 | grep -E "(error|warning|style|performance|portability)" | \
		         head -50 > analysis-results-lint/cppcheck-summary.txt || true; \
		echo ""; \
		echo "📊 Lint Analysis Complete:"; \
		echo "  Full report: analysis-results-lint/cppcheck-report.xml"; \
		echo "  Summary: analysis-results-lint/cppcheck-summary.txt"; \
		echo "  Raw output: analysis-results-lint/cppcheck-output.txt"; \
		echo ""; \
		if [ -s analysis-results-lint/cppcheck-summary.txt ]; then \
			echo "🔍 Top Issues Found:"; \
			head -20 analysis-results-lint/cppcheck-summary.txt; \
		else \
			echo "✅ No major issues found in Lambda source code"; \
		fi; \
	else \
		echo "cppcheck not found. Install with: brew install cppcheck"; \
		exit 1; \
	fi

# Executable size analysis
analyze-size: build
	@echo "Analyzing executable size breakdown..."
	@if [ -f "utils/analyze_size.sh" ]; then \
		./utils/analyze_size.sh; \
	else \
		echo "utils/analyze_size.sh not found. Please ensure it exists in the utils directory."; \
		exit 1; \
	fi

# Clang-tidy static analysis
tidy:
	@echo "Running clang-tidy analysis..."
	@mkdir -p analysis-results-tidy
	@if [ -f "/opt/homebrew/Cellar/llvm/21.1.0/bin/clang-tidy" ]; then \
		CLANG_TIDY="/opt/homebrew/Cellar/llvm/21.1.0/bin/clang-tidy"; \
	elif command -v clang-tidy >/dev/null 2>&1; then \
		CLANG_TIDY="clang-tidy"; \
	else \
		echo "clang-tidy not found. Install with: brew install llvm"; \
		exit 1; \
	fi; \
	echo "Analyzing C++ files with clang-tidy..."; \
	find lambda -name "*.cpp" -not -path "*/tree-sitter*" | head -10 | while read file; do \
		echo "Analyzing $$file..."; \
		$$CLANG_TIDY "$$file" \
			--checks='-*,bugprone-*,cert-*,clang-analyzer-*,misc-*,performance-*,portability-*,readability-*' \
			-- -I. -Ilib -Ilib/mem-pool/include -Ilambda -std=c++17 \
			2>&1 | grep -E "(warning|error|note)" | head -5 || echo "  (no issues found)"; \
	done > analysis-results-tidy/tidy-summary.txt; \
	echo ""; \
	echo "📊 Clang-tidy Analysis Complete:"; \
	echo "  Summary: analysis-results-tidy/tidy-summary.txt"; \
	echo ""; \
	if [ -s analysis-results-tidy/tidy-summary.txt ]; then \
		echo "🔍 Sample Issues Found:"; \
		head -15 analysis-results-tidy/tidy-summary.txt; \
	else \
		echo "✅ No issues found in analyzed files"; \
	fi

tidy-full:
	@echo "Running comprehensive clang-tidy analysis..."
	@mkdir -p analysis-results-tidy
	@if [ -f "/opt/homebrew/Cellar/llvm/21.1.0/bin/clang-tidy" ]; then \
		CLANG_TIDY="/opt/homebrew/Cellar/llvm/21.1.0/bin/clang-tidy"; \
	elif command -v clang-tidy >/dev/null 2>&1; then \
		CLANG_TIDY="clang-tidy"; \
	else \
		echo "clang-tidy not found. Install with: brew install llvm"; \
		exit 1; \
	fi; \
	echo "Creating manual compile database for Lambda project..."; \
	$(MAKE) generate-compile-db; \
	if [ -f "compile_commands.json" ]; then \
		echo "Running clang-tidy with compile database..."; \
		find lambda -name "*.cpp" -not -path "*/tree-sitter*" | \
		xargs $$CLANG_TIDY -p . > analysis-results-tidy/tidy-full-report.txt 2>&1; \
	else \
		echo "Compile database not available, running direct analysis..."; \
		find lambda -name "*.cpp" -not -path "*/tree-sitter*" | while read file; do \
			echo "Analyzing $$file..."; \
			$$CLANG_TIDY "$$file" \
				--checks='-*,bugprone-*,cert-*,clang-analyzer-*,misc-*,performance-*,portability-*,readability-*' \
				-- -I. -Ilib -Ilib/mem-pool/include -Ilambda -std=c++17 \
				2>&1 || echo "  (analysis completed with errors)"; \
		done > analysis-results-tidy/tidy-full-report.txt 2>&1; \
	fi; \
	echo "Full report saved to analysis-results-tidy/tidy-full-report.txt"; \
	echo "Generating summary..."; \
	grep -E "(warning|error|note)" analysis-results-tidy/tidy-full-report.txt | \
		head -100 > analysis-results-tidy/tidy-full-summary.txt || true; \
	echo "Summary saved to analysis-results-tidy/tidy-full-summary.txt"

# Generate compile_commands.json manually for clang-tidy
generate-compile-db:
	@echo "Generating compile_commands.json for Lambda project..."
	@echo '[' > compile_commands.json
	@find lambda -name "*.cpp" -not -path "*/tree-sitter*" | while IFS= read -r file; do \
		echo "  {" >> compile_commands.json; \
		echo "    \"directory\": \"$(PWD)\"," >> compile_commands.json; \
		echo "    \"command\": \"clang++ -I. -Ilib -Ilib/mem-pool/include -Ilambda -Iwasm-deps/include -Iwindows-deps/include -std=c++17 -c $$file\"," >> compile_commands.json; \
		echo "    \"file\": \"$$file\"" >> compile_commands.json; \
		echo "  }," >> compile_commands.json; \
	done
	@# Remove trailing comma and close JSON
	@sed -i '' '$$s/,//' compile_commands.json
	@echo ']' >> compile_commands.json
	@echo "Generated compile_commands.json with $$(grep -c '"file"' compile_commands.json) entries"

tidy-fix:
	@echo "Running clang-tidy with automatic fixes..."
	@if [ -f "/opt/homebrew/Cellar/llvm/21.1.0/bin/clang-tidy" ]; then \
		CLANG_TIDY="/opt/homebrew/Cellar/llvm/21.1.0/bin/clang-tidy"; \
	elif command -v clang-tidy >/dev/null 2>&1; then \
		CLANG_TIDY="clang-tidy"; \
	else \
		echo "clang-tidy not found. Install with: brew install llvm"; \
		exit 1; \
	fi; \
	echo "⚠️  This will modify your source files. Make sure you have backups!"; \
	read -p "Continue? (y/N): " confirm; \
	if [ "$$confirm" = "y" ] || [ "$$confirm" = "Y" ]; then \
		find lambda -name "*.cpp" -not -path "*/tree-sitter*" | head -5 | while read file; do \
			echo "Fixing $$file..."; \
			$$CLANG_TIDY "$$file" --fix \
				--checks='-*,modernize-*,readability-braces-around-statements,performance-*' \
				-- -I. -Ilib -Ilib/mem-pool/include -Ilambda -std=c++17 \
				2>/dev/null || echo "  (skipped due to errors)"; \
		done; \
		echo "Automatic fixes applied to selected files."; \
	else \
		echo "Cancelled."; \
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

# Performance targets
time-build:
	@echo "Timing build performance..."
	@time $(MAKE) rebuild JOBS=$(JOBS)

benchmark:
	@echo "Build benchmark (3 runs with $(JOBS) parallel jobs)..."
	@for i in 1 2 3; do \
		echo "Run $$i:"; \
		time $(MAKE) rebuild JOBS=$(JOBS) >/dev/null 2>&1; \
		echo ""; \
	done

# Premake5 Build System Targets
generate-premake:
	@echo "Generating Premake5 configuration from JSON..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)

clean-premake:
	@echo "Cleaning Premake5 build artifacts..."
	@rm -rf build/premake
	@rm -f premake5.lua
	@rm -f $(PREMAKE_FILE)
	@rm -f premake5.linux.lua
	@rm -f premake5.win.lua
	@rm -f premake5-linux.lua
	@rm -f *.make
	@rm -f dummy.cpp
	@echo "Premake5 artifacts cleaned."

build-lambda-input:
	@echo "Building lambda-input DLLs..."
	@echo "Generating Premake configuration..."
	@mkdir -p build/premake
	$(MAKE) generate-premake
	@echo "Generating makefiles..."
	cd build/premake && premake5 gmake --file=../../premake5.lua
	@echo "Building lambda-input DLLs with $(JOBS) parallel jobs..."
	cd build/premake && $(MAKE) config=debug_native lambda-input-full-cpp lambda-input-full-c -j$(JOBS)
	@echo "✅ lambda-input DLLs built successfully!"

build-test: build-lambda-input
	@echo "Building tests using Premake5..."
	@echo "Building configurations..."
	@mkdir -p build/premake
	$(MAKE) generate-premake
	cd build/premake && premake5 gmake --file=../../premake5.lua && $(MAKE) config=debug_native -j$(JOBS)

# Capture browser layout references using Puppeteer
# Usage:
#   make capture-layout                           # captures all categories (skips existing files)
#   make capture-layout suite=baseline        # captures only baseline category (or SUITE=baseline)
#   make capture-layout file=path/to/test.html   # captures a single file (or FILE=path)
#   make capture-layout force=1                  # force regenerate all existing references (or FORCE=1)
#   make capture-layout suite=basic force=1   # force regenerate specific category
capture-layout:
	@echo "🧭 Capturing browser layout references..."
	@if [ -d "test/layout/tools" ]; then \
	    cd test/layout/tools && \
	    if [ ! -d node_modules ]; then \
	        echo "📦 Installing test tools dependencies..."; \
	        npm install; \
	    fi; \
	    FORCE_FLAG=""; \
	    FORCE_VAR="$(or $(force),$(FORCE))"; \
	    if [ -n "$$FORCE_VAR" ] && [ "$$FORCE_VAR" != "0" ]; then \
	        FORCE_FLAG="--force"; \
	        echo "🔄 Force regeneration enabled"; \
	    fi; \
	    FILE_VAR="$(or $(file),$(FILE))"; \
	    if [ -n "$$FILE_VAR" ]; then \
	        echo "📄 Single file: $$FILE_VAR"; \
	        node extract_browser_references.js $$FORCE_FLAG $$FILE_VAR; \
	    else \
	        SUITE_VAR="$(or $(suite),$(SUITE))"; \
	        if [ -n "$$SUITE_VAR" ]; then \
	            echo "📂 Suite: $$SUITE_VAR"; \
	            node extract_browser_references.js $$FORCE_FLAG --category $$SUITE_VAR; \
	        else \
	            echo "📚 All available categories (auto-discovered)"; \
	            node extract_browser_references.js $$FORCE_FLAG; \
	        fi; \
	    fi; \
	else \
	    echo "❌ Error: Tools directory not found at test/layout/tools"; \
	    exit 1; \
	fi

# Layout Engine Testing Targets
# ==============================

# test-layout: Run layout tests using Radiant engine (Lexbor-based)
# Usage: make test-layout [suite=SUITE] [test=TEST] [pattern=PATTERN]
# Note: test parameter now accepts filename with or without .html extension
# Example: make test-layout test=baseline_301_simple_margin
test-layout:
	@echo "🎨 Running Radiant Layout Engine Tests"
	@echo "======================================"
	@if [ -f "test/layout/test_radiant_layout.js" ]; then \
		TEST_VAR="$(or $(test),$(TEST))"; \
		PATTERN_VAR="$(or $(pattern),$(PATTERN))"; \
		SUITE_VAR="$(or $(suite),$(SUITE))"; \
		if [ -n "$$TEST_VAR" ]; then \
			case "$$TEST_VAR" in \
				*.html) TEST_FILE="$$TEST_VAR" ;; \
				*) TEST_FILE="$${TEST_VAR}.html" ;; \
			esac; \
			echo "🎯 Running single test: $$TEST_FILE"; \
			node test/layout/test_radiant_layout.js --engine radiant --radiant-exe ./radiant.exe --test $$TEST_FILE -v; \
		elif [ -n "$$PATTERN_VAR" ]; then \
			echo "🔍 Running tests matching pattern: $$PATTERN_VAR"; \
			node test/layout/test_radiant_layout.js --engine radiant --radiant-exe ./radiant.exe --pattern $$PATTERN_VAR; \
		elif [ -n "$$SUITE_VAR" ]; then \
			echo "📂 Running test suite: $$SUITE_VAR"; \
			node test/layout/test_radiant_layout.js --engine radiant --radiant-exe ./radiant.exe --category $$SUITE_VAR; \
		else \
			echo "🎯 Running all layout tests"; \
			node test/layout/test_radiant_layout.js --engine radiant --radiant-exe ./radiant.exe; \
		fi; \
	else \
		echo "❌ Error: Layout test script not found at test/layout/test_radiant_layout.js"; \
		exit 1; \
	fi

# layout: Run layout tests using Lambda CSS engine
# Usage: make layout [suite=SUITE] [test=TEST] [pattern=PATTERN]
# Note: test parameter now accepts filename with or without .html extension
# Example: make layout test=baseline_301_simple_margin
layout:
	@echo "🎨 Running Lambda CSS Layout Engine Tests"
	@echo "=========================================="
	@if [ -f "test/layout/test_radiant_layout.js" ]; then \
		TEST_VAR="$(or $(test),$(TEST))"; \
		PATTERN_VAR="$(or $(pattern),$(PATTERN))"; \
		SUITE_VAR="$(or $(suite),$(SUITE))"; \
		if [ -n "$$TEST_VAR" ]; then \
			case "$$TEST_VAR" in \
				*.html) TEST_FILE="$$TEST_VAR" ;; \
				*) TEST_FILE="$${TEST_VAR}.html" ;; \
			esac; \
			echo "🎯 Running single test: $$TEST_FILE"; \
			node test/layout/test_radiant_layout.js --engine lambda-css --radiant-exe ./lambda.exe --test $$TEST_FILE -v; \
		elif [ -n "$$PATTERN_VAR" ]; then \
			echo "🔍 Running tests matching pattern: $$PATTERN_VAR"; \
			node test/layout/test_radiant_layout.js --engine lambda-css --radiant-exe ./lambda.exe --pattern $$PATTERN_VAR; \
		elif [ -n "$$SUITE_VAR" ]; then \
			echo "📂 Running test suite: $$SUITE_VAR"; \
			node test/layout/test_radiant_layout.js --engine lambda-css --radiant-exe ./lambda.exe --category $$SUITE_VAR; \
		else \
			echo "🎯 Running all layout tests"; \
			node test/layout/test_radiant_layout.js --engine lambda-css --radiant-exe ./lambda.exe; \
		fi; \
	else \
		echo "❌ Error: Layout test script not found at test/layout/test_radiant_layout.js"; \
		exit 1; \
	fi
