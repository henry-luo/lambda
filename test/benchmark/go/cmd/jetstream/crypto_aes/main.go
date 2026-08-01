package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("jetstream", "crypto_aes") {
		os.Exit(1)
	}
}
