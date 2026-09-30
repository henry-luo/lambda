package bench

// The algorithm follows the checked-in fast-diff.js implementation (Apache 2.0,
// derived from Neil Fraser's Diff Match and Patch). Its input is ASCII, so byte
// offsets have the same meaning as the JavaScript UTF-16 offsets in this suite.

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"time"
)

const (
	textDiffDelete = -1
	textDiffEqual  = 0
	textDiffInsert = 1
)

type textDiffPart struct {
	op   int
	text string
}

func textCommonPrefix(left, right string) int {
	index := 0
	for index < len(left) && index < len(right) && left[index] == right[index] {
		index++
	}
	return index
}

func textCommonSuffix(left, right string) int {
	index := 0
	for index < len(left) && index < len(right) && left[len(left)-index-1] == right[len(right)-index-1] {
		index++
	}
	return index
}

func textCommonOverlap(left, right string) int {
	count := len(left)
	if len(right) < count {
		count = len(right)
	}
	for length := count; length > 0; length-- {
		if strings.HasSuffix(left, right[:length]) {
			return length
		}
	}
	return 0
}

func textDiffHalfMatch(left, right string) ([5]string, bool) {
	longText, shortText := left, right
	if len(right) >= len(left) {
		longText, shortText = right, left
	}
	if len(longText) < 4 || len(shortText)*2 < len(longText) {
		return [5]string{}, false
	}
	findSeed := func(start int) ([5]string, bool) {
		seed := longText[start : start+len(longText)/4]
		best := [5]string{}
		found := false
		for position := strings.Index(shortText, seed); position >= 0; {
			prefix := textCommonPrefix(longText[start:], shortText[position:])
			suffix := textCommonSuffix(longText[:start], shortText[:position])
			middle := shortText[position-suffix : position+prefix]
			if !found || len(middle) > len(best[4]) {
				best = [5]string{longText[:start-suffix], longText[start+prefix:],
					shortText[:position-suffix], shortText[position+prefix:], middle}
				found = true
			}
			next := strings.Index(shortText[position+1:], seed)
			if next < 0 {
				break
			}
			position += next + 1
		}
		return best, found && len(best[4])*2 >= len(longText)
	}
	first, firstOK := findSeed((len(longText) + 3) / 4)
	second, secondOK := findSeed((len(longText) + 1) / 2)
	if !firstOK && !secondOK {
		return [5]string{}, false
	}
	match := second
	if !secondOK || firstOK && len(first[4]) > len(second[4]) {
		match = first
	}
	if len(left) > len(right) {
		return match, true
	}
	return [5]string{match[2], match[3], match[0], match[1], match[4]}, true
}

func textDiffBisect(left, right string) []textDiffPart {
	n, m := len(left), len(right)
	maximum := (n + m + 1) / 2
	offset, length := maximum, 2*maximum
	forward, reverse := make([]int, length), make([]int, length)
	for index := range forward {
		forward[index], reverse[index] = -1, -1
	}
	forward[offset+1], reverse[offset+1] = 0, 0
	delta := n - m
	front := delta%2 != 0
	firstStart, firstEnd, secondStart, secondEnd := 0, 0, 0, 0
	for distance := 0; distance < maximum; distance++ {
		for diagonal := -distance + firstStart; diagonal <= distance-firstEnd; diagonal += 2 {
			position := offset + diagonal
			x := 0
			if diagonal == -distance || diagonal != distance && forward[position-1] < forward[position+1] {
				x = forward[position+1]
			} else {
				x = forward[position-1] + 1
			}
			y := x - diagonal
			for x < n && y < m && left[x] == right[y] {
				x++
				y++
			}
			forward[position] = x
			if x > n {
				firstEnd += 2
			} else if y > m {
				firstStart += 2
			} else if front {
				reversePosition := offset + delta - diagonal
				if reversePosition >= 0 && reversePosition < length && reverse[reversePosition] != -1 && x >= n-reverse[reversePosition] {
					return append(textDiffMain(left[:x], right[:y], false), textDiffMain(left[x:], right[y:], false)...)
				}
			}
		}
		for diagonal := -distance + secondStart; diagonal <= distance-secondEnd; diagonal += 2 {
			position := offset + diagonal
			x := 0
			if diagonal == -distance || diagonal != distance && reverse[position-1] < reverse[position+1] {
				x = reverse[position+1]
			} else {
				x = reverse[position-1] + 1
			}
			y := x - diagonal
			for x < n && y < m && left[n-x-1] == right[m-y-1] {
				x++
				y++
			}
			reverse[position] = x
			if x > n {
				secondEnd += 2
			} else if y > m {
				secondStart += 2
			} else if !front {
				forwardPosition := offset + delta - diagonal
				if forwardPosition >= 0 && forwardPosition < length && forward[forwardPosition] != -1 {
					splitX := forward[forwardPosition]
					splitY := offset + splitX - forwardPosition
					if splitX >= n-x {
						return append(textDiffMain(left[:splitX], right[:splitY], false), textDiffMain(left[splitX:], right[splitY:], false)...)
					}
				}
			}
		}
	}
	return []textDiffPart{{textDiffDelete, left}, {textDiffInsert, right}}
}

