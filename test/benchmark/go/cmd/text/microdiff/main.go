package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("text", "microdiff") {
		os.Exit(1)
	}
}
