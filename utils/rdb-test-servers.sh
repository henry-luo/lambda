#!/usr/bin/env bash
# Local PostgreSQL / MySQL servers for the RDB driver corpus
# (vibe/Lambda_IO_RDB.md section 13.11), run with Apple's `container` tool.
# Both servers use TLS with a throwaway CA in temp/rdb-tls, and every `up`
# reloads the fixtures in test/rdb/fixture so runs are reproducible.
#
# usage: utils/rdb-test-servers.sh up|down|status
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TLS="$ROOT/temp/rdb-tls"
PG_PORT=15432
MYSQL_PORT=13306

need_container() {
    command -v container >/dev/null || { echo "rdb-test-servers: Apple 'container' is not installed" >&2; exit 1; }
    container system status >/dev/null 2>&1 || container system start --enable-kernel-install >/dev/null
}

make_ca() {
    [ -f "$TLS/server.crt" ] && openssl x509 -checkend 86400 -noout -in "$TLS/server.crt" >/dev/null 2>&1 && return
    mkdir -p "$TLS"
    (
        cd "$TLS"
        openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.crt -days 30 \
            -subj "/CN=Lambda RDB Test CA" 2>/dev/null
        openssl req -newkey rsa:2048 -nodes -keyout server.key -out server.csr \
            -subj "/CN=localhost" 2>/dev/null
        printf "subjectAltName=DNS:localhost,IP:127.0.0.1\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n" > ext.cnf
        openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
            -out server.crt -days 30 -extfile ext.cnf 2>/dev/null
    )
}

running() { container list 2>/dev/null | awk 'NR>1 {print $1}' | grep -qx "$1"; }

start_pg() {
    running lambda-pg && return
    container rm lambda-pg >/dev/null 2>&1 || true
    # PostgreSQL insists on owning its key file, so the certs are copied in
    container run -d --name lambda-pg -p "127.0.0.1:$PG_PORT:5432" \
        -e POSTGRES_USER=lambda -e POSTGRES_PASSWORD=lambdapw -e POSTGRES_DB=shop \
        -v "$TLS:/certs:ro" --entrypoint bash docker.io/library/postgres:18 -c \
        'cp /certs/server.crt /certs/server.key /tmp/ && chown postgres /tmp/server.crt /tmp/server.key && chmod 600 /tmp/server.key && exec docker-entrypoint.sh postgres -c ssl=on -c ssl_cert_file=/tmp/server.crt -c ssl_key_file=/tmp/server.key' >/dev/null
}

start_mysql() {
    running lambda-mysql && return
    container rm lambda-mysql >/dev/null 2>&1 || true
    container run -d --name lambda-mysql -p "127.0.0.1:$MYSQL_PORT:3306" \
        -e MYSQL_ROOT_PASSWORD=rootpw -e MYSQL_DATABASE=shop -e MYSQL_USER=lambda -e MYSQL_PASSWORD=lambdapw \
        -v "$TLS:/certs:ro" --entrypoint bash docker.io/library/mysql:8.4 -c \
        'mkdir -p /tmp/tls && cp /certs/ca.crt /certs/server.crt /certs/server.key /tmp/tls/ && chown -R mysql /tmp/tls && chmod 600 /tmp/tls/server.key && exec docker-entrypoint.sh mysqld --ssl-ca=/tmp/tls/ca.crt --ssl-cert=/tmp/tls/server.crt --ssl-key=/tmp/tls/server.key' >/dev/null
}

wait_for() {  # wait_for <name> <readiness command...>
    local name="$1"; shift
    for _ in $(seq 1 60); do
        "$@" >/dev/null 2>&1 && return
        sleep 2
    done
    echo "rdb-test-servers: $name did not become ready" >&2
    exit 1
}

load_fixtures() {
    container exec lambda-pg psql -q -U lambda -d shop \
        -c "SET client_min_messages TO warning; DROP SCHEMA public CASCADE; CREATE SCHEMA public;" >/dev/null
    container exec -i lambda-pg psql -q -v ON_ERROR_STOP=1 -U lambda -d shop \
        < "$ROOT/test/rdb/fixture/postgresql.sql" >/dev/null
    container exec lambda-mysql mysql -uroot -prootpw \
        -e "DROP DATABASE IF EXISTS shop; CREATE DATABASE shop; GRANT ALL ON shop.* TO 'lambda'@'%';" 2>/dev/null
    container exec -i lambda-mysql mysql -uroot -prootpw shop \
        < "$ROOT/test/rdb/fixture/mysql.sql" 2>/dev/null
}

case "${1:-}" in
    up)
        need_container
        make_ca
        start_pg
        start_mysql
        wait_for lambda-pg container exec lambda-pg pg_isready -U lambda -d shop -h 127.0.0.1
        wait_for lambda-mysql container exec lambda-mysql mysqladmin -uroot -prootpw -h 127.0.0.1 ping
        load_fixtures
        echo "export LAMBDA_TEST_PG_URI='postgresql://lambda:lambdapw@localhost:$PG_PORT/shop?sslmode=verify-full&sslrootcert=$TLS/ca.crt'"
        echo "export LAMBDA_TEST_MYSQL_URI='mysql://lambda:lambdapw@localhost:$MYSQL_PORT/shop?ssl-mode=VERIFY_IDENTITY&ssl-ca=$TLS/ca.crt'"
        ;;
    down)
        need_container
        container stop lambda-pg lambda-mysql >/dev/null 2>&1 || true
        container rm lambda-pg lambda-mysql >/dev/null 2>&1 || true
        ;;
    status)
        container list 2>/dev/null | grep -E "^ID|lambda-(pg|mysql)" || echo "no rdb test servers running"
        ;;
    *)
        echo "usage: $0 up|down|status" >&2
        exit 2
        ;;
esac
