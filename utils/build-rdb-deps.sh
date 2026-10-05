#!/usr/bin/env bash
# Build the static client libraries linked into the rdb-drivers Jube module
# (vibe/Lambda_IO_RDB.md §13.7). Sources are pinned upstream releases, built
# unmodified (RDB8); TLS is done by the host bridge (RDB11), so neither client
# is built with a TLS library.
#
# usage: utils/build-rdb-deps.sh [deps-dir]     (default: mac-deps/rdb)
# output: <deps-dir>/include, <deps-dir>/lib (static, position-independent)
set -euo pipefail

DEPS_DIR="${1:-mac-deps/rdb}"
SRC_DIR="$DEPS_DIR/src"
BUILD_DIR="$DEPS_DIR/build"
PREFIX="$(mkdir -p "$DEPS_DIR" && cd "$DEPS_DIR" && pwd)"

PG_VERSION=18.6
PG_TARBALL="postgresql-$PG_VERSION.tar.bz2"
PG_URL="https://ftp.postgresql.org/pub/source/v$PG_VERSION/$PG_TARBALL"

MARIADB_CC_VERSION=3.3.21
MARIADB_CC_TARBALL="mariadb-connector-c-$MARIADB_CC_VERSION.tar.gz"
MARIADB_CC_URL="https://github.com/mariadb-corporation/mariadb-connector-c/archive/refs/tags/v$MARIADB_CC_VERSION.tar.gz"
# GitHub tag archives publish no checksum; this pins the archive fetched 2026-10-05
MARIADB_CC_SHA256=828a1b2d0409d08339051a2cf25fb49e296d835ade8115011baaa5b7857e8138

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
verify "$PG_TARBALL" "$(cut -d' ' -f1 "$SRC_DIR/$PG_TARBALL.sha256")"
rm -rf "$BUILD_DIR/postgresql-$PG_VERSION"
tar xjf "$SRC_DIR/$PG_TARBALL" -C "$BUILD_DIR"
(
    cd "$BUILD_DIR/postgresql-$PG_VERSION"
    meson setup _build --buildtype=release -Ddefault_library=static -Db_staticpic=true \
        -Dssl=none -Dgssapi=disabled -Dldap=disabled -Dlibcurl=disabled -Dicu=disabled \
        -Dreadline=disabled -Dzlib=disabled -Dlz4=disabled -Dzstd=disabled \
        -Dnls=disabled -Dlibxml=disabled -Dlibxslt=disabled -Dpam=disabled \
        -Dbonjour=disabled -Dselinux=disabled -Dsystemd=disabled -Duuid=none >/dev/null
    ninja -C _build src/interfaces/libpq/libpq.a \
        src/common/libpgcommon_shlib.a src/port/libpgport_shlib.a >/dev/null
    cp _build/src/interfaces/libpq/libpq.a _build/src/common/libpgcommon_shlib.a \
        _build/src/port/libpgport_shlib.a "$PREFIX/lib/"
    cp src/interfaces/libpq/libpq-fe.h src/include/postgres_ext.h "$PREFIX/include/"
)

# ── MariaDB Connector/C ───────────────────────────────────────────────
fetch "$MARIADB_CC_URL" "$MARIADB_CC_TARBALL"
verify "$MARIADB_CC_TARBALL" "$MARIADB_CC_SHA256"
rm -rf "$BUILD_DIR/mariadb-connector-c-$MARIADB_CC_VERSION"
tar xzf "$SRC_DIR/$MARIADB_CC_TARBALL" -C "$BUILD_DIR"
(
    cd "$BUILD_DIR/mariadb-connector-c-$MARIADB_CC_VERSION"
    # 3.3 is the line that still builds without a TLS library (3.4 requires
    # one). caching_sha2_password / sha256_password need a crypto backend, so
    # the module registers Lambda-side implementations instead (RDB12)
    cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DWITH_SSL=OFF -DWITH_EXTERNAL_ZLIB=OFF -DWITH_CURL=OFF -DWITH_UNIT_TESTS=OFF \
        -DCLIENT_PLUGIN_DIALOG=OFF -DCLIENT_PLUGIN_AUTH_GSSAPI_CLIENT=OFF \
        -DCLIENT_PLUGIN_REMOTE_IO=OFF -DCLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD=STATIC \
        -DCLIENT_PLUGIN_MYSQL_OLD_PASSWORD=OFF -DCLIENT_PLUGIN_CLIENT_ED25519=OFF >/dev/null
    cmake --build _build --target mariadbclient -j8 >/dev/null
    cp _build/libmariadb/libmariadbclient.a "$PREFIX/lib/"
    mkdir -p "$PREFIX/include/mariadb"
    cp include/*.h _build/include/*.h "$PREFIX/include/mariadb/"
    mkdir -p "$PREFIX/include/mariadb/mysql"
    cp include/mysql/*.h "$PREFIX/include/mariadb/mysql/"
)

echo "build-rdb-deps: libpq $PG_VERSION and Connector/C $MARIADB_CC_VERSION in $PREFIX"
