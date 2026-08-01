package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("awfy", "permute") {
		os.Exit(1)
	}
}
