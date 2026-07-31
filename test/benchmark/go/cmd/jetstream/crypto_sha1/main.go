package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("jetstream", "crypto_sha1") {
		os.Exit(1)
	}
}
