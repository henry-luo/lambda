package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("r7rs", "fft") {
		os.Exit(1)
	}
}
