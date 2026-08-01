package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("larceny", "primes") {
		os.Exit(1)
	}
}
