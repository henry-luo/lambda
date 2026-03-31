# Lambda CI/CD Proposal — GitHub Actions

## Overview

Add GitHub Actions to the Lambda project for automated nightly testing on **Linux** and **Windows**. Both workflows use Docker images to ensure reproducible, isolated build environments. Test reports are delivered via **Email** and **Telegram**.

---

## 1. Architecture

```
.github/
  workflows/
    nightly-linux.yml        # Triggers Linux Docker build+test
    nightly-windows.yml      # Triggers Windows Docker build+test
    Dockerfile.linux         # Ubuntu-based build environment
    run-tests-linux.sh       # Linux test runner inside container
    run-tests-windows.sh     # Windows test runner inside container
    notify.sh                # Unified notification script (Email + Telegram)
```

### Flow

```
GitHub Actions (cron 3pm SGT)
  └─► Build Docker image (from .github/workflows/Dockerfile.*)
       └─► Run setup-*-deps.sh inside container
            └─► make build
                 └─► make test
                      └─► Collect results + artifacts
                           └─► Send notification (Email / Telegram)
```

---

## 2. Schedule

Both workflows trigger at **3:00 PM Singapore Time (SGT = UTC+8)** daily.

- **Cron expression:** `0 7 * * *` (07:00 UTC = 15:00 SGT)
- Manual trigger (`workflow_dispatch`) is also enabled for on-demand runs.

---

## 3. Workflow Details

### 3.1 Linux Nightly (`nightly-linux.yml`)

| Setting | Value |
|---------|-------|
| Runner | `ubuntu-latest` |
| Container | Custom Docker image from `.github/workflows/Dockerfile.linux` |
| Base image | `ubuntu:22.04` |
| Compiler | gcc/g++ (system) + clang |
| Steps | 1. Build Docker image → 2. `setup-linux-deps.sh` (baked into image) → 3. `make build` → 4. `make test` → 5. Notify |
| Artifacts | `log.txt`, test output, build timing |

### 3.2 Windows Nightly (`nightly-windows.yml`)

| Setting | Value |
|---------|-------|
| Runner | `windows-latest` |
| Environment | MSYS2 with CLANG64 toolchain |
| Compiler | clang/clang++ via MSYS2 CLANG64 |
| Steps | 1. Setup MSYS2 → 2. `setup-windows-deps.sh` → 3. `make build` → 4. `make test` → 5. Notify |
| Artifacts | `log.txt`, test output, build timing |

**Note on Windows Docker:** GitHub Actions `windows-latest` runners do not support Linux Docker. Windows containers have limited toolchain support. The recommended approach for Windows is to use the **`msys2/setup-msys2`** action directly on the runner, which provides a reproducible MSYS2/CLANG64 environment without Docker overhead. This is the standard approach used by major open-source projects targeting Windows + MSYS2.

---

## 4. Docker Images

### 4.1 Linux Dockerfile (`.github/workflows/Dockerfile.linux`)

```dockerfile
FROM ubuntu:22.04
# Pre-install all Lambda dependencies
# Cached in GitHub Actions Docker layer cache for fast rebuilds
# See .github/workflows/Dockerfile.linux for full implementation
```

Key points:
- Based on `ubuntu:22.04` for stability
- Pre-installs all packages from `setup-linux-deps.sh` (apt packages, Node.js, cmake, etc.)
- Builds from-source dependencies (MIR, RE2, utf8proc, ThorVG, etc.)
- Uses Docker layer caching — dependency layers rebuild only when `setup-linux-deps.sh` changes
- Final layer copies the project source and runs tests

### 4.2 Windows Environment

Since GitHub Actions Windows runners cannot run Linux Docker, the Windows workflow uses **`msys2/setup-msys2`** GitHub Action (v2) to provide an equivalent reproducible environment:

- Installs MSYS2 with CLANG64 subsystem
- Caches the MSYS2 installation across runs
- Runs `setup-windows-deps.sh` inside the MSYS2 shell
- Functionally equivalent to a Docker-based setup

---

## 5. Test Report Delivery

GitHub Actions can deliver test reports through multiple channels:

### 5.1 GitHub Actions Artifacts (Built-in, Free)

- Test output and `log.txt` uploaded as downloadable artifacts
- Retained for 90 days (configurable)
- Accessible from the Actions tab in the GitHub repository
- **Always enabled** as the baseline reporting mechanism

### 5.2 Email Notification

**Method:** Use the `dawidd6/action-send-mail@v3` GitHub Action.

