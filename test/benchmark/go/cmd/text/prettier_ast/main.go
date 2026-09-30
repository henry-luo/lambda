package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("text", "prettier_ast") {
		os.Exit(1)
	}
}