func textDiffCompute(left, right string) []textDiffPart {
	if left == "" {
		return []textDiffPart{{textDiffInsert, right}}
	}
	if right == "" {
		return []textDiffPart{{textDiffDelete, left}}
	}
	longText, shortText := left, right
	if len(right) >= len(left) {
		longText, shortText = right, left
	}
	if position := strings.Index(longText, shortText); position >= 0 {
		kind := textDiffInsert
		if len(left) > len(right) {
			kind = textDiffDelete
		}
		return []textDiffPart{{kind, longText[:position]}, {textDiffEqual, shortText},
			{kind, longText[position+len(shortText):]}}
	}
	if len(shortText) == 1 {
		return []textDiffPart{{textDiffDelete, left}, {textDiffInsert, right}}
	}
	if match, ok := textDiffHalfMatch(left, right); ok {
		parts := textDiffMain(match[0], match[2], false)
		parts = append(parts, textDiffPart{textDiffEqual, match[4]})
		return append(parts, textDiffMain(match[1], match[3], false)...)
	}
	return textDiffBisect(left, right)
}

func textDiffSplice(parts []textDiffPart, start, count int, replacement ...textDiffPart) []textDiffPart {
	result := make([]textDiffPart, 0, len(parts)-count+len(replacement))
	result = append(result, parts[:start]...)
	result = append(result, replacement...)
	return append(result, parts[start+count:]...)
}

func textDiffCleanupMerge(parts []textDiffPart) []textDiffPart {
	parts = append(parts, textDiffPart{textDiffEqual, ""})
	pointer, inserts, deletes := 0, 0, 0
	inserted, removed := "", ""
	for pointer < len(parts) {
		if pointer < len(parts)-1 && parts[pointer].text == "" {
			parts = textDiffSplice(parts, pointer, 1)
			continue
		}
		switch parts[pointer].op {
		case textDiffInsert:
			inserts++
			inserted += parts[pointer].text
			pointer++
		case textDiffDelete:
			deletes++
			removed += parts[pointer].text
			pointer++
		case textDiffEqual:
			previous := pointer - inserts - deletes - 1
			if removed != "" || inserted != "" {
				if removed != "" && inserted != "" {
					prefix := textCommonPrefix(inserted, removed)
					if prefix > 0 {
						if previous >= 0 {
							parts[previous].text += inserted[:prefix]
						} else {
							parts = textDiffSplice(parts, 0, 0, textDiffPart{textDiffEqual, inserted[:prefix]})
							pointer++
						}
						inserted, removed = inserted[prefix:], removed[prefix:]
					}
					suffix := textCommonSuffix(inserted, removed)
					if suffix > 0 {
						parts[pointer].text = inserted[len(inserted)-suffix:] + parts[pointer].text
						inserted, removed = inserted[:len(inserted)-suffix], removed[:len(removed)-suffix]
					}
				}
				replacement := make([]textDiffPart, 0, 2)
				if removed != "" {
					replacement = append(replacement, textDiffPart{textDiffDelete, removed})
				}
				if inserted != "" {
					replacement = append(replacement, textDiffPart{textDiffInsert, inserted})
				}
				count := inserts + deletes
				parts = textDiffSplice(parts, pointer-count, count, replacement...)
				pointer = pointer - count + len(replacement)
			}
			if pointer > 0 && parts[pointer-1].op == textDiffEqual {
				parts[pointer-1].text += parts[pointer].text
				parts = textDiffSplice(parts, pointer, 1)
			} else {
				pointer++
			}
			inserts, deletes = 0, 0
			inserted, removed = "", ""
		}
	}
	if len(parts) > 0 && parts[len(parts)-1].text == "" {
		parts = parts[:len(parts)-1]
	}

	changed := false
	for pointer = 1; pointer < len(parts)-1; pointer++ {
		if parts[pointer-1].op != textDiffEqual || parts[pointer+1].op != textDiffEqual {
			continue
		}
		before, edit, after := parts[pointer-1].text, parts[pointer].text, parts[pointer+1].text
		if strings.HasSuffix(edit, before) {
			parts[pointer].text = before + edit[:len(edit)-len(before)]
			parts[pointer+1].text = before + after
			parts = textDiffSplice(parts, pointer-1, 1)
			changed = true
		} else if strings.HasPrefix(edit, after) {
			parts[pointer-1].text += after
			parts[pointer].text = edit[len(after):] + after
			parts = textDiffSplice(parts, pointer+1, 1)
			changed = true
		}
	}
	if changed {
		return textDiffCleanupMerge(parts)
	}
	return parts
}

