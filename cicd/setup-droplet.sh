#!/bin/bash
# ============================================================
# Lambda Dev Droplet Provisioning Script
# Runs during Packer snapshot build or manually on a fresh Ubuntu 22.04 droplet
# ============================================================
set -euo pipefail

DEV_USERNAME="${DEV_USERNAME:-lambda}"
DEV_PASSWORD="${DEV_PASSWORD:-lambda}"

echo "=========================================="
echo " Lambda Dev Droplet Provisioning"
echo "=========================================="

# ============================================================
# 1. System packages (mirrors Dockerfile)
# ============================================================
apt-get update
apt-get install -y --no-install-recommends \
    build-essential clang cmake meson ninja-build pkg-config \
    git curl wget xxd ca-certificates \
    libcurl4-openssl-dev libmpdec-dev libutf8proc-dev libssl-dev \
    zlib1g-dev libnghttp2-dev libncurses5-dev libevent-dev libbrotli-dev \
    libglfw3-dev libfreetype6-dev libpng-dev libbz2-dev \
    libturbojpeg0-dev libgif-dev gettext \
    libgl1-mesa-dev libglu1-mesa-dev libegl1-mesa-dev \
    libgtest-dev libgmock-dev \
    openssh-server sudo \
    gdb lldb valgrind htop less vim jq unzip \
    uuid-dev python3 python3-pip \
    coreutils ufw

# ============================================================
# 2. Node.js 18.x
# ============================================================
if ! command -v node >/dev/null 2>&1; then
    curl -fsSL https://deb.nodesource.com/setup_18.x | bash -
    apt-get install -y --no-install-recommends nodejs
fi

# ============================================================
# 3. Build Google Test libraries
# ============================================================
if [ ! -f /usr/local/lib/libgtest.a ]; then
    cd /usr/src/gtest
    cmake CMakeLists.txt && make
    cp lib/*.a /usr/local/lib/ 2>/dev/null || cp *.a /usr/local/lib/ 2>/dev/null || true
    ldconfig
    cd /
fi

# ============================================================
# 4. Build premake5
# ============================================================
if ! command -v premake5 >/dev/null 2>&1; then
    cd /tmp
    git clone --depth 1 --recurse-submodules https://github.com/premake/premake-core.git
    cd premake-core && make -f Bootstrap.mak linux
    cp bin/release/premake5 /usr/local/bin/ && chmod +x /usr/local/bin/premake5
    cd / && rm -rf /tmp/premake-core
fi

# ============================================================
# 5. Create dev user
# ============================================================
if ! id "$DEV_USERNAME" >/dev/null 2>&1; then
    useradd -m -s /bin/bash -G sudo "$DEV_USERNAME"
    echo "${DEV_USERNAME}:${DEV_PASSWORD}" | chpasswd
    echo "${DEV_USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${DEV_USERNAME}
    chmod 0440 /etc/sudoers.d/${DEV_USERNAME}
fi

# SSH directory for the dev user
mkdir -p /home/${DEV_USERNAME}/.ssh
chmod 700 /home/${DEV_USERNAME}/.ssh
chown ${DEV_USERNAME}:${DEV_USERNAME} /home/${DEV_USERNAME}/.ssh

# Shell prompt
cat >> /home/${DEV_USERNAME}/.bashrc << 'BASHRC'
export PS1="\[\033[01;32m\]lambda-dev\[\033[00m\]:\[\033[01;34m\]\w\[\033[00m\]\$ "
export PATH="/home/$USER/Lambda:$PATH"
BASHRC

# ============================================================
# 6. SSH hardening
# ============================================================
sed -i 's/#\?PermitRootLogin.*/PermitRootLogin no/' /etc/ssh/sshd_config
sed -i 's/#\?PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config
sed -i 's/#\?PubkeyAuthentication.*/PubkeyAuthentication yes/' /etc/ssh/sshd_config
echo "AllowUsers ${DEV_USERNAME}" >> /etc/ssh/sshd_config

# ============================================================
# 7. Firewall (allow SSH only)
# ============================================================
ufw default deny incoming
ufw default allow outgoing
ufw allow 22/tcp
echo "y" | ufw enable

# ============================================================
# 8. Install idle watchdog (auto power-off after 30 min idle)
# ============================================================
if [ -f /tmp/idle-watchdog.sh ]; then
    cp /tmp/idle-watchdog.sh /usr/local/bin/idle-watchdog.sh
    chmod +x /usr/local/bin/idle-watchdog.sh
fi

if [ -f /tmp/idle-watchdog.service ]; then
    cp /tmp/idle-watchdog.service /etc/systemd/system/idle-watchdog.service
    systemctl daemon-reload
    systemctl enable idle-watchdog.service
fi

# ============================================================
# 9. Workspace directory
# ============================================================
mkdir -p /home/${DEV_USERNAME}/Lambda
chown ${DEV_USERNAME}:${DEV_USERNAME} /home/${DEV_USERNAME}/Lambda

# ============================================================
# 10. Cleanup
# ============================================================
apt-get clean
rm -rf /var/lib/apt/lists/* /tmp/*

echo "=========================================="
echo " Provisioning complete"
echo " User: ${DEV_USERNAME}"
echo " Idle watchdog: enabled (30 min timeout)"
echo "=========================================="
