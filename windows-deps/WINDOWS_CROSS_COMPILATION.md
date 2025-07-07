# Windows Cross-Compilation Guide for Jubily Lambda Project

## 🎯 **Current Status**

✅ **Working**: 
- MinGW-w64 cross-compiler setup
- Basic cross-compilation infrastructure
- Tree-sitter cross-compilation
- Enhanced build scripts with cross-compilation support

⚠️ **Challenges**:
- Complex dependency chain (GMP, MIR, zlog, lexbor)
- Some dependencies may not support Windows
- Intricate API compatibility requirements

## 📋 **Files Created**

### 1. Enhanced Build Scripts
- `compile-lambda-cross.sh` - Cross-compilation build script
- `setup-windows-deps.sh` - Dependency setup (real libraries)
- `setup-windows-deps-stub.sh` - Stub setup (compilation testing)
- `test-windows-setup.sh` - Testing script

### 2. Configuration Files
- `build_lambda_windows_config.json` - Full Windows build config
- `build_lambda_minimal_windows_config.json` - Minimal test config

## 🔧 **Cross-Compilation Architecture**

```
macOS Development Machine
├── MinGW-w64 Cross-Compiler (x86_64-w64-mingw32)
├── Windows Dependencies (windows-deps/)
│   ├── include/ (Headers)
│   ├── lib/ (Static libraries)
│   └── src/ (Source code)
└── Enhanced Build System
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

### Step 2: Build Dependencies

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

#### Tree-sitter (Already Working)
```bash
cd lambda/tree-sitter
make clean
CC=x86_64-w64-mingw32-gcc \
AR=x86_64-w64-mingw32-ar \
CFLAGS="-O3 -static" \
make libtree-sitter.a
```

#### MIR (JIT Compiler) - Challenging
```bash
# Check if MIR supports Windows cross-compilation
git clone https://github.com/vnmakarov/mir.git windows-deps/src/mir
cd windows-deps/src/mir

# May need patches for Windows support
# Consult MIR documentation for cross-compilation
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

| Library | Windows Support | Difficulty | Alternative |
|---------|----------------|------------|-------------|
| tree-sitter | ✅ Good | Easy | - |
| GMP | ✅ Good | Medium | MPIR (Windows-native) |
| lexbor | 🟡 Partial | Medium | Alternative HTML parsers |
| zlog | 🟡 Unknown | Hard | Windows Event Logging |
| MIR | 🔴 Limited | Very Hard | LLVM, TCC |

## 🎯 **Recommended Path Forward**

### Immediate (Testing)
1. Use stub libraries for compilation testing
2. Verify cross-compilation infrastructure works
3. Identify minimal working subset

### Short-term (MVP)
1. Replace problematic dependencies with Windows-compatible alternatives
2. Create simplified Windows build configuration
3. Focus on core functionality

### Long-term (Production)
1. Work with upstream projects for Windows support
2. Consider alternative architectures (e.g., WebAssembly)
3. Implement Windows-native alternatives

## 🔍 **Debugging Tips**

### Check Cross-Compiler
```bash
x86_64-w64-mingw32-gcc --version
which x86_64-w64-mingw32-gcc
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

## ⚠️ **Known Issues**

1. **mpf_t type**: GMP floating-point type missing from stub
2. **mir-gen.h**: MIR code generation header not available
3. **lexbor API**: Complex URL parsing API incompatibilities
4. **Static linking**: Large executable size due to static linking

## 🎉 **Success Criteria**

- [ ] All source files compile without errors
- [ ] Linker creates valid PE32+ executable
- [ ] Binary runs on Windows (even with limited functionality)
- [ ] Core language features work cross-platform

This comprehensive setup provides the foundation for Windows cross-compilation. The main challenge is building or replacing the complex dependencies with Windows-compatible alternatives.
