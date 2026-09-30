package bench

import (
	"fmt"
	"reflect"
	"time"
)

type textDiffField struct {
	key   string
	value any
}

type textDiffObject []textDiffField
type textDiffArray []any

type textRichValue struct {
	kind, value string
}

type textDifference struct {
	kind string
	path []any
	old  any
	new  any
}

func textDiffKeys(value any) []any {
	switch object := value.(type) {
	case textDiffObject:
		keys := make([]any, len(object))
		for index, field := range object {
			keys[index] = field.key
		}
		return keys
	case textDiffArray:
		keys := make([]any, len(object))
		for index := range object {
			keys[index] = index
		}
		return keys
	default:
		return nil
	}
}

func textDiffGet(value, key any) (any, bool) {
	switch object := value.(type) {
	case textDiffObject:
		name, ok := key.(string)
		if !ok {
			return nil, false
		}
		for _, field := range object {
			if field.key == name {
				return field.value, true
			}
		}
	case textDiffArray:
		index, ok := key.(int)
		if ok && index < len(object) {
			return object[index], true
		}
	}
	return nil, false
}

func textDiffObjectKind(value any) int {
	switch value.(type) {
	case textDiffObject:
		return 1
	case textDiffArray:
		return 2
	case textRichValue:
		return 3
	default:
		return 0
	}
}

func textMicroDiff(old, new any) []textDifference {
	differences := make([]textDifference, 0)
	for _, key := range textDiffKeys(old) {
		oldValue, _ := textDiffGet(old, key)
		newValue, found := textDiffGet(new, key)
		if !found {
			differences = append(differences, textDifference{kind: "REMOVE", path: []any{key}, old: oldValue})
			continue
		}
		oldKind, newKind := textDiffObjectKind(oldValue), textDiffObjectKind(newValue)
		if oldKind != 0 && oldKind == newKind && oldKind != 3 {
			// The checked-in snapshots are acyclic, so the JS cycle guard never fires.
			for _, nested := range textMicroDiff(oldValue, newValue) {
				nested.path = append([]any{key}, nested.path...)
				differences = append(differences, nested)
			}
		} else if !reflect.DeepEqual(oldValue, newValue) {
			differences = append(differences, textDifference{
				kind: "CHANGE", path: []any{key}, old: oldValue, new: newValue,
			})
		}
	}
	for _, key := range textDiffKeys(new) {
		if _, found := textDiffGet(old, key); !found {
			newValue, _ := textDiffGet(new, key)
			differences = append(differences, textDifference{kind: "CREATE", path: []any{key}, new: newValue})
		}
	}
	return differences
}

func textSnapshot(version bool) textDiffObject {
	title, theme, pattern, timestamp, value := "Text benchmark", "light", "source|text", "1700000000000", 41
	codeLines, headingLevel, tags := 12, 1, textDiffArray{"text", "benchmark"}
	items := textDiffArray{"diff", "snapshot"}
	if version {
		title, theme, pattern, timestamp, value = "Text benchmark — revised", "dark", "source|text|diff", "1700000001000", 42
		codeLines, headingLevel = 18, 2
		tags = textDiffArray{"text", "benchmark", "updated"}
		items = textDiffArray{"diff", "snapshot", "hyphen"}
	}
	return textDiffObject{
		{"document", textDiffObject{
			{"title", title},
			{"sections", textDiffArray{
				textDiffObject{{"id", "intro"}, {"blocks", textDiffArray{
					textDiffObject{{"type", "paragraph"}, {"text", "A short paragraph of source text."}},
					textDiffObject{{"type", "code"}, {"language", "js"}, {"lines", codeLines}},
				}}},
				textDiffObject{{"id", "body"}, {"blocks", textDiffArray{
					textDiffObject{{"type", "heading"}, {"level", headingLevel}, {"text", "Algorithms"}},
					textDiffObject{{"type", "list"}, {"items", items}},
				}}},
			}},
		}},
		{"options", textDiffObject{
			{"theme", theme},
			{"flags", textDiffObject{{"trackChanges", version}, {"preserveWhitespace", true}}},
		}},
		{"tags", tags},
		{"updated", textRichValue{"Date", timestamp}},
		{"pattern", textRichValue{"RegExp", pattern}},
		{"value", value},
	}
}

func runMicrodiff() bool {
	const rounds = 512
	const modulus = 1000000007
	pairs := [4][2]textDiffObject{}
	for index := range pairs {
		pairs[index][0] = textSnapshot(index%2 == 0)
		pairs[index][1] = textSnapshot(index%2 != 0)
	}
	checksum := 0
	started := time.Now()
	for round := 0; round < rounds; round++ {
		for _, pair := range pairs {
			differences := textMicroDiff(pair[0], pair[1])
			checksum = (checksum + len(differences)*19) % modulus
			for _, difference := range differences {
				checksum = (checksum + len(difference.kind)*23 + len(difference.path)) % modulus
			}
		}
	}
	elapsedMs := float64(time.Since(started).Nanoseconds()) / 1e6
	ok := checksum == 3278848
	fmt.Printf("CHECKSUM:%d\n", checksum)
	fmt.Printf("__TIMING__:%.6f\n", elapsedMs)
	return ok
}
