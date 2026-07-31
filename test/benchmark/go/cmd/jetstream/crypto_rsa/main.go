package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("jetstream", "crypto_rsa") {
		os.Exit(1)
	}
}
