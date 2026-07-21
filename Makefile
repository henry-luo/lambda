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
LAMBDA_PROFILE_EXE = lambda-profile.exe
LAMBDA_CLI_EXE = lambda-cli.exe

# Unicode support is always enabled (utf8proc-based)
# No longer using conditional compilation flags

# Auto-detect number of jobs for parallel compilation
NPROCS := 1
OS := $(shell uname -s)
ifeq ($(OS),Darwin)
	NPROCS := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 1)
	PREMAKE_FILE := premake5.mac.lua
	PREMAKE_CLI_FILE := premake5.cli.mac.lua
	PREMAKE_JUBE_FILE := premake5.jube.mac.lua
	MACOS_DEPLOYMENT_TARGET ?= $(or $(MACOSX_DEPLOYMENT_TARGET),15.0)
	MACOS_CMAKE_FLAGS := -DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOS_DEPLOYMENT_TARGET) -DCMAKE_OSX_SYSROOT=$(shell xcrun --show-sdk-path)
else ifeq ($(OS),Linux)
	NPROCS := $(shell nproc 2>/dev/null || echo 1)
	PREMAKE_FILE := premake5.lin.lua
	PREMAKE_CLI_FILE := premake5.cli.lin.lua
	PREMAKE_JUBE_FILE := premake5.jube.lin.lua
else
	# Windows/MSYS2 detection
	NPROCS := $(shell nproc 2>/dev/null || echo 4)
	PREMAKE_FILE := premake5.win.lua
	PREMAKE_CLI_FILE := premake5.cli.win.lua
	PREMAKE_JUBE_FILE := premake5.jube.win.lua
endif

NPROCS := $(shell n="$(NPROCS)"; if expr "$$n" : '^[1-9][0-9]*$$' >/dev/null; then echo "$$n"; else echo 1; fi)

# Render visual tests are CPU-heavy but independent; leave one core free for
# the OS and browser/reference helpers.
RADIANT_RENDER_JOBS := $(shell n=$(NPROCS); if [ "$$n" -gt 1 ]; then echo $$((n - 1)); else echo 1; fi)
# DOM fixtures are process-isolated and CPU-heavy; bound concurrency while
# leaving one core available for the host. Override with DOM_UI_JOBS=<n>.
DOM_UI_JOBS ?= $(shell n=$(NPROCS); if [ "$$n" -gt 1 ]; then echo $$((n - 1)); else echo 1; fi)
LAYOUT_TEST_ENV ?= LAMBDA_AUTO_CLOSE=1
# Ranges and reflection remain extended `make test` coverage; their large
# known-failure inventories are not part of the fast Radiant baseline gate.
RADIANT_BASELINE_TEST_PROJECTS := test_ui_automation_gtest test_page_load_gtest test_radiant_view_gtest test_layout_fuzzy_gtest test_wpt_css_syntax_gtest test_wpt_input_events_gtest
RADIANT_DOM2_WPT_RUNNERS := input_events

# Optimize parallel jobs: use all cores for compilation, limit linking to 1
JOBS := $(NPROCS)
LINK_JOBS := 1
# Fast debug test builds do not use ASan by default; use all cores for tests.
TEST_JOBS := $(NPROCS)

# ccache enablement moved below — must run AFTER the CC/CXX compiler-detection
# block (lines ~94-109) so the ccache prefix isn't overwritten.

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
PREMAKE5_BIN := $(shell command -v premake5 2>/dev/null || command -v /mingw64/bin/premake5 2>/dev/null || command -v /clang64/bin/premake5 2>/dev/null || echo premake5)
# Route every `$(PREMAKE5) gmake` through a wrapper that suppresses premake5's
# noisy per-file "Generated ..." progress (185+ lines) while keeping errors and
# its exit status. Absolute path so it also works from `cd build/premake` sites.
PREMAKE5 := bash $(CURDIR)/utils/premake_quiet.sh $(PREMAKE5_BIN)

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

# Enable ccache for faster builds if available.
# MUST come after the CC/CXX detection block above; otherwise the platform
# branches overwrite our `ccache <compiler>` wrap.
ifneq ($(shell which ccache 2>/dev/null),)
	CC := ccache $(CC)
	CXX := ccache $(CXX)
	export CCACHE_DIR := $(shell pwd)/build/.ccache
	# 5 GiB matches ccache's own default; 500M was undersized for 147 test TUs.
	export CCACHE_MAXSIZE := 5G
	export CCACHE_COMPRESS := 1
	# time_macros: __DATE__/__TIME__ shouldn't invalidate cache entries.
	# include_file_mtime: rely on content hash, not mtime, when headers are
	#   regenerated identically (common with generated parser.c / ts-enum.h).
	export CCACHE_SLOPPINESS := time_macros,include_file_mtime
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
	@out=$$(cd lambda/tree-sitter-lambda && npx tree-sitter-cli@0.24.7 generate 2>&1) || { printf '%s\n' "$$out"; exit 1; }

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
TREE_SITTER_BASH_LIB = lambda/tree-sitter-bash/libtree-sitter-bash.a
TREE_SITTER_PYTHON_LIB = lambda/tree-sitter-python/libtree-sitter-python.a
TREE_SITTER_TYPESCRIPT_LIB = lambda/tree-sitter-typescript/libtree-sitter-typescript.a
TREE_SITTER_RUBY_LIB = lambda/tree-sitter-ruby/libtree-sitter-ruby.a
TREE_SITTER_LATEX_LIB = lambda/tree-sitter-latex/libtree-sitter-latex.a
TREE_SITTER_LATEX_MATH_LIB = lambda/tree-sitter-latex-math/libtree-sitter-latex-math.a
RE2_LIB = build_temp/re2-noabsl/cmake_build/libre2.a

# MIR JIT library (platform-specific paths match build_lambda_config.json)
ifeq ($(OS),Darwin)
    MIR_LIB = mac-deps/mir/libmir.a
    MIR_BUILD_DIR = mac-deps/mir
else ifeq ($(IS_MSYS2),yes)
    MIR_LIB = win-native-deps/lib/libmir.a
    MIR_BUILD_DIR = build_temp/mir
else
    MIR_LIB = /usr/local/lib/libmir.a
    MIR_BUILD_DIR = build_temp/mir
endif
MIR_PATCH = patches/mir-alloca-branch-fix.patch

# JavaScript scanner dependencies
JS_SCANNER_C = lambda/tree-sitter-javascript/src/scanner.c

# TypeScript grammar and scanner dependencies
TS_GRAMMAR_JS = lambda/tree-sitter-typescript/grammar.js
TS_PARSER_C = lambda/tree-sitter-typescript/src/parser.c
TS_SCANNER_C = lambda/tree-sitter-typescript/src/scanner.c
TS_SCANNER_H = lambda/tree-sitter-typescript/scanner_v2.h

# LaTeX grammar dependencies
LATEX_GRAMMAR_JS = lambda/tree-sitter-latex/grammar.js
LATEX_PARSER_C = lambda/tree-sitter-latex/src/parser.c
LATEX_GRAMMAR_JSON = lambda/tree-sitter-latex/src/grammar.json
LATEX_NODE_TYPES_JSON = lambda/tree-sitter-latex/src/node-types.json

# LaTeX Math grammar dependencies
LATEX_MATH_GRAMMAR_JS = lambda/tree-sitter-latex-math/grammar.js
LATEX_MATH_PARSER_C = lambda/tree-sitter-latex-math/src/parser.c

# Quietly build lambda/tree-sitter-$(1)'s static lib: run the sub-make with its
# output captured and shown only on failure, so a successful parser build is a
# single status line. $(2) = extra sub-make args (e.g. TS=...).
define ts_lib_build
@out=$$(env -u OS PATH="/mingw64/bin:$$PATH" $(MAKE) -C lambda/tree-sitter-$(1) libtree-sitter-$(1).a CC="$(CC)" CXX="$(CXX)" $(2) 2>&1) || { printf '%s\n' "$$out"; exit 1; }; \
echo "✅ tree-sitter-$(1) library built"
endef

# Build tree-sitter library (amalgamated build, no ICU dependency)
# Uses lib.c single-file approach - no external ICU/Unicode library needed
$(TREE_SITTER_LIB):
	@echo "Building tree-sitter library (amalgamated, no ICU)..."
	@cd lambda/tree-sitter && \
	rm -f libtree-sitter.a tree_sitter.o && \
	$(CC) -c lib/src/lib.c \
		-Ilib/include \
		-Ilib/src \
		-O3 -Wall -Wextra -std=c11 -fPIC \
		-D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE \
		-o tree_sitter.o && \
	$(AR) rcs libtree-sitter.a tree_sitter.o && \
	rm -f tree_sitter.o
	@echo "✅ tree-sitter library built: lambda/tree-sitter/libtree-sitter.a"

# Build tree-sitter-lambda library (depends on parser generation)
$(TREE_SITTER_LAMBDA_LIB): $(PARSER_C)
	$(call ts_lib_build,lambda,)

# Build tree-sitter-javascript library (depends on scanner source)
$(TREE_SITTER_JAVASCRIPT_LIB): $(JS_SCANNER_C)
	@# regenerate parser.c at ABI 14 (it's gitignored) then build; output is
	@# captured and shown only on failure so success is a single status line.
	@out=$$(env -u OS PATH="/mingw64/bin:$$PATH" $(MAKE) -B -C lambda/tree-sitter-javascript src/parser.c TS="npx tree-sitter-cli@0.24.7" 2>&1 && \
		env -u OS PATH="/mingw64/bin:$$PATH" $(MAKE) -C lambda/tree-sitter-javascript libtree-sitter-javascript.a CC="$(CC)" CXX="$(CXX)" TS="npx tree-sitter-cli@0.24.7" 2>&1) || { printf '%s\n' "$$out"; exit 1; }
	@echo "✅ tree-sitter-javascript library built"

# Build tree-sitter-bash library
$(TREE_SITTER_BASH_LIB):
	$(call ts_lib_build,bash,TS="npx tree-sitter-cli@0.24.7")

# Build tree-sitter-python library
$(TREE_SITTER_PYTHON_LIB):
	$(call ts_lib_build,python,TS="npx tree-sitter-cli@0.24.7")

# Generate TypeScript parser from grammar.js when it changes
$(TS_PARSER_C): $(TS_GRAMMAR_JS)
	@out=$$(cd lambda/tree-sitter-typescript && \
		{ [ -d node_modules/tree-sitter-javascript ] || npm install --save tree-sitter-javascript@file:../tree-sitter-javascript; } && \
		npx tree-sitter-cli@0.24.7 generate 2>&1) || { printf '%s\n' "$$out"; exit 1; }

# Build tree-sitter-typescript library (depends on parser generation + scanner)
$(TREE_SITTER_TYPESCRIPT_LIB): $(TS_PARSER_C) $(TS_SCANNER_C) $(TS_SCANNER_H)
	$(call ts_lib_build,typescript,)

# Build tree-sitter-ruby library
$(TREE_SITTER_RUBY_LIB):
	$(call ts_lib_build,ruby,TS="npx tree-sitter-cli@0.24.7")

# Generate LaTeX parser from grammar.js when it changes
$(LATEX_PARSER_C) $(LATEX_GRAMMAR_JSON) $(LATEX_NODE_TYPES_JSON): $(LATEX_GRAMMAR_JS)
	@out=$$(cd lambda/tree-sitter-latex && npx tree-sitter-cli@0.24.7 generate 2>&1) || { printf '%s\n' "$$out"; exit 1; }

# Build tree-sitter-latex library (depends on parser generation)
$(TREE_SITTER_LATEX_LIB): $(LATEX_PARSER_C)
	$(call ts_lib_build,latex,)

# Generate LaTeX Math parser from grammar.js when it changes
$(LATEX_MATH_PARSER_C): $(LATEX_MATH_GRAMMAR_JS)
	@out=$$(cd lambda/tree-sitter-latex-math && npx tree-sitter-cli@0.24.7 generate 2>&1) || { printf '%s\n' "$$out"; exit 1; }

# Build tree-sitter-latex-math library (depends on parser generation)
$(TREE_SITTER_LATEX_MATH_LIB): $(LATEX_MATH_PARSER_C)
	$(call ts_lib_build,latex-math,)

# Build re2 library (reconfigures cmake if CMakeCache is stale/wrong platform)
# On Windows/CLANG64: must pass explicit compiler flags so cmake uses clang++
# with -stdlib=libc++ (matching the rest of the build) instead of defaulting
# to GCC/libstdc++, which would cause an ABI mismatch at link time.
$(RE2_LIB):
	@echo "Building re2 library from source..."
	@mkdir -p build_temp/re2-noabsl/cmake_build
	@# CMake needs a single executable for CMAKE_*_COMPILER; if ccache is in
	@# use, $(CC)/$(CXX) is "ccache gcc"/"ccache g++" — split into launcher
	@# + real compiler so cmake's compiler-ID probe works.
	@cd build_temp/re2-noabsl/cmake_build && \
		RE2_CC="$(firstword $(filter-out ccache,$(CC)))" ; \
		RE2_CXX="$(firstword $(filter-out ccache,$(CXX)))" ; \
		RE2_LAUNCHER="$(filter ccache,$(firstword $(CC)))" ; \
		cmake .. \
			-DCMAKE_C_COMPILER="$$RE2_CC$(if $(filter yes,$(IS_MSYS2)),.exe,)" \
			-DCMAKE_CXX_COMPILER="$$RE2_CXX$(if $(filter yes,$(IS_MSYS2)),.exe,)" \
			$$( [ -n "$$RE2_LAUNCHER" ] && echo "-DCMAKE_C_COMPILER_LAUNCHER=$$RE2_LAUNCHER -DCMAKE_CXX_COMPILER_LAUNCHER=$$RE2_LAUNCHER" ) \
			-DCMAKE_CXX_FLAGS="$(if $(filter /clang64/bin/clang++,$(CXX)),-stdlib=libc++,) -O2" \
			$(MACOS_CMAKE_FLAGS) \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
			-DBUILD_SHARED_LIBS=OFF \
			-DRE2_BUILD_TESTING=OFF \
			-Wno-dev \
			-G Ninja 2>&1 | tail -3 && \
		ninja -j$(JOBS)
	@echo "re2 library built: $(RE2_LIB)"

