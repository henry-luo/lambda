package bench

import (
	"fmt"
	"math"
	"os"
	"time"
)

// These scalar workloads share counts, arithmetic order and oracles with SUITE.md.
const (
	microDecimalBytes     = 20 // maximum signed int64 decimal width
	microOutputBatchLines = 256
)

// Append into caller-owned storage without allocating a string for each digit.
func microAppendDecimal(text []byte, value int64) []byte {
	negative := value < 0
	if negative {
		value = -value
	}
	divisor := int64(1)
	// Find the leading divisor with one constant divide, then reuse each quotient.
	limit := value / 10
	for divisor <= limit {
		divisor *= 10
	}
	if negative {
		text = append(text, '-')
	}
	for divisor > 0 {
		digit := value / divisor
		text = append(text, byte(48+digit))
		value -= digit * divisor
		divisor /= 10
	}
	return text
}
func microDecimalValue(text []byte) int64 {
	negative, index, value := text[0] == '-', 0, int64(0)
	if negative {
		index = 1
	}
	for index < len(text) {
		value = value*10 + int64(text[index]) - 48
		index++
	}
	if negative {
		return -value
	}
	return value
}
func microParseIntegers() [4]int64 {
	seed, checksum, size, errors := int64(42), int64(0), int64(0), int64(0)
	var storage [microDecimalBytes]byte
	for index := 0; index < 100000; index++ {
		seed = seed * 16807 % 2147483647
		value := seed
		if index%8 == 0 {
			value = 0
		} else if index%8 == 1 {
			value = -seed
		}
		text := microAppendDecimal(storage[:0], value)
		parsed := microDecimalValue(text)
		if parsed != value {
			errors++
		}
		size += int64(len(text))
		checksum = (checksum*31 + parsed + 2147483647) % 1000000007
	}
	return [4]int64{checksum, size, seed, errors}
}
func microGram(matrix []float64, rows, columns int, result []float64) {
	for i := 0; i < columns; i++ {
		for j := 0; j < columns; j++ {
			total := 0.0
			for k := 0; k < rows; k++ {
				total += matrix[k*columns+i] * matrix[k*columns+j]
			}
			result[i*columns+j] = total
		}
	}
}
func microSquare(matrix []float64, n int, result []float64) {
	for i := 0; i < n; i++ {
		row := matrix[i*n : (i+1)*n]
		out := result[i*n : (i+1)*n]
		for j := range out {
			total := 0.0
			for k, value := range row {
				total += value * matrix[k*n+j]
			}
			out[j] = total
		}
	}
}
func microTraceFourth(matrix []float64, rows, columns int, scratch []float64) float64 {
	n := columns * columns
	first, second := scratch[:n], scratch[n:2*n]
	// Each product overwrites its destination; the Gram storage can hold G^4.
	microGram(matrix, rows, columns, first)
	microSquare(first, columns, second)
	microSquare(second, columns, first)
	total := 0.0
	for i := 0; i < columns; i++ {
		total += first[i*columns+i]
	}
	return total
}
func microVariation(values []float64) float64 {
	total := 0.0
	for _, value := range values {
		total += value
	}
	mean := total / float64(len(values))
	total = 0.0
	for _, value := range values {
		delta := value - mean
		total += delta * delta
	}
	return math.Sqrt(total/float64(len(values)-1)) / mean
}
func microMatrixStatistics() [4]int64 {
	seed, digest := int64(42), int64(0)
	v, w := make([]float64, 1000), make([]float64, 1000)
	// The largest Gram is 20x20; reuse two disjoint matrices within this workload.
	var scratch [2 * 20 * 20]float64
	blocks, p, q := make([]float64, 100), make([]float64, 100), make([]float64, 100)
	for iteration := 0; iteration < 1000; iteration++ {
		for i := 0; i < 100; i++ {
			seed = seed * 16807 % 2147483647
			blocks[i] = float64(seed)/2147483647.0*2.0 - 1.0
		}
		for block := 0; block < 4; block++ {
			for row := 0; row < 5; row++ {
				for column := 0; column < 5; column++ {
					value := blocks[block*25+row*5+column]
					p[row*20+block*5+column] = value
					q[(block/2*5+row)*10+block%2*5+column] = value
				}
			}
		}
		v[iteration] = microTraceFourth(p, 5, 20, scratch[:])
		w[iteration] = microTraceFourth(q, 10, 10, scratch[:])
		digest = (digest*31 + int64(math.Floor(v[iteration]*1000))) % 1000000007
		digest = (digest*31 + int64(math.Floor(w[iteration]*1000))) % 1000000007
	}
	return [4]int64{int64(math.Floor(microVariation(v) * 1e9)), int64(math.Floor(microVariation(w) * 1e9)), digest, seed}
}
func microIterationPiSum() [4]int64 {
	values := make([]float64, 500)
	for iteration := 0; iteration < 500; iteration++ {
		total := 0.0
		for k := 1; k <= 10000+iteration; k++ {
			total += 1.0 / (float64(k) * float64(k))
		}
		values[iteration] = total
	}
	digest := 0.0
	for i := 0; i < 500; i++ {
		digest += values[i] * float64(i+1)
	}
	return [4]int64{int64(math.Floor(values[0] * 1e12)), int64(math.Floor(values[499] * 1e12)), int64(math.Floor(digest * 1e6)), 5124750}
}
func microFormattedOutput() [4]int64 {
	size, digest, writes := int64(0), int64(0), int64(0)
	// Retain batch capacity so appending a line never copies the preceding lines.
	buffer := make([]byte, 0, microOutputBatchLines*(2*microDecimalBytes+2))
	for i := int64(1); i <= 100000; i++ {
		start := len(buffer)
		buffer = microAppendDecimal(buffer, i)
		buffer = append(buffer, ' ')
		buffer = microAppendDecimal(buffer, i+1)
		buffer = append(buffer, '\n')
		for _, value := range buffer[start:] {
			digest = (digest*31 + int64(value)) % 1000000007
		}
		size += int64(len(buffer) - start)
		if i%microOutputBatchLines == 0 || i == 100000 {
			if err := os.WriteFile(os.DevNull, buffer, 0666); err != nil {
				panic(err)
			}
			writes++
			buffer = buffer[:0]
		}
	}
	return [4]int64{size, digest, writes, 100000}
}
func runJuliaMicro(name string) bool {
	var workload func() [4]int64
	var expected [4]int64
	switch name {
	case "parse_integers":
		workload, expected = microParseIntegers, [4]int64{592470661, 854479, 1966931148, 0}
	case "matrix_statistics":
		workload, expected = microMatrixStatistics, [4]int64{464726438, 486656926, 47509838, 1966931148}
	case "iteration_pi_sum":
		workload, expected = microIterationPiSum, [4]int64{1644834071848, 1644838824217, 206015869118, 5124750}
	case "formatted_output":
		workload, expected = microFormattedOutput, [4]int64{1177795, 584298900, 391, 100000}
	default:
		return false
	}
	if workload() != expected {
		fmt.Printf("%s: FAIL warmup\n", name)
		return false
	}
	started := time.Now()
	result := workload()
	elapsed := float64(time.Since(started).Nanoseconds()) / 1e6
	if result != expected {
		fmt.Printf("%s: FAIL %v\n", name, result)
		return false
	}
	fmt.Printf("%s: PASS %d %d %d %d\n", name, result[0], result[1], result[2], result[3])
	fmt.Printf("__TIMING__:%.6f\n", elapsed)
	return true
}