func textDiffSemanticScore(left, right string) int {
	if left == "" || right == "" {
		return 6
	}
	first, second := left[len(left)-1], right[0]
	nonFirst := first < '0' || first > '9' && first < 'A' || first > 'Z' && first < 'a' || first > 'z'
	nonSecond := second < '0' || second > '9' && second < 'A' || second > 'Z' && second < 'a' || second > 'z'
	whiteFirst := nonFirst && (first == ' ' || first == '\t' || first == '\n' || first == '\r')
	whiteSecond := nonSecond && (second == ' ' || second == '\t' || second == '\n' || second == '\r')
	lineFirst := whiteFirst && (first == '\n' || first == '\r')
	lineSecond := whiteSecond && (second == '\n' || second == '\r')
	if lineFirst && (strings.HasSuffix(left, "\n\n") || strings.HasSuffix(left, "\n\r\n")) ||
		lineSecond && (strings.HasPrefix(right, "\n\n") || strings.HasPrefix(right, "\r\n\r\n")) {
		return 5
	}
	if lineFirst || lineSecond {
		return 4
	}
	if nonFirst && !whiteFirst && whiteSecond {
		return 3
	}
	if whiteFirst || whiteSecond {
		return 2
	}
	if nonFirst || nonSecond {
		return 1
	}
	return 0
}

func textDiffCleanupLossless(parts []textDiffPart) []textDiffPart {
	for pointer := 1; pointer < len(parts)-1; pointer++ {
		if parts[pointer-1].op != textDiffEqual || parts[pointer+1].op != textDiffEqual {
			continue
		}
		left, edit, right := parts[pointer-1].text, parts[pointer].text, parts[pointer+1].text
		suffix := textCommonSuffix(left, edit)
		if suffix > 0 {
			common := edit[len(edit)-suffix:]
			left, edit, right = left[:len(left)-suffix], common+edit[:len(edit)-suffix], common+right
		}
		bestLeft, bestEdit, bestRight := left, edit, right
		bestScore := textDiffSemanticScore(left, edit) + textDiffSemanticScore(edit, right)
		for edit != "" && right != "" && edit[0] == right[0] {
			left, edit, right = left+edit[:1], edit[1:]+right[:1], right[1:]
			score := textDiffSemanticScore(left, edit) + textDiffSemanticScore(edit, right)
			if score >= bestScore {
				bestLeft, bestEdit, bestRight, bestScore = left, edit, right, score
			}
		}
		if parts[pointer-1].text != bestLeft {
			if bestLeft != "" {
				parts[pointer-1].text = bestLeft
			} else {
				parts = textDiffSplice(parts, pointer-1, 1)
				pointer--
			}
			parts[pointer].text = bestEdit
			if bestRight != "" {
				parts[pointer+1].text = bestRight
			} else {
				parts = textDiffSplice(parts, pointer+1, 1)
				pointer--
			}
		}
	}
	return parts
}

