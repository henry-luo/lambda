package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("awfy", "mandelbrot") {
		os.Exit(1)
	}
}