| Setting | Value |
|---------|-------|
| SMTP server | Configurable (Gmail, SendGrid, etc.) |
| Required secrets | `MAIL_SERVER`, `MAIL_PORT`, `MAIL_USERNAME`, `MAIL_PASSWORD`, `MAIL_TO` |
| Content | Test pass/fail summary, link to full run, attached log |

Setup:
1. Configure an SMTP account (Gmail app password, or SendGrid free tier)
2. Add credentials as GitHub repository secrets
3. Workflow sends email on completion (success or failure)

### 5.3 Telegram Notification

**Method:** Use the `appleboy/telegram-action@master` GitHub Action.

| Setting | Value |
|---------|-------|
| Required secrets | `TELEGRAM_BOT_TOKEN`, `TELEGRAM_CHAT_ID` |
| Content | Test pass/fail summary, link to full run |

Setup:
1. Create a Telegram bot via [@BotFather](https://t.me/BotFather) → get bot token
2. Create a Telegram group/channel, add the bot, get the chat ID
3. Add `TELEGRAM_BOT_TOKEN` and `TELEGRAM_CHAT_ID` as GitHub repository secrets

### 5.4 Recommendation

Use **all three** in combination:
- **GitHub Artifacts**: always available, full logs for debugging
- **Telegram**: instant push notification with pass/fail summary (low-friction)
- **Email**: detailed summary for record-keeping, sent only on failure to reduce noise

---

## 6. GitHub Secrets Required

| Secret | Purpose |
|--------|---------|
| `MAIL_SERVER` | SMTP server hostname (e.g. `smtp.gmail.com`) |
| `MAIL_PORT` | SMTP port (e.g. `587`) |
| `MAIL_USERNAME` | SMTP login email |
| `MAIL_PASSWORD` | SMTP password or app-specific password |
| `MAIL_TO` | Recipient email address(es) |
| `TELEGRAM_BOT_TOKEN` | Telegram bot API token |
| `TELEGRAM_CHAT_ID` | Telegram chat/group ID |

---

## 7. CI/CD Scripts (`.github/workflows/`)

| File | Purpose |
|------|---------|
| `Dockerfile.linux` | Ubuntu 22.04 image with all Lambda build dependencies |
| `run-tests-linux.sh` | Build + test script executed inside the Linux container |
| `run-tests-windows.sh` | Build + test script executed in MSYS2 environment |
| `notify.sh` | Parse test results and format notification message |

---

## 8. Cost & Resource Estimates

| Resource | Free tier | Notes |
|----------|-----------|-------|
| GitHub Actions (public repo) | 2,000 min/month | Unlimited for public repos |
| GitHub Actions (private repo) | 2,000 min/month | Linux: 1x multiplier, Windows: 2x multiplier |
| Docker layer cache | 10 GB | Shared across all workflows |
| Artifact storage | 500 MB (free) | 90-day retention |
| Telegram Bot API | Free | No message limits for bots |
| Email (Gmail SMTP) | Free | 500 sends/day limit |

Estimated per-run cost:
- Linux build+test: ~15–30 min (first run longer due to Docker build)
- Windows build+test: ~20–40 min (MSYS2 setup + dependency compilation)
- With Docker cache hits, subsequent runs should be significantly faster

---

## 9. Future Extensions

Once the nightly pipeline is stable, consider:

1. **PR-triggered CI** — Run `make test-lambda-baseline` on every pull request
2. **macOS nightly** — Add `macos-latest` runner (no Docker needed, use `setup-mac-deps.sh` directly)
3. **Release pipeline** — Automated `make release` + artifact publishing on git tags
4. **Code quality gates** — Run `make analyze` / `make tidy` / `make lint` as separate jobs
5. **Performance regression** — Run benchmarks and compare against baseline
6. **Matrix builds** — Test across multiple compiler versions (GCC 12/13, Clang 16/17/18)

---

## 10. Implementation Checklist

- [x] Create `.github/workflows/Dockerfile.linux`
- [x] Create `.github/workflows/run-tests-linux.sh`
- [x] Create `.github/workflows/run-tests-windows.sh`
- [x] Create `.github/workflows/notify.sh`
- [x] Create `.github/workflows/nightly-linux.yml`
- [x] Create `.github/workflows/nightly-windows.yml`
- [ ] Configure GitHub repository secrets (Email + Telegram)
- [ ] Test Linux workflow manually via `workflow_dispatch`
- [ ] Test Windows workflow manually via `workflow_dispatch`
- [ ] Verify Email notification delivery
- [ ] Verify Telegram notification delivery
- [ ] Monitor first few nightly runs and fix any issues
