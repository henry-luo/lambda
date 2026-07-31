package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("beng", "regexredux") {
		os.Exit(1)
	}
}
