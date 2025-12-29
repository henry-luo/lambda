# Windows Build Notes

## Building on Windows

Lambda builds on Windows using MSYS2/MINGW64 environment.

### Quick Start

```bash
# Setup dependencies (one-time)
./setup-windows-deps.sh

# Build
make build

# Run (requires /mingw64/bin in PATH)
PATH="/mingw64/bin:$PATH" ./lambda.exe
```

### DLL Dependencies

Currently, lambda.exe requires some MINGW64 DLLs at runtime:
- libcurl-4.dll and its dependencies (ssl, crypto, ssh2, idn2, psl, nghttp2, etc.)
- libfreetype-6.dll
- libpng16-16.dll
- libgcc_s_seh-1.dll
- libgomp-1.dll  
- libwinpthread-1.dll
- libstdc++-6.dll
- zlib1.dll

**Option 1: Add /mingw64/bin to PATH** (recommended for development):
```bash
export PATH="/mingw64/bin:$PATH"
./lambda.exe
```

**Option 2: Copy DLLs** to lambda.exe directory:
```bash
cp /mingw64/bin/{libcurl-4.dll,libfreetype-6.dll,libpng16-16.dll,libgcc_s_seh-1.dll,libgomp-1.dll,libwinpthread-1.dll,libstdc++-6.dll,zlib1.dll} .
```

Note: libcurl-4.dll itself depends on additional DLLs from /mingw64/bin, so Option 1 is simpler.

### Future: Fully Static Build

To eliminate DLL dependencies completely, we need to build libcurl and its dependencies (zlib, brotli, zstd, openssl/schannel, nghttp2, libssh2, libidn2, libpsl) from source with fully static linking. This is a larger undertaking.

The setup script (`setup-windows-deps.sh`) includes a `build_minimal_static_libcurl()` function that can build a minimal HTTP/HTTPS-only curl with SChannel (Windows native SSL), but it requires internet access to download the curl source.

## Build Performance

- **Initial build**: ~2-3 minutes
- **Incremental build** (with ccache): ~6-11 seconds
- **Clean rebuild** (with ccache warm): ~6 seconds

The setup script automatically configures ccache for faster builds.

##Performance Optimizations Applied

1. **Removed verbose compiler flag** (`-v`): 54% faster builds
2. **ccache**: 30x speedup on incremental builds
3. **Parallel builds**: `-j4` uses all CPU cores
4. **Tree-sitter from source**: No external dependencies

## Known Issues

- **DLL dependencies**: Not fully static yet (see above)
- **Test failures**: Some layout tests may fail due to font rendering differences
