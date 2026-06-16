#!/usr/bin/env bash
# Render Markdown design-doc diagrams to SVG (for any doc set under doc/dev/*/).
#   Mermaid (.mmd)         -> <name>.svg              via mermaid-cli (npx -p @mermaid-js/mermaid-cli mmdc)
#   Structurizr DSL (.dsl) -> <view-key>.svg per view via structurizr-cli (export to mermaid) + mmdc
# Files whose name starts with "_" are scratch and skipped.
#
# Usage: bash utils/render_md_diagrams.sh <diagram-dir> [name ...]
#   no names  -> render every *.mmd and *.dsl in <diagram-dir>
#   name      -> "foo" / "foo.mmd" renders foo.mmd; "bar.dsl" renders the DSL workspace bar.dsl
#
# Structurizr output naming: each view is written to "<view-key>.svg", so name your views in the
#   .dsl to be the filenames your docs embed (e.g. a view key "c4_context" -> c4_context.svg).
#
# Requirements: node/npx (for mmdc). For .dsl also a JDK + structurizr-cli:
#   JAVA_HOME       (default: /opt/homebrew/opt/openjdk)
#   STRUCTURIZR_CLI (default: <repo>/temp/tools/structurizr-cli/structurizr.sh)
# Notes: this mmdc build rejects `Note over` in Mermaid sequence diagrams (use plain messages);
#   Structurizr DSL is line-oriented (one statement per line; no inline ';').
set -u
if [ "$#" -lt 1 ]; then echo "usage: $(basename "$0") <diagram-dir> [name ...]" >&2; exit 2; fi
DIR="$(cd "$1" 2>/dev/null && pwd)" || { echo "no such directory: $1" >&2; exit 2; }
shift
SELF="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SELF/.." && pwd)"
CFG="$SELF/puppeteer-config.json"
ERRLOG="$DIR/.render_err"
: "${JAVA_HOME:=/opt/homebrew/opt/openjdk}"
export JAVA_HOME; export PATH="$JAVA_HOME/bin:$PATH"
: "${STRUCTURIZR_CLI:=$REPO/temp/tools/structurizr-cli/structurizr.sh}"
fail=0
PCFG=(); [ -f "$CFG" ] && PCFG=(-p "$CFG")

mmdc_render() {  # $1 input, $2 output svg, $3 label
  printf 'rendering %s ... ' "$3"
  if npx -y -p @mermaid-js/mermaid-cli mmdc -i "$1" -o "$2" "${PCFG[@]}" >/dev/null 2>"$ERRLOG"; then
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
  local f="$1" base tmp m b out
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
    b="$(basename "$m" .mmd)"; out="${b#structurizr-}"
    mmdc_render "$m" "$DIR/$out.svg" "$base.dsl[$out] -> $out.svg"
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
