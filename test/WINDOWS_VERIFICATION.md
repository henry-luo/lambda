# Windows Cross-Compilation Verification

This document describes how to verify the cross-compiled `lambda-windows.exe` executable on macOS using Wine.

## Prerequisites

- Wine installed (`brew install wine`)
- MinGW-w64 cross-compiler installed (`brew install mingw-w64`)
- Built `lambda-windows.exe` (run `../compile.sh --platform=windows`)

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
# Type: :help
# Type: :quit
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

- Wine performance may be slower than native Windows execution
- Some timing-dependent operations may behave differently
- Graphics/UI operations may have limited support in Wine
- For production use, always test on real Windows systems
