#!/usr/bin/env bash
# Build the static client libraries linked into the rdb-drivers Jube module
# (vibe/Lambda_IO_RDB.md §13.7). Sources are pinned upstream releases, built
# unmodified (RDB8); TLS is done by the host bridge (RDB11), so neither client
# is built with a TLS library.
#
# Also writes the module's licence material (§13.8) from the same pinned
# variables, so modules/rdb-drivers/{LICENSES,SOURCES.md} always describe the
# archives actually built.
#
# usage: utils/build-rdb-deps.sh [deps-dir]     (default: mac-deps/rdb)
# output: <deps-dir>/include, <deps-dir>/lib (static, position-independent),
#         <deps-dir>/VERSIONS, modules/rdb-drivers/LICENSES, SOURCES.md
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS_DIR="${1:-mac-deps/rdb}"
SRC_DIR="$DEPS_DIR/src"
BUILD_DIR="$DEPS_DIR/build"
PREFIX="$(mkdir -p "$DEPS_DIR" && cd "$DEPS_DIR" && pwd)"
MODULE_DIR="$ROOT/modules/rdb-drivers"

PG_VERSION=18.6
PG_TARBALL="postgresql-$PG_VERSION.tar.bz2"
PG_URL="https://ftp.postgresql.org/pub/source/v$PG_VERSION/$PG_TARBALL"
PG_MESON_OPTS=(
    --buildtype=release -Ddefault_library=static -Db_staticpic=true
    -Dssl=none -Dgssapi=disabled -Dldap=disabled -Dlibcurl=disabled -Dicu=disabled
    -Dreadline=disabled -Dzlib=disabled -Dlz4=disabled -Dzstd=disabled
    -Dnls=disabled -Dlibxml=disabled -Dlibxslt=disabled -Dpam=disabled
    -Dbonjour=disabled -Dselinux=disabled -Dsystemd=disabled -Duuid=none
)

MARIADB_CC_VERSION=3.3.21
MARIADB_CC_TARBALL="mariadb-connector-c-$MARIADB_CC_VERSION.tar.gz"
MARIADB_CC_URL="https://github.com/mariadb-corporation/mariadb-connector-c/archive/refs/tags/v$MARIADB_CC_VERSION.tar.gz"
# GitHub tag archives publish no checksum; this pins the archive fetched 2026-10-05
MARIADB_CC_SHA256=828a1b2d0409d08339051a2cf25fb49e296d835ade8115011baaa5b7857e8138
# 3.3 is the line that still builds without a TLS library (3.4 requires one).
# The SHA-2 logins need a crypto backend, so the module registers Lambda-side
# implementations (RDB12); the cleartext-password plugin is off because it
# would send the password whether or not the bridge guarantees TLS.
MARIADB_CC_CMAKE_OPTS=(
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DWITH_SSL=OFF -DWITH_EXTERNAL_ZLIB=OFF -DWITH_CURL=OFF -DWITH_UNIT_TESTS=OFF
    -DCLIENT_PLUGIN_DIALOG=OFF -DCLIENT_PLUGIN_AUTH_GSSAPI_CLIENT=OFF
    -DCLIENT_PLUGIN_REMOTE_IO=OFF -DCLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD=OFF
    -DCLIENT_PLUGIN_MYSQL_OLD_PASSWORD=OFF -DCLIENT_PLUGIN_CLIENT_ED25519=OFF
)

mkdir -p "$SRC_DIR" "$BUILD_DIR" "$PREFIX/include" "$PREFIX/lib"

fetch() {  # fetch <url> <file>
    [ -f "$SRC_DIR/$2" ] || curl -sSL -o "$SRC_DIR/$2" "$1"
}

verify() {  # verify <file> <sha256>
    local actual
    actual="$(shasum -a 256 "$SRC_DIR/$1" | cut -d' ' -f1)"
    if [ "$actual" != "$2" ]; then
        echo "build-rdb-deps: checksum mismatch for $1 ($actual)" >&2
        exit 1
    fi
}

# ── libpq ─────────────────────────────────────────────────────────────
fetch "$PG_URL" "$PG_TARBALL"
fetch "$PG_URL.sha256" "$PG_TARBALL.sha256"
PG_SHA256="$(cut -d' ' -f1 "$SRC_DIR/$PG_TARBALL.sha256")"
verify "$PG_TARBALL" "$PG_SHA256"
PG_SRC="$BUILD_DIR/postgresql-$PG_VERSION"
rm -rf "$PG_SRC"
tar xjf "$SRC_DIR/$PG_TARBALL" -C "$BUILD_DIR"
(
    cd "$PG_SRC"
    meson setup _build "${PG_MESON_OPTS[@]}" >/dev/null
    ninja -C _build src/interfaces/libpq/libpq.a \
        src/common/libpgcommon_shlib.a src/port/libpgport_shlib.a >/dev/null
    cp _build/src/interfaces/libpq/libpq.a _build/src/common/libpgcommon_shlib.a \
        _build/src/port/libpgport_shlib.a "$PREFIX/lib/"
    cp src/interfaces/libpq/libpq-fe.h src/include/postgres_ext.h "$PREFIX/include/"
)

