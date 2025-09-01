# Windows Cross-Compilation Verification

This document describes how to verify the cross-compiled `lambda-windows.exe` executable on macOS using Wine.

## Prerequisites

- Wine installed (`brew install wine`)
- MinGW-w64 cross-compiler installed (`brew install mingw-w64`)
- Built `lambda-windows.exe` (run `../compile.sh --platform=windows`)

## How Wine Works on Apple Silicon Macs

### Architecture Translation Chain

When running `lambda-windows.exe` (compiled for x86-64 Windows) on an Apple Silicon Mac, multiple translation layers work together:

```
lambda-windows.exe (x86-64 Windows PE)
           ↓
    Wine (API Translation)
           ↓
    x86-64 Unix/macOS calls
           ↓
    Rosetta 2 (CPU Translation)
           ↓
    ARM64 macOS System
```

### **1. Cross-Compilation (Build Time)**
```bash
# Your compile.sh uses MinGW-w64 cross-compiler
x86_64-w64-mingw32-gcc source.c → lambda-windows.exe (x86-64 PE format)
```

### **2. Wine API Translation (Runtime)**
- **What Wine does**: Translates Windows API calls to Unix/macOS equivalents
- **CPU architecture**: Wine itself runs as x86-64 on your Mac
- **Example translation**:
  ```c
  // Windows API call in your exe
  CreateFile("test.txt", ...)
  
  // Wine translates to
  open("test.txt", ...)  // macOS system call
  ```

### **3. Rosetta 2 CPU Translation (Hardware Level)**
- **What Rosetta 2 does**: Translates x86-64 machine code to ARM64
- **When it happens**: Automatically when x86-64 binaries run on Apple Silicon
- **Performance**: ~80% of native speed (very efficient)

### **Architecture Check**
You can verify this translation chain:

```bash
# Check Wine architecture
file $(which wine)
# Output: Mach-O 64-bit executable x86_64

# Check your Windows executable
file ../lambda-windows.exe  
# Output: PE32+ executable (console) x86-64, for MS Windows

# Check what's actually running
ps aux | grep wine
# Shows wine running under Rosetta 2 (if on Apple Silicon)
```

### **Verify Translation Chain on Your System**

```bash
# 1. Check your Mac's CPU architecture
uname -m
# Output: arm64 (Apple Silicon)

# 2. Check Wine's architecture  
file $(which wine)
# Output: Mach-O 64-bit executable x86_64 (runs under Rosetta 2)

# 3. Check Windows executable architecture
file ../lambda-windows.exe
# Output: PE32+ executable (console) x86-64, for MS Windows

# 4. Verify Rosetta 2 is handling Wine
ps aux | grep wine | head -1
# Look for wine processes running under Rosetta translation
```

This confirms the translation chain: **ARM64 Mac → Rosetta 2 → x86-64 Wine → x86-64 Windows exe**

### **Why This Works**

1. **Wine compatibility**: Wine runs on both Intel and Apple Silicon Macs
2. **Rosetta 2**: Apple's translation layer handles x86-64 → ARM64 conversion
3. **API abstraction**: Wine's API translation is architecture-agnostic
4. **Performance**: Multiple translation layers but still reasonably fast

### **Alternative Approaches**

| Method | CPU Support | Performance | Complexity |
|--------|-------------|-------------|------------|
| **Current (Wine + Rosetta)** | x86-64 → ARM64 | ~70% | Low |
| **Native ARM64 Wine** | ARM64 only | ~85% | Medium |
| **ARM64 Cross-compile** | ARM64 native | 100% | High |

### **Performance Considerations**

- **Wine overhead**: ~10% (API translation)
- **Rosetta 2 overhead**: ~20% (CPU translation)  
- **Combined**: ~70% of native performance
- **Your use case**: Console apps perform better than GUI apps

## Verification Scripts

### 1. Full Verification Script: `verify-windows-exe.sh`

Comprehensive testing script that performs:
- Executable format verification
- Basic execution tests
- Sample file processing
- Native vs Wine output comparison
- Memory/crash testing
- Dependency analysis

```bash
cd test
./verify-windows-exe.sh
```

Or from the project root:
```bash
make verify-windows
```

### 2. CI Testing Script: `test-windows-exe-ci.sh`

Streamlined script for automated testing:
- Returns 0 on success, non-zero on failure
- Suitable for CI/CD pipelines
- Focuses on core functionality

```bash
cd test
./test-windows-exe-ci.sh
```

Or from the project root:
```bash
make test-windows
```

## Manual Verification

### Basic Test
```bash
wine ../lambda-windows.exe --help
```

### Script Processing Test
```bash
wine ../lambda-windows.exe hello-world.ls
```

### REPL Mode Test
```bash
wine ../lambda-windows.exe
# Type: .help
# Type: .quit
```

## Common Issues and Solutions

### Missing DLL Error
If you see `libwinpthread-1.dll not found`:

1. The verification scripts automatically handle this
2. Manual fix: Copy the DLL to Wine's system directory
   ```bash
   mkdir -p ~/.wine/drive_c/windows/system32/
   cp /opt/homebrew/Cellar/mingw-w64/*/toolchain-x86_64/x86_64-w64-mingw32/bin/libwinpthread-1.dll ~/.wine/drive_c/windows/system32/
   ```

### Wine Warnings
Wine may show various warnings about missing features or unsupported operations. These are generally harmless if the executable runs successfully.

## Additional Verification Methods

### Real Windows Testing
For complete verification, test on actual Windows systems:
1. Copy `../lambda-windows.exe` to a Windows machine
2. Run the same test commands
3. Compare functionality with the native version

### Virtual Machine Testing
Use Windows VMs for more thorough testing:
- VirtualBox, VMware, or Parallels
- Windows Subsystem for Linux (WSL) on Windows

### Automated CI Testing
For continuous integration, consider:
- GitHub Actions with Windows runners
- Azure DevOps Windows build agents
- AppVeyor Windows CI

## Build Troubleshooting

If the Windows executable doesn't work:

1. **Check the build**: Ensure cross-compilation completed without errors
   ```bash
   ../compile.sh --platform=windows --force
   ```

2. **Verify dependencies**: Check that all Windows libraries are properly linked
   ```bash
   objdump -p ../lambda-windows.exe | grep "DLL Name"
   ```

3. **Test native build**: Ensure the native version works first
   ```bash
   ../compile.sh --force
   ../lambda.exe --help
   ```

## Success Indicators

A successful verification should show:
- ✓ Wine environment working
- ✓ Executable loads and runs
- ✓ Help text displays correctly
- ✓ Script files can be processed
- ✓ No critical Wine errors

## Notes

- **Apple Silicon Compatibility**: On Apple Silicon Macs, Wine runs under Rosetta 2, which translates x86-64 instructions to ARM64. This adds translation overhead but maintains compatibility.
- Wine performance may be slower than native Windows execution
- Some timing-dependent operations may behave differently
- Graphics/UI operations may have limited support in Wine
- For production use, always test on real Windows systems

### Performance on Apple Silicon
- **Translation layers**: Windows API → Unix API → x86-64 → ARM64
- **Expected performance**: ~70% of native speed due to dual translation
- **Console applications**: Perform better than GUI applications under Wine
- **Memory usage**: Slightly higher due to translation overhead
