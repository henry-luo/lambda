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
TYPESET_DIR = typeset

# Output executables
LAMBDA_EXE = lambda.exe
RADIANT_EXE = radiant.exe
WINDOW_EXE = window.exe

# Typesetting system sources
TYPESET_SOURCES = \
    $(TYPESET_DIR)/typeset.c \
    $(TYPESET_DIR)/view/view_tree.c \
    $(TYPESET_DIR)/document/document.c \
    $(TYPESET_DIR)/document/page.c \
    $(TYPESET_DIR)/style/font.c \
    $(TYPESET_DIR)/style/style.c \
    $(TYPESET_DIR)/layout/layout.c \
    $(TYPESET_DIR)/math/math_layout.c \
    $(TYPESET_DIR)/math/math_metrics.c \
    $(TYPESET_DIR)/output/renderer.c \
    $(TYPESET_DIR)/output/html_renderer.c \
    $(TYPESET_DIR)/output/svg_renderer.c \
    $(TYPESET_DIR)/output/pdf_renderer.c \
    $(TYPESET_DIR)/output/tex_renderer.c \
    $(TYPESET_DIR)/output/png_renderer.c \
    $(TYPESET_DIR)/serialization/lambda_serializer.c \
    $(TYPESET_DIR)/serialization/markdown_serializer.c \
    $(TYPESET_DIR)/integration/lambda_bridge.c \
    $(TYPESET_DIR)/integration/stylesheet.c

# Unicode support configuration (utf8proc-based)
UNICODE_FLAGS = -DLAMBDA_UTF8PROC_SUPPORT

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

# Tree-sitter grammar dependencies
# This system automatically manages the dependency chain:
# grammar.js -> parser.c -> ts-enum.h -> C/C++ source files
# When grammar.js is modified, the parser and enum header are automatically regenerated
GRAMMAR_JS = lambda/tree-sitter-lambda/grammar.js
PARSER_C = lambda/tree-sitter-lambda/src/parser.c
GRAMMAR_JSON = lambda/tree-sitter-lambda/src/grammar.json
NODE_TYPES_JSON = lambda/tree-sitter-lambda/src/node-types.json
TS_ENUM_H = lambda/ts-enum.h
GENERATE_PARSER_SCRIPT = ./generate-parser.sh
UPDATE_TS_ENUM_SCRIPT = ./update_ts_enum.sh

# Auto-generate parser and ts-enum.h when grammar.js changes
$(TS_ENUM_H): $(GRAMMAR_JS)
	@echo "Grammar changed, regenerating parser and ts-enum.h..."
	@cd lambda/tree-sitter-lambda && npx tree-sitter-cli@0.24.7 generate
	$(UPDATE_TS_ENUM_SCRIPT)
	@echo "Updated ts-enum.h from grammar changes"

$(PARSER_C) $(GRAMMAR_JSON) $(NODE_TYPES_JSON): $(GRAMMAR_JS)
	@echo "Generating parser from grammar.js..."
	@cd lambda/tree-sitter-lambda && npx tree-sitter-cli@0.24.7 generate

# Tree-sitter library targets
TREE_SITTER_LIB = lambda/tree-sitter/libtree-sitter.a
TREE_SITTER_LAMBDA_LIB = lambda/tree-sitter-lambda/libtree-sitter-lambda.a

# Build tree-sitter library
$(TREE_SITTER_LIB):
	@echo "Building tree-sitter library..."
	$(MAKE) -C lambda/tree-sitter libtree-sitter.a

# Build tree-sitter-lambda library (depends on parser generation)
$(TREE_SITTER_LAMBDA_LIB): $(PARSER_C)
	@echo "Building tree-sitter-lambda library..."
	$(MAKE) -C lambda/tree-sitter-lambda libtree-sitter-lambda.a

# Combined target for all tree-sitter libraries
tree-sitter-libs: $(TREE_SITTER_LIB) $(TREE_SITTER_LAMBDA_LIB)

# Default target
.DEFAULT_GOAL := build

# Phony targets (don't correspond to actual files)
.PHONY: all build build-ascii clean clean-test clean-grammar generate-grammar debug release rebuild test test-input run help install uninstall \
        lambda radiant window cross-compile format lint check docs \
        build-windows build-debug build-release build-test clean-all distclean \
        build-windows build-debug build-release clean-all distclean \
        verify-windows test-windows tree-sitter-libs