# ── MariaDB Connector/C ───────────────────────────────────────────────
fetch "$MARIADB_CC_URL" "$MARIADB_CC_TARBALL"
verify "$MARIADB_CC_TARBALL" "$MARIADB_CC_SHA256"
CC_SRC="$BUILD_DIR/mariadb-connector-c-$MARIADB_CC_VERSION"
rm -rf "$CC_SRC"
tar xzf "$SRC_DIR/$MARIADB_CC_TARBALL" -C "$BUILD_DIR"
(
    cd "$CC_SRC"
    cmake -S . -B _build "${MARIADB_CC_CMAKE_OPTS[@]}" >/dev/null
    cmake --build _build --target mariadbclient -j8 >/dev/null
    cp _build/libmariadb/libmariadbclient.a "$PREFIX/lib/"
    mkdir -p "$PREFIX/include/mariadb/mysql"
    cp include/*.h _build/include/*.h "$PREFIX/include/mariadb/"
    cp include/mysql/*.h "$PREFIX/include/mariadb/mysql/"
)
ZLIB_VERSION="$(sed -n 's/^#define ZLIB_VERSION "\(.*\)"/\1/p' "$CC_SRC/external/zlib/zlib.h")"

# ── what was built, for the licence gate (§13.8) ─────────────────────
printf 'postgresql %s\nmariadb-connector-c %s\nzlib %s\n' \
    "$PG_VERSION" "$MARIADB_CC_VERSION" "$ZLIB_VERSION" > "$PREFIX/VERSIONS"

# ── licence material shipped beside the module (§13.8) ───────────────
mkdir -p "$MODULE_DIR/LICENSES"
cp "$PG_SRC/COPYRIGHT" "$MODULE_DIR/LICENSES/PostgreSQL.txt"
cp "$CC_SRC/COPYING.LIB" "$MODULE_DIR/LICENSES/MariaDB-Connector-C-LGPL-2.1.txt"
cp "$CC_SRC/external/zlib/LICENSE" "$MODULE_DIR/LICENSES/zlib.txt"
cat > "$MODULE_DIR/SOURCES.md" <<EOF
# rdb-drivers: third-party sources

The \`rdb-drivers\` module statically links the libraries below, built
unmodified from these upstream releases by \`utils/build-rdb-deps.sh\`
(\`make build-rdb-deps\`). Their licences are in \`LICENSES/\`. Design:
\`vibe/Lambda_IO_RDB.md\` section 13.

| Component | Version | Licence | Source | SHA-256 |
|---|---|---|---|---|
| libpq (PostgreSQL) | $PG_VERSION | PostgreSQL Licence (\`LICENSES/PostgreSQL.txt\`) | $PG_URL | \`$PG_SHA256\` |
| MariaDB Connector/C | $MARIADB_CC_VERSION | LGPL-2.1-or-later (\`LICENSES/MariaDB-Connector-C-LGPL-2.1.txt\`) | $MARIADB_CC_URL | \`$MARIADB_CC_SHA256\` |
| zlib (bundled in Connector/C) | $ZLIB_VERSION | zlib Licence (\`LICENSES/zlib.txt\`) | inside the Connector/C archive | (covered above) |

## Build options

- libpq (meson): \`${PG_MESON_OPTS[*]}\`
- Connector/C (cmake): \`${MARIADB_CC_CMAKE_OPTS[*]}\`

## Relinking against a modified Connector/C (LGPL-2.1 section 6)

The module's own source is in this repository (\`lambda/module/rdb/\`). To
rebuild it against your own build of Connector/C, or of libpq, run from a
Lambda checkout:

\`\`\`
make build-rdb-deps
make release-rdb-drivers RDB_MARIADB_ARCHIVE=/path/to/libmariadbclient.a
\`\`\`

\`RDB_PQ_ARCHIVE\`, \`RDB_PGCOMMON_ARCHIVE\` and \`RDB_PGPORT_ARCHIVE\` replace
the libpq archives the same way. The replacement must keep the Connector/C
3.3 client API and be built position-independent and without TLS.
EOF

echo "build-rdb-deps: libpq $PG_VERSION and Connector/C $MARIADB_CC_VERSION in $PREFIX"
