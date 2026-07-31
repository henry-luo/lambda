package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("larceny", "divrec") {
		os.Exit(1)
	}
}
