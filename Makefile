# Lambda Project Makefile
# Utilizes Premake5 build system with standard make targets

# Project configuration
PROJECT_NAME = Lambda
DEFAULT_CONFIG = build_lambda_config.json

# Build configurations
BUILD_DIR = build
BUILD_DEBUG_DIR = build_debug
BUILD_RELEASE_DIR = build_release
BUILD_WINDOWS_DIR = build_windows
BUILD_LINUX_DIR = build_linux

# Output executables
LAMBDA_EXE = lambda.exe
LAMBDA_CLI_EXE = lambda-cli.exe

# Unicode support is always enabled (utf8proc-based)
# No longer using conditional compilation flags

# Auto-detect number of jobs for parallel compilation
NPROCS := 1
OS := $(shell uname -s)
ifeq ($(OS),Darwin)
	NPROCS := $(shell sysctl -n hw.ncpu)
	PREMAKE_FILE := premake5.mac.lua
	PREMAKE_CLI_FILE := premake5.cli.mac.lua
else ifeq ($(OS),Linux)
	NPROCS := $(shell nproc)
	PREMAKE_FILE := premake5.lin.lua
	PREMAKE_CLI_FILE := premake5.cli.lin.lua
else
	# Windows/MSYS2 detection
	NPROCS := $(shell nproc 2>/dev/null || echo 4)
	PREMAKE_FILE := premake5.win.lua
	PREMAKE_CLI_FILE := premake5.cli.win.lua
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

# MSYS2/Windows Environment Detection
MSYSTEM_DETECTED := $(shell echo $$MSYSTEM)
IS_MSYS2 := $(shell [ -n "$(MSYSTEM_DETECTED)" ] && echo "yes" || echo "no")

# Detect C/C++ compilers
# All platforms use Clang by default
# On MSYS2/Windows: CLANG64 Clang (avoids Universal CRT, uses MSVCRT.dll)
# Force explicit paths for MSYS environment compatibility
ifeq ($(shell test -f /clang64/bin/clang && echo yes),yes)
	CC := /clang64/bin/clang
	CXX := /clang64/bin/clang++
	AR := /clang64/bin/ar
	RANLIB := /clang64/bin/ranlib
else ifeq ($(OS),Linux)
	CC := clang
	CXX := clang++
	AR := ar
	RANLIB := ranlib
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
# Build from source on all platforms
TREE_SITTER_LIB = lambda/tree-sitter/libtree-sitter.a
TREE_SITTER_LAMBDA_LIB = lambda/tree-sitter-lambda/libtree-sitter-lambda.a
TREE_SITTER_JAVASCRIPT_LIB = lambda/tree-sitter-javascript/libtree-sitter-javascript.a
TREE_SITTER_LATEX_LIB = lambda/tree-sitter-latex/libtree-sitter-latex.a
TREE_SITTER_LATEX_MATH_LIB = lambda/tree-sitter-latex-math/libtree-sitter-latex-math.a
RE2_LIB = build_temp/re2-noabsl/build/libre2.a

# LaTeX grammar dependencies
LATEX_GRAMMAR_JS = lambda/tree-sitter-latex/grammar.js
LATEX_PARSER_C = lambda/tree-sitter-latex/src/parser.c
LATEX_GRAMMAR_JSON = lambda/tree-sitter-latex/src/grammar.json
LATEX_NODE_TYPES_JSON = lambda/tree-sitter-latex/src/node-types.json

# LaTeX Math grammar dependencies
LATEX_MATH_GRAMMAR_JS = lambda/tree-sitter-latex-math/grammar.js
LATEX_MATH_PARSER_C = lambda/tree-sitter-latex-math/src/parser.c

# Build tree-sitter library (amalgamated build, no ICU dependency)
# Uses lib.c single-file approach - no external ICU/Unicode library needed
$(TREE_SITTER_LIB):
	@echo "Building tree-sitter library (amalgamated, no ICU)..."
	@cd lambda/tree-sitter && \
	rm -f libtree-sitter.a tree_sitter.o && \
	$(CC) -c lib/src/lib.c \
		-Ilib/include \
		-O3 -Wall -Wextra -std=c11 -fPIC \
		-D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE \
		-o tree_sitter.o && \
	$(AR) rcs libtree-sitter.a tree_sitter.o && \
	rm -f tree_sitter.o
	@echo "✅ tree-sitter library built: lambda/tree-sitter/libtree-sitter.a"

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

# Generate LaTeX parser from grammar.js when it changes
$(LATEX_PARSER_C) $(LATEX_GRAMMAR_JSON) $(LATEX_NODE_TYPES_JSON): $(LATEX_GRAMMAR_JS)
	@echo "Generating LaTeX parser from grammar.js..."
	@echo "🔧 Working directory: lambda/tree-sitter-latex"
	@if command -v tree-sitter >/dev/null 2>&1; then \
		echo "Using local tree-sitter CLI"; \
		cd lambda/tree-sitter-latex && tree-sitter generate; \
	elif command -v npx >/dev/null 2>&1; then \
		echo "Using npx tree-sitter-cli"; \
		cd lambda/tree-sitter-latex && npx tree-sitter-cli@0.24.7 generate; \
	else \
		echo "❌ Error: tree-sitter CLI not found!"; \
		echo "Install with: npm install -g tree-sitter-cli"; \
		exit 1; \
	fi
	@echo "✅ LaTeX parser generated successfully"

# Build tree-sitter-latex library (depends on parser generation)
$(TREE_SITTER_LATEX_LIB): $(LATEX_PARSER_C)
	@echo "Building tree-sitter-latex library..."
	@echo "🔧 Compiler: $(CC)"
	@echo "🔧 CXX: $(CXX)"
	@echo "🔧 Environment: MSYSTEM=$(MSYSTEM)"
	@echo "🔧 Working directory: lambda/tree-sitter-latex"
	@echo "🔧 Unsetting OS variable to bypass Windows check..."
	@echo "🔧 Adding /mingw64/bin to PATH for DLL dependencies..."
	env -u OS PATH="/mingw64/bin:$$PATH" $(MAKE) -C lambda/tree-sitter-latex libtree-sitter-latex.a CC="$(CC)" CXX="$(CXX)" V=1 VERBOSE=1

# Generate LaTeX Math parser from grammar.js when it changes
$(LATEX_MATH_PARSER_C): $(LATEX_MATH_GRAMMAR_JS)
	@echo "Generating LaTeX Math parser from grammar.js..."
	@echo "🔧 Working directory: lambda/tree-sitter-latex-math"
	@if command -v tree-sitter >/dev/null 2>&1; then \
		echo "Using local tree-sitter CLI"; \
		cd lambda/tree-sitter-latex-math && tree-sitter generate; \
	elif command -v npx >/dev/null 2>&1; then \
		echo "Using npx tree-sitter-cli"; \
		cd lambda/tree-sitter-latex-math && npx tree-sitter-cli@0.24.7 generate; \
	else \
		echo "❌ Error: tree-sitter CLI not found!"; \
		echo "Install with: npm install -g tree-sitter-cli"; \
		exit 1; \
	fi
	@echo "✅ LaTeX Math parser generated successfully"

