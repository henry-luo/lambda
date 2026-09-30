package bench

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"time"
)

type textHyphenTables struct {
	LevelOffsets     []int    `json:"level_offsets"`
	LevelLengths     []int    `json:"level_lengths"`
	LevelValues      []int    `json:"level_values"`
	NodeFirst        []int    `json:"node_first"`
	NodeCount        []int    `json:"node_count"`
	NodeLevel        []int    `json:"node_level"`
	EdgeCode         []int    `json:"edge_code"`
	EdgeChild        []int    `json:"edge_child"`
	ExceptionOffsets []int    `json:"exception_offsets"`
	ExceptionCounts  []int    `json:"exception_counts"`
	ExceptionMarkers []int    `json:"exception_markers"`
	ExceptionWords   []string `json:"exception_words"`
	Root             int      `json:"root"`
}

type textHyphenator struct {
	tables      *textHyphenTables
	wordCache   map[string]string
	markerCache map[string][]int
	exceptions  map[string][]int
}

func newTextHyphenator(tables *textHyphenTables) *textHyphenator {
	exceptions := make(map[string][]int, len(tables.ExceptionWords))
	for index, word := range tables.ExceptionWords {
		offset := tables.ExceptionOffsets[index]
		exceptions[word] = tables.ExceptionMarkers[offset : offset+tables.ExceptionCounts[index]]
	}
	return &textHyphenator{
		tables: tables, wordCache: make(map[string]string),
		markerCache: make(map[string][]int), exceptions: exceptions,
	}
}

func textAsciiLetter(char byte) bool {
	return char >= 'A' && char <= 'Z' || char >= 'a' && char <= 'z'
}

func textHyphenWordChar(char byte) bool {
	return textAsciiLetter(char) || char == '\''
}

func (hyphenator *textHyphenator) trieChild(node, code int) int {
	first := hyphenator.tables.NodeFirst[node]
	for edge := first; edge < first+hyphenator.tables.NodeCount[node]; edge++ {
		if hyphenator.tables.EdgeCode[edge] == code {
			return hyphenator.tables.EdgeChild[edge]
		}
	}
	return -1
}

func (hyphenator *textHyphenator) markersForWord(word string) []int {
	lowered := strings.ToLower(word)
	if markers, found := hyphenator.exceptions[lowered]; found {
		return markers
	}
	if markers, found := hyphenator.markerCache[lowered]; found {
		return markers
	}
	tables := hyphenator.tables
	length := len(word)
	levels := make([]int, length+1)
	extended := "." + lowered + "."
	for start := 0; start < length; start++ {
		node := tables.Root
		position := 0
		if start > 0 {
			position = start - 1
		}
		for cursor := start; cursor < length+2; cursor++ {
			node = hyphenator.trieChild(node, int(extended[cursor]))
			if node < 0 {
				break
			}
			levelIndex := tables.NodeLevel[node]
			if levelIndex >= 0 {
				offset := tables.LevelOffsets[levelIndex]
				for levelOffset := 0; levelOffset < tables.LevelLengths[levelIndex]; levelOffset++ {
					target := position + levelOffset
					value := tables.LevelValues[offset+levelOffset]
					if target >= 0 && target <= length && value > levels[target] {
						levels[target] = value
					}
				}
			}
		}
	}
	levels[0], levels[1], levels[length-1], levels[length] = 0, 0, 0, 0
	markers := make([]int, 0)
	for index, value := range levels {
		if value&1 == 1 {
			markers = append(markers, index)
		}
	}
	hyphenator.markerCache[lowered] = markers
	return markers
}

func (hyphenator *textHyphenator) hyphenateWord(word string) string {
	if cached, found := hyphenator.wordCache[word]; found {
		return cached
	}
	result := word
	if len(word) >= 5 && !strings.Contains(word, "-") {
		markers := hyphenator.markersForWord(word)
		var output strings.Builder
		markerIndex := 0
		for index := 0; index < len(word); index++ {
			if markerIndex < len(markers) && markers[markerIndex] == index {
				output.WriteByte('-')
				markerIndex++
			}
			output.WriteByte(word[index])
		}
		for markerIndex < len(markers) {
			output.WriteByte('-')
			markerIndex++
		}
		result = output.String()
	}
	hyphenator.wordCache[word] = result
	return result
}

func (hyphenator *textHyphenator) hyphenateText(source string) string {
	var output strings.Builder
	for index := 0; index < len(source); {
		if source[index] == '<' && index+1 < len(source) &&
			(textAsciiLetter(source[index+1]) || source[index+1] == '/') {
			for index < len(source) {
				char := source[index]
				output.WriteByte(char)
				index++
				if char == '>' {
					break
				}
			}
		} else if textHyphenWordChar(source[index]) {
			start := index
			for index < len(source) {
				char := source[index]
				if textHyphenWordChar(char) {
					index++
				} else if char == '-' && index > start && index+1 < len(source) &&
					textAsciiLetter(source[index+1]) {
					index++
				} else {
					break
				}
			}
			output.WriteString(hyphenator.hyphenateWord(source[start:index]))
		} else {
			output.WriteByte(source[index])
			index++
		}
	}
	return output.String()
}

func runHyphen() bool {
	tableData, err := os.ReadFile("test/benchmark/text/hyphen_tables.json")
	if err != nil {
		return false
	}
	caseData, err := os.ReadFile("test/benchmark/text/hyphen_cases.json")
	if err != nil {
		return false
	}
	var tables textHyphenTables
	var cases [][2]string
	if json.Unmarshal(tableData, &tables) != nil || json.Unmarshal(caseData, &cases) != nil {
		return false
	}
	verifier := newTextHyphenator(&tables)
	for index, pair := range cases {
		if verifier.hyphenateText(pair[0]) != pair[1] {
			fmt.Printf("hyphen: FAIL fixture verification at case %d\n", index)
			return false
		}
	}
	checksum := 0
	started := time.Now()
	for round := 0; round < 32; round++ {
		hyphenator := newTextHyphenator(&tables)
		for index, pair := range cases {
			result := hyphenator.hyphenateText(pair[0])
			checksum = (checksum + len(result)*29) % 1000000007
			if len(result) > 0 {
				checksum = (checksum + int(result[index%len(result)])) % 1000000007
			}
		}
	}
	elapsedMs := float64(time.Since(started).Nanoseconds()) / 1e6
	ok := checksum == 1183296
	fmt.Printf("CHECKSUM:%d\n", checksum)
	fmt.Printf("__TIMING__:%.6f\n", elapsedMs)
	return ok
}
