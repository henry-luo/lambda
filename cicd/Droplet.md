# Lambda Dev on DigitalOcean Droplet

Packer-based workflow to create a pre-built DigitalOcean snapshot with all Lambda build
dependencies. Launch a ready-to-code droplet in ~60 seconds, connect with VS Code Remote SSH and GitHub Copilot. Auto powers off after 30 minutes of inactivity to save costs.

## Prerequisites

On your **local machine**:

```bash
# Install Packer
brew install packer          # macOS
# or: https://developer.hashicorp.com/packer/install

# Install DigitalOcean CLI (optional, for droplet management)
brew install doctl           # macOS
doctl auth init              # Authenticate with your DO token

# Get a DigitalOcean API token
# https://cloud.digitalocean.com/account/api/tokens
export DIGITALOCEAN_TOKEN="dop_v1_your_token_here"
```

## Step 1: Build the Snapshot

This creates a DigitalOcean snapshot with all Lambda dependencies pre-installed.
Takes ~15-20 minutes (one-time cost). The temporary build droplet is auto-destroyed.

```bash
cd Lambda

# Initialize Packer plugins (first time only)
packer init cicd/packer/lambda-dev.pkr.hcl

# Build the snapshot
packer build \
  -var "do_token=$DIGITALOCEAN_TOKEN" \
  -var "region=sfo3" \
  cicd/packer/lambda-dev.pkr.hcl
```

Packer will output the snapshot ID:

```
==> Builds finished. The artifacts of successful builds are:
--> digitalocean.lambda-dev: A snapshot was created: 'lambda-dev-1712345678' (ID: 123456789) in regions 'sfo3'
```

Note the **snapshot ID** for the next step.

### Available Regions

| Region | Code | Location |
|--------|------|----------|
| San Francisco 3 | `sfo3` | US West |
| New York 3 | `nyc3` | US East |
| London 1 | `lon1` | Europe |
| Singapore 1 | `sgp1` | Asia |
| Bangalore 1 | `blr1` | India |
| Sydney 1 | `syd1` | Australia |

Pick the region closest to you for lowest SSH latency.

## Step 2: Create a Droplet from the Snapshot

### Via doctl CLI

```bash
# List your snapshots to find the ID
doctl compute snapshot list

# Create the droplet
doctl compute droplet create lambda-dev \
  --image <snapshot-id> \
  --size s-4vcpu-8gb \
  --region sfo3 \
  --ssh-keys $(doctl compute ssh-key list --format ID --no-header | head -1) \
  --tag-name lambda \
  --wait

# Get the IP address
doctl compute droplet list --format Name,PublicIPv4
```

### Via DigitalOcean Web Console

1. Go to **Images → Snapshots**
2. Click **More → Create Droplet** on your `lambda-dev-*` snapshot
3. Choose size: **Basic Premium, 4 vCPU / 8 GB RAM** ($48/mo recommended)
4. Select your SSH key
5. Create

## Step 3: First-Time Droplet Setup

```bash
# SSH in (root login is disabled — use the lambda user)
# First time: the snapshot has password auth enabled
ssh lambda@<droplet-ip>
# Default password: lambda

# IMPORTANT: Change the password immediately
passwd

# Copy your SSH public key for key-based auth
# (run this from your local machine)
ssh-copy-id lambda@<droplet-ip>

# Clone the repo and build
cd ~/Lambda
git clone <your-repo-url> .
./setup-linux-deps.sh
make build
```

## Step 4: Connect with VS Code + Copilot

Add to your local `~/.ssh/config`:

```
Host lambda-dev
    HostName <droplet-ip>
    User lambda
    ForwardAgent yes
```

Then in VS Code:

1. **Cmd+Shift+P** → **Remote-SSH: Connect to Host** → `lambda-dev`
2. Open folder: `/home/lambda/Lambda`
3. Install extensions in remote session:
   - **GitHub Copilot** (`GitHub.copilot`)
   - **GitHub Copilot Chat** (`GitHub.copilot-chat`)
   - **C/C++** (`ms-vscode.cpptools`)
4. Start coding with AI assistance

## Auto Power-Off (Idle Watchdog)

