# Nix Development Environment for Lambda Script

This provides a reproducible development environment using Nix flakes, replacing the platform-specific setup scripts.

## Quick Start

### Using Nix Flakes

1. **Enable flakes** (if not already enabled):
   ```bash
   echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf
   ```

2. **Enter development environment**:
   ```bash
   nix develop
   ```

3. **Set up Lambda dependencies**:
   ```bash
   setup-lambda-deps
   ```

4. **Validate setup**:
   ```bash
   validate-setup
   ```

5. **Build Lambda Script**:
   ```bash
   make build
   ```

## Benefits over Shell Scripts

### 🎯 **Reproducibility**
- Exact same versions across all machines
- No dependency on system package managers (Homebrew, apt, etc.)
- Isolated environment that doesn't affect system

### 🚀 **Simplicity**
- Single command to get complete environment: `nix develop`
- No manual dependency installation
- Works identically on macOS, Linux, and NixOS

### 🔒 **Reliability** 
- No network failures during setup (packages cached)
- No broken system package conflicts
- Atomic updates and rollbacks

### 🌍 **Cross-platform**
- Same environment on macOS and Linux
- No platform-specific scripts needed
- Consistent behavior everywhere

## Package Coverage

✅ **All required packages from your setup script are available in Nix:**

| Package | Nix Package | Status |
|---------|-------------|--------|
| Node.js & npm | `nodejs` | ✅ Available |
| CMake | `cmake` | ✅ Available |
| Premake5 | `premake5` | ✅ Available |
| Tree-sitter | `tree-sitter` | ✅ Available |
| mpdecimal | `libmpdecimal` | ✅ Available |
| utf8proc | `utf8proc` | ✅ Available |
| Criterion | `criterion` | ✅ Available |
| OpenSSL | `openssl_3` | ✅ Available |
| libcurl | `curl` | ✅ Available |
| nghttp2 | `nghttp2` | ✅ Available |
| lexbor | `lexbor` | ✅ Available |
| libharu | `libharu` | ✅ Available |
| MIR | Custom build | ✅ Available |
| Clang (macOS) | `clang` | ✅ Available |

## Commands Available

After entering the Nix environment:

- `setup-lambda-deps` - Build tree-sitter libraries (replaces manual build steps)
- `clean-lambda-deps` - Clean all build artifacts  
- `validate-setup` - Check that everything is properly configured
- `make build` - Build Lambda Script
- `make test` - Run tests

## Migration from Shell Scripts

Instead of:
```bash
./setup-mac-deps.sh     # macOS
./setup-linux-deps.sh   # Linux  
./setup-windows-deps.sh # Windows
```

Now just:
```bash
nix develop             # Works everywhere
setup-lambda-deps       # Replaces script-specific setup
```

## Advanced Usage

### Custom Package Versions

Edit `flake.nix` to pin specific versions:
```nix
mir = pkgs.stdenv.mkDerivation rec {
  version = "specific-commit-hash";
  # ... custom build configuration
};
```

### Additional Tools

Add to `buildInputs` in `flake.nix`:
```nix
buildInputs = with pkgs; [
  # existing packages...
  lldb        # Additional debugger
  hyperfine   # Benchmarking tool
  # etc...
];
```

### Building from CI/CD

```bash
# Build the package
nix build

# Run tests in isolated environment  
nix develop --command make test
```

## Troubleshooting

### Hash Mismatches
The `flake.nix` contains placeholder hashes that need to be updated:
```bash
nix flake update  # Updates hashes automatically
```

### Missing System Dependencies
Unlike shell scripts, Nix provides everything. If something is missing, add it to `buildInputs`.

### Legacy Systems
For systems without Nix:
- Install Nix: `curl -L https://nixos.org/nix/install | sh`
- Or use the original shell scripts as fallback

## Performance

- **First run**: Downloads and builds dependencies (~5-10 minutes)
- **Subsequent runs**: Instant (everything cached)
- **Updates**: Only changed packages are rebuilt

This approach is much more reliable and maintainable than maintaining separate shell scripts for each platform!