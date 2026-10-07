package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("text", "jq_bf") {
		os.Exit(1)
	}
}
