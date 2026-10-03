package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("julia", "formatted_output") {
		os.Exit(1)
	}
}
