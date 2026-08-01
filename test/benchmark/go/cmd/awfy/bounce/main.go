package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("awfy", "bounce") {
		os.Exit(1)
	}
}