# Build MIR JIT library (clone, patch, compile)
$(MIR_LIB): $(MIR_PATCH)
	@echo "Building MIR library..."
	@if [ ! -d "$(MIR_BUILD_DIR)" ]; then \
		echo "Cloning MIR repository..."; \
		mkdir -p $(dir $(MIR_BUILD_DIR)); \
		git clone https://github.com/vnmakarov/mir.git $(MIR_BUILD_DIR); \
	fi
	@if [ -f "$(MIR_PATCH)" ]; then \
		echo "Applying MIR patch $(MIR_PATCH)..."; \
		if git -C "$(MIR_BUILD_DIR)" apply --reverse --check "$(CURDIR)/$(MIR_PATCH)" >/dev/null 2>&1; then \
			echo "  MIR patch already applied — skipping."; \
		elif git -C "$(MIR_BUILD_DIR)" apply --check "$(CURDIR)/$(MIR_PATCH)" >/dev/null 2>&1; then \
			git -C "$(MIR_BUILD_DIR)" apply "$(CURDIR)/$(MIR_PATCH)" && echo "  MIR patch applied."; \
		else \
			echo "ERROR: $(MIR_PATCH) does not apply cleanly and is not already applied;" >&2; \
			echo "       MIR would be built WITHOUT the patch. Aborting." >&2; \
			exit 1; \
		fi; \
	fi
ifeq ($(IS_MSYS2),yes)
	@cd $(MIR_BUILD_DIR) && CC=/clang64/bin/clang.exe AR=/clang64/bin/llvm-ar.exe \
		CFLAGS="-O2 -DNDEBUG -fPIC" make libmir.a
	@mkdir -p win-native-deps/lib && cp $(MIR_BUILD_DIR)/libmir.a win-native-deps/lib/
else
	@$(MAKE) -C $(MIR_BUILD_DIR) -j$(JOBS)
endif
ifeq ($(OS),Linux)
	@echo "Installing MIR to system location (requires sudo)..."
	@sudo mkdir -p /usr/local/lib /usr/local/include
	@sudo cp $(MIR_BUILD_DIR)/libmir.a /usr/local/lib/
	@sudo cp $(MIR_BUILD_DIR)/mir.h /usr/local/include/ 2>/dev/null || true
endif
	@echo "✅ MIR built: $(MIR_LIB)"

build-mir: $(MIR_LIB)

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
	@target="$(if $(1),$(1),lambda.exe)"; \
	if [ -f "$$target" ]; then \
		ldd "$$target" 2>/dev/null | grep -E "not found|mingw64|msys64|ucrt|api-ms-win-crt" || echo "✅ No problematic dependencies found"; \
	else \
		echo "⚠️  $$target not found, skipping DLL check"; \
	fi
endef

# Function to run make with error collection and summary.
# Full clang output goes to temp/build_<TARGET>.log; only errors + a summary
# print to the terminal (keeps agent/CI logs small and readable).
# Usage: $(call run_make_with_error_summary,LABEL[,CONFIG[,EXTRA_FLAGS[,MAKE_TARGET]]])
# LABEL:       log/message label; also the make target unless MAKE_TARGET is set
#              (e.g., lambda, lambda-cli, tests). Log goes to temp/build_<LABEL>.log
# CONFIG:      premake config (default: debug_native; e.g. release_native)
# EXTRA_FLAGS: extra flags/vars appended to the sub-make (e.g. -s CFLAGS="-w")
# MAKE_TARGET: actual make target when it differs from LABEL (e.g. 'all' to build
#              every project while logging under a friendly label)
define run_make_with_error_summary
	@echo "🔨 Building '$(1)' (config: $(if $(2),$(2),debug_native)) — full clang log: temp/build_$(1).log"
	@mkdir -p temp && BUILD_LOG="temp/build_$(1).log" && \
	if $(MAKE) -C build/premake --no-print-directory config=$(if $(2),$(2),debug_native) $(if $(4),$(4),$(1)) -j$(JOBS) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)" LINK_JOBS="$(LINK_JOBS)" $(3) > "$$BUILD_LOG" 2>&1; then \
		BUILD_RC=0; echo "✅ Build completed successfully for target: $(1)"; \
	else \
		BUILD_RC=$$?; echo "❌ Build failed for target: $(1)"; \
	fi; \
	echo ""; \
	echo "📋 Build Summary:"; \
	echo "================"; \
	ERROR_COUNT=`grep -E "(error:|Error:|ERROR:|fatal:|Failed:|failed:)" "$$BUILD_LOG" 2>/dev/null | grep -v -E "warning:|Warning:|WARNING:" | wc -l | tr -d ' '`; \
	WARNING_COUNT=`grep -E "(warning:|Warning:|WARNING:)" "$$BUILD_LOG" 2>/dev/null | wc -l | tr -d ' '`; \
	echo "Errors: $$ERROR_COUNT"; \
	echo "Warnings: $$WARNING_COUNT"; \
	echo ""; \
	if [ "$$ERROR_COUNT" -gt 0 ] 2>/dev/null; then \
		echo "🔴 ERRORS FOUND:"; \
		echo "================"; \
		grep -n -E "(error:|Error:|ERROR:|fatal:|Failed:|failed:)" "$$BUILD_LOG" | grep -v -E "warning:|Warning:|WARNING:" | while IFS=: read -r line_num content; do \
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
	echo "📄 Full build log: temp/build_$(1).log"; \
	exit $$BUILD_RC
endef

# Combined tree-sitter libraries target
# Core: only parsers needed by lambda.exe (Lambda, JS, TS, LaTeX)
tree-sitter-core-libs: $(TREE_SITTER_LIB) $(TREE_SITTER_LAMBDA_LIB) $(TREE_SITTER_JAVASCRIPT_LIB) $(TREE_SITTER_TYPESCRIPT_LIB) $(TREE_SITTER_LATEX_LIB) $(TREE_SITTER_LATEX_MATH_LIB)

# All: includes jube-only parsers (Python, Bash, Ruby)
tree-sitter-libs: tree-sitter-core-libs $(TREE_SITTER_BASH_LIB) $(TREE_SITTER_PYTHON_LIB) $(TREE_SITTER_RUBY_LIB)

# Default target
.DEFAULT_GOAL := build

# Phony targets (don't correspond to actual files)
.PHONY: all build build-ascii clean clean-grammar generate-grammar debug release rebuild \
	    test test-all test-all-baseline test-lambda-baseline test-gc-rooting test-gc-rooting-core test-gc-rooting-python test-bash-baseline test-input-baseline test-radiant-baseline test-layout-baseline test-page-load test-radiant-online test-pdf-render test-extended test-input run help \
	    lambda lambda-cli build-cli lambda-jube build-jube build-lang-python release-lang-python package-standard package-jube verify-jube-package test-jube-module-integrity release-jube format lint lint-full check-code-dup check-lambda-dup check-radiant-dup hosted-python-coupling-inventory check-hosted-python-architecture check-hosted-python-module-boundary docs intellisense analyze-binary \
	    build-debug build-release build-debug-profile build-release-profile clean-all distclean \
	    tree-sitter-libs tree-sitter-core-libs \
	    generate-premake clean-premake build-test build-radiant-baseline build-pdf-render-test build-test-linux build-jube-test test-jube run-radiant-baseline run-layout-baseline-suites \
	    capture-layout test-layout layout layout-snapshot layout-snapshot-check layout-snapshot-diff count-loc tidy-printf benchmark bench-compile \
	    fuzz-lambda fuzz-lambda-extended fuzz-radiant fuzz-radiant-quick type-chart build-mir \
	    ensure-test262-gtest test262-baseline test262-full \
	    test-ui-automation test-reactive-ui test-redex-baseline dom-ui dom-ui-run \
	    build-graph-mermaid-test test-graph-mermaid build-graph-graphviz-test test-graph-graphviz \
	    build-graph-structurizr-test test-graph-structurizr \
	    node-baseline node-regression-gate node-full node-update-baseline node-official-report

# Help target - shows available commands
help:
	@echo "$(PROJECT_NAME) - Available Make Targets:"
	@echo ""
	@echo "Build Targets (Premake-based):"
	@echo "  build         - Build lambda (core) using Premake build system (incremental, default)"
	@echo "                  On Windows/MSYS2: Uses CLANG64 Clang (avoids Universal CRT)"
	@echo "                  All platforms use Clang as the default compiler"
	@echo "  debug         - Build with debug symbols and AddressSanitizer using Premake"
	@echo "  build-release - Build optimized release version using Premake"
	@echo "  build-debug-profile   - Build optimized, symbolized debug profile with JS execution profiling"
	@echo "  build-release-profile - Build optimized release with JS execution profiling enabled"
	@echo "  release       - Build release version and prepare release artifacts"
	@echo "  lambda-cli    - Build headless CLI-only version (release, no Radiant/GUI, outputs lambda-cli.exe)"
	@echo "  build-mir     - Build MIR JIT library (clone, patch, compile)"
	@echo "  build-jube    - Build the standard host plus hosted Python and a compatibility link"
	@echo "  release-jube  - Package the full hosted-language bundle (same host binary)"
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
	@echo "  build-test    - Build all test executables using Premake"
	@echo "  build-radiant-baseline - Build only lambda and the native runners used by test-radiant-baseline"
	@echo "  build-pdf-render-test - Build PDF render visual gtest executable using Premake"
	@echo "  build-jube-test - Build hosted Python compatibility bundle and test executables"
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
	@echo "  test-gc-rooting - Run precise-only forced-GC JIT/interpreter/root-effect gates"
	@echo "  test-bash-baseline - Run Bash transpiler baseline test suite"
	@echo "  test-input-baseline - Run HTML5 WPT, CommonMark, YAML, ASCII Math, and LaTeX Math parser tests"
	@echo "  test-radiant-baseline - Run shared layout baselines ($(LAYOUT_BASELINE_SUITES)) + render visual + other checks"
	@echo "  test-layout-baseline - Run the shared layout baseline suites only"
	@echo "  test-radiant-online - Run Radiant online URL smoke tests"
	@echo "  test-reactive-ui     - Run Reactive UI event simulation tests (todo toggle/delete)"
	@echo "  test-redex-baseline  - Run Redex formal semantics baseline verification"
	@echo "  test-graph-mermaid   - Run Mermaid graph corpus and Lambda integration fixtures"
	@echo "  test-graph-graphviz  - Run DOT parser and Graphviz package integration fixtures"
	@echo "  test-graph-structurizr - Run Structurizr parser and C4 package fixtures"
	@echo "  test-pdf-render - Run PDF render visual gtest suite"
	@echo "  layout-snapshot       - Save page suite snapshot: make layout-snapshot suite=page"
	@echo "  test-extended - Run EXTENDED test suites only (HTTP/HTTPS, ongoing features)"
	@echo "  test-library  - Run library tests only"
	@echo "  test-input    - Run input processing test suite (MIME detection & math)"
	@echo "  test-validator- Run validator tests only"
	@echo "  test-mir      - Run MIR JIT tests only"
	@echo "  test-lambda   - Run lambda runtime tests only"
	@echo "  test-std      - Run Lambda Standard Tests (custom test runner)"
	@echo "  test-coverage - Run tests with code coverage analysis"
	@echo "  test-benchmark- Run performance benchmark tests"
	@echo "  fuzz-lambda    - Run fuzzy tests (5 minutes, mutation + random generation)"
	@echo "  fuzz-lambda-extended - Run extended fuzzy tests (1 hour)"
	@echo "  test-integration - Run end-to-end integration tests"
	@echo "  test-all      - Run complete test suite (all test types)"

	@echo "  run           - Build and run the default executable"
	@echo "  analyze       - Run static analysis with scan-build (fixed for custom build)"
	@echo "  analyze-verbose - Run detailed static analysis with extra checkers"
	@echo "  analyze-single - Run static analysis on individual files"
	@echo "  analyze-direct - Direct clang static analysis (bypasses build system)"
	@echo "  analyze-compile-db - Use compile_commands.json for analysis (requires bear)"
	@echo "  tidy-printf   - Convert printf/fprintf(stderr) to log_debug() using Clang AST"
	@echo "                  Usage: make tidy-printf FILE='pattern' [DRY_RUN=1] [BACKUP=1]"
	@echo "  format        - Format source code with clang-format"
	@echo "  lint          - Unified policy linter, fast pass (ast-grep + alint + hybrid + structural)"
	@echo "                  Usage: make lint [ARGS='--rule <id>' | --report | --list]   ~10 s"
	@echo "  lint-full     - Same plus the clang-tidy backend (slow, comprehensive)"
	@echo "                  Usage: make lint-full [ARGS=--report]                         ~4 min"
	@echo "  check-code-dup    - Check lib, lambda, and radiant for duplicate code"
	@echo "  check-lambda-dup  - Check lambda for duplicate code"
	@echo "  check-radiant-dup - Check radiant for duplicate code"
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
build: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-core-libs $(RE2_LIB) $(MIR_LIB)
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
	@$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
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
# build-release compiles the optimized lambda.exe.
# prepare-release packages an existing release binary plus assets/docs into ./release/.
release: build-release
	@$(MAKE) prepare-release

