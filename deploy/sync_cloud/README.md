# Stateless cloud-sync HTTP adapter

The application lives in `lambda.sync.cloud_function` (S12.1.1v3, D11.1.2).
`http_host.py` only translates HTTP into a fresh native script invocation and
returns its encoded response. It holds no membership, sync cursor, repository
secret or publication state. All authoritative service state is in S3-compatible
object storage. See the [setup, protocol and limits](../../lmd/package/sync/CLOUD.md).

There are three integration surfaces:

- `invoke(body: bytes, root=..., storage=...) -> (status, response_bytes)` is a
  generic one-request host entry, suitable for a provider's HTTP-function adapter.
- `handler(event, context)` handles a function-URL-style `{body,isBase64Encoded}`
  envelope and returns `{statusCode,headers,body}`. Map the provider's own envelope
  to `invoke` when different; do not assume every provider has the same event API.
- Running `python3 deploy/sync_cloud/http_host.py` starts a loopback development
  server. `--bind 0.0.0.0` is explicit for a serverless container's HTTP listener;
  `PORT` chooses its port. `/sync` accepts bounded POST requests only.

A deployment must provide:

1. A native Lambda runtime built for the provider's OS/architecture, its native
   dependencies, and the shipped `lmd` package tree. The macOS qualification
   executable cannot run in a Linux container.
2. Python 3.9 or newer, `LAMBDA_ROOT` pointing to that runtime directory, a writable
   `temp/` directory there, and `LAMBDA_CLOUD_STORAGE` from host secret injection.
3. A private pre-created bucket and a storage principal limited to the required
   prefix operations. Run cloud bootstrap as an explicit operator command.
4. HTTPS termination, a direct function/container endpoint, a request limit of
   128 KiB, response limit of 1 MiB and a deadline sufficient for bounded storage
   operations. The adapter bounds a child to 60 seconds. Storage/client timeouts
   can abort first; retry starts with durable-state inspection.
5. Provider concurrency/resource limits. The development/container HTTP adapter
   admits four function children; excess requests receive 503. It does not host
   live-update WebSockets or long polling.

Use the provider's secret configuration; never bake access credentials, wrapping
keys or a private agent directory into an image. `--no-log` keeps interpreter
URL diagnostics out of shared application logs. HTTP callers receive generic
rejection/failure responses, never the child process's diagnostics.

`Dockerfile` is a packaging template for an **already qualified Linux runtime
image** with Python and `/app/lambda.exe`. It replaces the script package tree
and adds this adapter. It has no guessed runtime image default:

```sh
docker build -f deploy/sync_cloud/Dockerfile \
  --build-arg LAMBDA_RUNTIME_IMAGE=YOUR_QUALIFIED_RUNTIME_IMAGE \
  -t lambda-sync-function .
```

The Dockerfile-specific ignore file sends only shipped script sources and the
HTTP wrapper to the build context. Docker/container execution and hosted provider
execution have not been qualified by the native macOS gate. The function can also
be packaged directly with the provider's native-function runtime; a separate API
gateway is not an application dependency.

For a real local S3 backend, set `LAMBDA_CLOUD_TEST_STORAGE_FILE` to a protected
JSON file holding an isolated test bucket's adapter configuration, then run:

```sh
LAMBDA_CLOUD_TEST_STORAGE_FILE="$PWD/temp/cloud-test-storage.json" \
python3 test/sync_cloud/test_cloud.py
```

The test owns its function hosts, proxy and client children. It never starts or
modifies a production bucket. Each run uses a distinct space prefix; retained test objects require
operator cleanup after qualification. Test secrets stay in the operator config
and child environment; ordinary test clients receive no storage credentials.

## Authorized maintenance

The storage adapter now requires bounded `erase(key)` (S3 DeleteObject) in
addition to reads, conditional writes and capabilities. Public clients never
receive DELETE capabilities. Selection/copy/cut requires the owner's key holder;
`cloud_function.maintain(store,space,budget=32,reap=false)` is an optional private
scheduler entry that executes only an already committed deletion plan. Bind it
to deployment authority, never the public HTTP route (D11.2.1–D11.2.4).

[Maintenance and recovery](../../lmd/package/sync/MAINTENANCE.md) documents
client scheduling, epoch fences, immutable catalog backups and fail-closed
recovery. Drain old handler invocations and upgrade writers before enabling
retention. Destructive maintenance on legacy/recovered catalogs requires an
inventory audit; that audit tool is outside this phase.

The additional isolated-backend gate explicitly tests authorized old-key deletion,
cache reservations and crash recovery:

```sh
LAMBDA_CLOUD_TEST_STORAGE_FILE="$PWD/temp/cloud-test-storage.json" \
python3 test/sync_retention/test_retention.py
```
