#!/bin/bash

# Test script for setup-linux-deps.sh in Docker
set -e

echo "Testing setup-linux-deps.sh in Docker container..."

# Create a temporary directory for the test
TEST_DIR="/tmp/lambda-docker-test"
mkdir -p "$TEST_DIR"

# Copy necessary files
echo "Copying project files..."
cp setup-linux-deps.sh "$TEST_DIR/"
cp build_lambda_config.json "$TEST_DIR/" 2>/dev/null || echo "Warning: build_lambda_config.json not found"
cp Makefile "$TEST_DIR/" 2>/dev/null || echo "Warning: Makefile not found"
cp compile.sh "$TEST_DIR/" 2>/dev/null || echo "Warning: compile.sh not found"

# Ensure scripts are executable
chmod +x "$TEST_DIR/setup-linux-deps.sh" 2>/dev/null || true
chmod +x "$TEST_DIR/compile.sh" 2>/dev/null || true

# Copy all essential project directories
echo "Copying project structure..."
for dir in lambda lib test utils typeset radiant vibe; do
    if [ -d "$dir" ]; then
        cp -r "$dir" "$TEST_DIR/"
        echo "Copied $dir directory"
    else
        echo "Warning: $dir directory not found"
    fi
done

# Copy all shell scripts and source files
echo "Copying additional files..."
for file in *.c *.h *.cpp *.hpp *.sh; do
    if [ -f "$file" ]; then
        cp "$file" "$TEST_DIR/"
        # Make shell scripts executable
        if [[ "$file" == *.sh ]]; then
            chmod +x "$TEST_DIR/$file"
        fi
    fi
done 2>/dev/null || true

