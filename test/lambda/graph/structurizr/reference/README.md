# Structurizr semantic references

These JSON files are maintenance references generated from this repository's
DSL fixtures with the exact Structurizr CLI release recorded in
`STRUCTURIZR_VERSION`. Ordinary tests do not require Java or Structurizr.

To regenerate after intentionally updating the pinned tool:

```sh
STRUCTURIZR_CLI_HOME=/path/to/structurizr-cli \
  test/lambda/graph/structurizr/reference/generate_workspace_refs.sh
```

Review semantic adapter output and update every recorded SHA-256 value when a
fixture or reference changes.
