# Enhanced Test Script Management Plan

## ✅ **PHASE 2 COMPLETE - True Single Source of Truth Achieved**

**Major Achievement**: Successfully consolidated all test configurations into `build_lambda_config.json` and eliminated the duplicate `test/test_config.json`.

**Completed Tasks**:
- ✅ Consolidated test suite definitions into `build_lambda_config.json` test section
- ✅ Updated `test_all.sh` to use only the consolidated config file  
- ✅ Updated `lib/build_utils.sh` to reference the build config instead of test config
- ✅ Removed duplicate `test/test_config.json` file completely
- ✅ Updated all jq queries to use `.test.test_suites[]` path structure
- ✅ Validated library dependency resolution working correctly
- ✅ Verified Makefile integration still works perfectly
- ✅ All test suites running successfully with raw mode summary

**Results**: The system now has a true single source of truth for both build libraries AND test configurations in `build_lambda_config.json`.

## Session Progress Update (August 2025)

### ✅ **Major Accomplishments - Phase 1 Complete**

**🔧 Centralized Build Infrastructure** - Successfully established shared utilities:
- Created `lib/build_utils.sh` with 327 lines of reusable build functions  
- Integrated JSON parsing, configuration validation, and dependency resolution
- `test_all.sh` now sources shared utilities (Phase 1 enhancement active)
- Unified compilation logic between main build system and test system

**📋 JSON-Based Test Configuration** - Moved from hardcoded to data-driven:
- Complete test suite definitions in `test/test_config.json` (99 lines)
- Structured configuration for 5 test suites (validator, library, input, mir, lambda)
- Suite-specific metadata: sources, dependencies, binaries, flags, environment vars
- Platform-specific and build-config-driven compilation support

**⚙️ Enhanced Test Runner** - Comprehensive `test_all.sh` improvements:
- 2,400+ lines of robust test orchestration logic
- Raw mode with synthesis line aggregation and final summary display
- Parallel and sequential execution modes with CPU core detection
- Graceful pipe handling and proper signal management (SIGPIPE)
- Individual test targeting and suite-level execution

**🎯 Raw Mode Summary Feature** - User experience enhancement:
- Automatic parsing of Criterion `[====] Synthesis: Tested: X | Passing: Y | Failing: Z | Crashing: W` lines
- Cross-test aggregation with macOS sed compatibility (using `*` vs `\+`)
- Final summary format: `[====] Final Summary: Tested: X | Passed: Y | Failed: Z | Crashed: W`
- Working for multi-test suites (library: 103 total tests, input: 19 total tests)

**🔗 Makefile Integration** - Streamlined developer workflow:
- 687-line comprehensive Makefile with 30+ test targets
- Proper delegation to `test_all.sh` with parameter passing
- Individual suite targets (`test-library`, `test-input`, etc.)
- Memory testing, coverage analysis, and cross-platform support

### 📊 **Current System Assessment**

**✅ What's Working:**
- Phase 1 build utilities integration (shared JSON parsing, config validation)
- Complete test configuration externalization from hardcoded strings
- Raw mode output aggregation and final summaries
- Parallel test execution with proper synchronization
- Cross-platform compilation (macOS, Linux, Windows cross-compile)

**🔄 What Still Needs Enhancement:**
- **Dependency duplication**: Test configs still contain hardcoded dependency strings like:
  ```json
  "lib/strbuf.c lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c -Ilib/mem-pool/include"
  ```
- **Build system fragmentation**: Tests don't reuse main build system's object files
- **Manual library mapping**: No automatic dependency resolution from library names

### 🎯 **Next Priority Action Item**

---

## **PROPOSED NEXT ACTION: Phase 2 - Library Name References**

**Objective**: Replace hardcoded dependency strings with structured library name references in `test/test_config.json`.

**Specific Task**: 
1. Add `library_dependencies` field to test suite configurations
2. Create library name mappings that reference existing `build_lambda_config.json` library definitions
3. Implement dependency resolution in `test_all.sh` that translates library names to actual file paths/flags

**Example Transformation**:
```json
// BEFORE (Current):
"dependencies": [
  "lib/strbuf.c lib/mem-pool/src/variable.c -Ilib/mem-pool/include",
  "lib/strview.c", 
  "lib/num_stack.c"
]

// AFTER (Phase 2):
"library_dependencies": [
  ["strbuf", "mem-pool"],
  ["strview"],
  ["num_stack"]
]
```

