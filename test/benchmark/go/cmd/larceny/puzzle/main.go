package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("larceny", "puzzle") {
		os.Exit(1)
	}
}
