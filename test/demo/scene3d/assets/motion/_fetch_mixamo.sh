#!/bin/sh
# Local-only Mixamo fixtures (vibe/Lambda_Pkg_Motion.md §11): the two Mixamo
# exports that Three.js r186 ships with its examples, fetched into temp/mixamo/
# and checked against pinned hashes. Mixamo files are never committed; README.md
# lists the checks that read them.
set -eu
dir=temp/mixamo
base=https://raw.githubusercontent.com/mrdoob/three.js/r186/examples/models/fbx
mkdir -p "$dir"
fetch() {
    curl -sSfL -o "$dir/$1" "$base/$2"
    echo "$3  $dir/$1" | shasum -a 256 -c -
}
fetch mixamo.fbx mixamo.fbx 062bfc75d03a68b8a4aaa4cae6d50c6624bef45684df8aca5302ab1023e27d6c
fetch "Samba Dancing.fbx" "Samba%20Dancing.fbx" b9003ee562c87bf03051c3a502411b0808d3513f1d74a2011f7530d9f067069f