# Build tree-sitter-latex-math library (depends on parser generation)
$(TREE_SITTER_LATEX_MATH_LIB): $(LATEX_MATH_PARSER_C)
	@echo "Building tree-sitter-latex-math library..."
	@echo "🔧 Working directory: lambda/tree-sitter-latex-math"
	env -u OS PATH="/mingw64/bin:$$PATH" $(MAKE) -C lambda/tree-sitter-latex-math libtree-sitter-latex-math.a CC="$(CC)" CXX="$(CXX)" V=1 VERBOSE=1

# Build re2 library (reconfigures cmake if CMakeCache is stale/wrong platform)
$(RE2_LIB):
	@echo "Building re2 library from source..."
	@mkdir -p build_temp/re2-noabsl/build
	@cd build_temp/re2-noabsl/build && \
		cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DRE2_BUILD_TESTING=OFF -Wno-dev 2>&1 | tail -3 && \
		cmake --build . --target re2 -- -j$(JOBS)
	@echo "re2 library built: $(RE2_LIB)"

# Toolchain Validation Functions
define toolchain_verify
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

# Checking DLL dependencies on Windows (avoid Universal CRT)
define windows_dll_check
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
tree-sitter-libs: $(TREE_SITTER_LIB) $(TREE_SITTER_LAMBDA_LIB) $(TREE_SITTER_JAVASCRIPT_LIB) $(TREE_SITTER_LATEX_LIB) $(TREE_SITTER_LATEX_MATH_LIB)

# Default target
.DEFAULT_GOAL := build

# Phony targets (don't correspond to actual files)
.PHONY: all build build-ascii clean clean-grammar generate-grammar debug release rebuild test test-all test-all-baseline test-lambda-baseline test-input-baseline test-radiant-baseline test-layout-baseline test-tex test-tex-baseline test-tex-dvi test-tex-dvi-baseline test-tex-dvi-extended test-tex-reference test-extended test-input run help install uninstall \
	    lambda lambda-cli build-cli format lint check docs intellisense analyze-binary \
	    build-debug build-release clean-all distclean \
	    tree-sitter-libs \
	    verify-windows verify-linux \
	    generate-premake clean-premake build-test build-test-linux \
	    capture-layout test-layout layout count-loc tidy-printf benchmark bench-compile \
	    test-pdf test-pdf-export setup-pdf-tests \
	    test-fuzzy test-fuzzy-extended test-c2mir type-chart

# Help target - shows available commands
help:
	@echo "$(PROJECT_NAME) - Available Make Targets:"
	@echo ""
	@echo "Build Targets (Premake-based):"
	@echo "  build         - Build lambda project using Premake build system (incremental, default)"
	@echo "                  On Windows/MSYS2: Uses CLANG64 Clang (avoids Universal CRT)"
	@echo "                  All platforms use Clang as the default compiler"
	@echo "  debug         - Build with debug symbols and AddressSanitizer using Premake"
	@echo "  release       - Build optimized release version using Premake"
	@echo "  lambda-cli    - Build headless CLI-only version (release, no Radiant/GUI, outputs lambda-cli.exe)"
	@echo "  rebuild       - Force complete rebuild using Premake"
	@echo "  lambda        - Build lambda project specifically using Premake"
	@echo "  all           - Build all projects"
	@echo ""
	@echo "Maintenance:"
	@echo "  clean         - Remove build artifacts"
	@echo "  clean-test    - Remove test output and temporary files"
	@echo "  clean-grammar - Remove generated grammar and embed files (parser.c, ts-enum.h, lambda-embed.h)"
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
	@echo "  tree-sitter-libs - Build all tree-sitter libraries (amalgamated, no ICU)"
	@echo "                     Automatically regenerates LaTeX parser if grammar.js changes"
	@echo ""
	@echo "Development:"
	@echo "  test          - Run ALL test suites (baseline + extended, alias for test-all)"
	@echo "  test-all      - Run ALL test suites (baseline + extended)"
	@echo "  test-all-baseline - Run ALL BASELINE test suites (core functionality, must pass 100%)"
	@echo "  test-lambda-baseline - Run LAMBDA baseline test suite only"
	@echo "  test-input-baseline - Run HTML5 WPT, CommonMark, YAML, ASCII Math, and LaTeX Math parser tests"
	@echo "  test-radiant-baseline - Run RADIANT layout baseline test suite only (alias for test-layout-baseline)"
	@echo "  test-tex      - Run all TeX typesetting unit tests"
	@echo "  test-tex-baseline - Run TeX baseline tests (core box/AST tests)"
	@echo "  test-tex-dvi  - Run TeX DVI comparison tests against reference (all tests)"
	@echo "  test-tex-dvi-baseline - Run TeX DVI comparison BASELINE tests (stable, must pass)"
	@echo "  test-tex-dvi-extended - Run TeX DVI comparison EXTENDED tests (work-in-progress)"
	@echo "  test-tex-reference - Generate reference DVI files from test/*.tex"
	@echo "  test-pdf      - Run PDF rendering test suite (compare vs pdf.js)"
	@echo "  test-pdf-export - Export pdf.js operator lists as JSON references"
	@echo "  setup-pdf-tests - Set up PDF test fixtures and dependencies"
	@echo "  test-extended - Run EXTENDED test suites only (HTTP/HTTPS, ongoing features)"
	@echo "  test-library  - Run library tests only"
	@echo "  test-input    - Run input processing test suite (MIME detection & math)"
	@echo "  test-validator- Run validator tests only"
	@echo "  test-mir      - Run MIR JIT tests only"
	@echo "  test-c2mir    - Run Lambda baseline tests with legacy C2MIR JIT path"
	@echo "  test-lambda   - Run lambda runtime tests only"
	@echo "  test-std      - Run Lambda Standard Tests (custom test runner)"
	@echo "  test-coverage - Run tests with code coverage analysis"
	@echo "  test-benchmark- Run performance benchmark tests"
	@echo "  test-fuzzy    - Run fuzzy tests (5 minutes, mutation + random generation)"
	@echo "  test-fuzzy-extended - Run extended fuzzy tests (1 hour)"
	@echo "  test-integration - Run end-to-end integration tests"
	@echo "  test-all      - Run complete test suite (all test types)"

	@echo "  run           - Build and run the default executable"
	@echo "  analyze       - Run static analysis with scan-build (fixed for custom build)"
	@echo "  analyze-verbose - Run detailed static analysis with extra checkers"
	@echo "  analyze-single - Run static analysis on individual files"
	@echo "  analyze-direct - Direct clang static analysis (bypasses build system)"
	@echo "  analyze-compile-db - Use compile_commands.json for analysis (requires bear)"
	@echo "  tidy          - Run clang-tidy analysis on C++ files"
	@echo "  tidy-full     - Comprehensive clang-tidy with compile database"
	@echo "  tidy-fix      - Run clang-tidy with automatic fixes (interactive)"
	@echo "  tidy-printf   - Convert printf/fprintf(stderr) to log_debug() using Clang AST"
	@echo "                  Usage: make tidy-printf FILE='pattern' [DRY_RUN=1] [BACKUP=1]"
	@echo "  check         - Run basic code checks (TODO/FIXME finder)"
	@echo "  format        - Format source code with clang-format"
	@echo "  lint          - Run linter (cppcheck) on source files"
	@echo "  count-loc     - Count lines of code in the repository"
	@echo "  cheatsheet    - Regenerate Lambda_Cheatsheet.pdf from Markdown (requires pandoc, xelatex)"
	@echo "  bench-compile - Run C/C++ compilation performance benchmark"
	@echo "                  Tests single-file, template, multi-file, and full Lambda builds"
	@echo "  test-layout              - Run Lambda CSS layout integration tests (all suites)"
	@echo "                             Uses Lambda CSS engine (custom CSS cascade and layout)"
	@echo "                             Usage: make test-layout suite=baseline (run specific suite)"
	@echo "                             Usage: make test-layout test=table_simple (run specific test, .html/.htm optional)"
	@echo "                             Usage: make test-layout pattern=float (run tests matching pattern)"
	@echo "                             Note: Uppercase variants also work (SUITE=, TEST=, PATTERN=)"
	@echo "                             Available suites: auto-detected from test/layout/data/"
	@echo "  layout                   - Alias for test-layout"
	@echo "                             Usage: make layout suite=baseline"
	@echo "                             Usage: make layout pattern=float (run tests matching pattern)"
	@echo "                             Note: Uppercase variants also work (SUITE=, TEST=, PATTERN=)"
	@echo "                             Available suites: auto-detected from test/layout/data/"
	@echo "  capture-layout   - Extract browser layout references using Puppeteer"
	@echo "                             REQUIRES: test=<name> OR suite=<name>"
	@echo "                             Usage: make capture-layout test=basic-text-align"
	@echo "                             Usage: make capture-layout suite=baseline"
	@echo "                             Usage: make capture-layout test=table_007 force=1"
	@echo "                             Note: Uppercase variants also work (TEST=, SUITE=, FORCE=)"
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

