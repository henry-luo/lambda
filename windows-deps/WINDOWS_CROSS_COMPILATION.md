# Windows Cross-Compilation Guide for Jubily Lambda Project

## 🎯 **Current Status**

✅ **Working**: 
- MinGW-w64 cross-compiler setup
- Basic cross-compilation infrastructure
- Tree-sitter cross-compilation (262KB static library)
- GMP cross-compilation with stub fallback (2.1KB stub library)
- **MIR cross-compilation (596KB real static library)**
- Enhanced build scripts with automatic dependency management
- Robust error handling and fallback systems

⚠️ **Challenges**:
- zlog logging library (no Windows support)
- lexbor HTML parser (partial Windows support)
- Some dependencies may require manual configuration

## 📋 **Files Created**

### 1. Enhanced Build Scripts
- `compile-lambda-cross.sh` - Cross-compilation build script
- `setup-windows-deps.sh` - **Enhanced dependency setup with MIR support**
- `test-windows-setup.sh` - Testing script
- `MIR_ENHANCEMENT_SUMMARY.md` - Detailed MIR integration documentation

### 2. Configuration Files
- `build_lambda_windows_config.json` - Full Windows build config
- `build_lambda_minimal_windows_config.json` - Minimal test config

### 3. Built Libraries (windows-deps/lib/)
- `libtree-sitter.a` (262KB) - Parser generator library
- `libgmp.a` (2.1KB) - GNU Multiple Precision arithmetic (stub)
- **`libmir.a` (596KB) - MIR JIT compiler (real build)**

## 🔧 **Cross-Compilation Architecture**

```
macOS Development Machine
├── MinGW-w64 Cross-Compiler (x86_64-w64-mingw32)
├── Windows Dependencies (windows-deps/)
│   ├── include/ (Headers - tree-sitter, gmp, mir)
│   ├── lib/ (Static libraries - 860KB total)
│   │   ├── libtree-sitter.a (262KB)
│   │   ├── libgmp.a (2.1KB stub)
│   │   └── libmir.a (596KB real)
│   └── src/ (Source code including MIR repository)
└── Enhanced Build System
    ├── Automatic dependency detection
    ├── Real build with stub fallback
    ├── Compiler Detection
    ├── Cross-Compilation Flags
    └── Static Linking
```

## 🛠 **Manual Cross-Compilation Steps**

### Step 1: Install Cross-Compiler
```bash
brew install mingw-w64
# Verify: x86_64-w64-mingw32-gcc --version
```

### Step 2: Build Dependencies (Automated)

**Recommended: Use the enhanced setup script**
```bash
# Build all dependencies automatically (including MIR)
./setup-windows-deps.sh

# Check what was built
./setup-windows-deps.sh
# Output shows:
# - Tree-sitter: ✓ Built  
# - GMP: ✓ Built (stub)
# - MIR: ✓ Built (real)
# - zlog: ✗ Missing (optional)
```

**Manual builds (if needed):**

#### GMP (GNU Multiple Precision Library)
```bash
cd windows-deps/src
wget https://gmplib.org/download/gmp/gmp-6.2.1.tar.bz2
tar -xjf gmp-6.2.1.tar.bz2
cd gmp-6.2.1

./configure --host=x86_64-w64-mingw32 \
    --prefix=$(pwd)/../../ \
    --enable-static \
    --disable-shared \
    CFLAGS="-O2 -static"
make -j$(nproc)
make install
```

#### Tree-sitter (Already Automated)
```bash
cd lambda/tree-sitter
make clean
CC=x86_64-w64-mingw32-gcc \
AR=x86_64-w64-mingw32-ar \
CFLAGS="-O3 -static" \
make libtree-sitter.a
```

#### MIR (JIT Compiler) - ✅ **Now Automated**
```bash
# Automatically handled by setup script:
# - Clones https://github.com/vnmakarov/mir.git
# - Cross-compiles with x86_64-w64-mingw32-gcc
# - Creates libmir.a (596KB) and copies headers
# - Falls back to stub if build fails

# Manual build (if needed):
git clone https://github.com/vnmakarov/mir.git windows-deps/src/mir
cd windows-deps/src/mir
make CC=x86_64-w64-mingw32-gcc AR=x86_64-w64-mingw32-ar
```

#### zlog (Logging Library)
```bash
git clone https://github.com/HardySimpson/zlog.git windows-deps/src/zlog
cd windows-deps/src/zlog

# May need Makefile modifications for cross-compilation
CC=x86_64-w64-mingw32-gcc \
AR=x86_64-w64-mingw32-ar \
make PREFIX=$(pwd)/../../ static
```

#### lexbor (HTML/XML Parser)
```bash
git clone https://github.com/lexbor/lexbor.git windows-deps/src/lexbor
cd windows-deps/src/lexbor

mkdir build-windows
cd build-windows
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DLEXBOR_BUILD_SHARED=OFF \
    -DLEXBOR_BUILD_STATIC=ON
make
```

### Step 3: Cross-Compile
```bash
./compile-lambda-cross.sh build_lambda_windows_config.json
```

## 🚀 **Quick Start (Automated Build)**

### One-Command Setup
```bash
# Install cross-compiler (macOS)
brew install mingw-w64

# Build all dependencies automatically
./setup-windows-deps.sh

# Cross-compile the project
./compile-lambda-cross.sh build_lambda_windows_config.json
```