# Help target - shows available commands
help:
	@echo "$(PROJECT_NAME) - Available Make Targets:"
	@echo ""
	@echo "Build Targets:"
	@echo "  build         - Build lambda project with Unicode support (incremental, default)"
	@echo "  build-ascii   - Build lambda project with ASCII-only support (no Unicode)"
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
	@echo "  clean-test    - Remove test output and temporary files"
	@echo "  clean-grammar - Remove generated grammar files (parser.c, ts-enum.h)"
	@echo "  clean-all     - Remove all build directories"
	@echo "  distclean     - Complete cleanup (build dirs + executables + tests)"
	@echo ""
	@echo "Grammar & Parser:"
	@echo "  generate-grammar - Generate parser and ts-enum.h from grammar.js"
	@echo "                     (automatic when grammar.js changes)"
	@echo "  tree-sitter-libs - Build tree-sitter and tree-sitter-lambda libraries"
	@echo ""
	@echo "Development:"
	@echo "  test          - Run comprehensive unit tests"
	@echo "  test-library  - Run library tests only"
	@echo "  test-input    - Run input processing test suite (MIME detection & math)"
	@echo "  test-validator- Run validator tests only"
	@echo "  test-mir      - Run MIR JIT tests only"
	@echo "  test-lambda   - Run lambda runtime tests only"
	@echo "  test-verbose  - Run tests with verbose output"
	@echo "  test-sequential - Run tests sequentially (not parallel)"
	@echo "  test-coverage - Run tests with code coverage analysis"
	@echo "  test-memory   - Run memory leak detection tests"
	@echo "  test-benchmark- Run performance benchmark tests"  
	@echo "  test-fuzz     - Run fuzzing tests for robustness"
	@echo "  test-integration - Run end-to-end integration tests"
	@echo "  test-all      - Run complete test suite (all test types)"
	@echo "  test-ci       - Run CI test suite (unit + memory + integration)"
	@echo "  verify-windows - Verify Windows cross-compiled executable with Wine"
	@echo "  test-windows  - Run CI tests for Windows executable"
	@echo "  run           - Build and run the default executable"
	@echo "  check         - Run static analysis and checks"
	@echo "  format        - Format source code"
	@echo "  lint          - Run linter on source files"
	@echo ""
	@echo "Unicode Support:"
	@echo "  test-unicode  - Run Unicode string comparison tests"
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
build: $(TS_ENUM_H) tree-sitter-libs
	@echo "Building $(PROJECT_NAME) (incremental)..."
	UNICODE_FLAGS="$(UNICODE_FLAGS)" $(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --jobs=$(JOBS)

# ASCII-only build (no Unicode support)
build-ascii:
	@echo "Building $(PROJECT_NAME) with ASCII-only support (no Unicode)..."
	UNICODE_FLAGS="" $(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --jobs=$(JOBS)

print-vars:
	@echo "UNICODE_FLAGS=$(UNICODE_FLAGS)"

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
lambda: $(TS_ENUM_H) tree-sitter-libs
	@echo "Building lambda project..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --jobs=$(JOBS)

radiant:
	@echo "Building radiant project..."
	$(COMPILE_SCRIPT) $(RADIANT_CONFIG) --jobs=$(JOBS)

window: radiant

# Build all projects
all: lambda radiant
	@echo "All projects built successfully."

test-unicode: build
	@echo "Running Unicode string comparison tests..."
	./$(LAMBDA_EXE) test/lambda/unicode_test.ls

# Cross-compilation for Windows
cross-compile: build-windows

build-windows:
	@echo "Cross-compiling for Windows..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --platform=windows --jobs=$(JOBS)

# Debugging builds with specific directories
build-debug:
	@echo "Building with debug configuration..."
	$(COMPILE_SCRIPT) $(DEFAULT_CONFIG) --debug --jobs=$(JOBS)

build-wasm:
	@echo "Building WebAssembly version..."
	./compile-wasm.sh --linking-only

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
	@echo "Test build outputs cleaned."

clean-grammar:
	@echo "Cleaning generated grammar files..."
	@rm -f $(TS_ENUM_H)
	@rm -f $(PARSER_C)
	@rm -f $(GRAMMAR_JSON)
	@rm -f $(NODE_TYPES_JSON)
	@echo "Generated grammar files cleaned."

# Generate grammar explicitly (useful for development)
generate-grammar: $(TS_ENUM_H)
	@echo "Grammar generation complete."

clean-all:
	@echo "Removing all build directories..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(BUILD_DEBUG_DIR)
	@rm -rf $(BUILD_WINDOWS_DIR)
	@echo "All build directories removed."

distclean: clean-all clean-grammar clean-test
	@echo "Complete cleanup..."
	@rm -f $(LAMBDA_EXE)
	@rm -f lambda-windows.exe
	@rm -f $(WINDOW_EXE)
	@rm -f _transpiled.c
	@rm -f *.exe
	@echo "Complete cleanup finished."

# Development targets
test: build-test
	@echo "Running comprehensive test suite..."
	@if [ -f "test_modern.sh" ]; then \
		./test_modern.sh; \
	elif [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --sequential; \
	else \
		echo "Error: No test suite found"; \
		exit 1; \
	fi

test-dev:
	@echo "Running comprehensive test suite (development mode)..."
	@if [ -f "test_modern.sh" ]; then \
		./test_modern.sh || echo "Note: Some tests failed due to incomplete features (math parser, missing dependencies)"; \
	elif [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --sequential || echo "Note: Some tests failed due to incomplete features"; \
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

build-test: build
	@echo "Building all test executables..."
	@if [ -f "test/test_build.sh" ]; then \
		PARALLEL_JOBS=$(NPROCS) ./test/test_build.sh all; \
	else \
		echo "Error: test_build.sh not found"; \
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

test-mir: build
	@echo "Running MIR test suite..."
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --target=mir --raw; \
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

test-sequential: build
	@echo "Running tests sequentially (not parallel)..."
	@if [ -f "test/test_run.sh" ]; then \
		./test/test_run.sh --sequential; \
	else \
		echo "Error: Test script not found at test/test_run.sh"; \
		exit 1; \
	fi

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
