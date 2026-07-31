package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("beng", "fannkuch") {
		os.Exit(1)
	}
}
