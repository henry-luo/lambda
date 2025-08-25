# Premake5 Migration Plan for Lambda Test System

## Overview

This document outlines the migration plan from the current shell-based test build system (`test_build.sh`) to a Premake5-based system while preserving the existing JSON configuration structure in `build_lambda_config.json`.

## Current System Analysis

### Current Architecture
```
build_lambda_config.json → test_build.sh → Direct compilation/linking
```

### Current Components
- **Configuration**: `build_lambda_config.json` contains test suites, library dependencies, and build flags
- **Build Script**: `test_build.sh` (494 lines) parses JSON and manages compilation
- **Test Suites**: 6 test suites (library, input, validator, mir, lambda, lambda-std)
- **Libraries**: 15+ library configurations with complex dependency chains
- **Parallel Building**: Supported via shell background processes

### Current Pain Points
1. **Complexity**: Shell script has grown to 494 lines with complex dependency resolution
2. **Duplicate Symbol Issues**: Manual handling of conflicting object files
3. **Library Management**: Complex library dependency chains hard to debug
4. **Platform Support**: Limited cross-platform build capabilities
5. **IDE Integration**: No native IDE project file generation

## Proposed New Architecture

```
build_lambda_config.json → generate_premake.py → premake5.lua → Makefile → Test Executables
```

## Migration Plan

### Phase 1: JSON Config Parser & Premake Generator

**File**: `utils/generate_premake.py`

**Responsibilities**:
- Parse `build_lambda_config.json`
- Extract test suite configurations
- Generate `premake5.lua` with proper project structure
- Handle library dependency resolution
- Map JSON flags to Premake5 equivalents

**Key Mappings**:
```python
# JSON to Premake5 mappings
json_to_premake = {
    "special_flags": "buildoptions",
    "cpp_flags": "cppdialect", 
    "library_dependencies": "links",
    "sources": "files",
    "parallel": "parallelbuilds"
}
```

### Phase 2: Premake5 Project Structure

**File**: `premake5.lua` (generated)

**Structure**:
```lua
workspace "LambdaTests"
    configurations { "Debug", "Release" }
    platforms { "x64" }
    
-- Static libraries for reusable components
project "lambda-lib"
    kind "StaticLib"
    -- Library sources and dependencies
    
project "lambda-runtime-full" 
    kind "StaticLib"
    -- Full runtime sources
    
-- Test executables
project "test_strbuf"
    kind "ConsoleApp"
    links { "lambda-lib", "criterion" }
    -- Test-specific configuration
```

### Phase 3: Build Pipeline Integration

**Makefile Integration**:
```makefile
# New test build targets
generate-premake:
	python3 utils/generate_premake.py

build-test: generate-premake
	premake5 gmake2
	make -C build/tests config=debug

clean-test:
	rm -rf build/tests premake5.lua
```

## Technical Implementation Details

### 1. JSON Configuration Preservation

The existing `build_lambda_config.json` structure will be preserved:
- `libraries[]`: Static library definitions
- `test.test_suites[]`: Test suite configurations
- Platform-specific settings maintained

### 2. Library Dependency Resolution

**Current Issues**:
- Duplicate symbols between `test_context.o` and `lambda-mem.o`
- Complex manual object file inclusion/exclusion

**Premake5 Solution**:
```lua
-- Separate library projects prevent symbol conflicts
project "lambda-mem"
    kind "StaticLib"
    files { "lambda/lambda-mem.cpp" }
    
project "test-context"  
    kind "StaticLib"
    files { "test/test_context.c" }
    excludes { "lambda/lambda-mem.cpp" } -- Explicit exclusion
```

### 3. Test Suite Generation

Each test suite becomes a Premake5 filter:
```lua
filter "configurations:Debug"
    defines { "DEBUG" }
    symbols "On"
    
filter "files:test_*.c"
    buildoptions { "-fms-extensions" }
```

### 4. Parallel Build Support

Premake5 native parallel building:
```lua
workspace "LambdaTests"
    parallelbuilds "On" 
    numjobs(8) -- From JSON config
```

## Migration Benefits

### 1. **Maintainability**
- Declarative configuration vs imperative shell scripts
- IDE project file generation (Xcode, Visual Studio, etc.)
- Better dependency visualization

### 2. **Reliability** 
- Premake5 handles symbol conflicts automatically
- Proper incremental builds
- Cross-platform compatibility

### 3. **Performance**
- Native parallel building
- Better dependency tracking
- Faster incremental builds

### 4. **Developer Experience**
- IDE integration for debugging tests
- IntelliSense support in generated projects
- Visual dependency graphs

## Implementation Steps

### Step 1: Create JSON Parser
```python
# utils/generate_premake.py
def parse_test_config(json_file):
    """Parse build_lambda_config.json and extract test configuration"""
    
def generate_library_projects(libraries):
    """Generate static library projects from JSON config"""
    
def generate_test_projects(test_suites):
    """Generate test executable projects"""
    
def write_premake_file(output_path):
    """Write complete premake5.lua file"""
```

### Step 2: Generate Premake5 Template
- Workspace configuration
- Library project templates
- Test project templates
- Platform-specific settings

### Step 3: Integrate with Makefile
- Add `generate-premake` target
- Update `build-test` to use Premake5
- Preserve existing `make test` workflow

### Step 4: Validation & Testing
- Compare build outputs with current system
- Verify all 14 test executables build correctly
- Performance benchmarking

## Compatibility Considerations

### Backward Compatibility
- Keep existing `test_build.sh` during transition
- Makefile targets remain the same (`make build-test`, `make test`)
- JSON configuration format unchanged

### Platform Support
- macOS: Native Xcode project generation
- Linux: Makefile generation
- Windows: Visual Studio project support

## Risk Mitigation

### 1. **Gradual Migration**
- Implement alongside existing system
- A/B testing of build outputs
- Rollback capability maintained

### 2. **Configuration Validation**
- JSON schema validation
- Premake5 syntax checking
- Build verification tests

### 3. **Documentation**
- Migration guide for developers
- Troubleshooting documentation
- Performance comparison metrics

## Success Criteria

1. **Functional**: All 14 test executables build successfully
2. **Performance**: Build time ≤ current system performance
3. **Maintainability**: Reduced complexity in build configuration
4. **Reliability**: Zero duplicate symbol conflicts
5. **Developer Experience**: IDE project file generation working

## Timeline Estimate

- **Phase 1** (JSON Parser): 2-3 days
- **Phase 2** (Premake5 Generation): 3-4 days  
- **Phase 3** (Integration): 1-2 days
- **Phase 4** (Testing & Validation): 2-3 days

**Total**: ~8-12 days for complete migration

## Future Enhancements

1. **Test Discovery**: Automatic test file detection
2. **Code Coverage**: Integration with coverage tools
3. **Continuous Integration**: GitHub Actions integration
4. **Cross-Compilation**: Enhanced platform support
5. **Package Management**: Conan/vcpkg integration for dependencies
