package bench

import (
	"fmt"
	"strconv"
	"strings"
	"time"
)

func textNaiveSearch(text, pattern []byte, start int) int {
	if len(pattern) == 0 {
		return start
	}
	for position := start; position <= len(text)-len(pattern); position++ {
		offset := 0
		for offset < len(pattern) && text[position+offset] == pattern[offset] {
			offset++
		}
		if offset == len(pattern) {
			return position
		}
	}
	return -1
}

func textPrefixTable(pattern []byte) []int {
	table := make([]int, len(pattern))
	length := 0
	for index := 1; index < len(pattern); {
		if pattern[index] == pattern[length] {
			length++
			table[index] = length
			index++
		} else if length > 0 {
			length = table[length-1]
		} else {
			index++
		}
	}
	return table
}

func textKMPSearch(text, pattern []byte, start int) int {
	if len(pattern) == 0 {
		return start
	}
	table := textPrefixTable(pattern)
	textIndex, patternIndex := start, 0
	for textIndex < len(text) {
		if text[textIndex] == pattern[patternIndex] {
			textIndex++
			patternIndex++
			if patternIndex == len(pattern) {
				return textIndex - len(pattern)
			}
		} else if patternIndex > 0 {
			patternIndex = table[patternIndex-1]
		} else {
			textIndex++
		}
	}
	return -1
}

func textBoyerMooreSearch(text, pattern []byte, start int) int {
	if len(pattern) == 0 {
		return start
	}
	var last [256]int
	for index := range last {
		last[index] = -1
	}
	for index, code := range pattern {
		last[code] = index
	}
	for position := start; position <= len(text)-len(pattern); {
		offset := len(pattern) - 1
		for offset >= 0 && text[position+offset] == pattern[offset] {
			offset--
		}
		if offset < 0 {
			return position
		}
		advance := offset - last[text[position+offset]]
		if advance < 1 {
			advance = 1
		}
		position += advance
	}
	return -1
}

func runTextSearch() bool {
	const rounds = 1536
	const modulus = 1000000007
	rows := make([]string, 512)
	for index := range rows {
		rows[index] = fmt.Sprintf("record-%d alpha aaaaaaaaaaaaaaaaaaaaaaaa token-%d omega needle-%d",
			index, index%23, index%11)
	}
	corpus := []byte(strings.Join(rows, "\n"))
	patterns := [][]byte{
		[]byte("record-0 alpha"), []byte("record-2048 alpha"),
		[]byte("token-22 omega"), []byte("needle-10"),
		[]byte("omega needle-7"),
		[]byte("alpha aaaaaaaaaaaaaaaaaaaaaaaa token-3"),
		[]byte("missing-marker"), []byte("record-2047 omega"),
	}
	checksum := 0
	started := time.Now()
	for round := 0; round < rounds; round++ {
		for index, pattern := range patterns {
			start := (round*17 + index*13) % 97
			naive := textNaiveSearch(corpus, pattern, start)
			kmp := textKMPSearch(corpus, pattern, start)
			boyerMoore := textBoyerMooreSearch(corpus, pattern, start)
			if naive != kmp || kmp != boyerMoore {
				return false
			}
			checksum = (checksum + (naive+2)*(index+3) + (round+1)*7) % modulus
		}
	}
	elapsedMs := float64(time.Since(started).Nanoseconds()) / 1e6
	ok := checksum == 91395120
	fmt.Printf("text_search: CHECKSUM:%d\n", checksum)
	fmt.Printf("__TIMING__:%.6f\n", elapsedMs)
	return ok
}

func textMakeVariant(base []string, side string) []string {
	lines := make([]string, len(base))
	for index, line := range base {
		if index%17 == 0 {
			line += fmt.Sprintf(" %s edit %d keeps the paragraph useful", side, index%31)
		} else if side == "left" && index%23 == 0 {
			line += " left-only annotation"
		} else if side == "right" && index%29 == 0 {
			line += " right-only annotation"
		}
		lines[index] = line
	}
	return lines
}

func textWordAt(words []string, index int) string {
	if index < len(words) {
		return words[index]
	}
	return ""
}

func textMergeWords(baseLine, leftLine, rightLine string) string {
	if leftLine == rightLine {
		return leftLine
	}
	if leftLine == baseLine {
		return rightLine
	}
	if rightLine == baseLine {
		return leftLine
	}
	baseWords := strings.Split(baseLine, " ")
	leftWords := strings.Split(leftLine, " ")
	rightWords := strings.Split(rightLine, " ")
	count := len(baseWords)
	if len(leftWords) > count {
		count = len(leftWords)
	}
	if len(rightWords) > count {
		count = len(rightWords)
	}
	words := make([]string, 0, count)
	for index := 0; index < count; index++ {
		baseWord := textWordAt(baseWords, index)
		leftWord := textWordAt(leftWords, index)
		rightWord := textWordAt(rightWords, index)
		switch {
		case leftWord == rightWord:
			words = append(words, leftWord)
		case leftWord == baseWord:
			words = append(words, rightWord)
		case rightWord == baseWord:
			words = append(words, leftWord)
		default:
			words = append(words, "<<<<<<< LEFT", leftWord, "=======", rightWord, ">>>>>>> RIGHT")
		}
	}
	return strings.Join(words, " ")
}