**Expected Outcome**: 
- Single source of truth for library definitions
- Automatic propagation of library path changes to tests
- Foundation for Phase 3 object file reuse

**Estimated Effort**: 2-3 hours (modify config + implement resolution logic)

---

## Current State Analysis

### Problems with Current System

1. **Duplicated Dependency Management**: Test configurations have hardcoded dependency strings that duplicate library information already defined in `build_lambda_config.json`

2. **Inconsistent Compilation Logic**: `test_all.sh` has complex, duplicated compilation logic that doesn't leverage the robust dependency tracking in `compile.sh`

3. **Manual Dependency Tracking**: Test dependencies are manually specified as raw compiler flags instead of referencing the structured library definitions

4. **Build System Fragmentation**: Tests use different compilation approaches than the main build system, leading to inconsistencies

### Current Test Configuration Issues

Looking at the current `test_suites` in `build_lambda_config.json`:

```json
"dependencies": [
    "lib/strbuf.c lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c -Ilib/mem-pool/include",
    "lib/strview.c",
    "lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c -Ilib/mem-pool/include",
    "lib/num_stack.c",
    "lib/datetime.c lib/string.c lib/strbuf.c lib/strview.c lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c -Ilib/mem-pool/include"
]
```

These should reference library names instead of hardcoded paths.

## Enhancement Plan

### Phase 1: Refactor Test Configuration Structure

#### 1.1 Clean Up Test Dependencies
- Replace hardcoded dependency strings with library name references
- Define test-specific library configurations where needed
- Leverage existing library definitions from the main `libraries` section

#### 1.2 Standardize Test Library References
Current problematic dependencies should become:
```json
"library_dependencies": [
    ["strbuf", "mem-pool"],
    ["strview"],
    ["mem-pool"],
    ["num_stack"],
    ["datetime", "string", "strbuf", "strview", "mem-pool"]
]
```

#### 1.3 Add Test-Specific Libraries
Define libraries specifically for testing:
```json
"test_libraries": [
    {
        "name": "criterion",
        "include": "/opt/homebrew/Cellar/criterion/2.4.2_2/include",
        "lib": "/opt/homebrew/Cellar/criterion/2.4.2_2/lib",
        "link": "dynamic"
    },
    {
        "name": "mem-pool",
        "include": "lib/mem-pool/include",
        "sources": [
            "lib/mem-pool/src/variable.c",
            "lib/mem-pool/src/buffer.c",
            "lib/mem-pool/src/utils.c"
        ],
        "link": "inline"
    }
]
```

### Phase 2: Refactor test_all.sh to Leverage compile.sh

#### 2.1 Create Test-Specific Compilation Function
- Extract compilation logic from `compile.sh` into reusable functions
- Create `compile_test()` function that uses the same dependency resolution as main build
- Integrate with existing object file generation and library linking

#### 2.2 Automatic Dependency Resolution
```bash
# Instead of manual dependency strings:
compile_test() {
    local test_source="$1"
    local test_binary="$2"
    local library_deps="$3"  # Array of library names
    
    # Leverage compile.sh's library resolution
    local include_flags=""
    local static_libs=""
    local dynamic_libs=""
    
    # Resolve library dependencies using existing config
    for lib_name in $library_deps; do
        resolve_library_for_test "$lib_name"
    done
    
    # Use existing object files from main build
    collect_required_objects "$library_deps"
    
    # Compile test with resolved dependencies
    $CC $test_source -o $test_binary $include_flags $static_libs $dynamic_libs
}
```

#### 2.3 Reuse Object Files from Main Build
- Tests should use pre-compiled object files from `build/` directory
- Automatic dependency tracking based on which libraries the test needs
- No need to recompile source files that are already built

### Phase 3: Intelligent Test Dependency Management

#### 3.1 Automatic Source-to-Library Mapping
```bash
# Map test files to required libraries automatically
get_test_dependencies() {
    local test_file="$1"
    case "$test_file" in
        "test_strbuf.c")     echo "strbuf mem-pool" ;;
        "test_strview.c")    echo "strview" ;;
        "test_variable_pool.c") echo "mem-pool" ;;
        "test_math.c")       echo "math input format" ;;
        "test_validator.c")  echo "validator" ;;
    esac
}
```