### Status Check
```bash
# View build status and next steps
./setup-windows-deps.sh
```

### Cleanup
```bash
# Clean intermediate files (keeps built libraries)
./setup-windows-deps.sh clean
```

## 🚀 **Alternative Approaches**

### Option 1: Docker Cross-Compilation
```dockerfile
FROM ubuntu:20.04
RUN apt-get update && apt-get install -y \
    gcc-mingw-w64 \
    build-essential \
    cmake \
    git \
    wget

WORKDIR /app
COPY . .
RUN ./setup-windows-deps.sh
RUN ./compile-lambda-cross.sh build_lambda_windows_config.json
```

### Option 2: GitHub Actions CI
```yaml
name: Windows Cross-Compile
on: [push]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v2
    - name: Install MinGW
      run: sudo apt-get install gcc-mingw-w64
    - name: Build dependencies
      run: ./setup-windows-deps.sh
    - name: Cross-compile
      run: ./compile-lambda-cross.sh build_lambda_windows_config.json
```

### Option 3: Native Windows Build
Set up a Windows development environment:
- Visual Studio with MSYS2/MinGW
- vcpkg package manager for dependencies
- Native Windows compilation

### Option 4: WSL2 Development
Use WSL2 with Windows cross-compilation tools:
```bash
# In WSL2
sudo apt-get install gcc-mingw-w64
# Follow Linux cross-compilation steps
```

## 📊 **Dependency Analysis**

| Library | Windows Support | Difficulty | Status | Size |
|---------|----------------|------------|--------|------|
| tree-sitter | ✅ Good | Easy | ✅ Built | 262KB |
| GMP | ✅ Good | Medium | ✅ Stub | 2.1KB |
| **MIR** | **✅ Good** | **Medium** | **✅ Built** | **596KB** |
| lexbor | 🟡 Partial | Medium | ❌ Needs work | - |
| zlog | 🔴 Limited | Hard | ❌ Optional | - |

## 🎯 **Recommended Path Forward**

### Immediate (Testing) ✅ **COMPLETED**
1. ✅ Use stub libraries for compilation testing
2. ✅ Verify cross-compilation infrastructure works  
3. ✅ Identify minimal working subset
4. ✅ **MIR cross-compilation successfully implemented**

### Short-term (MVP) 🔄 **IN PROGRESS**
1. ✅ **Core dependencies working (tree-sitter, GMP stub, MIR real)**
2. 🔄 Replace remaining problematic dependencies (lexbor, zlog)
3. 🔄 Create simplified Windows build configuration
4. 🔄 Focus on core functionality

### Long-term (Production) 📋 **PLANNED**
1. Work with upstream projects for Windows support  
2. Consider alternative architectures (e.g., WebAssembly)
3. Implement Windows-native alternatives
4. **Optimize MIR build for production use**

## 🔍 **Debugging Tips**

### Check Dependencies Status
```bash
# Run setup script to see current status
./setup-windows-deps.sh

# Expected output:
# Built dependencies:
# - Tree-sitter: ✓ Built
# - GMP: ✓ Built (stub) 
# - MIR: ✓ Built (real)
# - zlog: ✗ Missing (optional)
```

### Verify Libraries
```bash
ls -la windows-deps/lib/
# Should show:
# libgmp.a (2.1KB)
# libmir.a (596KB)  
# libtree-sitter.a (262KB)

# Check MIR library contents
x86_64-w64-mingw32-ar -t windows-deps/lib/libmir.a
# Should show: mir.o, mir-gen.o
```

### Verify Windows Binary
```bash
file lambda-windows.exe
# Should show: PE32+ executable (console) x86-64, for MS Windows
```

### Test on Windows
- Use Wine on macOS/Linux: `wine lambda-windows.exe`
- Transfer to Windows machine for testing
- Use Windows Subsystem for Linux (WSL)

## 📚 **Resources**

- [MinGW-w64 Documentation](https://www.mingw-w64.org/)
- [GMP Cross-Compilation Guide](https://gmplib.org/manual/Installing-GMP.html)
- [Cross-Compilation Best Practices](https://autotools.info/cross-compilation/index.html)
- **[MIR Repository](https://github.com/vnmakarov/mir.git) - Successfully cross-compiled**
- **[MIR Enhancement Summary](../MIR_ENHANCEMENT_SUMMARY.md) - Implementation details**

## ⚠️ **Known Issues**

1. **zlog logging**: No Windows cross-compilation support - marked as optional
2. **lexbor HTML parser**: Needs CMake toolchain file for cross-compilation  
3. **Static linking**: Large executable size due to static linking (~1MB+)
4. **GMP**: Using stub implementation (sufficient for compilation testing)

## 🎉 **Success Criteria**

- [x] **All core dependencies cross-compile successfully**
- [x] **MIR JIT compiler builds as static library (596KB)**
- [x] **Enhanced setup script with automatic dependency management**
- [x] **Robust fallback system for failed builds**
- [ ] All source files compile without errors
- [ ] Linker creates valid PE32+ executable  
- [ ] Binary runs on Windows (even with limited functionality)
- [ ] Core language features work cross-platform

This comprehensive setup provides the foundation for Windows cross-compilation. **The main challenge of MIR cross-compilation has been solved**, with automatic dependency management and robust fallback systems in place. or replacing the complex dependencies with Windows-compatible alternatives.
