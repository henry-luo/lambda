package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("text", "three_way_merge") {
		os.Exit(1)
	}
}
