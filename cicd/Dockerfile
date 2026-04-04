# Lambda Development Environment
# Ubuntu 22.04 LTS with all build dependencies for Lambda Script
# Designed for VS Code Remote - Containers / Remote SSH + GitHub Copilot
#
# Build:
#   docker build -t lambda-dev -f cicd/Dockerfile .
#
# Run (interactive):
#   docker run -it --name lambda-dev -v $(pwd):/workspace lambda-dev
#
# Run (SSH daemon for VS Code Remote SSH):
#   docker run -d --name lambda-dev -p 2222:22 -v $(pwd):/workspace lambda-dev /usr/sbin/sshd -D
#
# VS Code Remote - Containers (devcontainer):
#   Use cicd/devcontainer.json for automatic setup

FROM ubuntu:22.04

LABEL maintainer="Lambda Project"
LABEL description="Lambda Script development environment with VS Code Remote + Copilot support"

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

# ============================================================
# Stage 1: System packages and build tools
# ============================================================
RUN apt-get update && apt-get install -y --no-install-recommends \
    # --- Essential build tools ---
    build-essential \
    clang \
    cmake \
    meson \
    ninja-build \
    pkg-config \
    git \
    curl \
    wget \
    xxd \
    ca-certificates \
    # --- Lambda core dependencies ---
    libcurl4-openssl-dev \
    libmpdec-dev \
    libutf8proc-dev \
    libssl-dev \
    zlib1g-dev \
    libnghttp2-dev \
    libncurses5-dev \
    libevent-dev \
    libbrotli-dev \
    # --- Radiant (CSS layout engine) dependencies ---
    libglfw3-dev \
    libfreetype6-dev \
    libpng-dev \
    libbz2-dev \
    libturbojpeg0-dev \
    libgif-dev \
    gettext \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libegl1-mesa-dev \
    # --- Testing ---
    libgtest-dev \
    libgmock-dev \
    # --- VS Code Remote SSH support ---
    openssh-server \
    sudo \
    # --- Developer utilities ---
    gdb \
    lldb \
    valgrind \
    htop \
    less \
    vim \
    jq \
    unzip \
    # --- UUID dev (needed for premake5 build) ---
    uuid-dev \
    # --- Python (needed for build system) ---
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# ============================================================
# Stage 2: Node.js (needed for tree-sitter CLI via npx)
# ============================================================
RUN curl -fsSL https://deb.nodesource.com/setup_18.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

# ============================================================
# Stage 3: Build Google Test libraries from apt source
# ============================================================
RUN cd /usr/src/gtest && cmake CMakeLists.txt && make \
    && cp lib/*.a /usr/local/lib/ 2>/dev/null || cp *.a /usr/local/lib/ 2>/dev/null || true \
    && ldconfig

# ============================================================
# Stage 4: Build premake5 from source
# ============================================================
RUN cd /tmp && git clone --depth 1 --recurse-submodules https://github.com/premake/premake-core.git \
    && cd premake-core && make -f Bootstrap.mak linux \
    && cp bin/release/premake5 /usr/local/bin/ && chmod +x /usr/local/bin/premake5 \
    && cd / && rm -rf /tmp/premake-core

# ============================================================
# Stage 5: SSH server configuration for VS Code Remote SSH
# ============================================================
RUN mkdir -p /var/run/sshd \
    && sed -i 's/#PermitRootLogin.*/PermitRootLogin no/' /etc/ssh/sshd_config \
    && sed -i 's/#PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config \
    && sed -i 's/#PubkeyAuthentication.*/PubkeyAuthentication yes/' /etc/ssh/sshd_config \
    && echo "AllowTcpForwarding yes" >> /etc/ssh/sshd_config

# ============================================================
# Stage 6: Create development user
# ============================================================
ARG USERNAME=lambda
ARG USER_UID=1000
ARG USER_GID=1000

RUN groupadd --gid ${USER_GID} ${USERNAME} \
    && useradd --uid ${USER_UID} --gid ${USER_GID} -m -s /bin/bash ${USERNAME} \
    && echo "${USERNAME}:lambda" | chpasswd \
    && echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers.d/${USERNAME} \
    && chmod 0440 /etc/sudoers.d/${USERNAME}

# Set up SSH authorized_keys directory for the dev user
RUN mkdir -p /home/${USERNAME}/.ssh \
    && chmod 700 /home/${USERNAME}/.ssh \
    && chown ${USERNAME}:${USERNAME} /home/${USERNAME}/.ssh

# ============================================================
# Stage 7: Workspace setup
# ============================================================
RUN mkdir -p /workspace && chown ${USERNAME}:${USERNAME} /workspace
WORKDIR /workspace

# Shell prompt with project context
RUN echo 'export PS1="\[\033[01;32m\]lambda-dev\[\033[00m\]:\[\033[01;34m\]\w\[\033[00m\]\$ "' \
    >> /home/${USERNAME}/.bashrc \
    && echo 'export PATH="/workspace:$PATH"' >> /home/${USERNAME}/.bashrc \
    && echo 'cd /workspace' >> /home/${USERNAME}/.bashrc

# ============================================================
# Ports and entry point
# ============================================================
# SSH
EXPOSE 22

# Default: start bash shell (override with sshd for remote access)
COPY cicd/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

USER ${USERNAME}
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["/bin/bash"]
