package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("text", "jq_tree") {
		os.Exit(1)
	}
}