# Create Dockerfile
cat > "$TEST_DIR/Dockerfile" << 'EOF'
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Update and install basic tools
RUN apt-get update && apt-get install -y \
    sudo \
    git \
    curl \
    wget \
    build-essential \
    clang \
    nodejs \
    npm \
    cmake \
    libmpdec-dev \
    libmpdecimal-dev \
    libedit-dev \
    libcurl4-openssl-dev \
    libutf8proc-dev \
    vim-common \
    pkg-config \
    && rm -rf /var/lib/apt/lists/* || \
    (apt-get install -y \
    sudo \
    git \
    curl \
    wget \
    build-essential \
    clang \
    nodejs \
    npm \
    cmake \
    libmpdec-dev \
    libedit-dev \
    libcurl4-openssl-dev \
    libutf8proc-dev \
    vim-common \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*)

# Create test user
RUN useradd -m -s /bin/bash testuser && \
    echo 'testuser:testpass' | chpasswd && \
    adduser testuser sudo && \
    echo 'testuser ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

USER testuser
WORKDIR /home/testuser

# Copy project files with proper ownership
COPY --chown=testuser:testuser . /home/testuser/

# Make script executable (ensure proper permissions)
RUN chmod +x /home/testuser/setup-linux-deps.sh

CMD ["/bin/bash"]
EOF

cd "$TEST_DIR"

# Build Docker image
echo "Building Docker image..."
docker build -t lambda-setup-test .

# Run the setup script in container
echo "Running setup script in container..."
docker run --rm -it lambda-setup-test bash -c "
    echo '=== Starting Lambda Setup Test ==='
    echo 'Current directory: \$(pwd)'
    echo 'Files present:'
    ls -la
    echo ''
    echo 'Project structure:'
    find . -maxdepth 2 -type d | head -20
    echo ''
    echo '=== Running setup-linux-deps.sh ==='
    ./setup-linux-deps.sh
    echo ''
    echo '=== Setup Complete ==='
    echo 'Verifying installations...'
    echo 'GCC version:' && gcc --version | head -1
    echo 'Clang version:' && clang --version | head -1
    echo 'Clang++ version:' && clang++ --version | head -1
    echo 'Node version:' && node --version
    echo 'NPM version:' && npm --version
    echo 'CMake version:' && cmake --version | head -1
    echo 'Git version:' && git --version
    echo 'xxd available:' && xxd -v 2>&1 | head -1
    echo 'Timeout available:' && timeout --version | head -1
    echo ''
    echo 'Checking compiler paths:'
    echo 'which gcc:' && which gcc
    echo 'which clang:' && which clang
    echo 'which clang++:' && which clang++
    echo '/usr/bin/clang exists:' && [ -x /usr/bin/clang ] && echo 'YES' || echo 'NO'
    echo '/usr/bin/clang++ exists:' && [ -x /usr/bin/clang++ ] && echo 'YES' || echo 'NO'
    echo ''
    echo 'Header file verification:'
    echo 'mpdecimal.h location:' && find /usr -name 'mpdecimal.h' 2>/dev/null || echo 'Not found'
    echo 'mpdec.h location:' && find /usr -name 'mpdec.h' 2>/dev/null || echo 'Not found'  
    echo 'Include directories with mpdec*:'
    find /usr/include -name '*mpdec*' 2>/dev/null || echo 'No mpdec headers found'
    echo 'libmpdec library location:' && find /usr -name 'libmpdec*' 2>/dev/null || echo 'Not found'
    echo 'pkg-config mpdecimal:' && pkg-config --exists mpdecimal && echo 'Found' || echo 'Not found'
    echo 'dpkg libmpdec-dev status:' && dpkg -l | grep libmpdec-dev || echo 'Not installed via dpkg'
    echo ''
    echo 'MIR header verification:'
    echo 'mir.h location:' && find /usr -name 'mir.h' 2>/dev/null || echo 'Not found'
    echo 'libmir library location:' && find /usr -name 'libmir*' 2>/dev/null || echo 'Not found'
    echo '/usr/local/include/mir.h exists:' && [ -f /usr/local/include/mir.h ] && echo 'YES' || echo 'NO'
    echo '/usr/local/lib/libmir.a exists:' && [ -f /usr/local/lib/libmir.a ] && echo 'YES' || echo 'NO'
    echo ''
    echo 'Library checks:'
    echo 'libcurl:' && pkg-config --exists libcurl && echo '✓ Found' || echo '✗ Missing'
    echo 'libmpdec:' && (pkg-config --exists libmpdec || [ -f /usr/lib/x86_64-linux-gnu/libmpdec.so ] || dpkg -l | grep -q libmpdec-dev) && echo '✓ Found' || echo '✗ Missing'
    echo 'libedit:' && (pkg-config --exists libedit || [ -f /usr/lib/x86_64-linux-gnu/libedit.so ] || dpkg -l | grep -q libedit-dev) && echo '✓ Found' || echo '✗ Missing'
    echo 'libutf8proc:' && ls /usr/local/lib/libutf8proc* 2>/dev/null && echo '✓ Found' || echo '✗ Missing'
    echo 'libmir:' && ls /usr/local/lib/libmir* 2>/dev/null && echo '✓ Found' || echo '✗ Missing'
    echo 'libcriterion:' && (ls /usr/local/lib/libcriterion* 2>/dev/null || ls /usr/lib/x86_64-linux-gnu/libcriterion* 2>/dev/null || dpkg -l | grep -q libcriterion-dev) && echo '✓ Found' || echo '✗ Missing'
    echo ''
    echo '=== Starting Lambda Build Process ==='
    echo 'Checking required files...'
    echo 'Files in current directory:'
    ls -la | head -20
    echo ''
    if [ -f compile.sh ]; then
        echo 'compile.sh found and permissions:'
        ls -la compile.sh
        echo 'Making sure compile.sh is executable...'
        chmod +x compile.sh
    else
        echo '❌ compile.sh not found!'
        echo 'Available files:'
        ls -la *.sh 2>/dev/null || echo 'No shell scripts found'
        exit 1
    fi
    echo ''
    if [ -f Makefile ]; then
        echo 'Running make...'
        make build || {
            echo '❌ make build failed, trying make...'
            make || {
                echo '❌ make failed, trying direct compilation...'
                echo 'Available make targets:'
                make help 2>/dev/null || make -n 2>/dev/null || echo 'No help available'
                exit 1
            }
        }
        echo '✅ Build completed successfully'
        echo ''
        echo 'Build artifacts:'
        find . -name '*.o' -o -name '*.a' -o -name '*.so' -o -name 'lambda' -o -name 'lambda.exe' | head -10
        echo ''
        echo '=== Running Tests ==='
        if make test; then
            echo '✅ Tests passed successfully'
        else
            echo '❌ Tests failed'
            echo 'Test output:'
            ls -la test/ 2>/dev/null || echo 'No test directory found'
            ls -la build/ 2>/dev/null || echo 'No build directory found'
        fi
    else
        echo '❌ No Makefile found - cannot run build and test'
        echo 'Available files:'
        ls -la
        exit 1
    fi
    echo ''
    echo '=== Lambda Docker Test Complete ==='
"

echo "Test completed. Cleaning up..."
cd /tmp
rm -rf "$TEST_DIR"

echo "Docker test finished!"
