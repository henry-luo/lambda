# Windows Native Compilation Setup for Lambda Script

## Overview

This document describes how to set up native Windows compilation for Lambda Script using MSYS2 and Clang, as an alternative to cross-compilation from macOS/Linux.

## Which Bash to Use on Windows?

For the best VS Code integration and development experience on Windows, we recommend **MSYS2** over Git Bash. Here's why:

### MSYS2 (Recommended) 🏆

**Pros:**
- Full POSIX environment with package manager (pacman)
- Native Windows compilation support
- Excellent VS Code integration
- Multiple environments: CLANG64, MINGW64, MSYS
- Easy dependency management
- Active development and maintenance

**Cons:**
- Larger installation size
- Learning curve for pacman package manager

**VS Code Integration:**
```json
{
    "terminal.integrated.profiles.windows": {
        "MSYS2 CLANG64": {
            "path": "C:\\msys64\\usr\\bin\\bash.exe",
            "args": ["--login"],
            "env": {
                "MSYSTEM": "CLANG64",
                "CHERE_INVOKING": "1"
            }
        }
    },
    "terminal.integrated.defaultProfile.windows": "MSYS2 CLANG64"
}
```

### Git Bash (Limited)

**Pros:**
- Lightweight installation
- Familiar to Git users
- Good for basic scripting

**Cons:**
- Limited package ecosystem
- No native compiler toolchain
- Harder to install development dependencies
- Less suitable for complex builds

## MSYS2 Environments Explained

MSYS2 provides three main environments:

### 1. CLANG64 (Recommended for Lambda)
- **Compiler:** Clang/LLVM
- **Target:** Native Windows x86_64
- **Libraries:** LLVM-based toolchain
- **Best for:** Modern C++, better error messages, cross-platform compatibility

### 2. MINGW64
- **Compiler:** GCC
- **Target:** Native Windows x86_64  
- **Libraries:** Traditional GCC toolchain
- **Best for:** Maximum compatibility, established workflows

### 3. MSYS
- **Purpose:** POSIX environment layer
- **Best for:** Running Unix tools, not for compilation

## Installation Guide

### Step 1: Install MSYS2

1. Download MSYS2 from: https://www.msys2.org/
2. Run the installer and follow the setup instructions
3. Update the package database:
   ```bash
   pacman -Syu
   ```

### Step 2: Configure VS Code

Add this to your VS Code `settings.json`:

```json
{
    "terminal.integrated.profiles.windows": {
        "MSYS2 CLANG64": {
            "path": "C:\\msys64\\usr\\bin\\bash.exe",
            "args": ["--login"],
            "env": {
                "MSYSTEM": "CLANG64",
                "CHERE_INVOKING": "1"
            }
        },
        "MSYS2 MINGW64": {
            "path": "C:\\msys64\\usr\\bin\\bash.exe", 
            "args": ["--login"],
            "env": {
                "MSYSTEM": "MINGW64",
                "CHERE_INVOKING": "1"
            }
        }
    },
    "terminal.integrated.defaultProfile.windows": "MSYS2 CLANG64"
}
```

### Step 3: Set Up Lambda Dependencies

Run the setup script in MSYS2 terminal:

```bash
# Open MSYS2 CLANG64 terminal in VS Code
cd /path/to/lambda/project
./setup-win-native-deps.sh
```

## Building Lambda Natively

### Quick Build
```bash
# Use the convenience script
./compile-win-native.sh
```

### Manual Build
```bash
# Use specific configuration
./compile.sh build_lambda_win_native_config.json
```

### Debug Build
```bash
# Debug build with AddressSanitizer
./compile-win-native.sh --debug
```

## Compiler Differences

### Clang vs GCC for Lambda

| Feature | Clang (CLANG64) | GCC (MINGW64) |
|---------|----------------|---------------|
| Error Messages | ⭐⭐⭐⭐⭐ Excellent | ⭐⭐⭐ Good |
| C++17 Support | ⭐⭐⭐⭐⭐ Excellent | ⭐⭐⭐⭐ Good |
| Build Speed | ⭐⭐⭐⭐ Fast | ⭐⭐⭐ Moderate |
| Binary Size | ⭐⭐⭐ Moderate | ⭐⭐⭐⭐ Smaller |
| Compatibility | ⭐⭐⭐⭐ Good | ⭐⭐⭐⭐⭐ Excellent |
| AddressSanitizer | ⭐⭐⭐⭐⭐ Full Support | ⭐⭐⭐ Limited |

**Recommendation:** Use CLANG64 for development (better debugging tools) and MINGW64 for production builds if needed.

## Troubleshooting

### Common Issues

1. **"pacman not found"**
   - Ensure you're in MSYS2 terminal, not Git Bash
   - Check MSYS2 installation

2. **Compiler not found**
   - Verify MSYSTEM environment variable
   - Run `pacman -S mingw-w64-clang-x86_64-clang` for Clang

3. **Permission denied on scripts**
   - Run `chmod +x script-name.sh`
   - Check file line endings (should be LF, not CRLF)

4. **Library linking errors**
   - Ensure dependencies are built for correct architecture
   - Check library paths in configuration

### VS Code Integration Issues

1. **Terminal doesn't start**
   - Check MSYS2 installation path in settings
   - Verify bash.exe exists at specified path

2. **Wrong environment loaded**
   - Check MSYSTEM environment variable in terminal
   - Restart VS Code after changing settings

3. **IntelliSense not working**
   - Configure C++ extension with correct compiler paths
   - Add include paths from MSYS2

## Performance Comparison

| Build Type | Cross-compile (macOS→Windows) | Native Windows (MSYS2) |
|------------|------------------------------|------------------------|
| Setup Time | ⭐⭐ Complex | ⭐⭐⭐⭐ Simple |
| Build Speed | ⭐⭐⭐ Good | ⭐⭐⭐⭐ Faster |
| Debugging | ⭐⭐ Limited | ⭐⭐⭐⭐⭐ Excellent |
| Dependencies | ⭐⭐ Manual | ⭐⭐⭐⭐⭐ Package manager |
| VS Code Integration | ⭐⭐⭐ Good | ⭐⭐⭐⭐⭐ Excellent |

## Next Steps

1. **Set up VS Code terminal profiles** for MSYS2
2. **Run the setup script** to install dependencies  
3. **Test compilation** with the convenience script
4. **Configure debugging** in VS Code with GDB
5. **Set up continuous integration** for Windows builds

## Advanced Configuration

### Custom Compiler Flags

Edit `build_lambda_win_native_config.json` to customize:

```json
{
    "flags": [
        "std=c++17",
        "fms-extensions", 
        "pedantic",
        "fcolor-diagnostics",
        "O2",
        "DNATIVE_WINDOWS_BUILD"
    ]
}
```

### Adding New Dependencies

Use pacman to install system packages:

```bash
# Search for packages
pacman -Ss package-name

# Install development packages
pacman -S mingw-w64-clang-x86_64-package-name
```

### Integration with WSL

If you also use WSL, you can cross-compile between environments:

```bash
# In WSL: Cross-compile for Windows
./compile.sh --platform=windows

# In MSYS2: Native Windows compilation  
./compile-win-native.sh
```

This setup provides a robust, native Windows development environment for Lambda Script that integrates well with VS Code and provides excellent debugging capabilities.