func textDiffCleanupSemantic(parts []textDiffPart) []textDiffPart {
	equalities := make([]int, 0)
	last := ""
	beforeInserts, beforeDeletes, afterInserts, afterDeletes := 0, 0, 0, 0
	changed := false
	for pointer := 0; pointer < len(parts); pointer++ {
		part := parts[pointer]
		if part.op == textDiffEqual {
			equalities = append(equalities, pointer)
			beforeInserts, beforeDeletes = afterInserts, afterDeletes
			afterInserts, afterDeletes = 0, 0
			last = part.text
		} else {
			if part.op == textDiffInsert {
				afterInserts += len(part.text)
			} else {
				afterDeletes += len(part.text)
			}
			maxBefore, maxAfter := beforeInserts, afterInserts
			if beforeDeletes > maxBefore {
				maxBefore = beforeDeletes
			}
			if afterDeletes > maxAfter {
				maxAfter = afterDeletes
			}
			if last != "" && len(last) <= maxBefore && len(last) <= maxAfter {
				at := equalities[len(equalities)-1]
				parts = textDiffSplice(parts, at, 0, textDiffPart{textDiffDelete, last})
				parts[at+1].op = textDiffInsert
				equalities = equalities[:len(equalities)-1]
				if len(equalities) > 0 {
					equalities = equalities[:len(equalities)-1]
				}
				pointer = -1
				if len(equalities) > 0 {
					pointer = equalities[len(equalities)-1]
				}
				beforeInserts, beforeDeletes, afterInserts, afterDeletes = 0, 0, 0, 0
				last = ""
				changed = true
			}
		}
	}
	if changed {
		parts = textDiffCleanupMerge(parts)
	}
	parts = textDiffCleanupLossless(parts)
	for pointer := 1; pointer < len(parts); pointer++ {
		if parts[pointer-1].op != textDiffDelete || parts[pointer].op != textDiffInsert {
			continue
		}
		removed, inserted := parts[pointer-1].text, parts[pointer].text
		forward, reverse := textCommonOverlap(removed, inserted), textCommonOverlap(inserted, removed)
		if forward >= reverse && (forward*2 >= len(removed) || forward*2 >= len(inserted)) {
			parts = textDiffSplice(parts, pointer, 0, textDiffPart{textDiffEqual, inserted[:forward]})
			parts[pointer-1].text = removed[:len(removed)-forward]
			parts[pointer+1].text = inserted[forward:]
			pointer++
		} else if reverse > forward && (reverse*2 >= len(removed) || reverse*2 >= len(inserted)) {
			parts = textDiffSplice(parts, pointer, 0, textDiffPart{textDiffEqual, removed[:reverse]})
			parts[pointer-1] = textDiffPart{textDiffInsert, inserted[:len(inserted)-reverse]}
			parts[pointer+1] = textDiffPart{textDiffDelete, removed[reverse:]}
			pointer++
		}
		pointer++
	}
	return parts
}

func textDiffMain(left, right string, cleanup bool) []textDiffPart {
	if left == right {
		if left != "" {
			return []textDiffPart{{textDiffEqual, left}}
		}
		return nil
	}
	prefix := textCommonPrefix(left, right)
	leading := left[:prefix]
	left, right = left[prefix:], right[prefix:]
	suffix := textCommonSuffix(left, right)
	trailing := ""
	if suffix > 0 {
		trailing = left[len(left)-suffix:]
		left, right = left[:len(left)-suffix], right[:len(right)-suffix]
	}
	parts := textDiffCompute(left, right)
	if leading != "" {
		parts = textDiffSplice(parts, 0, 0, textDiffPart{textDiffEqual, leading})
	}
	if trailing != "" {
		parts = append(parts, textDiffPart{textDiffEqual, trailing})
	}
	parts = textDiffCleanupMerge(parts)
	if cleanup {
		parts = textDiffCleanupSemantic(parts)
	}
	return parts
}

func runFastDiff() bool {
	const rounds = 256
	const modulus = 1000000007
	data, err := os.ReadFile("test/benchmark/text/fast_diff_pairs.json")
	if err != nil {
		fmt.Println("fast_diff fixture:", err)
		return false
	}
	var pairs [][2]string
	if err := json.Unmarshal(data, &pairs); err != nil {
		fmt.Println("fast_diff fixture:", err)
		return false
	}
	checksum := 0
	started := time.Now()
	for round := 0; round < rounds; round++ {
		for _, pair := range pairs {
			parts := textDiffMain(pair[0], pair[1], true)
			checksum = (checksum + len(parts)*17) % modulus
			for _, part := range parts {
				checksum = (checksum + part.op*31 + len(part.text)) % modulus
			}
		}
	}
	ms := float64(time.Since(started).Nanoseconds()) / 1e6
	fmt.Printf("CHECKSUM:%d\n__TIMING__:%.6f\n", checksum, ms)
	return checksum == 390912
}
