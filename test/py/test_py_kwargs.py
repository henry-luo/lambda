def configure(host, port=8080, **options):
    print(f"{host}:{port}")
    for k, v in options.items():
        print(f"  {k}={v}")

configure("localhost", debug=True, timeout=30)

configure("example.com", 443, ssl=True)

# ** unpacking at call site — single dict
defaults = {"debug": False, "timeout": 60}
configure("server", **defaults)

# ** unpacking with additional named kwarg (non-overlapping)
extra = {"verbose": True}
configure("server2", 9000, **extra)
