package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("kostya", "base64") {
		os.Exit(1)
	}
}
