package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("r7rs", "sumfp") {
		os.Exit(1)
	}
}
