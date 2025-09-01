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
cp build_lambda_config.json "$TEST_DIR/"

# Copy lambda directories (minimal structure)
mkdir -p "$TEST_DIR/lambda"
cp -r lambda/tree-sitter "$TEST_DIR/lambda/" 2>/dev/null || echo "Warning: tree-sitter directory not found"
cp -r lambda/tree-sitter-lambda "$TEST_DIR/lambda/" 2>/dev/null || echo "Warning: tree-sitter-lambda directory not found"

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
    && rm -rf /var/lib/apt/lists/*

# Create test user
RUN useradd -m -s /bin/bash testuser && \
    echo 'testuser:testpass' | chpasswd && \
    adduser testuser sudo && \
    echo 'testuser ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

USER testuser
WORKDIR /home/testuser

# Copy project files
COPY . /home/testuser/

# Make script executable
RUN chmod +x setup-linux-deps.sh

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
    echo 'Current directory: $(pwd)'
    echo 'Files present:'
    ls -la
    echo ''
    echo 'Lambda directory contents:'
    ls -la lambda/ 2>/dev/null || echo 'lambda directory not found'
    echo ''
    echo '=== Running setup-linux-deps.sh ==='
    ./setup-linux-deps.sh
    echo ''
    echo '=== Setup Complete ==='
    echo 'Verifying installations...'
    echo 'GCC version:' && gcc --version | head -1
    echo 'CMake version:' && cmake --version | head -1
    echo 'Git version:' && git --version
    echo 'Timeout available:' && timeout --version | head -1
    echo ''
    echo 'Library checks:'
    echo 'libcurl:' && pkg-config --exists libcurl && echo '✓ Found' || echo '✗ Missing'
    echo 'libmpdec:' && pkg-config --exists libmpdec && echo '✓ Found' || echo '✗ Missing'
    echo 'libreadline:' && pkg-config --exists libreadline && echo '✓ Found' || echo '✗ Missing'
    echo 'libutf8proc:' && ls /usr/local/lib/libutf8proc* 2>/dev/null && echo '✓ Found' || echo '✗ Missing'
    echo 'libmir:' && ls /usr/local/lib/libmir* 2>/dev/null && echo '✓ Found' || echo '✗ Missing'
    echo 'libcriterion:' && ls /usr/local/lib/libcriterion* 2>/dev/null && echo '✓ Found' || echo '✗ Missing'
"

echo "Test completed. Cleaning up..."
cd /tmp
rm -rf "$TEST_DIR"

echo "Docker test finished!"
