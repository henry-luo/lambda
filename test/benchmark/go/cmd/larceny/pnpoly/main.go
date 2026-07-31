package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("larceny", "pnpoly") {
		os.Exit(1)
	}
}
