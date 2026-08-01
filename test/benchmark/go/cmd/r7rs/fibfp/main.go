package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("r7rs", "fibfp") {
		os.Exit(1)
	}
}