func textMergeLines(base, left, right []string) string {
	merged := make([]string, len(base))
	for index, baseLine := range base {
		leftLine, rightLine := left[index], right[index]
		switch {
		case leftLine == rightLine:
			merged[index] = leftLine
		case leftLine == baseLine:
			merged[index] = rightLine
		case rightLine == baseLine:
			merged[index] = leftLine
		default:
			merged[index] = textMergeWords(baseLine, leftLine, rightLine)
		}
	}
	return strings.Join(merged, "\n")
}

func runThreeWayMerge() bool {
	const rounds = 11000
	const lineCount = 768
	const modulus = 1000000007
	base := make([]string, lineCount)
	for index := range base {
		base[index] = fmt.Sprintf("section %d records the base document with stable words for merging and review", index)
	}
	left := textMakeVariant(base, "left")
	right := textMakeVariant(base, "right")
	checksum := 0
	started := time.Now()
	for round := 0; round < rounds; round++ {
		merged := textMergeLines(base, left, right)
		checksum = (checksum + len(merged)*31 + int(merged[(round*37)%len(merged)])) % modulus
	}
	elapsedMs := float64(time.Since(started).Nanoseconds()) / 1e6
	ok := checksum == 342313356
	fmt.Printf("three_way_merge: CHECKSUM:%d\n", checksum)
	fmt.Printf("__TIMING__:%.6f\n", elapsedMs)
	return ok
}

type textLogRecord struct {
	timestamp, level, service, region, route, message string
	status, latency, bytes                            int
}

type textLogGroup struct {
	count, errors, slow, totalLatency, totalBytes int
}

func textMakeLogLine(index int) string {
	timestamp := fmt.Sprintf("2026-09-07T%02d:%02d:%02dZ", index%24, index%60, (index*7)%60)
	level := "INFO"
	if index%13 == 0 {
		level = "ERROR"
	} else if index%5 == 0 {
		level = "WARN"
	}
	services := [4]string{"api", "worker", "db", "cache"}
	regions := [3]string{"us-east", "eu-west", "ap-south"}
	service := services[index%len(services)]
	region := regions[(index*3)%len(regions)]
	status := 200
	if index%19 == 0 {
		status = 503
	} else if index%7 == 0 {
		status = 404
	}
	latency := (index*37)%900 + 4
	bytes := (index*113)%50000 + 512
	route := "/v1/search"
	if index%2 == 0 {
		route = "/v1/items"
	}
	message := "request-complete"
	if index%11 == 0 {
		message = "retry-scheduled"
	}
	prefix := fmt.Sprintf("%s %s %s", timestamp, level, service)
	if index%3 == 0 {
		prefix = fmt.Sprintf("%s level=%s service=%s", timestamp, level, service)
	}
	return fmt.Sprintf("%s status=%d latency=%d region=%s route=%s bytes=%d message=%s",
		prefix, status, latency, region, route, bytes, message)
}

func textParseLogLine(line string) textLogRecord {
	fields := strings.Split(line, " ")
	record := textLogRecord{timestamp: fields[0]}
	start := 1
	if !strings.Contains(fields[1], "=") {
		record.level, record.service = fields[1], fields[2]
		start = 3
	}
	for _, token := range fields[start:] {
		separator := strings.IndexByte(token, '=')
		if separator < 0 {
			continue
		}
		key, value := token[:separator], token[separator+1:]
		switch key {
		case "level":
			record.level = value
		case "service":
			record.service = value
		case "status":
			record.status, _ = strconv.Atoi(value)
		case "latency":
			record.latency, _ = strconv.Atoi(value)
		case "region":
			record.region = value
		case "route":
			record.route = value
		case "bytes":
			record.bytes, _ = strconv.Atoi(value)
		case "message":
			record.message = value
		}
	}
	return record
}

func textProcessLogs(lines []string) (map[string]*textLogGroup, int, int) {
	groups := map[string]*textLogGroup{
		"api": {}, "worker": {}, "db": {}, "cache": {},
	}
	accepted, rejected := 0, 0
	for _, line := range lines {
		record := textParseLogLine(line)
		if record.status >= 500 || record.level == "ERROR" {
			rejected++
			continue
		}
		group := groups[record.service]
		group.count++
		group.totalLatency += record.latency
		group.totalBytes += record.bytes
		if record.latency >= 500 {
			group.slow++
		}
		accepted++
	}
	return groups, accepted, rejected
}

func runLogPipeline() bool {
	const rounds = 180
	const count = 12000
	const modulus = 1000000007
	logs := make([]string, count)
	for index := range logs {
		logs[index] = textMakeLogLine(index)
	}
	checksum := 0
	started := time.Now()
	for round := 0; round < rounds; round++ {
		groups, accepted, rejected := textProcessLogs(logs)
		checksum = (checksum + accepted*31 + rejected*17 + groups["api"].totalLatency + groups["worker"].totalBytes + round) % modulus
	}
	elapsedMs := float64(time.Since(started).Nanoseconds()) / 1e6
	ok := checksum == 292634526
	fmt.Printf("log_pipeline: CHECKSUM:%d\n", checksum)
	fmt.Printf("__TIMING__:%.6f\n", elapsedMs)
	return ok
}

func runText(name string) bool {
	switch name {
	case "fast_diff":
		return runFastDiff()
	case "text_search":
		return runTextSearch()
	case "three_way_merge":
		return runThreeWayMerge()
	case "log_pipeline":
		return runLogPipeline()
	case "microdiff":
		return runMicrodiff()
	case "prettier_ast":
		return runPrettierAST()
	case "hyphen":
		return runHyphen()
	case "jq_mix", "jq_records", "jq_bf", "jq_tree":
		return runJq(name)
	default:
		return false
	}
}
