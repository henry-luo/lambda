package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("awfy", "queens") {
		os.Exit(1)
	}
}
