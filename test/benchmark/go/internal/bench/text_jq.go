package bench

import (
	"encoding/json"
	"fmt"
	"os"
	"time"

	"github.com/itchyny/gojq"
)

// jqWorkloads maps each jq_* text row to its input and expected checksum.
// Every column runs the same filter text, test/benchmark/text/jq/<name>.jq.
var jqWorkloads = map[string]struct {
	inputKind string // "null", "json" or "raw"
	inputPath string
	expected  int
}{
	"jq_mix":     {"null", "", 98172625},
	"jq_records": {"json", "test/benchmark/text/jq/orders.json", 878885883},
	"jq_bf":      {"raw", "test/benchmark/text/jq/fib.bf", 478890292},
	"jq_tree":    {"null", "", 313746104},
}

func runJq(name string) bool {
	workload, found := jqWorkloads[name]
	if !found {
		return false
	}
	source, err := os.ReadFile("test/benchmark/text/jq/" + name[3:] + ".jq")
	if err != nil {
		fmt.Println(name+":", err)
		return false
	}
	query, err := gojq.Parse(string(source))
	if err != nil {
		fmt.Println(name+":", err)
		return false
	}
	code, err := gojq.Compile(query)
	if err != nil {
		fmt.Println(name+":", err)
		return false
	}
	var input any
	switch workload.inputKind {
	case "json":
		data, err := os.ReadFile(workload.inputPath)
		if err != nil || json.Unmarshal(data, &input) != nil {
			fmt.Println(name+": cannot load", workload.inputPath)
			return false
		}
	case "raw":
		data, err := os.ReadFile(workload.inputPath)
		if err != nil {
			fmt.Println(name+":", err)
			return false
		}
		input = string(data)
	}
	// parsing the filter and loading the input stay outside the timed region
	started := time.Now()
	var outputs []any
	iter := code.Run(input)
	for {
		value, more := iter.Next()
		if !more {
			break
		}
		if err, isErr := value.(error); isErr {
			fmt.Println(name+":", err)
			return false
		}
		outputs = append(outputs, value)
	}
	elapsedMs := float64(time.Since(started).Nanoseconds()) / 1e6
	checksum := -1
	if len(outputs) == 1 {
		switch value := outputs[0].(type) {
		case int:
			checksum = value
		case float64:
			checksum = int(value)
		}
	}
	fmt.Printf("%s: CHECKSUM:%d\n", name, checksum)
	fmt.Printf("__TIMING__:%.6f\n", elapsedMs)
	return checksum == workload.expected
}
