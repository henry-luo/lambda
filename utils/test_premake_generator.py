#!/usr/bin/env python3
"""Verify native test-host dependency expansion in the Premake generator."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parent.parent
GENERATOR_PATH = ROOT / "utils" / "generate_premake.py"


def fail(message: str) -> None:
    print(f"PREMAKE_GENERATOR_SELFTEST: {message}", file=sys.stderr)
    raise SystemExit(1)


def load_generator_module():
    spec = importlib.util.spec_from_file_location("premake_generator", GENERATOR_PATH)
    if spec is None or spec.loader is None:
        fail("could not load generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def generate_validation_host(module, platform_name: str) -> str:
    # Construct under Linux first so this pure generation test never refreshes
    # macOS archive artifacts, then select the requested output policy.
    generator = module.PremakeGenerator(str(ROOT / "build_lambda_config.json"), "linux")
    generator.use_linux_config = platform_name == "linux"
    generator.use_macos_config = platform_name == "macos"
    generator.use_windows_config = False
    generator._generate_single_test(
        "test_runtime_full_trace_provider",
        "test/test_validator_integration.cpp",
        ["lambda-runtime-full"],
        "",
        "",
        ["gtest", "gtest_main"],
        target_name="test_runtime_full_trace_provider.exe",
    )
    return "\n".join(generator.premake_content)


def expect_node_core_provider(module, platform_name: str) -> None:
    generated = generate_validation_host(module, platform_name)
    if '"node-core",' not in generated:
        fail(f"{platform_name} runtime-full test omitted the node-core trace provider")


def main() -> int:
    module = load_generator_module()
    expect_node_core_provider(module, "macos")
    expect_node_core_provider(module, "linux")
    print("PREMAKE_GENERATOR_SELFTEST: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
