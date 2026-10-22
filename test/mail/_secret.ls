// lambda.io.secret against the OS secret store (the login Keychain on
// macOS) under a test-only service, removed again at the end.
import secret: lambda.io.secret

let SERVICE = "org.lambda.test.secret"

pn main() {
    secret.delete(SERVICE, "ada@example.com")^
    print("absent ", secret.get(SERVICE, "ada@example.com")^, "\n")
    secret.set(SERVICE, "ada@example.com", "app password one")^
    print("stored ", secret.get(SERVICE, "ada@example.com")^, "\n")
    secret.set(SERVICE, "ada@example.com", "pässword two ✓")^
    print("replaced ", secret.get(SERVICE, "ada@example.com")^, "\n")
    print("other ", secret.get(SERVICE, "bob@example.com")^, "\n")
    print("deleted ", secret.delete(SERVICE, "ada@example.com")^, "\n")
    print("again ", secret.delete(SERVICE, "ada@example.com")^, "\n")
    print("gone ", secret.get(SERVICE, "ada@example.com")^, "\n")
    secret.set(SERVICE, "ada@example.com", 42) ^ { print("refused: ", ^.message, "\n") }
    secret.get(SERVICE, "") ^ { print("refused: ", ^.message, "\n") }
}
