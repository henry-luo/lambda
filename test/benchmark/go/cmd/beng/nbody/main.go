package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("beng", "nbody") {
		os.Exit(1)
	}
}
