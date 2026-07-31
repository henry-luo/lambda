package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("jetstream", "raytrace3d") {
		os.Exit(1)
	}
}
