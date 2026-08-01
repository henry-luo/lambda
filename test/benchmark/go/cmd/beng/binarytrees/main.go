package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("beng", "binarytrees") {
		os.Exit(1)
	}
}
