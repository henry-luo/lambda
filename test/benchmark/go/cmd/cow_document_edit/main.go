package main

import (
	"os"

	"lambda-benchmarks/internal/bench"
)

func main() {
	if !bench.Run("cow_document_edit", "") {
		os.Exit(1)
	}
}
