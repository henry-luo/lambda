package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("beng", "mandelbrot") {
		os.Exit(1)
	}
}