prepare-release:
	@bash utils/prepare_release.sh

build-release:
	@$(MAKE) clean-all
	@$(MAKE) build-release-compile

build-release-compile: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-core-libs $(RE2_LIB)
	@echo "Building release version using Premake build system..."
	@echo "Optimizations: LTO, dead code elimination, symbol visibility, stripped logging"
	$(call toolchain_verify)
	@echo "Generating Premake configuration..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	@echo "Generating makefiles..."
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building lambda executable (release) with $(JOBS) parallel jobs..."
	$(call run_make_with_error_summary,lambda,release_native)
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

build-release-profile: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-core-libs $(RE2_LIB)
	@echo "Building release_profile version using Premake build system..."
	@echo "Optimizations: LTO, dead code elimination, JS execution profiling enabled"
	$(call toolchain_verify)
	@echo "Generating Premake configuration..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	@echo "Generating makefiles..."
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building lambda executable (release_profile) with $(JOBS) parallel jobs..."
	$(call run_make_with_error_summary,lambda,release_profile_native)
	@echo "Release profile build completed."
	@ls -lh lambda-profile.exe 2>/dev/null || true
	$(call windows_dll_check,lambda-profile.exe)

# Keep regular debug free of profiler hooks so its runtime cost reflects only
# debugging and sanitizer instrumentation; use this target to collect JS profiles.
build-debug-profile: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) tree-sitter-core-libs $(RE2_LIB)
	@echo "Building debug_profile version using Premake build system..."
	@echo "Optimizations: O3 with symbols, frame pointers, JS execution profiling"
	$(call toolchain_verify)
	@echo "Generating Premake configuration..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	@echo "Generating makefiles..."
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@echo "Building lambda executable (debug_profile) with $(JOBS) parallel jobs..."
	$(call run_make_with_error_summary,lambda,debug_profile_native)
	@echo "Debug profile build completed. Executable: lambda-debug-profile.exe"
	@ls -lh lambda-debug-profile.exe 2>/dev/null || true
	$(call windows_dll_check,lambda-debug-profile.exe)

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
	$(call run_make_with_error_summary,lambda-cli,release_native,-s CFLAGS="-w" CXXFLAGS="-w")
ifeq ($(OS),Darwin)
	@strip -x lambda-cli.exe 2>/dev/null || true
else
	@strip lambda-cli.exe 2>/dev/null || true
endif
	@echo "✅ CLI build completed. Executable: lambda-cli.exe"
	@ls -lh lambda-cli.exe 2>/dev/null || true

# Compatibility name for the former polyglot executable. It is a link to the
# normal host, never a separately compiled runtime.
lambda-jube: build-jube

build-jube: build build-lang-python
	@ln -sfn lambda.exe lambda-jube.exe
	@echo "✅ Jube compatibility name points to the standard host: lambda-jube.exe -> lambda.exe"

# Hosted Python is a separately loaded native module. It is never a dependency
# of the standard host build, so Python stays absent unless this target is run.
build-lang-python: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) $(TREE_SITTER_PYTHON_LIB)
	@echo "Building external lang-python hosted module..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	$(MAKE) -C build/premake config=debug_native lang-python -j$(JOBS) CC="$(CC)" CXX="$(CXX)" --no-print-directory -s CFLAGS="-w" CXXFLAGS="-w"
	$(PYTHON) utils/update_jube_manifest_integrity.py modules/lang-python
	@ls -lh modules/lang-python/lang-python.dylib modules/lang-python/lang-python.so modules/lang-python/lang-python.dll 2>/dev/null || true

