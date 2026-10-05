# rdb-drivers: third-party sources

The `rdb-drivers` module statically links the libraries below, built
unmodified from these upstream releases by `utils/build-rdb-deps.sh`
(`make build-rdb-deps`). Their licences are in `LICENSES/`. Design:
`vibe/Lambda_IO_RDB.md` section 13.

| Component | Version | Licence | Source | SHA-256 |
|---|---|---|---|---|
| libpq (PostgreSQL) | 18.6 | PostgreSQL Licence (`LICENSES/PostgreSQL.txt`) | https://ftp.postgresql.org/pub/source/v18.6/postgresql-18.6.tar.bz2 | `555610c24d53e4316da5b7d3fc25c279d96856d5e0e23ee308c328c5fa881d9f` |
| MariaDB Connector/C | 3.3.21 | LGPL-2.1-or-later (`LICENSES/MariaDB-Connector-C-LGPL-2.1.txt`) | https://github.com/mariadb-corporation/mariadb-connector-c/archive/refs/tags/v3.3.21.tar.gz | `828a1b2d0409d08339051a2cf25fb49e296d835ade8115011baaa5b7857e8138` |
| zlib (bundled in Connector/C) | 1.3.1 | zlib Licence (`LICENSES/zlib.txt`) | inside the Connector/C archive | (covered above) |

## Build options

- libpq (meson): `--buildtype=release -Ddefault_library=static -Db_staticpic=true -Dssl=none -Dgssapi=disabled -Dldap=disabled -Dlibcurl=disabled -Dicu=disabled -Dreadline=disabled -Dzlib=disabled -Dlz4=disabled -Dzstd=disabled -Dnls=disabled -Dlibxml=disabled -Dlibxslt=disabled -Dpam=disabled -Dbonjour=disabled -Dselinux=disabled -Dsystemd=disabled -Duuid=none`
- Connector/C (cmake): `-DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DWITH_SSL=OFF -DWITH_EXTERNAL_ZLIB=OFF -DWITH_CURL=OFF -DWITH_UNIT_TESTS=OFF -DCLIENT_PLUGIN_DIALOG=OFF -DCLIENT_PLUGIN_AUTH_GSSAPI_CLIENT=OFF -DCLIENT_PLUGIN_REMOTE_IO=OFF -DCLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD=OFF -DCLIENT_PLUGIN_MYSQL_OLD_PASSWORD=OFF -DCLIENT_PLUGIN_CLIENT_ED25519=OFF`

## Relinking against a modified Connector/C (LGPL-2.1 section 6)

The module's own source is in this repository (`lambda/module/rdb/`). To
rebuild it against your own build of Connector/C, or of libpq, run from a
Lambda checkout:

```
make build-rdb-deps
make release-rdb-drivers RDB_MARIADB_ARCHIVE=/path/to/libmariadbclient.a
```

`RDB_PQ_ARCHIVE`, `RDB_PGCOMMON_ARCHIVE` and `RDB_PGPORT_ARCHIVE` replace
the libpq archives the same way. The replacement must keep the Connector/C
3.3 client API and be built position-independent and without TLS.
