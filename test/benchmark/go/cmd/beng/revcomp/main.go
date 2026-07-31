package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("beng", "revcomp") {
		os.Exit(1)
	}
}
