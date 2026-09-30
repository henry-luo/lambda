package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("text", "hyphen") {
		os.Exit(1)
	}
}
