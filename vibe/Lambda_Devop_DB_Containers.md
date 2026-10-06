# Lambda Database Test Servers with Apple Container

**Status:** Active development procedure  
**Host:** Apple Silicon macOS 26+  
**Servers:** PostgreSQL 18 and MySQL 8.4, both with TLS  
**Used by:** the RDB driver corpus (`make test-rdb-drivers-local`)

## Purpose

The `rdb-drivers` Jube module (PostgreSQL and MySQL/MariaDB) and the host TLS
bridge can only be tested against real servers. This procedure runs both
servers locally in Apple's native `container` VMs, each with TLS enabled from
a throwaway CA, loads the same fixture into both, and points the driver corpus
at them.

Design background: `vibe/Lambda_IO_RDB.md` §13 (driver module, TLS bridge
§13.15, corpus §13.11). Use Apple `container` for these servers, not Docker.

## One-time setup

1. **Install Apple `container`** (1.5.0 or later). Download the signed
   installer `container-<version>-installer-signed.pkg` from
   <https://github.com/apple/container/releases> and run it; it needs an admin
   password:

   ```bash
   sudo installer -pkg container-1.5.0-installer-signed.pkg -target /
   ```

   Check the package first if you like:
   `pkgutil --check-signature <pkg>` should report Apple's "Developer ID
   Installer: Apple Inc. - Containerization" and notarisation.

2. **Start the container service.** The first start also installs Apple's
   recommended Linux kernel:

   ```bash
   container system start --enable-kernel-install
   container system status
   ```

   `utils/rdb-test-servers.sh` starts the service itself when it is not
   running, so this step is only needed once per boot if you use the tool
   directly.

3. **First server start pulls the images** `docker.io/library/postgres:18`
   and `docker.io/library/mysql:8.4` (roughly 0.5 GB and 0.6 GB). Later starts
   reuse them.

## Everyday use

All commands run from the repository root.

| Command | What it does |
|---|---|
| `make rdb-test-servers-up` | Starts both servers if needed, reloads the fixtures, prints the two `export` lines |
| `make test-rdb-drivers-local` | Builds the module, brings the servers up, runs the whole corpus |
| `make test-rdb-drivers` | Runs the corpus against `LAMBDA_TEST_PG_URI` / `LAMBDA_TEST_MYSQL_URI` you set yourself; an unset backend is reported as skipped |
| `make rdb-test-servers-down` | Stops and removes both containers |
| `utils/rdb-test-servers.sh status` | Lists the running test containers |

Typical session:

```bash
make test-rdb-drivers-local        # servers up + corpus (expect "24 passed")
make rdb-test-servers-down         # when done
```

To run individual scripts by hand, export the URIs `up` prints:

```bash
eval "$(utils/rdb-test-servers.sh up | grep '^export')"
echo "$LAMBDA_TEST_PG_URI"
```

## What the servers look like

`utils/rdb-test-servers.sh up` creates:

| | PostgreSQL | MySQL |
|---|---|---|
| Container | `lambda-pg` | `lambda-mysql` |
| Image | `postgres:18` | `mysql:8.4` |
| Host port | `127.0.0.1:15432` | `127.0.0.1:13306` |
| Database | `shop` | `shop` |
| User / password | `lambda` / `lambdapw` | `lambda` / `lambdapw` (root: `rootpw`) |
| Login method | SCRAM-SHA-256 | `caching_sha2_password` (MySQL 8 default) |
| TLS | on, test CA | on, test CA |

These are throwaway test credentials; never reuse them anywhere else.

**TLS material** lives in `temp/rdb-tls/`: a CA (`ca.crt`, `ca.key`) and a
server certificate for `localhost` / `127.0.0.1`, valid for 30 days. `up`
regenerates them when they are missing or about to expire. The servers get
the certificates through a read-only mount and copy them inside the
container, because PostgreSQL insists on owning its private key file.

**Fixtures** come from `test/rdb/fixture/postgresql.sql` and
`test/rdb/fixture/mysql.sql`: the same tables and rows on both backends
(authors, books, a foreign key, indexes, a view, a trigger, and
decimal/datetime/JSON/BLOB columns). Every `up` drops and reloads them, so
runs start from a known state.

**URIs** printed by `up` verify the server fully:

```text
postgresql://lambda:lambdapw@localhost:15432/shop?sslmode=verify-full&sslrootcert=<repo>/temp/rdb-tls/ca.crt
mysql://lambda:lambdapw@localhost:13306/shop?ssl-mode=VERIFY_IDENTITY&ssl-ca=<repo>/temp/rdb-tls/ca.crt
```

## Connecting from Lambda

```lambda
let db = input("postgresql://lambda:lambdapw@localhost:15432/shop?sslmode=verify-full&sslrootcert=temp/rdb-tls/ca.crt")^
db.table_names
```

```lambda
let db = input("mysql://lambda:lambdapw@localhost:13306/shop?ssl-mode=REQUIRED")^
[for (b in db.data.books) b.title]
```

Things worth knowing when trying modes by hand:

- **No CA in the URI** means the host's OS trust store is used
  (`lib/trust_store`). The throwaway CA is not in it, so `verify-ca` /
  `verify-full` without `sslrootcert` / `ssl-ca` fail by design. Setting
  `SSL_CERT_FILE=temp/rdb-tls/ca.crt` makes the test CA the whole trust store
  for that process (HTTP included).
- **MySQL full authentication needs a TLS guarantee.** With
  `ssl-mode=DISABLED` or `PREFERRED`, a login succeeds only while the server
  still caches the user's credentials from an earlier TLS login. To exercise
  the full-authentication path, clear the cache first:

  ```bash
  container exec lambda-mysql mysql -uroot -prootpw -e "FLUSH PRIVILEGES"
  ```

- Credentials never reach `log.txt`; the corpus checks that on every run.

## Inspecting the servers

```bash
container list                                  # running containers and their IPs
container logs lambda-pg | tail                 # server logs
container logs lambda-mysql | tail
container exec -it lambda-pg psql -U lambda -d shop
container exec -it lambda-mysql mysql -uroot -prootpw shop
container exec lambda-pg psql -U lambda -d shop -tAc "show ssl"   # expect: on
```

## Cleaning up

```bash
make rdb-test-servers-down                      # stop and remove both containers
container image rm postgres:18 mysql:8.4        # free the image space (optional)
rm -rf temp/rdb-tls                             # drop the test CA (optional)
container system stop                           # stop the container service (optional)
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| `apiserver is not running` | `container system start --enable-kernel-install` |
| `rdb-test-servers: Apple 'container' is not installed` | Install it (one-time setup, step 1) |
| `lambda-mysql did not become ready` | MySQL's first initialisation takes 20–40 s; rerun `up`, or check `container logs lambda-mysql` |
| Port 15432 or 13306 already in use | Stop whatever holds it, or `make rdb-test-servers-down` to remove stale containers |
| `certificate verification ... needs a CA file` in `log.txt` | The URI has a verify mode but no `sslrootcert` / `ssl-ca`, and the test CA is not an OS root; add the CA or use `require` |
| TLS handshake fails after a month | The test certificate expired; `up` regenerates it, but restart the servers so they load the new one (`down`, then `up`) |
| Corpus reports `SKIP postgresql` / `SKIP mysql` | `make test-rdb-drivers` ran without the URIs; use `make test-rdb-drivers-local` or export them from `up` |
