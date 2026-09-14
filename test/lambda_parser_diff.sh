#!/usr/bin/env bash

# Run the reviewed differential corpus. The old all-repository sweep mixed
# helpers, intentionally-invalid files, and parser gaps, so it could not be a
# trustworthy acceptance gate. `run_fuzz.py` now runs grammar.js as first cut,
# the C parser as production implementation, and the reviewed-gap ratchet
# under D8.1.2v3. `utils/lambda_parser_manifest.sh` remains available for a
# manual bulk-audit artifact under ./temp/.
set -euo pipefail

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

make --no-print-directory lambda-cst
python3 test/fuzzy/lambda/run_fuzz.py --target parser --verify-only "$@"
