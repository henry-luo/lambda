#!/bin/bash
# ============================================================
# Lambda Dev Idle Watchdog
# Monitors for developer activity and powers off the droplet
# after IDLE_TIMEOUT seconds of inactivity.
#
# Activity signals:
#   - Active SSH sessions (includes VS Code Remote SSH)
#   - VS Code server processes (vscode-server, node with vscode)
#   - Active Copilot agent processes (copilot-agent)
#
# When no activity is detected for the timeout period, the
# droplet is shut down. DigitalOcean stops compute billing
# when the droplet is powered off (disk billing continues).
# ============================================================
set -euo pipefail

# Idle timeout in seconds (default: 30 minutes)
IDLE_TIMEOUT="${IDLE_TIMEOUT:-1800}"

# Check interval in seconds
CHECK_INTERVAL="${CHECK_INTERVAL:-60}"

# Log file
LOG="/var/log/idle-watchdog.log"

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') $1" | tee -a "$LOG"
}

# Returns 0 if there is developer activity, 1 if idle
check_activity() {
    # 1. Check for active SSH sessions (includes VS Code Remote SSH)
    local ssh_sessions
    ssh_sessions=$(who 2>/dev/null | grep -c 'pts/' || true)
    if [ "$ssh_sessions" -gt 0 ]; then
        return 0
    fi

    # 2. Check for VS Code Remote server processes
    if pgrep -f 'vscode-server' >/dev/null 2>&1; then
        # vscode-server exists, but check if any client is connected
        # VS Code Remote keeps a watchdog; check for active RPC connections
        local vscode_connections
        vscode_connections=$(ss -tnp 2>/dev/null | grep -c 'vscode' || true)
        if [ "$vscode_connections" -gt 0 ]; then
            return 0
        fi
    fi

    # 3. Check for Copilot agent processes
    if pgrep -f 'copilot-agent\|copilot-language-server' >/dev/null 2>&1; then
        return 0
    fi

    # 4. Check for active Node.js processes that are likely Copilot/VS Code extensions
    if pgrep -f 'node.*copilot\|node.*github.copilot' >/dev/null 2>&1; then
        return 0
    fi

    # No activity detected
    return 1
}

# ============================================================
# Main loop
# ============================================================
log "Idle watchdog started (timeout: ${IDLE_TIMEOUT}s, check interval: ${CHECK_INTERVAL}s)"

idle_since=0

while true; do
    if check_activity; then
        if [ "$idle_since" -gt 0 ]; then
            log "Activity detected — resetting idle timer"
        fi
        idle_since=0
    else
        idle_since=$((idle_since + CHECK_INTERVAL))

        if [ "$idle_since" -ge "$IDLE_TIMEOUT" ]; then
            log "No activity for ${IDLE_TIMEOUT}s — shutting down droplet"
            # Give any lingering processes a moment to wrap up
            sleep 5
            # Power off the machine
            /sbin/shutdown -h now "Idle watchdog: no dev activity for $(( IDLE_TIMEOUT / 60 )) minutes"
            exit 0
        fi

        remaining=$(( (IDLE_TIMEOUT - idle_since) / 60 ))
        log "Idle for ${idle_since}s — shutdown in ~${remaining} min"
    fi

    sleep "$CHECK_INTERVAL"
done
