#!/usr/bin/env bash
# Render the LambdaJS design diagrams (doc/dev/js/diagram/) to SVG.
#   Mermaid (.mmd)         -> <name>.svg            via mermaid-cli (npx -p @mermaid-js/mermaid-cli mmdc)
#   Structurizr DSL (.dsl) -> c4_<view>.svg per view via structurizr-cli (export to mermaid) + mmdc
# Files whose name starts with "_" are scratch and skipped.
# Requirements: node/npx (for mmdc). For .dsl also a JDK + structurizr-cli:
#   export JAVA_HOME=<jdk>            (default: /opt/homebrew/opt/openjdk)
#   export STRUCTURIZR_CLI=<path to structurizr.sh>   (default: <repo>/temp/tools/structurizr-cli/structurizr.sh)
# Usage: bash utils/render_js_diagrams.sh [name ...]   (no args = render all *.mmd and *.dsl)
# Note: this mmdc build rejects `Note over` in sequence diagrams — use plain messages instead.
set -u
SELF="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SELF/.." && pwd)"
DIR="$REPO/doc/dev/js/diagram"
CFG="$DIR/puppeteer-config.json"
ERRLOG="$DIR/.render_err"
: "${JAVA_HOME:=/opt/homebrew/opt/openjdk}"
export JAVA_HOME; export PATH="$JAVA_HOME/bin:$PATH"
: "${STRUCTURIZR_CLI:=$REPO/temp/tools/structurizr-cli/structurizr.sh}"
fail=0

mmdc_render() {  # $1 input, $2 output svg, $3 label
  printf 'rendering %s ... ' "$3"
  if npx -y -p @mermaid-js/mermaid-cli mmdc -i "$1" -o "$2" -p "$CFG" >/dev/null 2>"$ERRLOG"; then
    printf 'ok (%s bytes)\n' "$(wc -c < "$2" | tr -d ' ')"
  else
    printf 'FAILED\n'; tail -6 "$ERRLOG"; fail=1
  fi
}

render_mmd() {
  local f="$1" base
  base="$(basename "$f" .mmd)"
  case "$base" in _*) return 0;; esac
  mmdc_render "$f" "$DIR/$base.svg" "$base.mmd -> $base.svg"
}

render_dsl() {
  local f="$1" base tmp m b key out
  base="$(basename "$f" .dsl)"
  case "$base" in _*) return 0;; esac
  if [ ! -f "$STRUCTURIZR_CLI" ]; then
    printf 'SKIP %s.dsl (structurizr-cli not at %s; set STRUCTURIZR_CLI)\n' "$base" "$STRUCTURIZR_CLI"; return 0
  fi
  tmp="$DIR/.c4_$base"; rm -rf "$tmp"; mkdir -p "$tmp"
  if ! sh "$STRUCTURIZR_CLI" export -workspace "$f" -format mermaid -output "$tmp" >"$ERRLOG" 2>&1; then
    printf 'FAILED export %s.dsl\n' "$base"; tail -8 "$ERRLOG"; fail=1; rm -rf "$tmp"; return 0
  fi
  for m in "$tmp"/*.mmd; do
    [ -e "$m" ] || continue
    b="$(basename "$m" .mmd)"; key="${b#structurizr-}"
    case "$key" in
      c4ctx)  out=c4_context;;
      c4cont) out=c4_container;;
      c4comp) out=c4_component;;
      *)      out="c4_$key";;
    esac
    mmdc_render "$m" "$DIR/$out.svg" "$base.dsl[$key] -> $out.svg"
  done
  rm -rf "$tmp"
}

if [ "$#" -gt 0 ]; then
  for n in "$@"; do
    case "$n" in
      *.dsl) render_dsl "$DIR/${n%.dsl}.dsl";;
      *)     render_mmd "$DIR/${n%.mmd}.mmd";;
    esac
  done
else
  for f in "$DIR"/*.mmd; do [ -e "$f" ] && render_mmd "$f"; done
  for f in "$DIR"/*.dsl; do [ -e "$f" ] && render_dsl "$f"; done
fi
rm -f "$ERRLOG"
exit $fail