# Main build target (incremental)
build: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-libs $(RE2_LIB)
	@rm -f .lambda_release_build 2>/dev/null || true
ifeq ($(IS_MSYS2),yes)
	@echo "Building $(PROJECT_NAME) using MSYS2 CLANG64 environment..."
	PATH="/clang64/bin:$$PATH" $(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	@echo "Generating makefiles..."
	PATH="/clang64/bin:$$PATH" $(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building lambda executable with $(JOBS) parallel jobs..."
	PATH="/clang64/bin:$$PATH" $(MAKE) -C build/premake -j$(JOBS) lambda CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)" --no-print-directory -s CFLAGS="-w" CXXFLAGS="-w"
	$(call windows_dll_check)
	@echo "✅ Build completed successfully!"
else
	@echo "Building $(PROJECT_NAME) using Premake build system..."
	$(call toolchain_verify)
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




# Debug build
debug: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-libs $(RE2_LIB)
	@rm -f .lambda_release_build 2>/dev/null || true
	@echo "Building debug version using Premake build system..."
	$(call toolchain_verify)
	@echo "Generating Premake configuration..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	@echo "Generating makefiles..."
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building lambda executable (debug) with $(JOBS) parallel jobs..."
	$(MAKE) -C build/premake config=debug_native lambda -j$(JOBS) CC="$(CC)" CXX="$(CXX)"
	@echo "Debug build completed. Executable: lambda.exe"
	$(call windows_dll_check)

# Release build (optimized with size reduction)
# Optimizations applied:
#   1. Dead code elimination (-ffunction-sections, -fdata-sections, -Wl,-dead_strip)
#   2. log_debug() and log_info() stripped via NDEBUG macro
#   3. Symbol visibility control (-fvisibility=hidden)
#   4. Debug symbols stripped (-s linker flag + post-build strip)
#   5. LTO enabled (-flto)
release: build-release

build-release:
	@$(MAKE) clean-all
	@$(MAKE) build-release-compile

build-release-compile: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-libs $(RE2_LIB)
	@echo "Building release version using Premake build system..."
	@echo "Optimizations: LTO, dead code elimination, symbol visibility, stripped logging"
	$(call toolchain_verify)
	@echo "Generating Premake configuration..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	@echo "Generating makefiles..."
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building lambda executable (release) with $(JOBS) parallel jobs..."
	$(MAKE) -C build/premake config=release_native lambda -j$(JOBS) CC="$(CC)" CXX="$(CXX)"
ifeq ($(OS),Darwin)
	@echo "Stripping debug symbols (macOS)..."
	@strip -x lambda_release.exe 2>/dev/null || strip -x lambda.exe 2>/dev/null || true
else
	@echo "Stripping debug symbols..."
	@strip lambda_release.exe 2>/dev/null || strip lambda.exe 2>/dev/null || true
endif
	@echo "Release build completed."
	@ls -lh lambda_release.exe 2>/dev/null || ls -lh lambda.exe 2>/dev/null || true
	@touch .lambda_release_build
	$(call windows_dll_check)

# Headless CLI build (no Radiant layout engine or GUI support)
# Produces lambda-cli.exe with only Lambda scripting capabilities (release build)
lambda-cli: build-cli

build-cli: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-libs
	@echo "Building Lambda CLI (headless, release) using Premake build system..."
	@echo "Excluded: Radiant layout engine, GUI windowing, font rendering, image codecs"
	$(PYTHON) utils/generate_premake.py --variant cli --output $(PREMAKE_CLI_FILE)
	@echo "Generating makefiles..."
	$(PREMAKE5) gmake --file=$(PREMAKE_CLI_FILE)
	@echo "Building lambda-cli executable (release) with $(JOBS) parallel jobs..."
	$(MAKE) -C build/premake config=release_native lambda-cli -j$(JOBS) CC="$(CC)" CXX="$(CXX)" --no-print-directory -s CFLAGS="-w" CXXFLAGS="-w"
ifeq ($(OS),Darwin)
	@strip -x lambda-cli.exe 2>/dev/null || true
else
	@strip lambda-cli.exe 2>/dev/null || true
endif
	@echo "✅ CLI build completed. Executable: lambda-cli.exe"
	@ls -lh lambda-cli.exe 2>/dev/null || true

# Force rebuild (clean + build)
rebuild: clean-all
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

# Build all projects
all: lambda
	@echo "All projects built successfully."

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
	@rm -f lambda_debug.exe
	@rm -f lambda_release.exe
	@rm -f lambda-windows.exe
	@rm -f lambda-linux.exe
	@rm -f .lambda_release_build
	@rm -f .lambda_release_backup.exe
	@rm -f .lambda_build_backup.exe
	@rm -f temp/_transpiled*.c
	@echo "Build artifacts and executables cleaned."

clean-test:
	@echo "Cleaning test build outputs..."
	@rm -rf test_output/ 2>/dev/null || true
	@find test/ -name "*.exe" -type f -delete 2>/dev/null || true
	@rm -f test_*.exe 2>/dev/null || true
	@rm -f *.exe.tmp 2>/dev/null || true
	@rm -f build_test_*.json 2>/dev/null || true
	@rm -f build_test_*.json.tmp 2>/dev/null || true
	@find test/ -name "*.dSYM" -type d -exec rm -rf {} + 2>/dev/null || true
	@find test/ -name "*.o" -type f -delete 2>/dev/null || true
	@echo "Test build outputs cleaned."

clean-grammar:
	@echo "Cleaning generated grammar and embed files..."
	@rm -f $(TS_ENUM_H)
	@rm -f $(PARSER_C)
	@rm -f $(GRAMMAR_JSON)
	@rm -f $(NODE_TYPES_JSON)
	@rm -f $(LAMBDA_EMBED_H_FILE)
	@echo "Generated grammar and embed files cleaned."

# IntelliSense support
intellisense:
	@echo "Updating IntelliSense database..."
	@./utils/update_intellisense.sh

# Generate type hierarchy chart
type-chart:
	dot -Tsvg doc/type_hierarchy.dot -o doc/type_hierarchy.svg
	@echo "Type hierarchy chart generated: doc/type_hierarchy.svg"

# Generate grammar explicitly (useful for development)
generate-grammar: $(TS_ENUM_H)
	@echo "Grammar generation complete."

clean-all: clean-premake clean-test
	@echo "Removing all build directories..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(BUILD_DEBUG_DIR)
	@rm -rf $(BUILD_RELEASE_DIR)
	@rm -rf $(BUILD_WINDOWS_DIR)
	@rm -rf $(BUILD_LINUX_DIR)
	@echo "Cleaning tree-sitter libraries..."
	@rm -f lambda/tree-sitter/libtree-sitter.a lambda/tree-sitter/tree_sitter.o
	@rm -f lambda/tree-sitter-lambda/libtree-sitter-lambda.a lambda/tree-sitter-lambda/src/*.o
	@rm -f lambda/tree-sitter-javascript/libtree-sitter-javascript.a lambda/tree-sitter-javascript/src/*.o
	@rm -f lambda/tree-sitter-latex/libtree-sitter-latex.a lambda/tree-sitter-latex/src/*.o
	@rm -f lambda/tree-sitter-latex-math/libtree-sitter-latex-math.a lambda/tree-sitter-latex-math/src/*.o
	@rm -f $(RE2_LIB)
	@echo "All build directories and tree-sitter libraries cleaned."

distclean: clean-all clean-grammar clean-test
	@echo "Complete cleanup..."
	@rm -f $(LAMBDA_EXE)
	@rm -f lambda_debug.exe
	@rm -f lambda_release.exe
	@rm -f lambda-windows.exe
	@rm -f lambda-linux.exe
	@rm -f temp/_transpiled*.c
	@rm -f *.exe
	@echo "Complete cleanup finished."

# Development targets
test: test-all

test-all: build-test
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@echo "Running ALL test suites (baseline + extended)..."
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --parallel; \
	else \
		echo "Error: No test suite found"; \
		exit 1; \
	fi

test-all-baseline: build-test
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@echo "Running BASELINE test suites only..."
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --category=baseline --parallel; \
	else \
		echo "Error: No test suite found"; \
		exit 1; \
	fi

test-lambda-baseline: build-test
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@echo "Running LAMBDA baseline test suite..."
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --target=lambda --category=baseline --parallel; \
	else \
		echo "Error: No test suite found"; \
		exit 1; \
	fi

test-c2mir: build-test
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@echo "Running LAMBDA baseline tests with C2MIR (legacy JIT path)..."
	@if [ -f "test/test_run.sh" ]; then \
		LAMBDA_USE_C2MIR=1 ./test/test_run.sh --target=lambda --category=baseline --parallel; \
	else \
		echo "Error: No test suite found"; \
		exit 1; \
	fi

test-input-baseline: build-test
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@echo "=============================================================="
	@echo "🧪 Running INPUT baseline tests"
	@echo "=============================================================="
	@total_passed=0; \
	total_failed=0; \
	total_skipped=0; \
	wpt_passed=0; wpt_failed=0; wpt_skipped=0; wpt_status="⏭️  SKIP"; \
	md_passed=0; md_failed=0; md_skipped=0; md_status="⏭️  SKIP"; \
	yaml_passed=0; yaml_failed=0; yaml_skipped=0; yaml_status="⏭️  SKIP"; \
	math_passed=0; math_failed=0; math_skipped=0; math_status="⏭️  SKIP"; \
	latex_math_passed=0; latex_math_failed=0; latex_math_skipped=0; latex_math_status="⏭️  SKIP"; \
	\
	echo ""; \
	echo "📦 HTML5 WPT Parser Tests:"; \
	if [ -f "test/test_wpt_html_parser_gtest.exe" ]; then \
		output=$$(./test/test_wpt_html_parser_gtest.exe 2>&1) || true; \
		echo "$$output" | tail -20; \
		wpt_passed=$$(echo "$$output" | grep -E "^\[  PASSED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		wpt_failed=$$(echo "$$output" | grep -E "^\[  FAILED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		wpt_skipped=$$(echo "$$output" | grep -E "^\[  SKIPPED \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		wpt_passed=$${wpt_passed:-0}; wpt_failed=$${wpt_failed:-0}; wpt_skipped=$${wpt_skipped:-0}; \
		if [ "$$wpt_failed" = "0" ] || [ -z "$$wpt_failed" ]; then wpt_status="✅ PASS"; wpt_failed=0; else wpt_status="❌ FAIL"; fi; \
	else \
		echo "   ⚠️  test/test_wpt_html_parser_gtest.exe not found"; \
	fi; \
	\
	echo ""; \
	echo "📦 CommonMark Markdown Tests:"; \
	if [ -f "test/test_markdown_gtest.exe" ]; then \
		output=$$(./test/test_markdown_gtest.exe 2>&1) || true; \
		echo "$$output" | tail -20; \
		md_passed=$$(echo "$$output" | grep -E "^\[  PASSED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		md_failed=$$(echo "$$output" | grep -E "^\[  FAILED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		md_skipped=$$(echo "$$output" | grep -E "^\[  SKIPPED \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		md_passed=$${md_passed:-0}; md_failed=$${md_failed:-0}; md_skipped=$${md_skipped:-0}; \
		if [ "$$md_failed" = "0" ] || [ -z "$$md_failed" ]; then md_status="✅ PASS"; md_failed=0; else md_status="❌ FAIL"; fi; \
	else \
		echo "   ⚠️  test/test_markdown_gtest.exe not found"; \
	fi; \
	\
	echo ""; \
	echo "📦 YAML Suite Tests:"; \
	if [ -f "test/test_yaml_suite_gtest.exe" ]; then \
		output=$$(./test/test_yaml_suite_gtest.exe 2>&1) || true; \
		echo "$$output" | grep -E "^===|^Total|^Pass rate" | head -10; \
		yaml_total=$$(echo "$$output" | grep "Total test cases:" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		yaml_json_passed=$$(echo "$$output" | grep "JSON Comparison Results" -A1 | grep "Total:" | grep -oE "Passed: [0-9]+" | grep -oE "[0-9]+" || echo "0"); \
		yaml_json_failed=$$(echo "$$output" | grep "JSON Comparison Results" -A1 | grep "Total:" | grep -oE "Failed: [0-9]+" | grep -oE "[0-9]+" || echo "0"); \
		yaml_error_passed=$$(echo "$$output" | grep "Error Test Results" -A1 | grep "Total:" | grep -oE "Passed[^:]*: [0-9]+" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		yaml_parse_passed=$$(echo "$$output" | grep "Parse-Only Test Results" -A1 | grep "Total:" | grep -oE "Passed[^:]*: [0-9]+" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		yaml_passed=$$((yaml_json_passed + yaml_error_passed + yaml_parse_passed)); \
		yaml_passed=$${yaml_passed:-0}; yaml_json_failed=$${yaml_json_failed:-0}; \
		yaml_failed=$${yaml_json_failed:-0}; yaml_skipped=0; \
		if [ "$$yaml_failed" = "0" ] || [ -z "$$yaml_failed" ]; then yaml_status="✅ PASS"; yaml_failed=0; else yaml_status="❌ FAIL"; fi; \
	else \
		echo "   ⚠️  test/test_yaml_suite_gtest.exe not found"; \
	fi; \
	\
	echo ""; \
	echo "📦 ASCII Math Tests:"; \
	if [ -f "test/test_math_ascii_gtest.exe" ]; then \
		output=$$(./test/test_math_ascii_gtest.exe 2>&1) || true; \
		echo "$$output" | tail -20; \
		math_passed=$$(echo "$$output" | grep -E "^\[  PASSED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		math_failed=$$(echo "$$output" | grep -E "^\[  FAILED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		math_skipped=$$(echo "$$output" | grep -E "^\[  SKIPPED \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		math_passed=$${math_passed:-0}; math_failed=$${math_failed:-0}; math_skipped=$${math_skipped:-0}; \
		if [ "$$math_failed" = "0" ] || [ -z "$$math_failed" ]; then math_status="✅ PASS"; math_failed=0; else math_status="❌ FAIL"; fi; \
	else \
		echo "   ⚠️  test/test_math_ascii_gtest.exe not found"; \
	fi; \
	\
	echo ""; \
	echo "📦 LaTeX Math Tests:"; \
	if [ -f "test/test_math_gtest.exe" ]; then \
		output=$$(./test/test_math_gtest.exe 2>&1) || true; \
		echo "$$output" | tail -20; \
		latex_math_passed=$$(echo "$$output" | grep -E "^\[  PASSED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		latex_math_failed=$$(echo "$$output" | grep -E "^\[  FAILED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		latex_math_skipped=$$(echo "$$output" | grep -E "^\[  SKIPPED \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		latex_math_passed=$${latex_math_passed:-0}; latex_math_failed=$${latex_math_failed:-0}; latex_math_skipped=$${latex_math_skipped:-0}; \
		if [ "$$latex_math_failed" = "0" ] || [ -z "$$latex_math_failed" ]; then latex_math_status="✅ PASS"; latex_math_failed=0; else latex_math_status="❌ FAIL"; fi; \
	else \
		echo "   ⚠️  test/test_math_gtest.exe not found"; \
	fi; \
	\
	total_passed=$$((wpt_passed + md_passed + yaml_passed + math_passed + latex_math_passed)); \
	total_failed=$$((wpt_failed + md_failed + yaml_failed + math_failed + latex_math_failed)); \
	total_tests=$$((total_passed + total_failed)); \
	\
	echo ""; \
	echo "=============================================================="; \
	echo "🏁 INPUT BASELINE TEST RESULTS"; \
	echo "=============================================================="; \
	echo ""; \
	echo "📊 Test Results by Suite:"; \
	echo "   ├── HTML5 WPT Parser    $$wpt_status  ($$wpt_passed passed, $$wpt_failed failed)"; \
	echo "   ├── CommonMark Markdown $$md_status  ($$md_passed passed, $$md_failed failed)"; \
	echo "   ├── YAML Suite          $$yaml_status  ($$yaml_passed passed, $$yaml_failed failed)"; \
	echo "   ├── ASCII Math          $$math_status  ($$math_passed passed, $$math_failed failed)"; \
	echo "   └── LaTeX Math          $$latex_math_status  ($$latex_math_passed passed, $$latex_math_failed failed)"; \
	echo ""; \
	echo "📊 Overall Results:"; \
	echo "   Total Tests: $$total_tests"; \
	echo "   ✅ Passed:   $$total_passed"; \
	if [ $$total_failed -gt 0 ]; then \
		echo "   ❌ Failed:   $$total_failed"; \
	fi; \
	echo "=============================================================="

test-radiant-baseline: test-layout-baseline

test-layout-baseline: build-test
	@echo "Running Radiant layout BASELINE test suite..."
	@echo "=============================================================="
	@node test/layout/test_radiant_layout.js -c baseline

# TeX Typesetting Test Targets
# These tests validate LaTeX/TeX typesetting against reference DVI files

test-tex: build-test
	@echo "Running TeX typesetting test suite..."
	@echo "=============================================================="
	@if [ -f "test/test_tex_output_gtest.exe" ]; then \
		./test/test_tex_output_gtest.exe; \
	fi
	@if [ -f "test/test_tex_ast_gtest.exe" ]; then \
		./test/test_tex_ast_gtest.exe; \
	fi
	@if [ -f "test/test_tex_box_gtest.exe" ]; then \
		./test/test_tex_box_gtest.exe; \
	fi
	@if [ -f "test/test_tex_math_layout_gtest.exe" ]; then \
		./test/test_tex_math_layout_gtest.exe; \
	fi
	@if [ -f "test/test_tex_paragraph_gtest.exe" ]; then \
		./test/test_tex_paragraph_gtest.exe; \
	fi
	@if [ -f "test/test_latex_integration_gtest.exe" ]; then \
		./test/test_latex_integration_gtest.exe; \
	fi

test-tex-baseline: build-test
	@echo "Running TeX BASELINE test suite..."
	@echo "=============================================================="
	@./test/test_tex_output_gtest.exe --gtest_filter=*Basic*:*Simple* 2>/dev/null || true
	@./test/test_tex_ast_gtest.exe 2>/dev/null || true
	@./test/test_tex_box_gtest.exe 2>/dev/null || true

test-tex-dvi: build
	@echo "Running TeX DVI comparison tests (ALL)..."
	@echo "=============================================================="
	@chmod +x test/latex/run_tex_tests.sh
	@./test/latex/run_tex_tests.sh

test-tex-dvi-baseline: build build-test
	@echo "Running TeX DVI comparison BASELINE tests..."
	@echo "=============================================================="
	@./test/test_latex_dvi_compare_gtest.exe --gtest_filter="DVICompareBaselineTest.*"

test-tex-dvi-extended: build build-test
	@echo "Running TeX DVI comparison EXTENDED tests..."
	@echo "=============================================================="
	@./test/test_latex_dvi_compare_gtest.exe --gtest_filter="DVICompareExtendedTest.*"

test-tex-reference:
	@echo "Generating TeX reference DVI files..."
	@echo "=============================================================="
	@mkdir -p test/latex/reference
	@for tex in test/latex/test_*.tex; do \
		if [ -f "$$tex" ]; then \
			base=$$(basename "$$tex" .tex); \
			echo "  Compiling: $$base"; \
			latex -output-directory=test/latex/reference -interaction=nonstopmode "$$tex" > /dev/null 2>&1 || echo "    Warning: $$base failed"; \
		fi; \
	done
	@echo "Reference DVI files generated in test/latex/reference/"

# Math Testing targets (multi-layered semantic comparison framework)
test-math: build
	@echo "Running LaTeX Math test suite (ALL)..."
	@echo "=============================================================="
	@cd test/latex && npm test

test-math-baseline: build
	@echo "Running LaTeX Math BASELINE tests (DVI must pass 100%)..."
	@echo "=============================================================="
	@cd test/latex && npm run test:baseline

test-math-extended: build
	@echo "Running LaTeX Math EXTENDED tests (semantic comparison)..."
	@echo "=============================================================="
	@cd test/latex && npm run test:extended

test-math-verbose: build
	@echo "Running LaTeX Math tests (verbose mode)..."
	@echo "=============================================================="
	@cd test/latex && npm run test:verbose

test-math-group: build
	@echo "Running LaTeX Math tests for group: $(group)"
	@echo "=============================================================="
	@cd test/latex && node test_math_comparison.js --group=$(group)

test-math-single: build
	@echo "Running single LaTeX Math test: $(test)"
	@echo "=============================================================="
	@cd test/latex && node test_math_comparison.js --test=$(test) --verbose

setup-math-tests:
	@echo "Setting up LaTeX Math test dependencies..."
	@cd test/latex && npm install
	@echo "LaTeX Math test setup complete."

generate-math-references:
	@echo "Generating reference files (MathLive + KaTeX)..."
	@echo "=============================================================="
	@cd test/latex && npm run generate:mathlive
	@cd test/latex && npm run generate:katex
	@echo "Reference files generated in test/latex/reference/"

# PDF Testing targets
test-pdf: build
	@echo "Running Radiant PDF test suite..."
	@echo "=============================================================="
	@cd test/pdf && npm test

test-pdf-export:
	@echo "Exporting pdf.js operator lists as JSON references..."
	@cd test/pdf && npm run export

setup-pdf-tests:
	@echo "Setting up PDF test fixtures..."
	@mkdir -p test/pdf/data/basic test/pdf/reference test/pdf/output
	@echo "Copying test PDFs from pdf-js..."
	@for pdf in tracemonkey.pdf standard_fonts.pdf colors.pdf empty.pdf rotated.pdf rotation.pdf basicapi.pdf canvas.pdf; do \
		if [ -f "pdf-js/test/pdfs/$$pdf" ]; then \
			cp "pdf-js/test/pdfs/$$pdf" test/pdf/data/basic/; \
			echo "  Copied: $$pdf"; \
		fi; \
	done
	@echo "Installing npm dependencies..."
	@cd test/pdf && npm install
	@echo "PDF test setup complete. Run 'make test-pdf-export' to generate references."

test-extended: build-test
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@echo "Running EXTENDED test suites only..."
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --category=extended --parallel; \
	else \
		echo "Error: No test suite found"; \
		exit 1; \
	fi

test-library: build
	@echo "Running library test suite..."
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

test-benchmark:
	@echo "Running performance benchmark tests..."
	@if [ -f "test/test_benchmark.sh" ]; then \
		chmod +x test/test_benchmark.sh; \
		./test/test_benchmark.sh; \
	else \
		echo "Benchmark test script not found at test/test_benchmark.sh"; \
		exit 1; \
	fi

# Fuzzy Testing Framework
# Shell-based fuzzer for testing Lambda robustness

# Run fuzzy tests (quick mode: 5 minutes)
test-fuzzy: build
	@echo "Running fuzzy tests (quick mode: 5 minutes)..."
	@chmod +x test/fuzzy/test_fuzzy.sh
	@./test/fuzzy/test_fuzzy.sh --duration=300
	@echo "✅ Fuzzy tests completed"

# Run extended fuzzy tests (1 hour)
test-fuzzy-extended: build
	@echo "Running extended fuzzy tests (1 hour)..."
	@chmod +x test/fuzzy/test_fuzzy.sh
	@./test/fuzzy/test_fuzzy.sh --duration=3600
	@echo "✅ Extended fuzzy tests completed"

test-integration:
	@echo "Running integration tests..."
	@if [ -f "test/test_integration.sh" ]; then \
		chmod +x test/test_integration.sh; \
		./test/test_integration.sh; \
	else \
		echo "Integration test script not found at test/test_integration.sh"; \
		exit 1; \
	fi

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

# Binary size analysis by library group
analyze-binary:
	@python3 utils/analyze_binary.py lambda.exe -v

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

# Refactor printf/fprintf to log_debug using clang-based tool
# Usage: make tidy-printf FILE='pattern' [DRY_RUN=1] [BACKUP=1]
tidy-printf:
	@if [ ! -f "utils/refactor_to_log_debug" ]; then \
		echo "Building refactor_to_log_debug tool..."; \
		cd utils && ./build_refactor_tool.sh || exit 1; \
	fi; \
	if [ -z "$(FILE)" ]; then \
		echo "Usage: make tidy-printf FILE='pattern' [DRY_RUN=1] [BACKUP=1]"; \
		echo ""; \
		echo "Examples:"; \
		echo "  make tidy-printf FILE='include/mir-bitmap.h' DRY_RUN=1"; \
		echo "  make tidy-printf FILE='lambda/lambda-eval.cpp' BACKUP=1"; \
		echo "  make tidy-printf FILE='lambda/*.cpp' DRY_RUN=1"; \
		echo "  make tidy-printf FILE='lib/*.c' BACKUP=1"; \
		echo "  make tidy-printf FILE='lambda/input/input*.cpp' BACKUP=1"; \
		echo ""; \
		echo "Options:"; \
		echo "  FILE='pattern'  File pattern with wildcards (MUST be quoted!)"; \
		echo "  DRY_RUN=1       Preview changes without modifying files"; \
		echo "  BACKUP=1        Create .bak backup files before modifying"; \
		echo ""; \
		echo "Note: Always quote wildcards to prevent shell expansion!"; \
		exit 1; \
	fi; \
	DRY_RUN_FLAG=""; \
	BACKUP_FLAG=""; \
	if [ "$(DRY_RUN)" = "1" ]; then \
		DRY_RUN_FLAG="--dry-run"; \
	fi; \
	if [ "$(BACKUP)" = "1" ]; then \
		BACKUP_FLAG="--backup"; \
	fi; \
	FILES=$$(eval echo $(FILE)); \
	if [ -z "$$FILES" ]; then \
		echo "No files found matching pattern: $(FILE)"; \
		exit 1; \
	fi; \
	FILE_COUNT=$$(echo $$FILES | wc -w | tr -d ' '); \
	echo "Found $$FILE_COUNT file(s) matching pattern: $(FILE)"; \
	echo ""; \
	for file in $$FILES; do \
		if [ ! -f "$$file" ]; then \
			echo "Skipping non-existent file: $$file"; \
			continue; \
		fi; \
		echo "Processing $$file..."; \
		./utils/refactor_to_log_debug "$$file" $$DRY_RUN_FLAG $$BACKUP_FLAG; \
		echo ""; \
	done; \
	echo "✓ Completed processing $$FILE_COUNT file(s)"

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

# Cheatsheet PDF generation
cheatsheet:
	@echo "Generating Lambda Script cheatsheet PDFs..."
	@cd doc/_template && bash generate_pdf_cheatsheets.sh

# Utility targets
info:
	@echo "Project Information:"
	@echo "  Name: $(PROJECT_NAME)"
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
	@rm -f premake5.lua premake5.*.lua
	@rm -f *.make
	@rm -f dummy.cpp
	@echo "Premake5 artifacts cleaned."

build-lambda-input:
	@echo "Building lambda-input DLLs..."
	@echo "Generating Premake configuration..."
	@mkdir -p build/premake
	$(MAKE) generate-premake
	@echo "Generating makefiles..."
	cd build/premake && premake5 gmake --file=../../$(PREMAKE_FILE)
	@echo "Building lambda-input DLLs with $(JOBS) parallel jobs..."
	cd build/premake && $(MAKE) config=debug_native lambda-input-full-cpp -j$(JOBS)
	@echo "✅ lambda-input DLLs built successfully!"

build-test: build-lambda-input
	@echo "Building tests using Premake5..."
	@echo "Building configurations..."
	@mkdir -p build/premake
	$(MAKE) generate-premake
	cd build/premake && premake5 gmake --file=../../$(PREMAKE_FILE)
	@# If last build was release, rebuild lambda.exe incrementally in release mode
	@if [ -f .lambda_release_build ]; then \
		echo "Rebuilding lambda.exe in release mode (incremental)..."; \
		$(MAKE) -C build/premake config=release_native lambda -j$(JOBS) CC="$(CC)" CXX="$(CXX)"; \
		cp -p lambda.exe .lambda_build_backup.exe; \
	fi
	@echo "Building all test executables (debug mode)..."
	$(MAKE) -C build/premake config=debug_native -j$(JOBS) CC="$(CC)" CXX="$(CXX)"
	@# Restore release lambda.exe over the debug one
	@if [ -f .lambda_build_backup.exe ]; then \
		echo "Restoring release lambda.exe..."; \
		mv .lambda_build_backup.exe lambda.exe; \
	fi

# Capture browser layout references using Puppeteer
# Usage:
#   make capture-layout test=test-name           # captures a specific test (auto-discovers suite)
#   make capture-layout suite=baseline           # captures only baseline category
#   make capture-layout test=test-name force=1   # force regenerate a specific test
#   make capture-layout suite=basic force=1      # force regenerate specific category
#   make capture-layout suite=baseline platform=linux  # capture Linux-specific references
# Note: Either test= or suite= is REQUIRED (no longer allows capturing all tests)
capture-layout:
	@echo "🧭 Capturing browser layout references..."
	@TEST_VAR="$(or $(test),$(TEST))"; \
	SUITE_VAR="$(or $(suite),$(SUITE))"; \
	PLATFORM_VAR="$(or $(platform),$(PLATFORM))"; \
	if [ -z "$$TEST_VAR" ] && [ -z "$$SUITE_VAR" ]; then \
		echo ""; \
		echo "❌ Error: Either test= or suite= parameter is required"; \
		echo ""; \
		echo "Usage:"; \
		echo "  make capture-layout test=<test-name>     # Capture a specific test"; \
		echo "  make capture-layout suite=<suite-name>   # Capture entire suite"; \
		echo "  make capture-layout suite=baseline platform=linux  # Linux-specific refs"; \
		echo ""; \
		echo "Examples:"; \
		echo "  make capture-layout test=basic-text-align"; \
		echo "  make capture-layout suite=baseline"; \
		echo "  make capture-layout test=table_007 force=1"; \
		echo "  make capture-layout suite=baseline platform=linux force=1"; \
		echo ""; \
		echo "Available suites: basic, baseline, css2.1, flex, grid, yoga, wpt-css-box, wpt-css-images, wpt-css-tables, wpt-css-position, wpt-css-text, wpt-css-lists"; \
		echo "Available platforms: linux, darwin, win32"; \
		echo ""; \
		exit 1; \
	fi; \
	if [ -d "test/layout" ]; then \
	    cd test/layout && \
	    if [ ! -d node_modules ]; then \
	        echo "📦 Installing test layout dependencies..."; \
	        npm install; \
	    fi; \
	    FORCE_FLAG=""; \
	    FORCE_VAR="$(or $(force),$(FORCE))"; \
	    if [ -n "$$FORCE_VAR" ] && [ "$$FORCE_VAR" != "0" ]; then \
	        FORCE_FLAG="--force"; \
	        echo "🔄 Force regeneration enabled"; \
	    fi; \
	    PLATFORM_FLAG=""; \
	    if [ -n "$$PLATFORM_VAR" ]; then \
	        PLATFORM_FLAG="--platform $$PLATFORM_VAR"; \
	        echo "📦 Platform-specific reference: $$PLATFORM_VAR"; \
	    fi; \
	    if [ -n "$$TEST_VAR" ]; then \
	        case "$$TEST_VAR" in \
	            *.html|*.htm) TEST_FILE="$$TEST_VAR" ;; \
	            *) \
	                TEST_FILE=""; \
	                FOUND_SUITE=""; \
	                for dir in basic baseline css2.1 flex grid yoga wpt-css-box wpt-css-images wpt-css-tables wpt-css-position wpt-css-text wpt-css-lists; do \
	                    if [ -f "data/$$dir/$${TEST_VAR}.htm" ]; then \
	                        TEST_FILE="data/$$dir/$${TEST_VAR}.htm"; \
	                        FOUND_SUITE="$$dir"; \
	                        break; \
	                    elif [ -f "data/$$dir/$${TEST_VAR}.html" ]; then \
	                        TEST_FILE="data/$$dir/$${TEST_VAR}.html"; \
	                        FOUND_SUITE="$$dir"; \
	                        break; \
	                    fi; \
	                    for subdir in data/$$dir/*/; do \
	                        if [ -d "$$subdir" ]; then \
	                            if [ -f "$${subdir}$${TEST_VAR}.htm" ]; then \
	                                TEST_FILE="$${subdir}$${TEST_VAR}.htm"; \
	                                FOUND_SUITE="$$dir"; \
	                                break 2; \
	                            elif [ -f "$${subdir}$${TEST_VAR}.html" ]; then \
	                                TEST_FILE="$${subdir}$${TEST_VAR}.html"; \
	                                FOUND_SUITE="$$dir"; \
	                                break 2; \
	                            fi; \
	                        fi; \
	                    done; \
	                done; \
	                if [ -z "$$TEST_FILE" ]; then \
	                    echo "❌ Error: Test file '$$TEST_VAR' not found in any suite directory"; \
	                    echo "   Searched in: basic, baseline, css2.1, flex, grid, yoga, wpt-css-*"; \
	                    exit 1; \
	                fi; \
	                echo "📄 Found test in suite: $$FOUND_SUITE" \
	            ;; \
	        esac; \
	        echo "📄 Capturing single test: $$TEST_FILE"; \
	        node extract_browser_references.js $$FORCE_FLAG $$PLATFORM_FLAG $$TEST_FILE; \
	    else \
	        echo "📂 Capturing suite: $$SUITE_VAR"; \
	        node extract_browser_references.js $$FORCE_FLAG $$PLATFORM_FLAG --category $$SUITE_VAR; \
	    fi; \
	else \
	    echo "❌ Error: Layout directory not found at test/layout"; \
	    exit 1; \
	fi

# Layout Engine Testing Targets
# ==============================

# test-layout: Run layout tests using Lambda CSS engine
# Usage: make test-layout [suite=SUITE] [test=TEST] [pattern=PATTERN]
# Note: test parameter now accepts filename with or without .html/.htm extension
# Example: make test-layout test=baseline_301_simple_margin
test-layout:
	@echo "🎨 Running Lambda CSS Layout Engine Tests"
	@echo "=========================================="
	@if [ -f "test/layout/test_radiant_layout.js" ]; then \
		TEST_VAR="$(or $(test),$(TEST))"; \
		PATTERN_VAR="$(or $(pattern),$(PATTERN))"; \
		SUITE_VAR="$(or $(suite),$(SUITE))"; \
		if [ -n "$$TEST_VAR" ]; then \
			case "$$TEST_VAR" in \
				*.html|*.htm) TEST_FILE="$$TEST_VAR" ;; \
				*) \
					TEST_FILE=""; \
					for dir in basic baseline css2.1 flex grid yoga wpt-css-box wpt-css-images wpt-css-tables wpt-css-position wpt-css-text wpt-css-lists; do \
						if [ -f "test/layout/data/$$dir/$${TEST_VAR}.htm" ]; then \
							TEST_FILE="$${TEST_VAR}.htm"; \
							break; \
						elif [ -f "test/layout/data/$$dir/$${TEST_VAR}.html" ]; then \
							TEST_FILE="$${TEST_VAR}.html"; \
							break; \
						fi; \
						for subdir in test/layout/data/$$dir/*/; do \
							if [ -d "$$subdir" ]; then \
								if [ -f "$${subdir}$${TEST_VAR}.htm" ]; then \
									TEST_FILE="$${TEST_VAR}.htm"; \
									break 2; \
								elif [ -f "$${subdir}$${TEST_VAR}.html" ]; then \
									TEST_FILE="$${TEST_VAR}.html"; \
									break 2; \
								fi; \
							fi; \
						done; \
					done; \
					if [ -z "$$TEST_FILE" ]; then \
						TEST_FILE="$${TEST_VAR}.html"; \
					fi \
				;; \
			esac; \
			echo "🎯 Running single test: $$TEST_FILE"; \
			node test/layout/test_radiant_layout.js --engine lambda-css --test $$TEST_FILE -v; \
		elif [ -n "$$PATTERN_VAR" ]; then \
			echo "🔍 Running tests matching pattern: $$PATTERN_VAR"; \
			node test/layout/test_radiant_layout.js --engine lambda-css --pattern $$PATTERN_VAR -j 5; \
		elif [ -n "$$SUITE_VAR" ]; then \
			echo "📂 Running test suite: $$SUITE_VAR"; \
			node test/layout/test_radiant_layout.js --engine lambda-css --category $$SUITE_VAR -j 5; \
		else \
			echo "🎯 Running all layout tests"; \
			node test/layout/test_radiant_layout.js --engine lambda-css -j 5; \
		fi; \
	else \
		echo "❌ Error: Layout test script not found at test/layout/test_radiant_layout.js"; \
		exit 1; \
	fi

# layout: Alias for test-layout
layout: test-layout

# compare-layout: Run Radiant layout and compare with browser reference
# Usage: make compare-layout test=<test-name> [category=<category>] [options]
# Example: make compare-layout test=sample3.html category=page
# Example: make compare-layout test=baseline_301 category=baseline verbose=1
compare-layout: build
	@if [ -z "$(test)" ] && [ -z "$(TEST)" ]; then \
		echo "Usage: make compare-layout test=<test-name> [category=<category>]"; \
		echo ""; \
		echo "Examples:"; \
		echo "  make compare-layout test=sample3.html category=page"; \
		echo "  make compare-layout test=baseline_301 category=baseline"; \
		echo "  make compare-layout test=sample3 category=page verbose=1"; \
		echo ""; \
		echo "Options (pass as make variables):"; \
		echo "  test=NAME       Test file name (required)"; \
		echo "  category=CAT    Test category (default: page)"; \
		echo "  verbose=1       Show all comparisons"; \
		echo "  threshold=N     Difference threshold (default: 0.5)"; \
		echo "  elements=LIST   Comma-separated list of elements"; \
		exit 1; \
	fi; \
	TEST_VAR="$${test:-$(TEST)}"; \
	CAT_VAR="$${category:-$(CATEGORY)}"; \
	CAT_VAR="$${CAT_VAR:-page}"; \
	OPTS="--run --test $$TEST_VAR --category $$CAT_VAR"; \
	if [ -n "$(verbose)" ] || [ -n "$(VERBOSE)" ]; then \
		OPTS="$$OPTS --verbose"; \
	fi; \
	if [ -n "$(threshold)" ] || [ -n "$(THRESHOLD)" ]; then \
		OPTS="$$OPTS --threshold $${threshold:-$(THRESHOLD)}"; \
	fi; \
	if [ -n "$(elements)" ] || [ -n "$(ELEMENTS)" ]; then \
		OPTS="$$OPTS --elements $${elements:-$(ELEMENTS)}"; \
	fi; \
	node test/layout/compare-layout.js $$OPTS

# layout-devtool: Launch the Layout DevTool Electron app
# Usage: make layout-devtool
layout-devtool:
	@echo "🚀 Launching Layout DevTool..."
	@if [ -d "utils/layout-devtool" ]; then \
		cd utils/layout-devtool && npm run electron:dev; \
	else \
		echo "❌ Error: Layout DevTool not found at utils/layout-devtool"; \
		exit 1; \
	fi

# download: Download a web page and save it to test/layout/data/page/
# Usage: make download <url>
# Example: make download https://example.com
# This will download the page and extract the domain as prefix
download:
	@if [ -z "$(filter-out download,$(MAKECMDGOALS))" ]; then \
		echo "Usage: make download <url>"; \
		echo ""; \
		echo "Example:"; \
		echo "  make download https://example.com/path/page.html"; \
		echo ""; \
		echo "This will:"; \
		echo "  1. Download the web page from the URL"; \
		echo "  2. Save to test/layout/data/page/"; \
		echo "  3. Use cleansed full URL path as filename"; \
		echo "  4. Extract domain name as resource prefix"; \
		exit 1; \
	fi; \
	URL_VAR="$(filter-out download,$(MAKECMDGOALS))"; \
	if [ -z "$$URL_VAR" ]; then \
		echo "❌ Error: No URL provided"; \
		exit 1; \
	fi; \
	if ! echo "$$URL_VAR" | grep -qE '^https?://'; then \
		echo "❌ Error: Invalid URL - must start with http:// or https://"; \
		echo "   Provided: $$URL_VAR"; \
		exit 1; \
	fi; \
	DOMAIN=$$(echo "$$URL_VAR" | sed -E 's|^https?://([^/:]+).*|\1|' | sed -E 's/^www\.//' | sed -E 's/\..*//' | tr '[:upper:]' '[:lower:]'); \
	PREFIX="$$DOMAIN"; \
	FULL_PATH=$$(echo "$$URL_VAR" | sed -E 's|^https?://[^/]+||' | sed -E 's|^/||' | sed -E 's|[^a-zA-Z0-9._-]|_|g' | sed -E 's|_+|_|g' | sed -E 's|^[^a-zA-Z0-9]+||'); \
	if [ -z "$$FULL_PATH" ] || [ "$$FULL_PATH" = "_" ]; then \
		FILENAME="$$PREFIX.html"; \
	else \
		FILENAME="$$FULL_PATH"; \
		if ! echo "$$FILENAME" | grep -qE '\.(html?|htm)$$'; then \
			FILENAME="$$FILENAME.html"; \
		fi; \
	fi; \
	OUTPUT_DIR="test/layout/data/page"; \
	echo "📥 Downloading web page..."; \
	echo "  URL: $$URL_VAR"; \
	echo "  Output directory: $$OUTPUT_DIR"; \
	echo "  Filename: $$FILENAME"; \
	echo "  Prefix: $$PREFIX"; \
	echo ""; \
	mkdir -p "$$OUTPUT_DIR"; \
	node utils/downloader/download-page.js "$$URL_VAR" "$$OUTPUT_DIR" --prefix "$$PREFIX" --name "$$FILENAME"

# Catch-all target to allow filenames as targets for download
%:
	@:

# Run compilation performance benchmark
bench-compile:
	@echo "Running C/C++ compilation benchmark..."
	@bash utils/benchmark_compile.sh

# Count lines of code in the repository
count-loc:
	@./utils/count_loc.sh
