package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("kostya", "primes") {
		os.Exit(1)
	}
}
