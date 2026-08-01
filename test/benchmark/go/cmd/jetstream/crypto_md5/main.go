package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("jetstream", "crypto_md5") {
		os.Exit(1)
	}
}
