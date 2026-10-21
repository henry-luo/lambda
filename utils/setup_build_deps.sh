#!/usr/bin/env bash

# configure and compile without elevated privileges; only the selected install needs sudo.
build_premake_dependency() {
    local dependency="$1" source_dir="$2" prefix="$3" libdir="${4:-lib}" privileged="${5:-no}"
    local args=("$SCRIPT_DIR/utils/build_dependencies.py" "$dependency" --source "$source_dir" --prefix "$prefix" --libdir "$libdir")
    (cd "$SCRIPT_DIR" && python3 "${args[@]}" --build-only) || return 1
    if [ "$privileged" = yes ]; then
        (cd "$SCRIPT_DIR" && sudo python3 "${args[@]}" --install-only)
    else
        (cd "$SCRIPT_DIR" && python3 "${args[@]}" --install-only)
    fi
}