The snapshot includes an **idle watchdog** service that automatically powers off the droplet
after **30 minutes** of no developer activity.

### What It Monitors

| Signal | How It's Detected |
|--------|-------------------|
| SSH sessions | `who` — includes VS Code Remote SSH |
| VS Code Server | `pgrep vscode-server` + active socket connections |
| Copilot Agent | `pgrep copilot-agent\|copilot-language-server` |
| Copilot Node.js | `pgrep node.*copilot` |

If **none** of these are present for 30 consecutive minutes → `shutdown -h now`.

### Managing the Watchdog

```bash
# Check status
sudo systemctl status idle-watchdog

# View the log
cat /var/log/idle-watchdog.log

# Temporarily disable (e.g., running a long unattended build)
sudo systemctl stop idle-watchdog

# Re-enable
sudo systemctl start idle-watchdog

# Change timeout to 1 hour (edit the environment variable)
sudo systemctl edit idle-watchdog
# Add:
#   [Service]
#   Environment=IDLE_TIMEOUT=3600
sudo systemctl restart idle-watchdog
```

### Cost Impact

When the droplet is powered off:
- **Compute billing stops** (the main cost)
- **Disk billing continues** (~$0.10/GB/mo for the snapshot storage)
- A 160 GB disk costs ~$16/mo even when off

To fully stop billing, **destroy** the droplet and keep only the snapshot ($0.06/GB/mo).

## Destroying the Droplet

### Quick Destroy (keep snapshot for next time)

```bash
# Power off first (if not already off from idle watchdog)
doctl compute droplet-action power-off <droplet-id> --wait

# Destroy the droplet
doctl compute droplet delete lambda-dev --force
```

Your snapshot remains — create a new droplet from it anytime.

### Full Cleanup (remove everything)

```bash
# Destroy the droplet
doctl compute droplet delete lambda-dev --force

# List and delete snapshots
doctl compute snapshot list
doctl compute snapshot delete <snapshot-id> --force
```

## Recommended Workflow

```
  ┌─────────────────────────────────────────────────┐
  │  ONE-TIME: Build snapshot with Packer (~15 min)  │
  └──────────────────────┬──────────────────────────┘
                         │
                         ▼
  ┌─────────────────────────────────────────────────┐
  │  Create droplet from snapshot (~60 sec)          │
  │  doctl compute droplet create ...                │
  └──────────────────────┬──────────────────────────┘
                         │
                         ▼
  ┌─────────────────────────────────────────────────┐
  │  Code with VS Code Remote SSH + Copilot          │
  └──────────────────────┬──────────────────────────┘
                         │
              ┌──────────┴──────────┐
              │  Stop working       │
              └──────────┬──────────┘
                         │
                         ▼
  ┌─────────────────────────────────────────────────┐
  │  Idle watchdog powers off after 30 min           │
  │  (or manually: doctl ... power-off)              │
  └──────────────────────┬──────────────────────────┘
                         │
              ┌──────────┴──────────┐
              ▼                     ▼
  ┌──────────────────┐   ┌──────────────────────────┐
  │  Next session:   │   │  Done for a while:       │
  │  Power on again  │   │  Destroy droplet,        │
  │  doctl ... on    │   │  keep snapshot            │
  └──────────────────┘   └──────────────────────────┘
```

## Quick Reference

```bash
# Build snapshot (one-time)
packer build -var "do_token=$DIGITALOCEAN_TOKEN" cicd/packer/lambda-dev.pkr.hcl

# Create droplet
doctl compute droplet create lambda-dev --image <snap-id> --size s-4vcpu-8gb --region sfo3 \
  --ssh-keys $(doctl compute ssh-key list --format ID --no-header | head -1) --wait

# Get IP
doctl compute droplet list --format Name,PublicIPv4

# Power off
doctl compute droplet-action power-off <droplet-id> --wait

# Power on
doctl compute droplet-action power-on <droplet-id> --wait

# Destroy droplet (keeps snapshot)
doctl compute droplet delete lambda-dev --force

# List snapshots
doctl compute snapshot list

# Delete snapshot
doctl compute snapshot delete <snapshot-id> --force
```