#### 3.2 Object File Collection Strategy
```bash
collect_required_objects() {
    local lib_deps="$1"
    local object_files=()
    
    # For each library dependency, collect its object files
    for lib in $lib_deps; do
        case "$lib" in
            "strbuf")
                object_files+=("build/strbuf.o")
                ;;
            "mem-pool")
                object_files+=("build/variable.o" "build/buffer.o" "build/utils.o")
                ;;
            "input")
                object_files+=(build/input*.o)
                ;;
        esac
    done
    
    echo "${object_files[@]}"
}
```

### Phase 4: Unified Build System Integration

#### 4.1 Shared Configuration Parsing
- Extract JSON parsing functions from `compile.sh` into shared utilities
- Create `lib/build_utils.sh` with common functions:
  - `parse_library_config()`
  - `resolve_includes()`
  - `resolve_static_libs()`
  - `resolve_dynamic_libs()`

#### 4.2 Test-Specific Build Configs
```json
{
    "test_build_config": {
        "exclude_objects": ["main.o"],
        "additional_flags": ["-DTEST_MODE"],
        "test_libraries": ["criterion"],
        "memory_checking": {
            "address_sanitizer": true,
            "valgrind_support": true
        }
    }
}
```

#### 4.3 Platform-Specific Test Configuration
```json
"platforms": {
    "debug": {
        "test": {
            "flags": ["-fsanitize=address", "-g3"],
            "linker_flags": ["-fsanitize=address"]
        }
    },
    "windows": {
        "test": {
            "libraries": [
                {
                    "name": "criterion-windows",
                    "lib": "windows-deps/lib/libcriterion.a"
                }
            ]
        }
    }
}
```

### Phase 5: Implementation Strategy

#### 5.1 Backward Compatibility
- Keep old dependency format as fallback during transition
- Gradual migration of test suites to new format
- Deprecation warnings for old-style dependencies

#### 5.2 Incremental Migration
1. **Week 1**: Implement shared build utilities
2. **Week 2**: Refactor library dependency resolution
3. **Week 3**: Update test configurations to use library references
4. **Week 4**: Integrate with `compile.sh` dependency tracking
5. **Week 5**: Add platform-specific test configurations
6. **Week 6**: Remove deprecated legacy dependency formats

### Phase 6: Benefits and Expected Outcomes

#### 6.1 Maintenance Benefits
- **Single Source of Truth**: Library configurations defined once, used everywhere
- **Automatic Dependency Updates**: Changes to library paths automatically propagate to tests
- **Consistent Build Behavior**: Tests use same compilation logic as main build
- **Reduced Configuration Duplication**: ~80% reduction in duplicated dependency strings

#### 6.2 Developer Experience
- **Faster Test Builds**: Reuse pre-compiled object files
- **Better Error Messages**: Leverage `compile.sh`'s enhanced diagnostics
- **Cross-Platform Consistency**: Tests work identically across platforms
- **Easier Test Addition**: New tests automatically get correct dependencies

#### 6.3 Technical Improvements
- **Precise Dependency Tracking**: Use `.d` files for test compilation
- **Parallel Test Compilation**: Leverage existing parallel build infrastructure
- **Memory Safety**: AddressSanitizer integration for debug test builds
- **Build Cache Efficiency**: Share object files between main build and tests

## Implementation Notes

### Critical Dependencies to Address
1. **ICU Library Integration**: Ensure Unicode support libraries are properly referenced
2. **MIR JIT Dependencies**: Complex linking requirements for JIT compilation tests
3. **Platform-Specific Libraries**: Handle Windows cross-compilation test dependencies
4. **Tree-sitter Integration**: Parser library dependencies for lambda runtime tests

### Testing the Enhanced System
1. **Regression Testing**: All existing tests must continue to pass
2. **Build Performance**: Measure compilation time improvements
3. **Cross-Platform Validation**: Ensure tests work on macOS, Linux, Windows
4. **Memory Leak Detection**: Verify AddressSanitizer integration works correctly

This plan transforms the test system from a collection of manual dependency specifications into an intelligent, automated system that leverages the existing robust build infrastructure while maintaining full backward compatibility during migration.
