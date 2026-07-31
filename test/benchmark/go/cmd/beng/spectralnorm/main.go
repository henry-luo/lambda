package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("beng", "spectralnorm") {
		os.Exit(1)
	}
}
