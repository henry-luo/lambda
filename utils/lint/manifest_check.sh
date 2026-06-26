#!/usr/bin/env bash
# manifest_check.sh — assert utils/lint/manifest.yml is in sync with the
# native rule sources (ast-grep YAMLs + alint config + Python scripts).
#
# Fails with a clear message on any of:
#   - a manifest entry whose backend/file points at a missing source
#   - a discovered rule (ast-grep YAML, alint rule, or registered Python check)
#     with no manifest entry
#
# Used as a sanity check during `make lint --list` and CI (when wired).
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MANIFEST="$ROOT/utils/lint/manifest.yml"
RULES_DIR="$ROOT/utils/lint/rules"
ALINT_CONFIG="$ROOT/.alint.yml"

need() { command -v "$1" >/dev/null 2>&1 || { echo "manifest_check: $1 not found" >&2; exit 127; }; }
need awk
[[ -f "$MANIFEST" ]] || { echo "manifest_check: $MANIFEST not found" >&2; exit 1; }

# ---------- discover rule ids from each backend ----------
discovered=$(mktemp)
trap 'rm -f "$discovered"' EXIT

# ast-grep: id: <name> at top of each YAML. Searches all language subdirs
# (c-cpp/, lambda/, …) but skips the structural/ dir which is for Python scripts.
while IFS= read -r f; do
  awk -F': *' '/^id:/{print $2; exit}' "$f" >> "$discovered"
done < <(find "$RULES_DIR" -name '*.yml' -not -path '*/structural/*')

# alint: `alint list -q` prints "<level> <id>" per rule
if command -v alint >/dev/null 2>&1 && [[ -f "$ALINT_CONFIG" ]]; then
  (cd "$ROOT" && alint list -q 2>/dev/null) | awk '{print $2}' >> "$discovered"
fi

# residual Python checks: derived from STRUCTURAL_CHECKS in run.sh
# Keep this list in lockstep with run.sh (small enough that grepping is fine).
awk -F: '/^  *"[a-z-]+:python/{ gsub(/[" ]/, "", $1); print $1 }' "$ROOT/utils/lint/run.sh" >> "$discovered"

# clang-tidy / libclang rules. Two shapes:
#   - int-cast-type-aware    : single rule, libclang AST tool (explicit casts)
#   - tidy-{bugprone,clang-analyzer,cert} : family rules — each fires under
#     many specific check names (tidy-bugprone-branch-clone, etc.). The
#     manifest tracks the families, not every check name; we record the
#     family ids here so the discovery side and manifest side reconcile.
if [[ -f "$ROOT/utils/lint/tidy/run_tidy.sh" ]]; then
  printf '%s\n' "int-cast-type-aware" \
                "tidy-bugprone" "tidy-clang-analyzer" "tidy-cert" >> "$discovered"
fi

# Hybrid backend (Phase 4 — unused-function check)
if [[ -f "$ROOT/utils/lint/dead-code/run_unused_function.sh" ]]; then
  printf '%s\n' "unused-function" >> "$discovered"
fi

# ---------- discover ids from manifest ----------
manifest_ids=$(awk '/^  [a-z][a-z0-9-]*:$/{ sub(":",""); gsub(/ /,""); print }' "$MANIFEST")

# ---------- diff ----------
missing_manifest=$(comm -23 <(sort -u "$discovered") <(echo "$manifest_ids" | sort -u))
missing_source=$(comm -13   <(sort -u "$discovered") <(echo "$manifest_ids" | sort -u))

fail=0
if [[ -n "$missing_manifest" ]]; then
  echo "❌ manifest: rules found in code but not in manifest.yml:"
  echo "$missing_manifest" | sed 's/^/   - /'
  fail=1
fi
if [[ -n "$missing_source" ]]; then
  echo "❌ manifest: rules in manifest.yml with no matching code/config:"
  echo "$missing_source" | sed 's/^/   - /'
  fail=1
fi
(( fail )) && exit 1
echo "✅ manifest: $(echo "$manifest_ids" | wc -l | tr -d ' ') rules in sync across backends"
