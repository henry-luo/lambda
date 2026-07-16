#!/bin/sh
set -eu

: "${STRUCTURIZR_CLI_HOME:?set STRUCTURIZR_CLI_HOME to the pinned CLI directory}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../.." && pwd)
reference="$root/test/lambda/graph/structurizr/reference"

for name in basic stage4c stage4d; do
  "$STRUCTURIZR_CLI_HOME/structurizr.sh" export \
    -workspace "$root/test/lambda/graph/structurizr/$name.dsl" \
    -format json -output "$reference"
  mv "$reference/$name.json" "$reference/$name.structurizr.json"
done

shasum -a 256 \
  "$root/test/lambda/graph/structurizr/basic.dsl" \
  "$root/test/lambda/graph/structurizr/stage4c.dsl" \
  "$root/test/lambda/graph/structurizr/stage4d.dsl" \
  "$reference/basic.structurizr.json" \
  "$reference/stage4c.structurizr.json" \
  "$reference/stage4d.structurizr.json"
