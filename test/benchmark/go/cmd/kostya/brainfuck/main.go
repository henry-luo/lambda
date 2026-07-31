package main

import (
	"lambda-benchmarks/internal/bench"
	"os"
)

func main() {
	if !bench.Run("kostya", "brainfuck") {
		os.Exit(1)
	}
}
