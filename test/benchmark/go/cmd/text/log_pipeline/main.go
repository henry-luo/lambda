package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("text", "log_pipeline") {
		os.Exit(1)
	}
}