# The release language module is built independently, then copied next to the
# full distribution's unchanged host executable.  The standard bundle never
# depends on this target.
release-lang-python: $(TS_ENUM_H) $(LAMBDA_EMBED_H_FILE) $(TREE_SITTER_PYTHON_LIB)
	@echo "Building release lang-python hosted module..."
	$(PYTHON) utils/generate_premake.py --output $(PREMAKE_FILE)
	$(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	$(MAKE) -C build/premake config=release_native lang-python -j$(JOBS) CC="$(CC)" CXX="$(CXX)" --no-print-directory -s CFLAGS="-w" CXXFLAGS="-w"
	$(PYTHON) utils/update_jube_manifest_integrity.py modules/lang-python
	@mkdir -p release/modules/lang-python
	@cp modules/lang-python/module.json release/modules/lang-python/module.json
	@cp modules/lang-python/lang-python.dylib modules/lang-python/lang-python.so modules/lang-python/lang-python.dll release/modules/lang-python/ 2>/dev/null || true

# Standard and full Jube packages deliberately reuse the identical host.  The
# full package adds language modules; it never recompiles a second runtime.
package-standard: release
	@mkdir -p release-standard
	@cp release/lambda release-standard/lambda
	# Keep a manifest-only descriptor so a missing optional language has a
	# deterministic diagnostic without putting its grammar or native code in the host.
	@mkdir -p release-standard/modules/lang-python
	@cp modules/lang-python/module.json release-standard/modules/lang-python/module.json

package-jube: package-standard release-lang-python
	@mkdir -p release-jube
	@cp release-standard/lambda release-jube/lambda
	@mkdir -p release-jube/modules/lang-python
	@cp release/modules/lang-python/module.json release-jube/modules/lang-python/module.json
	@cp release/modules/lang-python/lang-python.dylib release/modules/lang-python/lang-python.so release/modules/lang-python/lang-python.dll release-jube/modules/lang-python/ 2>/dev/null || true

verify-jube-package: package-jube
	@shasum -a 256 release-standard/lambda release-jube/lambda
	@cmp -s release-standard/lambda release-jube/lambda
	@mkdir -p temp/hosted-python-package-check
	@if cd temp/hosted-python-package-check && ../../release-standard/lambda py ../../test/py/test_py_basic.py --no-log >standard.out 2>standard.err; then \
		echo "standard bundle unexpectedly loaded lang-python"; exit 1; \
	fi
	@rg -q "Hosted language module for 'py' is unavailable or incompatible\." temp/hosted-python-package-check/standard.err
	@cd release-jube && ./lambda py ../test/py/test_py_basic.py --no-log >/dev/null

# The loader must reject bytes that no longer match the manifest digest before
# dlopen can execute module initializers. Work only on a disposable copied bundle.
test-jube-module-integrity: build build-lang-python
	@mkdir -p temp/jube-integrity/lang-python
	@cp modules/lang-python/module.json temp/jube-integrity/lang-python/module.json
	@cp modules/lang-python/lang-python.dylib modules/lang-python/lang-python.so modules/lang-python/lang-python.dll temp/jube-integrity/lang-python/ 2>/dev/null || true
	@library_path=$$(find temp/jube-integrity/lang-python -maxdepth 1 -type f \( -name 'lang-python.dylib' -o -name 'lang-python.so' -o -name 'lang-python.dll' \) | head -n 1); \
	if [ -z "$$library_path" ]; then echo "missing copied lang-python library"; exit 1; fi; \
	printf x | dd of="$$library_path" bs=1 seek=0 conv=notrunc status=none
	@if JUBE_MODULE_PATH=./temp/jube-integrity ./lambda.exe py test/py/test_py_basic.py --no-log > temp/jube-integrity/stdout 2> temp/jube-integrity/stderr; then \
		echo "tampered lang-python library was accepted"; exit 1; \
	fi
	@rg -q "Hosted language module for 'py' is unavailable or incompatible\." temp/jube-integrity/stderr

release-jube: package-jube
	@ln -sfn lambda release-jube/lambda-jube
	@echo "✅ Full Jube bundle uses the standard host plus modules."

# Build the compatibility host/module bundle and all test executables.
build-jube-test: build-jube build-test

# Run hosted Python through the compatibility host name.
test-jube: build-jube-test
	@LAMBDA_PY_HOST_EXE=./lambda-jube.exe ./test/test_py_gtest.exe

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
	@rm -f $(LAMBDA_PROFILE_EXE)
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

# Generate TypeScript grammar explicitly
generate-grammar-typescript: $(TS_PARSER_C)
	@echo "TypeScript grammar generation complete."

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
	@rm -f lambda/tree-sitter-bash/libtree-sitter-bash.a lambda/tree-sitter-bash/src/*.o
	@rm -f lambda/tree-sitter-python/libtree-sitter-python.a lambda/tree-sitter-python/src/*.o
	@rm -f lambda/tree-sitter-typescript/libtree-sitter-typescript.a lambda/tree-sitter-typescript/src/*.o
	@rm -f lambda/tree-sitter-ruby/libtree-sitter-ruby.a lambda/tree-sitter-ruby/src/*.o
	@rm -f lambda/tree-sitter-latex/libtree-sitter-latex.a lambda/tree-sitter-latex/src/*.o
	@rm -f lambda/tree-sitter-latex-math/libtree-sitter-latex-math.a lambda/tree-sitter-latex-math/src/*.o
	@rm -rf build_temp/re2-noabsl/cmake_build
	@echo "All build directories and tree-sitter libraries cleaned."

distclean: clean-all clean-grammar clean-test
	@echo "Complete cleanup..."
	@rm -f $(LAMBDA_EXE)
	@rm -f $(LAMBDA_PROFILE_EXE)
	@rm -f lambda_debug.exe
	@rm -f lambda_release.exe
	@rm -f lambda-windows.exe
	@rm -f lambda-linux.exe
	@rm -f temp/_transpiled*.c
	@rm -f *.exe
	@echo "Complete cleanup finished."

# Development targets
test: build-test
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@echo "Running lambda (core) test suites (excluding jube)..."
	@LAMBDA_TEST_HEAVY_LOAD=1 node test/test_run.js --exclude-target=jube --exclude-test=test_radiant_online_view_gtest --parallel

test-all: build-test
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@echo "Running ALL test suites (baseline + extended)..."
	@LAMBDA_TEST_HEAVY_LOAD=1 node test/test_run.js --parallel

test-all-baseline: build-test
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@echo "Running BASELINE test suites only..."
	@LAMBDA_TEST_HEAVY_LOAD=1 node test/test_run.js --category=baseline --parallel

test-lambda-baseline: build-test test-input-baseline
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@echo "Running LAMBDA baseline test suite..."
	@LAMBDA_TEST_HEAVY_LOAD=1 node test/test_run.js --target=lambda --category=baseline --parallel --input-results=test_output/input_baseline_results.json

# Permanent exact-root CI lane. Collection is injected only at public allocator
# boundaries, and poison makes a missed root deterministic instead of relying
# on a stale native-stack word or recycled slab contents.
#
# Keep the core lane independent of an optional hosted language.  This lets the
# standard host prove its root invariants without building or initializing
# Python, while the aggregate target preserves the historical full check.
test-gc-rooting-core: build
	@mkdir -p temp
	@echo "Running LambdaJS JIT precise-only forced-GC gate..."
	@LAMBDA_GC_ROOT_MODE=precise-only LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 \
		./lambda.exe js --no-log test/js/regression_side_stack_frame_gc.js > temp/gc_rooting_js_jit.txt
	@diff -u test/js/regression_side_stack_frame_gc.txt temp/gc_rooting_js_jit.txt
	@echo "Running LambdaJS deterministic randomized forced-GC gate..."
	@LAMBDA_GC_ROOT_MODE=precise-only LAMBDA_GC_FORCE_SEED=1592594996 LAMBDA_GC_FORCE_ONE_IN=3 LAMBDA_GC_POISON_FREED=1 \
		./lambda.exe js --no-log test/js/regression_side_stack_frame_gc.js > temp/gc_rooting_js_random.txt
	@diff -u test/js/regression_side_stack_frame_gc.txt temp/gc_rooting_js_random.txt
	@echo "Running LambdaJS MIR-interpreter precise-only forced-GC gate..."
	@LAMBDA_GC_ROOT_MODE=precise-only LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 \
		./lambda.exe js --mir-interp --no-log test/js/regression_side_stack_frame_gc.js > temp/gc_rooting_js_interp.txt
	@diff -u test/js/regression_side_stack_frame_gc.txt temp/gc_rooting_js_interp.txt
	@echo "Running Lambda MIR-Direct precise-only forced-GC gate..."
	@LAMBDA_GC_ROOT_MODE=precise-only LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 \
		./lambda.exe run --no-log test/lambda/proc/proc_var.ls > temp/gc_rooting_lambda.txt
	@diff -u test/lambda/proc/proc_var.txt temp/gc_rooting_lambda.txt
	@echo "Running number-model exact-conversion forced-GC gates..."
	@LAMBDA_GC_ROOT_MODE=precise-only LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 \
		./lambda.exe --no-log test/lambda/number_model_realign.ls > temp/gc_rooting_number_model_jit.txt
	@diff -u test/lambda/number_model_realign.txt temp/gc_rooting_number_model_jit.txt
	@LAMBDA_GC_ROOT_MODE=precise-only LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 \
		./lambda.exe --mir-interp --no-log test/lambda/number_model_realign.ls > temp/gc_rooting_number_model_interp.txt
	@diff -u test/lambda/number_model_realign.txt temp/gc_rooting_number_model_interp.txt
	@LAMBDA_GC_ROOT_MODE=precise-only LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 \
		./lambda.exe run --no-log test/lambda/proc/proc_number_model_realign.ls > temp/gc_rooting_number_model_proc.txt
	@diff -u -b -B test/lambda/proc/proc_number_model_realign.txt temp/gc_rooting_number_model_proc.txt
	@python3 utils/check_gc_effects.py
	@python3 utils/check_gc_root_hazards.py
	@echo "Core precise-root forced-GC gates passed."

test-gc-rooting-python: build build-lang-python
	@mkdir -p temp
	@echo "Running LambdaPy closure precise-only forced-GC gate..."
	@LAMBDA_GC_ROOT_MODE=precise-only LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 \
		$${LAMBDA_PY_HOST_EXE:-./lambda.exe} py --no-log test/py/test_py_closures.py > temp/gc_rooting_py_closures.txt
	@diff -u test/py/test_py_closures.txt temp/gc_rooting_py_closures.txt
	@echo "Running LambdaPy generator precise-only forced-GC gate..."
	@LAMBDA_GC_ROOT_MODE=precise-only LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 \
		$${LAMBDA_PY_HOST_EXE:-./lambda.exe} py --no-log test/py/test_py_generators.py > temp/gc_rooting_py_generators.txt
	@diff -u test/py/test_py_generators.txt temp/gc_rooting_py_generators.txt
	@echo "Hosted-Python precise-root forced-GC gates passed."

test-gc-rooting: test-gc-rooting-core test-gc-rooting-python
	@echo "Aggregate precise-root forced-GC gates passed."

test-redex-baseline: build
	@echo "Running Redex formal semantics baseline verification..."
	@cd lambda/semantics && racket baseline-verify.rkt

test-bash-baseline: build-jube-test
	@echo "Running Bash transpiler baseline tests (requires lambda-jube)..."
	@./test/test_bash_run_gtest.exe

# Build only the js262 gtest runner if it is missing; full build-test is too broad here.
ensure-test262-gtest:
	@if [ ! -x ./test/test_js_test262_gtest.exe ]; then \
		echo "test/test_js_test262_gtest.exe not found; building js262 gtest runner only..."; \
		mkdir -p build/premake; \
		$(MAKE) generate-premake; \
		PATH="/clang64/bin:$$PATH" $(PREMAKE5) gmake --file=$(PREMAKE_FILE); \
		PATH="/clang64/bin:$$PATH" $(MAKE) -C build/premake config=debug_native test_js_test262_gtest -j$(TEST_JOBS) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)"; \
	else \
		echo "Using existing test/test_js_test262_gtest.exe"; \
	fi

# test262 baseline: run only tests in baseline, must pass 100%
# Add VERBOSE=1 to dump per-batch timing (top 20/top 5) and top-20 memory growth.
test262-baseline: ensure-test262-gtest
	@echo "Running test262 baseline ($(shell wc -l < test/js262/test262_baseline.txt | tr -d ' ') entries)..."
	@echo "Ensuring release lambda.exe for js262 runtime performance..."
	@$(MAKE) build-release-compile
	@./test/test_js_test262_gtest.exe --baseline-only --batch-only --run-async --async-list=test/js262/test262_baseline.txt $(if $(VERBOSE),--verbose)

# test262 full: run all discovered test262 tests (slow, ~5min)
test262-full: ensure-test262-gtest
	@echo "Running full test262 suite..."
	@echo "Ensuring release lambda.exe for js262 runtime performance..."
	@$(MAKE) build-release-compile
	@./test/test_js_test262_gtest.exe --batch-only --run-async --async-list=test/js262/test262_baseline.txt $(if $(VERBOSE),--verbose)

# test262 update baseline: run all tests and update baseline with current passing set
test262-update-baseline: build-test
	@echo "Running full test262 suite and updating baseline..."
	@echo "Ensuring release lambda.exe for js262 runtime performance..."
	@$(MAKE) build-release-compile
	@./test/test_js_test262_gtest.exe --batch-only --run-async --async-list=test/js262/test262_baseline.txt --update-baseline $(if $(VERBOSE),--verbose)

# Node.js official: install Lambda-compatible shims for test harness
node-shim:
	@echo "Installing Lambda test shims into ref/node/test/common/..."
	@if [ -d ref/node/test/common ]; then \
		if [ ! -f ref/node/test/common/index.js.orig ]; then \
			cp ref/node/test/common/index.js ref/node/test/common/index.js.orig; \
		fi; \
		if [ ! -f ref/node/test/common/tmpdir.js.orig ]; then \
			cp ref/node/test/common/tmpdir.js ref/node/test/common/tmpdir.js.orig; \
		fi; \
		if [ ! -f ref/node/test/common/fixtures.js.orig ]; then \
			cp ref/node/test/common/fixtures.js ref/node/test/common/fixtures.js.orig; \
		fi; \
		if [ ! -f ref/node/test/common/internet.js.orig ]; then \
			cp ref/node/test/common/internet.js ref/node/test/common/internet.js.orig; \
		fi; \
		if [ ! -f ref/node/test/common/wpt.js.orig ]; then \
			cp ref/node/test/common/wpt.js ref/node/test/common/wpt.js.orig; \
		fi; \
		if [ ! -f ref/node/test/common/dns.js.orig ]; then \
			cp ref/node/test/common/dns.js ref/node/test/common/dns.js.orig; \
		fi; \
		if [ ! -f ref/node/test/common/crypto.js.orig ]; then \
			cp ref/node/test/common/crypto.js ref/node/test/common/crypto.js.orig; \
		fi; \
		cp lambda/js/test_shim/common_index.js ref/node/test/common/index.js; \
		cp lambda/js/test_shim/tmpdir.js ref/node/test/common/tmpdir.js; \
		cp lambda/js/test_shim/fixtures.js ref/node/test/common/fixtures.js; \
		cp lambda/js/test_shim/internet.js ref/node/test/common/internet.js; \
		cp lambda/js/test_shim/wpt.js ref/node/test/common/wpt.js; \
		cp lambda/js/test_shim/dns.js ref/node/test/common/dns.js; \
		cp lambda/js/test_shim/crypto.js ref/node/test/common/crypto.js; \
		cp lambda/js/test_shim/package.json ref/node/test/common/package.json; \
		echo "Shims installed."; \
	else \
		echo "ERROR: ref/node/test/common/ not found. Clone Node.js repo first."; \
		exit 1; \
	fi

# Node.js official: restore original common module
node-shim-restore:
	@echo "Restoring original ref/node/test/common/..."
	@if [ -f ref/node/test/common/index.js.orig ]; then \
		mv ref/node/test/common/index.js.orig ref/node/test/common/index.js; \
		mv ref/node/test/common/tmpdir.js.orig ref/node/test/common/tmpdir.js; \
		mv ref/node/test/common/fixtures.js.orig ref/node/test/common/fixtures.js; \
		if [ -f ref/node/test/common/internet.js.orig ]; then \
			mv ref/node/test/common/internet.js.orig ref/node/test/common/internet.js; \
		fi; \
		if [ -f ref/node/test/common/wpt.js.orig ]; then \
			mv ref/node/test/common/wpt.js.orig ref/node/test/common/wpt.js; \
		fi; \
		if [ -f ref/node/test/common/dns.js.orig ]; then \
			mv ref/node/test/common/dns.js.orig ref/node/test/common/dns.js; \
		fi; \
		if [ -f ref/node/test/common/crypto.js.orig ]; then \
			mv ref/node/test/common/crypto.js.orig ref/node/test/common/crypto.js; \
		fi; \
		echo "Originals restored."; \
	else \
		echo "No backup found — nothing to restore."; \
	fi

# Node.js official test suite: run official Node.js tests from ref/node/test/parallel/
node-baseline: build-test node-shim
	@echo "Running Node.js official test suite..."
	@./test/test_node_gtest.exe --baseline-only

# Node.js official: fast regression gate for currently locked passing tests
node-regression-gate: build-test node-shim
	@echo "Running Node.js official regression gate..."
	@./test/test_node_gtest.exe --baseline-only --gtest_brief=1

# Node.js official: run full enabled official Node.js test sweep
node-full: build-test node-shim
	@echo "Running full Node.js official test suite..."
	@./test/test_node_gtest.exe

# Node.js official: update baseline with current passing set
node-update-baseline: build-test node-shim
	@echo "Running Node.js official test suite and updating baseline..."
	@./test/test_node_gtest.exe --update-baseline

# Node.js official: generate inventory/report from baseline plus latest temp run files
node-official-report:
	@$(PYTHON) -B test/node/node_official_report.py

ensure-yaml-submodule:
	@if [ ! -f test/yaml/README.md ]; then \
		echo "Initializing test/yaml submodule..."; \
		git submodule update --init test/yaml; \
	fi

test-input-baseline: build-test ensure-yaml-submodule
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@mkdir -p test_output
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
	echo "=============================================================="; \
	echo "{\"total_passed\":$$total_passed,\"total_failed\":$$total_failed,\"suites\":[{\"name\":\"HTML5 WPT Parser\",\"passed\":$$wpt_passed,\"failed\":$$wpt_failed},{\"name\":\"CommonMark Markdown\",\"passed\":$$md_passed,\"failed\":$$md_failed},{\"name\":\"YAML Suite\",\"passed\":$$yaml_passed,\"failed\":$$yaml_failed},{\"name\":\"ASCII Math\",\"passed\":$$math_passed,\"failed\":$$math_failed},{\"name\":\"LaTeX Math\",\"passed\":$$latex_math_passed,\"failed\":$$latex_math_failed}]}" > test_output/input_baseline_results.json

# Layout baseline suites shared by test-radiant-baseline and test-layout-baseline.
LAYOUT_BASELINE_SUITES ?= baseline form wpt-css-text wpt-css-inline wpt-css-images wpt-css-multicol puppertino markdown
LAYOUT_BASELINE_RUNNER = $(LAYOUT_TEST_ENV) node test/layout/test_radiant_layout.js --baseline-only
LAYOUT_BASELINE_RESULTS = temp/_layout_baseline_results.txt

# Run the shared layout baseline inventory without building. Suites with a
# recorded baseline run only those entries; suites without one run in full.
run-layout-baseline-suites:
	@mkdir -p temp; \
	> $(LAYOUT_BASELINE_RESULTS); \
	any_failed=0; \
	echo ""; \
	echo "📦 Layout Baseline Tests:"; \
	for suite in $(LAYOUT_BASELINE_SUITES); do \
		echo ""; \
		echo "  ▸ $$suite:"; \
		layout_log="temp/_layout_baseline_$${suite}.log"; \
		rm -f "$$layout_log"; \
		layout_exit=0; \
		s_start=$$(date +%s); \
		$(LAYOUT_BASELINE_RUNNER) -c "$$suite" > "$$layout_log" 2>&1 || layout_exit=$$?; \
		s_elapsed=$$(($$(date +%s) - s_start)); \
		grep -E "Baseline-only:|No recorded baseline" "$$layout_log" | head -1 || true; \
		grep -A5 "Summary:" "$$layout_log" | tail -6 || true; \
		grep "Baseline Regressions" "$$layout_log" | head -1 || true; \
		s_passed=$$(grep "   ✅ Successful:" "$$layout_log" | grep -oE "[0-9]+" | head -1); s_passed=$${s_passed:-0}; \
		s_partial=$$(grep "   ⚠️  Partially Passing:" "$$layout_log" | grep -oE "[0-9]+" | head -1); s_partial=$${s_partial:-0}; \
		s_skipped=$$(grep "Skipped:" "$$layout_log" | grep -oE "[0-9]+" | head -1); s_skipped=$${s_skipped:-0}; \
		s_failed=$$(grep "   ❌ Failed:" "$$layout_log" | grep -oE "[0-9]+" | head -1); s_failed=$${s_failed:-0}; \
		s_status="✅ PASS"; \
		if grep -q "Baseline Regressions" "$$layout_log"; then \
			s_status="❌ FAIL"; any_failed=1; \
		elif [ $$layout_exit -ne 0 ] || ! grep -q "   ✅ Successful:" "$$layout_log"; then \
			if [ $$s_failed -eq 0 ]; then s_failed=1; fi; \
			s_status="❌ FAIL"; any_failed=1; \
		fi; \
		echo "$$suite|$$s_status|$$s_passed|$$s_partial|$$s_failed|$$s_skipped|$$s_elapsed" >> $(LAYOUT_BASELINE_RESULTS); \
		rm -f "$$layout_log"; \
	done; \
	if [ $$any_failed -gt 0 ]; then exit 1; fi

test-radiant-baseline: build-radiant-baseline
	@$(MAKE) --no-print-directory run-radiant-baseline

# Run radiant tests without rebuilding (use when test executables are already built).
# Commands wait directly on their child process; periodic polling used to round
# every short phase up to the next second and accumulated substantial tail time.
# Suite boundaries are timed here because the heterogeneous runners do not expose
# a common elapsed-time field in their result summaries.
# Layout details are retained in the parent shell because later child processes
# reuse the shared temp directory and may remove earlier runner artifacts.
# A crashed layout category must fail the gate instead of disappearing from its totals.
run-radiant-baseline:
	@ui_passed=0; ui_failed=0; ui_status="⏭️  SKIP"; \
	ui_elapsed=0; \
	dom_ui_passed=0; dom_ui_failed=0; dom_ui_status="⏭️  SKIP"; \
	dom_ui_elapsed=0; \
	radiant_view_passed=0; radiant_view_failed=0; radiant_view_status="⏭️  SKIP"; \
	radiant_view_elapsed=0; \
	page_passed=0; page_failed=0; page_status="⏭️  SKIP"; \
	page_elapsed=0; \
	fuzzy_passed=0; fuzzy_failed=0; fuzzy_status="⏭️  SKIP"; \
	fuzzy_elapsed=0; \
	render_passed=0; render_failed=0; render_total=0; render_xpassed=0; render_xfailed=0; render_skipped=0; render_errors=0; render_regressions=0; render_details="not run"; render_status="⏭️  SKIP"; \
	render_elapsed=0; \
	snapshot_passed=0; snapshot_failed=0; snapshot_status="⏭️  SKIP"; \
	snapshot_elapsed=0; \
	any_failed=0; \
	layout_total_passed=0; layout_total_partial=0; layout_total_failed=0; layout_total_skipped=0; \
	layout_elapsed=0; \
	layout_overall_status="✅ PASS"; \
	mkdir -p temp; \
	format_duration() { \
		seconds="$$1"; \
		if [ "$$seconds" -ge 3600 ]; then \
			printf "%dh%dm%ds" $$((seconds / 3600)) $$(((seconds % 3600) / 60)) $$((seconds % 60)); \
		elif [ "$$seconds" -ge 60 ]; then \
			printf "%dm%ds" $$((seconds / 60)) $$((seconds % 60)); \
		else \
			printf "%ds" "$$seconds"; \
		fi; \
	}; \
	run_logged() { \
		log_file="$$1"; shift; \
		rm -f "$$log_file"; \
		run_logged_start=$$(date +%s); \
		"$$@" > "$$log_file" 2>&1; \
		run_logged_exit=$$?; \
		run_logged_elapsed=$$(($$(date +%s) - run_logged_start)); \
		return $$run_logged_exit; \
	}; \
	\
	echo ""; \
	echo "=============================================================="; \
	echo "🧪 RADIANT BASELINE TEST SUITE"; \
	echo "=============================================================="; \
	\
	layout_run_exit=0; \
	layout_start=$$(date +%s); \
	$(MAKE) --no-print-directory run-layout-baseline-suites || layout_run_exit=$$?; \
	layout_elapsed=$$(($$(date +%s) - layout_start)); \
	layout_results=$$(cat $(LAYOUT_BASELINE_RESULTS)); \
	while IFS='|' read -r suite s_status s_passed s_partial s_failed s_skipped s_elapsed; do \
		layout_total_passed=$$((layout_total_passed + s_passed)); \
		layout_total_partial=$$((layout_total_partial + s_partial)); \
		layout_total_failed=$$((layout_total_failed + s_failed)); \
		layout_total_skipped=$$((layout_total_skipped + s_skipped)); \
		if [ "$$s_status" = "❌ FAIL" ]; then layout_overall_status="❌ FAIL"; any_failed=1; fi; \
	done < $(LAYOUT_BASELINE_RESULTS); \
	rm -f $(LAYOUT_BASELINE_RESULTS); \
	if [ $$layout_run_exit -ne 0 ]; then layout_overall_status="❌ FAIL"; any_failed=1; fi; \
	\
	if [ -f test/layout/snapshot/page.json ]; then \
		echo ""; \
		echo "📦 Layout Page Suite Regression:"; \
		snapshot_start=$$(date +%s); \
		snap_output=$$($(LAYOUT_TEST_ENV) node test/layout/test_radiant_layout.js --engine lambda-css -c page --json -j 5 2>/dev/null \
			| node test/layout/layout_suite_snapshot.js --check page 2>&1) || true; \
		snapshot_elapsed=$$(($$(date +%s) - snapshot_start)); \
		echo "$$snap_output" | tail -5; \
		snapshot_passed=$$(echo "$$snap_output" | grep "Current:" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		snapshot_passed=$${snapshot_passed:-0}; \
		if echo "$$snap_output" | grep -q "REGRESSION\|regression detected"; then \
			snapshot_status="❌ FAIL"; snapshot_failed=1; any_failed=1; \
		elif echo "$$snap_output" | grep -q "snapshot check passed\|No regressions\|No average regression"; then \
			snapshot_status="✅ PASS"; \
		else \
			snapshot_status="✅ PASS"; \
		fi; \
	fi; \
	\
	echo ""; \
	echo "📦 UI Automation Tests:"; \
	if [ -f "test/test_ui_automation_gtest.exe" ]; then \
		run_logged "temp/_radiant_ui_automation.log" ./test/test_ui_automation_gtest.exe || true; \
		ui_elapsed=$$run_logged_elapsed; \
		output=$$(cat "temp/_radiant_ui_automation.log"); \
		echo "$$output" | grep -E "^\[|tests executed" | tail -5; \
		ui_passed=$$(echo "$$output" | grep -E "^\[  PASSED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		ui_failed=$$(echo "$$output" | grep -E "^\[  FAILED  \][[:space:]]+[0-9]+ test" | head -1 | grep -oE "[0-9]+" | head -1 || echo "0"); \
		ui_passed=$${ui_passed:-0}; ui_failed=$${ui_failed:-0}; \
		if [ "$$ui_failed" = "0" ] || [ -z "$$ui_failed" ]; then ui_status="✅ PASS"; ui_failed=0; else ui_status="❌ FAIL"; any_failed=1; fi; \
	else \
		echo "   ⚠️  test/test_ui_automation_gtest.exe not found"; \
	fi; \
	\
	echo ""; \
	echo "📦 Radiant View Command Tests:"; \
	if [ -f "test/test_radiant_view_gtest.exe" ]; then \
		run_logged "temp/_radiant_view_cmd.log" ./test/test_radiant_view_gtest.exe || true; \
		radiant_view_elapsed=$$run_logged_elapsed; \
		output=$$(cat "temp/_radiant_view_cmd.log"); \
		echo "$$output" | grep -E "^\[|tests executed" | tail -5; \
		radiant_view_passed=$$(echo "$$output" | grep -E "^\[  PASSED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		radiant_view_failed=$$(echo "$$output" | grep -E "^\[  FAILED  \][[:space:]]+[0-9]+ test" | head -1 | grep -oE "[0-9]+" | head -1 || echo "0"); \
		radiant_view_passed=$${radiant_view_passed:-0}; radiant_view_failed=$${radiant_view_failed:-0}; \
		if [ "$$radiant_view_failed" = "0" ] || [ -z "$$radiant_view_failed" ]; then radiant_view_status="✅ PASS"; radiant_view_failed=0; else radiant_view_status="❌ FAIL"; any_failed=1; fi; \
	else \
		echo "   ⚠️  test/test_radiant_view_gtest.exe not found"; \
	fi; \
	\
	echo ""; \
	echo "📦 View Page and Markdown (Headless) Tests:"; \
	if [ -f "test/test_page_load_gtest.exe" ]; then \
		run_logged "temp/_radiant_page_load.log" ./test/test_page_load_gtest.exe || true; \
		page_elapsed=$$run_logged_elapsed; \
		output=$$(cat "temp/_radiant_page_load.log"); \
		echo "$$output" | grep -E "^\[|pages loaded" | tail -5; \
		page_passed=$$(echo "$$output" | grep -E "^\[  PASSED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		page_failed=$$(echo "$$output" | grep -E "^\[  FAILED  \][[:space:]]+[0-9]+ test" | head -1 | grep -oE "[0-9]+" | head -1 || echo "0"); \
		page_passed=$${page_passed:-0}; page_failed=$${page_failed:-0}; \
		if [ "$$page_failed" = "0" ] || [ -z "$$page_failed" ]; then page_status="✅ PASS"; page_failed=0; else page_status="❌ FAIL"; any_failed=1; fi; \
	else \
		echo "   ⚠️  test/test_page_load_gtest.exe not found"; \
	fi; \
	\
	echo ""; \
	echo "📦 Fuzzy Crash Tests:"; \
	if [ -f "test/test_layout_fuzzy_gtest.exe" ]; then \
		fuzzy_start=$$(date +%s); \
		output=$$(./test/test_layout_fuzzy_gtest.exe 2>&1) || true; \
		fuzzy_elapsed=$$(($$(date +%s) - fuzzy_start)); \
		echo "$$output" | grep -E "^\[|fuzzy files tested" | tail -5; \
		fuzzy_passed=$$(echo "$$output" | grep -E "^\[  PASSED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		fuzzy_failed=$$(echo "$$output" | grep -E "^\[  FAILED  \][[:space:]]+[0-9]+ test" | head -1 | grep -oE "[0-9]+" | head -1 || echo "0"); \
		fuzzy_passed=$${fuzzy_passed:-0}; fuzzy_failed=$${fuzzy_failed:-0}; \
		if [ "$$fuzzy_failed" = "0" ] || [ -z "$$fuzzy_failed" ]; then fuzzy_status="✅ PASS"; fuzzy_failed=0; else fuzzy_status="❌ FAIL"; any_failed=1; fi; \
	else \
		echo "   ⚠️  test/test_layout_fuzzy_gtest.exe not found"; \
	fi; \
	\
	echo ""; \
	echo "📦 Render Visual Tests:"; \
	if [ -f "test/render/test_radiant_render.js" ]; then \
		echo "   workers: $(RADIANT_RENDER_JOBS)"; \
		run_logged "temp/_radiant_render_visual.log" sh -c 'cd test/render && LAMBDA_ROOT="$(CURDIR)" node test_radiant_render.js -j $(RADIANT_RENDER_JOBS) --baseline' || true; \
		render_elapsed=$$run_logged_elapsed; \
		output=$$(cat "temp/_radiant_render_visual.log"); \
		echo "$$output" | grep -E "PASS|FAIL|ERROR|Results:|Baseline Regressions|baseline tests passed" | tail -15; \
		render_line=$$(echo "$$output" | grep "^Results:" | tail -1); \
		render_passed=$$(echo "$$render_line" | grep -oE "^Results: [0-9]+" | grep -oE "[0-9]+" || echo "0"); \
		render_total=$$(echo "$$render_line" | grep -oE "^Results: [0-9]+/[0-9]+" | grep -oE "/[0-9]+" | tr -d "/" || echo "0"); \
		render_xpassed=$$(echo "$$render_line" | grep -oE "[0-9]+ new passes" | grep -oE "[0-9]+" || echo "0"); \
		render_xfailed=$$(echo "$$render_line" | grep -oE "[0-9]+ expected failures" | grep -oE "[0-9]+" || echo "0"); \
		render_skipped=$$(echo "$$render_line" | grep -oE "[0-9]+ skipped" | grep -oE "[0-9]+" || echo "0"); \
		render_errors=$$(echo "$$render_line" | grep -oE "[0-9]+ errors" | grep -oE "[0-9]+" || echo "0"); \
		render_passed=$${render_passed:-0}; render_total=$${render_total:-0}; render_xpassed=$${render_xpassed:-0}; render_xfailed=$${render_xfailed:-0}; render_skipped=$${render_skipped:-0}; render_errors=$${render_errors:-0}; \
		render_details="$${render_passed}/$${render_total} passed"; \
		if [ $$render_xpassed -gt 0 ]; then render_details="$$render_details, $$render_xpassed new passes"; fi; \
		if [ $$render_xfailed -gt 0 ]; then render_details="$$render_details, $$render_xfailed expected failures"; fi; \
		if [ $$render_skipped -gt 0 ]; then render_details="$$render_details, $$render_skipped skipped"; fi; \
		if [ $$render_errors -gt 0 ]; then render_details="$$render_details, $$render_errors errors"; fi; \
		render_failed=0; \
		if echo "$$output" | grep -q "Baseline Regressions"; then \
			render_regressions=$$(echo "$$output" | grep "Baseline Regressions" | grep -oE "[0-9]+" | head -1); render_regressions=$${render_regressions:-0}; \
			render_failed=$$render_regressions; \
			render_details="$$render_details, $$render_regressions baseline regressions"; \
			render_status="❌ FAIL"; any_failed=1; \
		else \
			render_status="✅ PASS"; \
		fi; \
	else \
		echo "   ⚠️  test/render/test_radiant_render.js not found"; \
	fi; \
	\
	echo ""; \
	echo "📦 DOM UI Integration:"; \
	dom_ui_exit=0; \
	run_logged "temp/_radiant_dom_ui.log" $(MAKE) --no-print-directory dom-ui-run || dom_ui_exit=$$?; \
	dom_ui_elapsed=$$run_logged_elapsed; \
	output=$$(cat "temp/_radiant_dom_ui.log"); \
	echo "$$output" | tail -5; \
	dom_ui_line=$$(echo "$$output" | grep "^dom-ui:" | tail -1); \
	dom_ui_passed=$$(echo "$$dom_ui_line" | grep -oE "[0-9]+/[0-9]+" | cut -d/ -f1 || echo "0"); \
	dom_ui_total=$$(echo "$$dom_ui_line" | grep -oE "[0-9]+/[0-9]+" | cut -d/ -f2 || echo "0"); \
	dom_ui_passed=$${dom_ui_passed:-0}; dom_ui_total=$${dom_ui_total:-0}; \
	dom_ui_failed=$$((dom_ui_total - dom_ui_passed)); \
	if [ $$dom_ui_exit -eq 0 ] && [ $$dom_ui_total -gt 0 ]; then dom_ui_status="✅ PASS"; else dom_ui_status="❌ FAIL"; if [ $$dom_ui_failed -eq 0 ]; then dom_ui_failed=1; fi; any_failed=1; fi; \
	\
	wpt_syntax_passed=0; wpt_syntax_failed=0; wpt_syntax_status="⏭️  SKIP"; wpt_syntax_elapsed=0; \
	echo ""; \
	echo "📦 WPT CSS Syntax Conformance:"; \
	if [ -f "test/test_wpt_css_syntax_gtest.exe" ]; then \
		run_logged "temp/_radiant_wpt_css_syntax.log" ./test/test_wpt_css_syntax_gtest.exe || true; \
		wpt_syntax_elapsed=$$run_logged_elapsed; \
		output=$$(cat "temp/_radiant_wpt_css_syntax.log"); \
		echo "$$output" | grep -E "^\[  PASSED|^\[  FAILED|^\[  SKIPPED" | tail -5; \
		wpt_syntax_passed=$$(echo "$$output" | grep -E "^\[  PASSED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); \
		wpt_syntax_failed=$$(echo "$$output" | grep -E "^[0-9]+ FAILED TEST" | grep -oE "^[0-9]+" || echo "0"); \
		wpt_syntax_passed=$${wpt_syntax_passed:-0}; wpt_syntax_failed=$${wpt_syntax_failed:-0}; \
		wpt_syntax_baseline=25; \
		if [ "$$wpt_syntax_passed" -ge "$$wpt_syntax_baseline" ] 2>/dev/null; then wpt_syntax_status="✅ PASS"; else wpt_syntax_status="❌ FAIL"; any_failed=1; fi; \
	else \
		echo "   ⚠️  test/test_wpt_css_syntax_gtest.exe not found"; \
	fi; \
	\
	wpt_dom2_passed=0; wpt_dom2_failed=0; wpt_dom2_status="✅ PASS"; wpt_dom2_elapsed=0; \
	echo ""; \
	echo "📦 WPT DOM2 Conformance:"; \
	for runner in $(RADIANT_DOM2_WPT_RUNNERS); do \
		exe="test/test_wpt_$${runner}_gtest.exe"; \
		if [ ! -f "$$exe" ]; then echo "   ⚠️  $$exe not found"; wpt_dom2_failed=$$((wpt_dom2_failed + 1)); wpt_dom2_status="❌ FAIL"; any_failed=1; continue; fi; \
		runner_exit=0; log_file="temp/_radiant_wpt_$${runner}.log"; \
		run_logged "$$log_file" "$$exe" || runner_exit=$$?; \
		wpt_dom2_elapsed=$$((wpt_dom2_elapsed + run_logged_elapsed)); \
		output=$$(cat "$$log_file"); \
		passed=$$(echo "$$output" | grep -E "^\[  PASSED  \]" | grep -oE "[0-9]+" | head -1 || echo "0"); passed=$${passed:-0}; \
		failed=$$(echo "$$output" | grep -E "^\[  FAILED  \][[:space:]]+[0-9]+ test" | head -1 | grep -oE "[0-9]+" | head -1 || echo "0"); failed=$${failed:-0}; \
		if [ $$runner_exit -ne 0 ] && [ $$failed -eq 0 ]; then failed=1; fi; \
		wpt_dom2_passed=$$((wpt_dom2_passed + passed)); wpt_dom2_failed=$$((wpt_dom2_failed + failed)); \
		if [ $$failed -eq 0 ]; then echo "   ✅ $$runner ($$passed passing files)"; else echo "   ❌ $$runner ($$failed regressions)"; wpt_dom2_status="❌ FAIL"; any_failed=1; fi; \
	done; \
	\
	total_passed=$$((layout_total_passed + snapshot_passed + ui_passed + dom_ui_passed + radiant_view_passed + page_passed + fuzzy_passed + render_passed + wpt_syntax_passed + wpt_dom2_passed)); \
	total_failed=$$((layout_total_failed + snapshot_failed + ui_failed + dom_ui_failed + radiant_view_failed + page_failed + fuzzy_failed + render_failed + wpt_syntax_failed + wpt_dom2_failed)); \
	total_skipped=$$layout_total_skipped; \
	total_tests=$$((total_passed + layout_total_partial + total_failed)); \
	\
	echo ""; \
	echo "=============================================================="; \
	echo "🏁 RADIANT BASELINE TEST RESULTS BREAKDOWN"; \
	echo "=============================================================="; \
	echo ""; \
	echo "📊 Test Results by Suite:"; \
	echo "   ├── Layout Baseline     $$layout_overall_status  ($$(format_duration "$$layout_elapsed"), $$layout_total_passed passed, $$layout_total_partial partially passing, $$layout_total_failed failed, $$layout_total_skipped skipped)"; \
	if [ -n "$$layout_results" ]; then \
		suite_count=$$(printf "%s\n" "$$layout_results" | wc -l | tr -d ' '); \
		suite_idx=0; \
		printf "%s\n" "$$layout_results" | while IFS='|' read -r sname sstatus spassed spartial sfailed sskipped selapsed; do \
			suite_idx=$$((suite_idx + 1)); \
			if [ $$suite_idx -eq $$suite_count ]; then \
				printf "   │   └── %-14s $$sstatus  (%s, $$spassed passed, $$spartial partially passing, $$sfailed failed, $$sskipped skipped) (test_radiant_layout.js --baseline-only -c $$sname)\n" "$$sname" "$$(format_duration "$$selapsed")"; \
			else \
				printf "   │   ├── %-14s $$sstatus  (%s, $$spassed passed, $$spartial partially passing, $$sfailed failed, $$sskipped skipped) (test_radiant_layout.js --baseline-only -c $$sname)\n" "$$sname" "$$(format_duration "$$selapsed")"; \
			fi; \
		done; \
	fi; \
	echo "   ├── Layout Page Suite   $$snapshot_status  ($$(format_duration "$$snapshot_elapsed"), $$snapshot_passed passed, $$snapshot_failed failed) (layout_suite_snapshot.js --check page)"; \
	echo "   ├── UI Automation       $$ui_status  ($$(format_duration "$$ui_elapsed"), $$ui_passed passed, $$ui_failed failed) (test_ui_automation_gtest.exe)"; \
	echo "   ├── DOM UI Integration  $$dom_ui_status  ($$(format_duration "$$dom_ui_elapsed"), $$dom_ui_passed passed, $$dom_ui_failed failed) (dom-ui-run)"; \
	echo "   ├── Radiant View Cmd    $$radiant_view_status  ($$(format_duration "$$radiant_view_elapsed"), $$radiant_view_passed passed, $$radiant_view_failed failed) (test_radiant_view_gtest.exe)"; \
	echo "   ├── View Page & Markdown $$page_status  ($$(format_duration "$$page_elapsed"), $$page_passed passed, $$page_failed failed) (test_page_load_gtest.exe)"; \
	echo "   ├── Fuzzy Crash         $$fuzzy_status  ($$(format_duration "$$fuzzy_elapsed"), $$fuzzy_passed passed, $$fuzzy_failed failed) (test_layout_fuzzy_gtest.exe)"; \
	echo "   ├── Render Visual       $$render_status  ($$(format_duration "$$render_elapsed"), $$render_details) (test_radiant_render.js --baseline)"; \
	echo "   ├── WPT CSS Syntax      $$wpt_syntax_status  ($$(format_duration "$$wpt_syntax_elapsed"), $$wpt_syntax_passed passed, $$wpt_syntax_failed failed) (test_wpt_css_syntax_gtest.exe)"; \
	echo "   └── WPT DOM2            $$wpt_dom2_status  ($$(format_duration "$$wpt_dom2_elapsed"), $$wpt_dom2_passed passed, $$wpt_dom2_failed failed) ($(RADIANT_DOM2_WPT_RUNNERS))"; \
	echo ""; \
	echo "📊 Overall Results:"; \
	echo "   Total Tests: $$total_tests"; \
	echo "   ✅ Passed:   $$total_passed"; \
	if [ $$layout_total_partial -gt 0 ]; then \
		echo "   ⚠️  Partially Passing: $$layout_total_partial"; \
	fi; \
	if [ $$total_failed -gt 0 ]; then \
		echo "   ❌ Failed:   $$total_failed"; \
	fi; \
	if [ $$total_skipped -gt 0 ]; then \
		echo "   ⏭️  Skipped:  $$total_skipped"; \
	fi; \
	echo "=============================================================="; \
	rm -f $(LAYOUT_BASELINE_RESULTS); \
	if [ $$any_failed -gt 0 ]; then exit 1; fi

test-layout-baseline: build-test
	@echo "Running Radiant layout BASELINE test suite..."
	@echo "=============================================================="
	@layout_exit=0; \
	$(MAKE) --no-print-directory run-layout-baseline-suites || layout_exit=$$?; \
	rm -f $(LAYOUT_BASELINE_RESULTS); \
	if [ $$layout_exit -ne 0 ]; then exit $$layout_exit; fi
	@if [ -f test/layout/snapshot/page.json ]; then \
		echo ""; \
		echo "Running page suite snapshot regression check..."; \
		echo "=============================================================="; \
		$(LAYOUT_TEST_ENV) node test/layout/test_radiant_layout.js --engine lambda-css -c page --json -j 5 2>/dev/null \
			| node test/layout/layout_suite_snapshot.js --check page; \
	fi

test-ui-automation: build-test
	@echo "Running UI Automation test suite..."
	@echo "=============================================================="
	@if [ -f "test/test_ui_automation_gtest.exe" ]; then \
		./test/test_ui_automation_gtest.exe; \
	else \
		echo "Error: test/test_ui_automation_gtest.exe not found - run 'make build-test' first"; \
		exit 1; \
	fi

test-page-load: build-test
	@echo "Running View Page and Markdown (Headless) test suite..."
	@echo "=============================================================="
	@if [ -f "test/test_page_load_gtest.exe" ]; then \
		./test/test_page_load_gtest.exe; \
	else \
		echo "Error: test/test_page_load_gtest.exe not found - run 'make build-test' first"; \
		exit 1; \
	fi

test-radiant-online: build-test
	@echo "Running Radiant online URL smoke test suite..."
	@echo "=============================================================="
	@if [ -f "test/test_radiant_online_view_gtest.exe" ]; then \
		./test/test_radiant_online_view_gtest.exe; \
	else \
		echo "Error: test/test_radiant_online_view_gtest.exe not found - run 'make build-test' first"; \
		exit 1; \
	fi

build-pdf-render-test: build-lambda-input
	@echo "Building PDF render visual gtest executable using Premake5..."
	@mkdir -p build/premake
	$(MAKE) generate-premake
	cd build/premake && PATH="/clang64/bin:$$PATH" $(PREMAKE5) gmake --file=../../$(PREMAKE_FILE)
	PATH="/clang64/bin:$$PATH" $(MAKE) -C build/premake config=debug_native -j$(TEST_JOBS) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)" \
		test_pdf_render_visual_gtest

test-pdf-render: build-pdf-render-test
	@echo "Running PDF render visual gtest suite..."
	@echo "=============================================================="
	@if [ -f "test/test_pdf_render_visual_gtest.exe" ]; then \
		./test/test_pdf_render_visual_gtest.exe $(ARGS); \
	else \
		echo "Error: test/test_pdf_render_visual_gtest.exe not found - run 'make build-pdf-render-test' first"; \
		exit 1; \
	fi

test-reactive-ui: build
	@echo "Running Reactive UI test suite..."
	@echo "=============================================================="
	@PASS=0; FAIL=0; TOTAL=0; \
	for json in test/ui/todo_toggle.json test/ui/todo_delete.json test/ui/todo_add_item.json test/ui/todo_text_input.json; do \
		name=$$(basename $$json .json); \
		TOTAL=$$((TOTAL + 1)); \
		echo "--- $$name ---"; \
		if ./lambda.exe view test/lambda/ui/todo.ls --event-file $$json --headless --no-log; then \
			PASS=$$((PASS + 1)); \
		else \
			FAIL=$$((FAIL + 1)); \
			echo "FAIL: $$name"; \
		fi; \
	done; \
	echo "=============================================================="; \
	echo "Reactive UI: $$PASS/$$TOTAL passed"; \
	if [ $$FAIL -gt 0 ]; then exit 1; fi

# Stage 4C Phase A — the full plain-DOM editor suite headless under `lambda.exe js`.
# The runner (test/editor-js/tools/run-phase-a.mjs) bundles each test group
# (core/view/drawing + 6 tier corpora) to an IIFE, runs it, and aggregates the
# in-engine harness summaries. React `.test.tsx` are excluded by construction.
editor-4c-js: build
	@echo "Running Stage 4C Phase A (plain-DOM suite under lambda.exe js)..."
	@echo "=============================================================="
	@cd test/editor-js && node tools/run-phase-a.mjs
	@echo "=============================================================="

# Stage 4C Phase B/C — editor event-driven UI automation under lambda.exe view + event_sim.
# Runs the 4B baseline set (test/ui/editor4b/*.json), the 4C Phase-B set
# (test/ui/editor4c/*.json), and the Phase-C expansion set
# (test/ui/editor4c_phase_c/*.json). Each fixture may name its own harness page
# via the "html" field (default test/html/editor-dom.html).
editor-4c-view: build
	@echo "Running Stage 4C Phase B/C editor UI test suite..."
	@echo "=============================================================="
	@PASS=0; FAIL=0; TOTAL=0; \
	for json in test/ui/editor4b/*.json test/ui/editor4c/*.json test/ui/editor4c_phase_c/*.json; do \
		[ -f "$$json" ] || continue; \
		name=$$(basename $$json .json); \
		TOTAL=$$((TOTAL + 1)); \
		page=$$(sed -n 's/.*"html"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$$json" | head -1); \
		[ -n "$$page" ] || page="test/html/editor-dom.html"; \
		if ./lambda.exe view "$$page" --event-file $$json --headless --no-log >/dev/null 2>&1; then \
			PASS=$$((PASS + 1)); \
			printf "  \033[32m✓\033[0m %s\n" "$$name"; \
		else \
			FAIL=$$((FAIL + 1)); \
			printf "  \033[31m✗\033[0m %s\n" "$$name"; \
		fi; \
	done; \
	echo "=============================================================="; \
	echo "editor-4c-view: $$PASS/$$TOTAL passed"; \
	if [ $$FAIL -gt 0 ]; then exit 1; fi

# Browser-library DOM fixtures run through the real Radiant input/event/layout loop.
dom-ui: build dom-ui-run

dom-ui-run:
	@echo "Running DOM UI integration suite..."
	@echo "=============================================================="
	@node test/ui/dom/run-dom-ui.mjs --jobs "$(DOM_UI_JOBS)" $(if $(or $(test),$(TEST)),--test "$(or $(test),$(TEST))")

# Stage 4C Milestone 3 — parity report: cross-check the Radiant Phase-A pass-set
# against the vitest/jsdom oracle (the editor's own suite under Node). Runs
# Phase A (via run-phase-a.mjs) AND a fresh vitest oracle, reconciles per group,
# writes vibe/editing/Stage4C_Parity_Report.md, and exits non-zero on any
# unexplained divergence. React `.test.tsx` are the only intentional exclusion.
editor-4c-parity: build
	@echo "Running Stage 4C parity report (Radiant Phase-A vs vitest/jsdom oracle)..."
	@echo "=============================================================="
	@cd test/editor-js && node tools/parity-report.mjs --refresh-oracle
	@echo "=============================================================="

# Stage 4C — full editor conformance: parity (Phase A breadth + jsdom oracle
# cross-check) + Phase B (view depth). Parity subsumes the Phase-A run, so
# `editor-4c-js` is only needed for a quick Phase-A-only pass.
editor-4c: editor-4c-parity editor-4c-view
	@echo "=============================================================="
	@echo "Stage 4C complete: parity (Phase A + jsdom oracle) + Phase B (view + event_sim) all green."

# Save/check/diff layout suite snapshots for regression detection outside baseline
layout-snapshot:
	@SUITE_VAR="$(or $(suite),$(SUITE))"; \
	if [ -z "$$SUITE_VAR" ]; then \
		echo "Usage: make layout-snapshot suite=<name>"; \
		echo "  e.g. make layout-snapshot suite=page"; \
		exit 1; \
	fi; \
	echo "Saving snapshot for suite: $$SUITE_VAR"; \
	$(LAYOUT_TEST_ENV) node test/layout/test_radiant_layout.js --engine lambda-css -c $$SUITE_VAR --json -j 5 2>/dev/null \
		| node test/layout/layout_suite_snapshot.js --save $$SUITE_VAR

layout-snapshot-check:
	@SUITE_VAR="$(or $(suite),$(SUITE))"; \
	if [ -z "$$SUITE_VAR" ]; then \
		echo "Usage: make layout-snapshot-check suite=<name>"; \
		exit 1; \
	fi; \
	$(LAYOUT_TEST_ENV) node test/layout/test_radiant_layout.js --engine lambda-css -c $$SUITE_VAR --json -j 5 2>/dev/null \
		| node test/layout/layout_suite_snapshot.js --check $$SUITE_VAR

layout-snapshot-diff:
	@SUITE_VAR="$(or $(suite),$(SUITE))"; \
	if [ -z "$$SUITE_VAR" ]; then \
		echo "Usage: make layout-snapshot-diff suite=<name>"; \
		exit 1; \
	fi; \
	$(LAYOUT_TEST_ENV) node test/layout/test_radiant_layout.js --engine lambda-css -c $$SUITE_VAR --json -j 5 2>/dev/null \
		| node test/layout/layout_suite_snapshot.js --diff $$SUITE_VAR

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

test-extended: build-test
	@echo "Clearing HTTP cache for clean test runs..."
	@rm -rf temp/cache
	@echo "Running EXTENDED test suites only..."
	@LAMBDA_TEST_HEAVY_LOAD=1 node test/test_run.js --category=extended --parallel
	@$(MAKE) dom-ui-run

test-library: build
	@echo "Running library test suite..."
	@node test/test_run.js --target=library --raw

test-input: build
	@echo "Running input processing test suite..."
	@node test/test_run.js --target=input --raw

build-graph-mermaid-test:
	@echo "Building Mermaid graph semantic runner..."
	@mkdir -p build/premake
	@$(MAKE) generate-premake
	@PATH="/clang64/bin:$$PATH" $(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@PATH="/clang64/bin:$$PATH" $(MAKE) -C build/premake config=debug_native lambda test_graph_mermaid_gtest test_lambda_gtest -j$(TEST_JOBS) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)"

test-graph-mermaid: build-graph-mermaid-test
	@echo "Running Mermaid graph semantic corpus..."
	@./test/test_graph_mermaid_gtest.exe
	@echo "Running Mermaid graph package integration fixtures..."
	@./test/test_lambda_gtest.exe --gtest_filter='*mermaid*'

build-graph-graphviz-test:
	@echo "Building Graphviz graph semantic runner..."
	@mkdir -p build/premake
	@$(MAKE) generate-premake
	@PATH="/clang64/bin:$$PATH" $(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@PATH="/clang64/bin:$$PATH" $(MAKE) -C build/premake config=debug_native lambda test_graph_parser_gtest test_lambda_gtest -j$(TEST_JOBS) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)"

test-graph-graphviz: build-graph-graphviz-test
	@echo "Running native DOT parser coverage..."
	@./test/test_graph_parser_gtest.exe --gtest_filter='GraphParserTest.ParserLocBudget:GraphParserTest.ParseDOTGraph:GraphParserTest.ParseComplexDOTGraph:GraphParserTest.ParseUndirectedGraph:GraphParserTest.ParseEmptyGraph'
	@echo "Running manifest-driven Graphviz package fixtures..."
	@./test/test_lambda_gtest.exe --gtest_filter='*graphviz*'
	@echo "Running headless .gv view bridge..."
	@MEMTRACK_MODE=DEBUG ./lambda.exe view test/lambda/graph/graphviz/view.gv --headless --no-log 2>temp/graphviz_view_memtrack.log
	@! grep -qi 'memtrack: .*leak' temp/graphviz_view_memtrack.log
	@rm -f temp/graphviz_view_memtrack.log

build-graph-structurizr-test:
	@echo "Building Structurizr graph test runners..."
	@mkdir -p build/premake
	@$(MAKE) generate-premake
	@PATH="/clang64/bin:$$PATH" $(PREMAKE5) gmake --file=$(PREMAKE_FILE)
	@PATH="/clang64/bin:$$PATH" $(MAKE) -C build/premake config=debug_native lambda test_graph_parser_gtest test_lambda_gtest -j$(TEST_JOBS) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)"

test-graph-structurizr: build-graph-structurizr-test
	@echo "Running native Structurizr parser coverage..."
	@./test/test_graph_parser_gtest.exe --gtest_filter='GraphParserTest.ParserLocBudget:GraphParserTest.ParseStructurizrWorkspace:GraphParserTest.ParseStructurizrArchetypes:GraphParserTest.AutoDetectStructurizrDsl:GraphParserTest.RecoverStructurizrWorkspaceRoot'
	@echo "Running Structurizr/C4 package integration fixtures..."
	@./test/test_lambda_gtest.exe --gtest_filter='*structurizr*'
	@echo "Running headless .dsl selected-view bridge..."
	@./lambda.exe view test/lambda/graph/structurizr/advanced_static.dsl --view-key Expression --headless --no-log
	@echo "Running selected-view HTML conversion bridge..."
	@./lambda.exe convert test/lambda/graph/structurizr/advanced_static.dsl -t html -o temp/structurizr_convert.html --view-key Expression
	@grep -q 'data-node-id="api"' temp/structurizr_convert.html
	@grep -q 'data-node-id="user"' temp/structurizr_convert.html
	@! grep -q 'data-node-id="worker"' temp/structurizr_convert.html
	@rm -f temp/structurizr_convert.html

test-validator: build
	@echo "Running validator test suite..."
	@node test/test_run.js --target=validator --raw

test-lambda: build
	@echo "Running lambda test suite..."
	@node test/test_run.js --target=lambda --raw

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
	if [ ! -f "temp/view_tree.json" ]; then \
		echo "❌ Error: Lambda CSS output not generated at temp/view_tree.json"; \
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
		node test/test_run.js; \
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
fuzz-lambda: build
	@echo "Running fuzzy tests (quick mode: 5 minutes)..."
	@chmod +x test/fuzzy/lambda/test_fuzzy.sh
	@./test/fuzzy/lambda/test_fuzzy.sh --duration=300
	@echo "✅ Fuzzy tests completed"

# Run extended fuzzy tests (1 hour)
fuzz-lambda-extended: build
	@echo "Running extended fuzzy tests (1 hour)..."
	@chmod +x test/fuzzy/lambda/test_fuzzy.sh
	@./test/fuzzy/lambda/test_fuzzy.sh --duration=3600
	@echo "✅ Extended fuzzy tests completed"

# Radiant Layout Engine Fuzzy Testing
# Generates adversarial HTML/CSS and tests layout robustness

# Quick radiant fuzz (2 minutes, stress mode)
fuzz-radiant-quick: build
	@echo "Running Radiant layout fuzzy tests (quick: 2 minutes, stress mode)..."
	@chmod +x test/fuzzy/radiant/test_fuzzy_radiant.sh
	@./test/fuzzy/radiant/test_fuzzy_radiant.sh --duration=120 --stress

# Full radiant fuzz (default 5 minutes, override with duration=N)
fuzz-radiant: build
	@echo "Running Radiant layout fuzzy tests..."
	@chmod +x test/fuzzy/radiant/test_fuzzy_radiant.sh
	@./test/fuzzy/radiant/test_fuzzy_radiant.sh --duration=$(or $(duration),300) --stress

# Lambda JS Engine Fuzzy Testing
# Generates and mutates JavaScript programs to test JS engine robustness

# Quick JS fuzz (2 minutes)
fuzz-js-quick: build
	@echo "Running JS engine fuzzy tests (quick: 2 minutes)..."
	@chmod +x test/fuzzy/js/test_fuzzy_js.sh
	@./test/fuzzy/js/test_fuzzy_js.sh --duration=120

# Full JS fuzz (default 5 minutes, override with duration=N)
fuzz-js: build
	@echo "Running JS engine fuzzy tests..."
	@chmod +x test/fuzzy/js/test_fuzzy_js.sh
	@./test/fuzzy/js/test_fuzzy_js.sh --duration=$(or $(duration),300)

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

format:
	@echo "Formatting source code..."
	@if command -v clang-format >/dev/null 2>&1; then \
		find lambda -name "*.c" -o -name "*.cpp" -o -name "*.h" | xargs clang-format -i; \
		echo "Code formatted with clang-format."; \
	else \
		echo "clang-format not found. Install with: brew install clang-format"; \
	fi

# lint-cppcheck was retired in Phase 4 (vibe/Lambda_Lint.md §8). cppcheck's
# bug-finding overlap with the Phase 3 `bugprone-*` / `clang-analyzer-*`
# families made it largely redundant; its only unique capability was
# whole-program unusedFunction, replaced by the hybrid backend at
# utils/lint/dead-code/run_unused_function.sh (rule_id: unused-function).
# Install of cppcheck is no longer required.

# Binary size analysis by library group
analyze-binary:
	@python3 utils/analyze_binary.py lambda.exe -v

# Unified project-policy linter.
#
# Runs every ast-grep rule under utils/lint/rules/ (applying each rule's
# inline-suppression marker) plus the structural checks dispatched by run.sh
# (currently utils/check_state_machine.py), and aggregates into one pass/fail
# gate. This is the single entry point — no per-policy wrapper targets.
#
# Examples:
#   make lint                                  # full sweep, CI gate
#   make lint ARGS='--rule ^no-raw-alloc$$'    # one rule (or family regex)
#   make lint ARGS='--structural-only'         # skip pattern rules
#   make lint ARGS=--report                    # write Report_NNN.{md,json,tsv}
#   make lint ARGS=--format=github             # GitHub Actions annotations
#   utils/lint/run.sh --list                   # list all rule ids
lint:
	@utils/lint/run.sh $(ARGS)

# Same pipeline plus the clang-tidy backend (bugprone-* + clang-analyzer-* +
# cert-* across all of lambda/ + radiant/). Slow (~4 min on 8 cores); split
# off from `make lint` so iterative dev runs stay fast (~6 s without tidy).
# All Report_NNN.* still aggregate findings across every active backend.
lint-full:
	@utils/lint/run.sh --with-tidy $(ARGS)

# Lizard duplicate-code reports with documented generated-file and block exclusions.
check-code-dup:
	@python3 test/dedup/check_code_dup.py

check-lambda-dup:
	@python3 test/dedup/check_code_dup.py lambda

check-radiant-dup:
	@python3 test/dedup/check_code_dup.py radiant

check-hosted-python-architecture:
	@python3 utils/check_hosted_python_architecture.py

# Generate the checked review input for every remaining hosted-Python coupling.
# This is source analysis only; it never loads Jube or changes runtime behavior.
hosted-python-coupling-inventory:
	@python3 utils/check_hosted_python_architecture.py --inventory

check-hosted-python-module-boundary: build build-lang-python
	@python3 utils/check_hosted_python_architecture.py --require-module-binary

# Clang-tidy static analysis
# tidy / tidy-full / tidy-fix / generate-compile-db were retired in the Phase 3
# unification: the `bugprone-* + clang-analyzer-* + cert-*` subset (minus
# project-disabled noise) is now run by `make lint-full` (and on-demand by
# `make lint ARGS='--rule ^tidy-*'`) via utils/lint/tidy/run_tidy.sh.
# `tidy-printf` (below) is a different tool — a clang-based AST rewriter — and
# is retained.

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
	cd build/premake && PATH="/clang64/bin:$$PATH" $(PREMAKE5) gmake --file=../../$(PREMAKE_FILE)
	$(call run_make_with_error_summary,lambda-input,debug_native,,lambda-input-full-cpp)
	@echo "✅ lambda-input DLLs built successfully!"

build-test: build-lambda-input
	@echo "Building tests using Premake5..."
	@echo "Building configurations..."
	@mkdir -p build/premake
	$(MAKE) generate-premake
	cd build/premake && PATH="/clang64/bin:$$PATH" $(PREMAKE5) gmake --file=../../$(PREMAKE_FILE)
	@# If last build was release, rebuild lambda.exe incrementally in release mode
	@if [ -f .lambda_release_build ]; then \
		echo "Rebuilding lambda.exe in release mode (incremental) — log: temp/build_tests_lambda.log"; \
		mkdir -p temp; \
		$(MAKE) -C build/premake config=release_native lambda -j$(TEST_JOBS) CC="$(CC)" CXX="$(CXX)" > temp/build_tests_lambda.log 2>&1 || { echo "❌ release lambda rebuild failed (see temp/build_tests_lambda.log):"; tail -20 temp/build_tests_lambda.log; exit 1; }; \
		cp -p lambda.exe .lambda_build_backup.exe; \
	fi
	@echo "Building all test executables (debug mode, $(TEST_JOBS) jobs)..."
	$(call run_make_with_error_summary,tests,debug_native,,all)
	@# Restore release lambda.exe over the debug one
	@if [ -f .lambda_build_backup.exe ]; then \
		echo "Restoring release lambda.exe..."; \
		mv .lambda_build_backup.exe lambda.exe; \
	fi

# Radiant baseline needs lambda plus its focused native runners; building every test
# project made unrelated suites dominate this gate after a clean build.
build-radiant-baseline:
	@echo "Building the Radiant baseline runtime and native test runners..."
	@mkdir -p build/premake
	$(MAKE) generate-premake
	cd build/premake && PATH="/clang64/bin:$$PATH" $(PREMAKE5) gmake --file=../../$(PREMAKE_FILE)
	@echo "Building lambda-input DLLs with $(TEST_JOBS) parallel jobs..."
	$(MAKE) -C build/premake config=debug_native lambda-input-full-cpp -j$(TEST_JOBS) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)"
	@if [ -f .lambda_release_build ]; then \
		echo "Rebuilding lambda.exe in release mode (incremental)..."; \
		$(MAKE) -C build/premake config=release_native lambda -j$(TEST_JOBS) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)"; \
	else \
		echo "Rebuilding lambda.exe in debug mode (incremental)..."; \
		$(MAKE) -C build/premake config=debug_native lambda -j$(TEST_JOBS) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)"; \
	fi
	@echo "Building Radiant baseline test executables (debug mode, $(TEST_JOBS) jobs)..."
	PATH="/clang64/bin:$$PATH" $(MAKE) -C build/premake config=debug_native $(RADIANT_BASELINE_TEST_PROJECTS) -j$(TEST_JOBS) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" RANLIB="$(RANLIB)"

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
		echo "Available suites: antd, basic, baseline, bootstrap, css2.1, flex, grid, tailwind, yoga, web-tmpl, wpt-css-box, wpt-css-images, wpt-css-tables, wpt-css-position, wpt-css-text, wpt-css-lists"; \
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
	                for dir in antd basic baseline bootstrap css2.1 flex grid tailwind yoga wpt-css-box wpt-css-images wpt-css-tables wpt-css-position wpt-css-text wpt-css-lists; do \
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
	                if [ -z "$$TEST_FILE" ] && [ -f "data/web-tmpl/$${TEST_VAR}/index.html" ]; then \
	                    TEST_FILE="data/web-tmpl/$${TEST_VAR}/index.html"; \
	                    FOUND_SUITE="web-tmpl"; \
	                fi; \
	                if [ -z "$$TEST_FILE" ]; then \
	                    echo "❌ Error: Test file '$$TEST_VAR' not found in any suite directory"; \
	                    echo "   Searched in: antd, basic, baseline, bootstrap, css2.1, flex, grid, tailwind, yoga, web-tmpl, wpt-css-*"; \
	                    exit 1; \
	                fi; \
	                echo "📄 Found test in suite: $$FOUND_SUITE" \
	            ;; \
	        esac; \
	        echo "📄 Capturing single test: $$TEST_FILE"; \
	        LAMBDA_ROOT=$(CURDIR) node extract_browser_references.js $$FORCE_FLAG $$PLATFORM_FLAG $$TEST_FILE; \
	    else \
	        echo "📂 Capturing suite: $$SUITE_VAR"; \
	        LAMBDA_ROOT=$(CURDIR) node extract_browser_references.js $$FORCE_FLAG $$PLATFORM_FLAG --category $$SUITE_VAR; \
	    fi; \
	else \
	    echo "❌ Error: Layout directory not found at test/layout"; \
	    exit 1; \
	fi

# Layout Engine Testing Targets
# ==============================

# test-layout: Run layout tests using Lambda CSS engine
# Usage: make test-layout [suite=SUITE] [test=TEST] [pattern=PATTERN] [update=1]
# Note: test parameter now accepts filename with or without .html/.htm extension
# Example: make test-layout test=baseline_301_simple_margin
test-layout:
	@echo "🎨 Running Lambda CSS Layout Engine Tests"
	@echo "=========================================="
	@STALE_FILE=""; \
	if [ ! -x "$(LAMBDA_EXE)" ]; then \
		echo "🔧 $(LAMBDA_EXE) missing; building before layout tests"; \
		$(MAKE) build; \
	else \
		STALE_FILE=$$(find radiant lambda lib -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.h' -o -name '*.hpp' -o -name '*.m' -o -name '*.mm' \) -newer "$(LAMBDA_EXE)" -print -quit 2>/dev/null); \
		if [ -n "$$STALE_FILE" ]; then \
			echo "🔧 $(LAMBDA_EXE) is older than $$STALE_FILE; rebuilding before layout tests"; \
			$(MAKE) build; \
		fi; \
	fi
	@if [ -f "test/layout/test_radiant_layout.js" ]; then \
		TEST_VAR="$(or $(test),$(TEST))"; \
		PATTERN_VAR="$(or $(pattern),$(PATTERN))"; \
		SUITE_VAR="$(or $(suite),$(SUITE))"; \
		UPDATE_BASELINE_VAR="$(or $(update),$(UPDATE),$(update-baseline),$(UPDATE_BASELINE))"; \
		CONCURRENCY_VAR="$(or $(layout_concurrency),$(LAYOUT_CONCURRENCY),$(concurrency),$(CONCURRENCY))"; \
		UPDATE_BASELINE_FLAG=""; \
		CONCURRENCY_FLAG=""; \
		if [ -n "$$UPDATE_BASELINE_VAR" ]; then UPDATE_BASELINE_FLAG="--update-baseline"; fi; \
		if [ -n "$$CONCURRENCY_VAR" ]; then CONCURRENCY_FLAG="-j $$CONCURRENCY_VAR"; fi; \
		if [ -n "$$TEST_VAR" ]; then \
			case "$$TEST_VAR" in \
				*.html|*.htm) TEST_FILE="$$TEST_VAR" ;; \
				*) \
					TEST_FILE=""; \
					for dir in antd basic baseline bootstrap css2.1 flex grid tailwind yoga form wpt-css-box wpt-css-images wpt-css-tables wpt-css-position wpt-css-text wpt-css-lists wpt-css-inline; do \
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
					if [ -z "$$TEST_FILE" ] && [ -f "test/layout/data/web-tmpl/$${TEST_VAR}/index.html" ]; then \
						TEST_FILE="$${TEST_VAR}/index.html"; \
					fi; \
					if [ -z "$$TEST_FILE" ]; then \
						TEST_FILE="$${TEST_VAR}.html"; \
					fi \
				;; \
			esac; \
			echo "🎯 Running single test: $$TEST_FILE"; \
			$(LAYOUT_TEST_ENV) node test/layout/test_radiant_layout.js --engine lambda-css --test $$TEST_FILE -v; \
		elif [ -n "$$PATTERN_VAR" ]; then \
			echo "🔍 Running tests matching pattern: $$PATTERN_VAR"; \
			$(LAYOUT_TEST_ENV) node test/layout/test_radiant_layout.js --engine lambda-css --pattern $$PATTERN_VAR $$CONCURRENCY_FLAG; \
		elif [ -n "$$SUITE_VAR" ]; then \
			echo "📂 Running test suite: $$SUITE_VAR"; \
			$(LAYOUT_TEST_ENV) node test/layout/test_radiant_layout.js --engine lambda-css --category $$SUITE_VAR $$CONCURRENCY_FLAG $$UPDATE_BASELINE_FLAG; \
		else \
			echo "🎯 Running all layout tests"; \
			$(LAYOUT_TEST_ENV) node test/layout/test_radiant_layout.js --engine lambda-css $$CONCURRENCY_FLAG $$UPDATE_BASELINE_FLAG; \
		fi; \
	else \
		echo "❌ Error: Layout test script not found at test/layout/test_radiant_layout.js"; \
		exit 1; \
	fi

# layout: Alias for test-layout
layout: test-layout

# ─── Render Visual Regression Tests ─────────────────────────────────────────

# capture-render: Capture browser reference PNGs for render tests
# Usage: make capture-render [test=<test-name>] [force=1] [platform=<platform>] [suite=<suite>]
capture-render:
	@echo "🖼️  Capturing browser render references..."
	@if [ -d "test/render" ]; then \
		cd test/render && \
		if [ ! -d node_modules ]; then \
			echo "📦 Installing render test dependencies..."; \
			npm install; \
		fi; \
		ARGS=""; \
		TEST_VAR="$(or $(test),$(TEST))"; \
		FORCE_VAR="$(or $(force),$(FORCE))"; \
		PLATFORM_VAR="$(or $(platform),$(PLATFORM))"; \
		SUITE_VAR="$(or $(suite),$(SUITE))"; \
		if [ -n "$$TEST_VAR" ]; then \
			ARGS="$$ARGS --test $$TEST_VAR"; \
		fi; \
		if [ -n "$$FORCE_VAR" ] && [ "$$FORCE_VAR" != "0" ]; then \
			ARGS="$$ARGS --force"; \
		fi; \
		if [ -n "$$PLATFORM_VAR" ]; then \
			ARGS="$$ARGS --platform $$PLATFORM_VAR"; \
		fi; \
		if [ -n "$$SUITE_VAR" ]; then \
			ARGS="$$ARGS --suite $$SUITE_VAR"; \
		fi; \
		LAMBDA_ROOT=$(CURDIR) node capture_render_references.js $$ARGS; \
	else \
		echo "❌ Error: Render test directory not found at test/render"; \
		exit 1; \
	fi

# test-render: Run render visual regression tests, including baseline checks by default
# Usage: make test-render [test=<test-name>] [pattern=<regex>] [threshold=<percent>] [suite=<suite>] [baseline=0] [update=1]
test-render:
	@echo "🎨 Running Radiant Render Tests"
	@echo "================================"
	@if [ -d "test/render" ]; then \
		cd test/render && \
		if [ ! -d node_modules ]; then \
			echo "📦 Installing render test dependencies..."; \
			npm install; \
		fi; \
		ARGS=""; \
		TEST_VAR="$(or $(test),$(TEST))"; \
		PATTERN_VAR="$(or $(pattern),$(PATTERN))"; \
		THRESHOLD_VAR="$(or $(threshold),$(THRESHOLD))"; \
		SUITE_VAR="$(or $(suite),$(SUITE))"; \
		BASELINE_VAR="$(or $(baseline),$(BASELINE),1)"; \
		UPDATE_VAR="$(or $(update),$(UPDATE),$(update-baseline),$(UPDATE_BASELINE))"; \
		if [ -n "$$TEST_VAR" ]; then \
			ARGS="$$ARGS --test $$TEST_VAR"; \
		fi; \
		if [ -n "$$PATTERN_VAR" ]; then \
			ARGS="$$ARGS --pattern $$PATTERN_VAR"; \
		fi; \
		if [ -n "$$THRESHOLD_VAR" ]; then \
			ARGS="$$ARGS --threshold $$THRESHOLD_VAR"; \
		fi; \
		if [ -n "$$SUITE_VAR" ]; then \
			ARGS="$$ARGS --suite $$SUITE_VAR"; \
		fi; \
		if [ -n "$$BASELINE_VAR" ] && [ "$$BASELINE_VAR" != "0" ]; then \
			ARGS="$$ARGS --baseline"; \
		fi; \
		if [ -n "$$UPDATE_VAR" ] && [ "$$UPDATE_VAR" != "0" ]; then \
			ARGS="$$ARGS --update-baseline"; \
		fi; \
		LAMBDA_ROOT=$(CURDIR) node test_radiant_render.js $$ARGS; \
	else \
		echo "❌ Error: Render test directory not found at test/render"; \
		exit 1; \
	fi

# render-test: Alias for test-render
render-test: test-render

# render: Alias for test-render
render: test-render

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

# devtool: Launch the DevTool Electron app
# Usage: make devtool
devtool:
	@echo "🚀 Launching DevTool..."
	@if [ -d "utils/devtool" ]; then \
		cd utils/devtool && npm run electron:dev; \
	else \
		echo "❌ Error: DevTool not found at utils/devtool"; \
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

# Publish ./site to gh-pages branch
gh-pages:
	@echo "Publishing site to gh-pages..."
	@cd /tmp/lambda-ghpages && rm -rf * && cp -R $(CURDIR)/site/* . && \
		git add -A && git commit -m "Update site" && git push origin gh-pages
	@echo "Done. Site published to gh-pages."
