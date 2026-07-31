package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("kostya", "json_gen") {
		os.Exit(1)
	}
}
