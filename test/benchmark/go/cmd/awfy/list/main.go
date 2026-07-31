package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("awfy", "list") {
		os.Exit(1)
	}
}
