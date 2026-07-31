package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("kostya", "matmul") {
		os.Exit(1)
	}
}
