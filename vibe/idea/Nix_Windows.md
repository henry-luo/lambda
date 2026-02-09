# Nix on Windows for Lambda Script Development

Windows developers have several options for using Nix with Lambda Script development.

## 🎯 Recommended: WSL2 + Nix

This provides the best compatibility and matches the Linux development environment.

### Setup Steps:

1. **Install WSL2**:
   ```powershell
   # In PowerShell as Administrator
   wsl --install -d Ubuntu
   # Restart when prompted
   ```

2. **Configure WSL2**:
   ```bash
   # Inside WSL2 Ubuntu
   sudo apt update && sudo apt upgrade -y
   
   # Install Nix
   curl -L https://nixos.org/nix/install | sh
   source ~/.bashrc
   
   # Enable flakes
   mkdir -p ~/.config/nix
   echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf
   ```

3. **Clone and develop**:
   ```bash
   # Clone your repo in WSL2
   git clone https://github.com/henry-luo/lambda.git
   cd lambda
   
   # Use Nix development environment
   nix develop
   setup-lambda-deps
   make build
   ```

### Benefits:
- ✅ Full Nix compatibility
- ✅ Linux environment (same as production)
- ✅ All Lambda Script dependencies work
- ✅ Git integration with Windows
- ✅ VS Code integration via Remote-WSL extension

### File Access:
```bash
# Access Windows files from WSL2
/mnt/c/Users/YourName/Documents/

# Access WSL2 files from Windows
\\wsl$\Ubuntu\home\username\lambda
```

## 🔄 Alternative: MSYS2 Fallback

For developers who prefer staying in Windows native environment:

### Keep MSYS2 Setup
Continue using your existing `setup-windows-deps.sh` with MSYS2:

```bash
# Your existing approach works fine
./setup-windows-deps.sh
make build
```

### Hybrid Approach
Use MSYS2 for development, WSL2+Nix for testing:

```bash
# Develop in MSYS2
cd /c/Users/YourName/Projects/lambda
make build

# Test in Nix environment
wsl
cd /mnt/c/Users/YourName/Projects/lambda
nix develop --command make test
```

## 🐳 Docker Alternative

If WSL2 isn't available:

```dockerfile
# Dockerfile.dev
FROM nixos/nix:latest

RUN nix-env -iA nixpkgs.git nixpkgs.bashInteractive

WORKDIR /workspace
COPY flake.nix flake.lock ./

RUN nix develop --command echo "Dependencies cached"

CMD ["nix", "develop"]
```

```powershell
# Build and run
docker build -f Dockerfile.dev -t lambda-dev .
docker run -it -v ${PWD}:/workspace lambda-dev
```

## 🔧 VS Code Integration

### WSL2 Integration:
1. Install "Remote - WSL" extension
2. Open folder in WSL2: `code .` from WSL2 terminal
3. All development happens in Linux environment

### MSYS2 Integration:
1. Configure VS Code terminal to use MSYS2:
   ```json
   // settings.json
   {
     "terminal.integrated.profiles.windows": {
       "MSYS2": {
         "path": "C:\\msys64\\usr\\bin\\bash.exe",
         "args": ["--login"],
         "env": {
           "MSYSTEM": "MINGW64",
           "CHERE_INVOKING": "1"
         }
       }
     },
     "terminal.integrated.defaultProfile.windows": "MSYS2"
   }
   ```

## 📊 Comparison

| Approach | Setup Complexity | Compatibility | Performance | Maintenance |
|----------|------------------|---------------|-------------|-------------|
| **WSL2 + Nix** | Medium | ✅ Full | ✅ Excellent | ✅ Low |
| **MSYS2 Only** | Low | ⚠️ Windows-specific | ✅ Good | ⚠️ Medium |
| **Docker + Nix** | Medium | ✅ Full | ⚠️ Slower | ✅ Low |
| **Native Nix** | High | ❌ Experimental | ❓ Unknown | ❌ High |

## 🎯 Recommendation

**For Lambda Script development on Windows:**

1. **Primary**: Use WSL2 + Nix for development and testing
2. **Fallback**: Keep MSYS2 setup as backup for Windows-specific builds
3. **CI/CD**: Use Nix in GitHub Actions for consistent builds

This gives you the best of both worlds: reliable Nix environment for development and Windows compatibility when needed.

## 🚀 Quick Start Script

```powershell
# setup-windows-nix.ps1
# Run in PowerShell as Administrator

Write-Host "Setting up Lambda Script development on Windows..."

# Install WSL2 if not present
if (-not (wsl --list --quiet)) {
    Write-Host "Installing WSL2..."
    wsl --install -d Ubuntu
    Write-Host "Please restart your computer and run this script again."
    exit
}

# Setup Nix in WSL2
Write-Host "Setting up Nix in WSL2..."
wsl bash -c "
    curl -L https://nixos.org/nix/install | sh
    source ~/.bashrc
    mkdir -p ~/.config/nix
    echo 'experimental-features = nix-command flakes' >> ~/.config/nix/nix.conf
    echo 'Nix setup complete!'
"

Write-Host "✅ Setup complete!"
Write-Host "Next steps:"
Write-Host "1. Open WSL2: wsl"
Write-Host "2. Clone repo: git clone https://github.com/henry-luo/lambda.git"
Write-Host "3. Enter dev environment: nix develop"
```

This approach gives Windows developers a smooth path to using Nix while maintaining compatibility with existing Windows workflows.