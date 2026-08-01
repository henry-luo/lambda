package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("jetstream", "deltablue") {
		os.Exit(1)
	}
}
