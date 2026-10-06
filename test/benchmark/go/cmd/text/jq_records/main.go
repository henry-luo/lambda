package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("text", "jq_records") {
		os.Exit(1)
	}
}
