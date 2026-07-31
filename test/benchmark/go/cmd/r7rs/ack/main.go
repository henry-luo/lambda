package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("r7rs", "ack") {
		os.Exit(1)
	}
}
