packer {
  required_plugins {
    digitalocean = {
      version = ">= 1.1.0"
      source  = "github.com/digitalocean/digitalocean"
    }
  }
}

# ============================================================
# Variables
# ============================================================

variable "do_token" {
  type        = string
  sensitive   = true
  description = "DigitalOcean API token"
}

variable "region" {
  type        = string
  default     = "sfo3"
  description = "DigitalOcean region (sfo3, nyc3, lon1, sgp1, etc.)"
}

variable "size" {
  type        = string
  default     = "s-4vcpu-8gb"
  description = "Droplet size for building the snapshot (can be different from runtime size)"
}

variable "dev_username" {
  type    = string
  default = "lambda"
}

variable "dev_password" {
  type      = string
  default   = "lambda"
  sensitive = true
}

# ============================================================
# Source: DigitalOcean droplet used to build the snapshot
# ============================================================

source "digitalocean" "lambda-dev" {
  api_token     = var.do_token
  image         = "ubuntu-22-04-x64"
  region        = var.region
  size          = var.size
  ssh_username  = "root"
  snapshot_name = "lambda-dev-{{timestamp}}"
  snapshot_regions = [var.region]
  tags          = ["lambda", "dev"]
}

# ============================================================
# Build: provision the snapshot
# ============================================================

build {
  sources = ["source.digitalocean.lambda-dev"]

  # Upload the project files needed for provisioning
  provisioner "file" {
    source      = "cicd/setup-droplet.sh"
    destination = "/tmp/setup-droplet.sh"
  }

  provisioner "file" {
    source      = "cicd/idle-watchdog.sh"
    destination = "/tmp/idle-watchdog.sh"
  }

  provisioner "file" {
    source      = "cicd/idle-watchdog.service"
    destination = "/tmp/idle-watchdog.service"
  }

  # Run the provisioning script
  provisioner "shell" {
    environment_vars = [
      "DEV_USERNAME=${var.dev_username}",
      "DEV_PASSWORD=${var.dev_password}"
    ]
    inline = [
      "chmod +x /tmp/setup-droplet.sh",
      "/tmp/setup-droplet.sh"
    ]
    execute_command = "chmod +x {{ .Path }}; {{ .Vars }} bash {{ .Path }}"
  }
}
