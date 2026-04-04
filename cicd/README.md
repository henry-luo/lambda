# Lambda Development Docker Environment

Linux Docker image for Lambda Script development with VS Code Remote and GitHub Copilot support.

## Quick Start

### Option 1: VS Code Dev Containers (Recommended)

1. Install the [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) extension in VS Code
2. Copy devcontainer config to project root:
   ```bash
   mkdir -p .devcontainer
   cp cicd/devcontainer.json .devcontainer/devcontainer.json
   cp cicd/Dockerfile .devcontainer/Dockerfile
   cp cicd/entrypoint.sh .devcontainer/entrypoint.sh
   ```
3. Open the project in VS Code and click **"Reopen in Container"** when prompted
4. GitHub Copilot and C++ extensions are auto-installed

### Option 2: Docker Compose + VS Code Remote SSH

```bash
# Build and start the container
cd cicd
docker compose up -d

# Connect via VS Code Remote SSH:
#   Host: localhost
#   Port: 2222
#   User: lambda
#   Password: lambda (change after first login)

# First-time setup inside the container:
cd /workspace
./setup-linux-deps.sh
make build
```

### Option 3: Standalone Docker

```bash
# Build the image
docker build -t lambda-dev -f cicd/Dockerfile .

# Run interactive shell
docker run -it --name lambda-dev -v $(pwd):/workspace lambda-dev

# Or run with SSH daemon for remote access
docker run -d --name lambda-dev -p 2222:22 -v $(pwd):/workspace lambda-dev sshd
```

## Cloud Deployment

### AWS EC2 / GCP Compute / Azure VM

1. Launch a Linux VM (Ubuntu 22.04 recommended, 4+ vCPU, 8+ GB RAM)
2. Install Docker on the VM
3. Clone the Lambda repo and build the image:
   ```bash
   git clone <repo-url> Lambda && cd Lambda
   docker build -t lambda-dev -f cicd/Dockerfile .
   docker run -d --name lambda-dev -p 2222:22 -v $(pwd):/workspace lambda-dev sshd
   ```
4. In VS Code, add SSH host: `ssh lambda@<vm-ip> -p 2222`
5. Install GitHub Copilot extension in the remote session

### SSH Key Authentication (Recommended for Cloud)

```bash
# Copy your public key into the container
docker cp ~/.ssh/id_rsa.pub lambda-dev:/home/lambda/.ssh/authorized_keys
docker exec lambda-dev chown lambda:lambda /home/lambda/.ssh/authorized_keys
docker exec lambda-dev chmod 600 /home/lambda/.ssh/authorized_keys
```

## What's Included

| Component | Details |
|-----------|---------|
| **Base OS** | Ubuntu 22.04 LTS |
| **C/C++ Toolchain** | GCC, Clang, CMake, Meson, Ninja |
| **Node.js** | v18.x (for tree-sitter CLI) |
| **Python** | Python 3 (for build system) |
| **Premake5** | Built from source |
| **Lambda Deps** | libcurl, mpdecimal, utf8proc, ssl, zlib, nghttp2, ncurses, libevent, brotli |
| **Radiant Deps** | GLFW, FreeType, libpng, libjpeg-turbo, libgif, OpenGL/EGL |
| **Testing** | Google Test, Google Mock |
| **Debug Tools** | GDB, LLDB, Valgrind |
| **VS Code Support** | SSH server, Dev Container config |
| **AI Coding** | GitHub Copilot + Copilot Chat extensions pre-configured |

## First-Time Build (Inside Container)

```bash
# Install native dependencies (tree-sitter libs, MIR, RE2, rpmalloc, ThorVG, etc.)
./setup-linux-deps.sh

# Build Lambda
make build

# Run tests
make test

# Build release
make release
```

## Customization

- Edit `cicd/Dockerfile` to add packages or change the base image
- Edit `cicd/devcontainer.json` to change VS Code extensions or settings
- The default user is `lambda` with sudo access (password: `lambda`)
- Change the password after deployment: `passwd`
