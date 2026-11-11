// Check the shared ASCII document against Word's front-coded vocabulary.
package main

import (
	"encoding/json"
	"fmt"
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"
)

type issue struct {
	Word  string `json:"word"`
	Start int    `json:"start"`
	End   int    `json:"end"`
}

func read(path string) []byte {
	data, err := os.ReadFile(path)
	if err != nil {
		panic(err)
	}
	return data
}

func dictionary() map[string]struct{} {
	words := map[string]struct{}{"Lambda": {}, "Radiant": {}}
	for _, line := range strings.Split(strings.TrimSuffix(string(read("lmd/package/doc/spelling/data/en-US.dict")), "\n"), "\n") {
		for _, group := range strings.Split(line, "|") {
			tokens := strings.Split(group, ";")
			previous := tokens[0]
			words[previous] = struct{}{}
			for _, token := range tokens[1:] {
				if comma := strings.IndexByte(token, ','); comma >= 0 {
					count, err := strconv.Atoi(token[:comma])
					if err != nil {
						panic(err)
					}
					previous = string([]rune(previous)[:count]) + token[comma+1:]
				} else {
					previous += token
				}
				words[previous] = struct{}{}
			}
		}
	}
	return words
}

func main() {
	words := dictionary()
	text := string(read("test/benchmark/text/spelling_document.txt"))
	var expected []issue
	if err := json.Unmarshal(read("test/benchmark/text/spelling_expected.json"), &expected); err != nil {
		panic(err)
	}
	token := regexp.MustCompile(`[A-Za-z]+(?:'[A-Za-z]+)*`)
	start := time.Now()
	matches := token.FindAllStringIndex(text, -1)
	issues := make([]issue, 0, len(expected))
	for _, span := range matches {
		word := text[span[0]:span[1]]
		lower := strings.ToLower(word)
		folded := word == strings.ToUpper(word) || word == strings.ToUpper(word[:1])+lower[1:]
		_, exact := words[word]
		_, lowerFound := words[lower]
		if !exact && !(folded && lowerFound) {
			issues = append(issues, issue{word, span[0], span[1]})
		}
	}
	elapsed := time.Since(start).Seconds() * 1000
	if len(matches) != 7110 || len(issues) != len(expected) {
		panic("spelling: FAIL result count")
	}
	for index := range issues {
		if issues[index] != expected[index] {
			panic("spelling: FAIL issue mismatch")
		}
	}
	fmt.Println("spelling: CHECKED:7110 ISSUES:720")
	fmt.Printf("__TIMING__:%.3f\n", elapsed)
}
