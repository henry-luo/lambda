package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("beng", "pidigits") {
		os.Exit(1)
	}
}
