package bench

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"strconv"
	"strings"
	"time"
)

type prettyNode map[string]any

type prettyDoc struct {
	kind, value string
	parts       []*prettyDoc
	contents    *prettyDoc
	broken      *prettyDoc
	flat        *prettyDoc
	threshold   float64
}

type prettyState struct {
	column, indent, width int
}

func prettyObject(value any) prettyNode {
	if value == nil {
		return nil
	}
	return value.(map[string]any)
}

func prettyArray(value any) []any {
	if value == nil {
		return nil
	}
	return value.([]any)
}

func prettyString(value any) string {
	if value == nil {
		return ""
	}
	return fmt.Sprint(value)
}

func prettyBool(node prettyNode, key string) bool {
	value, ok := node[key].(bool)
	return ok && value
}

func prettyText(value any) *prettyDoc {
	return &prettyDoc{kind: "text", value: prettyString(value)}
}

func prettyConcat(parts ...any) *prettyDoc {
	flat := make([]*prettyDoc, 0, len(parts))
	for _, part := range parts {
		switch item := part.(type) {
		case *prettyDoc:
			flat = append(flat, item)
		case []*prettyDoc:
			flat = append(flat, item...)
		default:
			panic("unexpected pretty document part")
		}
	}
	return &prettyDoc{kind: "concat", parts: flat}
}

func prettyJoin(separator *prettyDoc, parts []*prettyDoc) *prettyDoc {
	joined := make([]*prettyDoc, 0, len(parts)*2)
	for index, part := range parts {
		if index > 0 {
			joined = append(joined, separator)
		}
		joined = append(joined, part)
	}
	return prettyConcat(joined)
}

func prettyIndent(contents *prettyDoc) *prettyDoc {
	return &prettyDoc{kind: "indent", contents: contents}
}

func prettyGroup(contents *prettyDoc, threshold ...float64) *prettyDoc {
	limit := math.Inf(1)
	if len(threshold) > 0 {
		limit = threshold[0]
	}
	return &prettyDoc{kind: "group", contents: contents, threshold: limit}
}

func prettyIfBreak(broken *prettyDoc, flat ...*prettyDoc) *prettyDoc {
	flatDoc := prettyText("")
	if len(flat) > 0 {
		flatDoc = flat[0]
	}
	return &prettyDoc{kind: "if-break", broken: broken, flat: flatDoc}
}

var prettyLine = &prettyDoc{kind: "line"}
var prettySoftline = &prettyDoc{kind: "softline"}
var prettyHardline = &prettyDoc{kind: "hardline"}

func prettyFlatLength(doc *prettyDoc) float64 {
	switch doc.kind {
	case "text":
		return float64(len(doc.value))
	case "concat":
		length := 0.0
		for _, part := range doc.parts {
			length += prettyFlatLength(part)
		}
		return length
	case "indent", "group":
		return prettyFlatLength(doc.contents)
	case "if-break":
		return prettyFlatLength(doc.flat)
	case "line":
		return 1
	case "softline":
		return 0
	default:
		return math.Inf(1)
	}
}

func prettyRender(doc *prettyDoc, state *prettyState, mode string) string {
	switch doc.kind {
	case "text":
		state.column += len(doc.value)
		return doc.value
	case "concat":
		var result strings.Builder
		for _, part := range doc.parts {
			result.WriteString(prettyRender(part, state, mode))
		}
		return result.String()
	case "indent":
		state.indent++
		value := prettyRender(doc.contents, state, mode)
		state.indent--
		return value
	case "group":
		flat := mode == "flat" || (doc.threshold >= prettyFlatLength(doc.contents) &&
			prettyFlatLength(doc.contents) <= float64(state.width-state.column))
		if flat {
			return prettyRender(doc.contents, state, "flat")
		}
		return prettyRender(doc.contents, state, "break")
	case "if-break":
		if mode == "flat" {
			return prettyRender(doc.flat, state, mode)
		}
		return prettyRender(doc.broken, state, mode)
	case "line":
		if mode == "flat" {
			state.column++
			return " "
		}
	case "softline":
		if mode == "flat" {
			return ""
		}
	case "hardline":
	default:
		panic("unknown pretty document node: " + doc.kind)
	}
	state.column = state.indent * 2
	return "\n" + strings.Repeat(" ", state.column)
}

func prettyRenderDoc(doc *prettyDoc) string {
	state := &prettyState{width: 80}
	return prettyRender(doc, state, "break")
}

func prettyDocs(values []any, printer func(prettyNode) *prettyDoc) []*prettyDoc {
	docs := make([]*prettyDoc, len(values))
	for index, value := range values {
		docs[index] = printer(prettyObject(value))
	}
	return docs
}

func prettyLiteral(node prettyNode) *prettyDoc {
	switch node["type"] {
	case "StringLiteral":
		return prettyText(strconv.Quote(prettyString(node["value"])))
	case "NumericLiteral":
		return prettyText(node["value"])
	case "BooleanLiteral":
		if prettyBool(node, "value") {
			return prettyText("true")
		}
		return prettyText("false")
	case "NullLiteral":
		return prettyText("null")
	case "RegExpLiteral":
		return prettyText("/" + prettyString(node["pattern"]) + "/" + prettyString(node["flags"]))
	default:
		panic("unknown pretty literal")
	}
}

func prettyNodeKey(node prettyNode) *prettyDoc {
	if prettyBool(node, "computed") {
		return prettyConcat(prettyText("["), prettyPrintNode(prettyObject(node["key"])), prettyText("]"))
	}
	return prettyPrintNode(prettyObject(node["key"]))
}

func prettyParameterList(params []any) *prettyDoc {
	return prettyGroup(prettyConcat(
		prettyText("("),
		prettyIndent(prettyConcat(prettySoftline,
			prettyJoin(prettyConcat(prettyText(","), prettyLine), prettyDocs(params, prettyPrintNode)))),
		prettyIfBreak(prettyText(",")), prettySoftline, prettyText(")"),
	))
}

func prettyArgumentList(args []any) *prettyDoc {
	if len(args) == 0 {
		return prettyText("()")
	}
	for _, arg := range args {
		if prettyObject(arg)["type"] == "ObjectExpression" {
			return prettyConcat(prettyText("("), prettyJoin(prettyText(", "), prettyDocs(args, prettyPrintNode)), prettyText(")"))
		}
	}
	return prettyGroup(prettyConcat(
		prettyText("("),
		prettyIndent(prettyConcat(prettySoftline,
			prettyJoin(prettyConcat(prettyText(","), prettyLine), prettyDocs(args, prettyPrintNode)))),
		prettyIfBreak(prettyText(",")), prettySoftline, prettyText(")"),
	))
}

func prettyArrayDoc(elements []any) *prettyDoc {
	if len(elements) == 0 {
		return prettyText("[]")
	}
	return prettyGroup(prettyConcat(
		prettyText("["),
		prettyIndent(prettyConcat(prettySoftline,
			prettyJoin(prettyConcat(prettyText(","), prettyLine), prettyDocs(elements, prettyPrintNode)))),
		prettyIfBreak(prettyText(",")), prettySoftline, prettyText("]"),
	))
}

func prettyObjectDoc(properties []any) *prettyDoc {
	if len(properties) == 0 {
		return prettyText("{}")
	}
	return prettyGroup(prettyConcat(
		prettyText("{"),
		prettyIndent(prettyConcat(prettyLine,
			prettyJoin(prettyConcat(prettyText(","), prettyLine), prettyDocs(properties, prettyPrintNode)),
			prettyIfBreak(prettyText(",")))),
		prettyLine, prettyText("}"),
	))
}

func prettyBlockDoc(body []any) *prettyDoc {
	if len(body) == 0 {
		return prettyText("{}")
	}
	return prettyConcat(prettyText("{"),
		prettyIndent(prettyConcat(prettyHardline, prettyJoin(prettyHardline, prettyDocs(body, prettyPrintStatement)))),
		prettyHardline, prettyText("}"))
}

func prettyVariableDoc(node prettyNode, terminator bool) *prettyDoc {
	declaration := prettyConcat(prettyText(prettyString(node["kind"])+" "),
		prettyJoin(prettyConcat(prettyText(","), prettyLine), prettyDocs(prettyArray(node["declarations"]), prettyPrintNode)))
	if terminator {
		return prettyConcat(declaration, prettyText(";"))
	}
	return declaration
}

func prettyPrintProperty(node prettyNode) *prettyDoc {
	if node["type"] == "SpreadElement" {
		return prettyConcat(prettyText("..."), prettyPrintNode(prettyObject(node["argument"])))
	}
	key := prettyNodeKey(node)
	if prettyBool(node, "shorthand") {
		return key
	}
	return prettyConcat(key, prettyText(": "), prettyPrintExpression(prettyObject(node["value"]), 0))
}

func prettyExpressionPrecedence(node prettyNode) int {
	if node == nil {
		return 100
	}
	switch node["type"] {
	case "AssignmentExpression", "ArrowFunctionExpression":
		return 1
	case "LogicalExpression":
		if node["operator"] == "&&" {
			return 3
		}
		return 2
	case "BinaryExpression":
		operator := prettyString(node["operator"])
		switch operator {
		case "*", "/", "%":
			return 12
		case "+", "-":
			return 11
		case "<", "<=", ">", ">=", "in", "instanceof":
			return 9
		case "==", "!=", "===", "!==":
			return 8
		default:
			return 7
		}
	default:
		return 20
	}
}

func prettyPrintExpression(node prettyNode, parentPrecedence int) *prettyDoc {
	doc := prettyPrintNode(node)
	if prettyExpressionPrecedence(node) < parentPrecedence {
		return prettyConcat(prettyText("("), doc, prettyText(")"))
	}
	return doc
}

func prettyFlattenAdditiveChain(node prettyNode, operands *[]prettyNode) {
	if node["type"] == "BinaryExpression" && node["operator"] == "+" {
		prettyFlattenAdditiveChain(prettyObject(node["left"]), operands)
		*operands = append(*operands, prettyObject(node["right"]))
	} else {
		*operands = append(*operands, node)
	}
}

func prettyAdditiveChainDoc(node prettyNode) *prettyDoc {
	operands := make([]prettyNode, 0)
	prettyFlattenAdditiveChain(node, &operands)
	tail := make([]*prettyDoc, 0)
	for _, operand := range operands[1:] {
		tail = append(tail, prettyText(" +"), prettyLine, prettyPrintExpression(operand, 12))
	}
	return prettyGroup(prettyConcat(prettyPrintExpression(operands[0], 11), prettyIndent(prettyConcat(tail))), 60)
}

func prettyPrintNode(node prettyNode) *prettyDoc {
	if node == nil {
		return prettyText("")
	}
	switch node["type"] {
	case "Identifier":
		return prettyText(node["name"])
	case "ThisExpression":
		return prettyText("this")
	case "StringLiteral", "NumericLiteral", "BooleanLiteral", "NullLiteral", "RegExpLiteral":
		return prettyLiteral(node)
	case "ArrayExpression", "ArrayPattern":
		return prettyArrayDoc(prettyArray(node["elements"]))
	case "ObjectExpression":
		return prettyObjectDoc(prettyArray(node["properties"]))
	case "ObjectProperty":
		return prettyPrintProperty(node)
	case "VariableDeclarator":
		if node["init"] != nil {
			return prettyConcat(prettyPrintNode(prettyObject(node["id"])), prettyText(" = "),
				prettyPrintExpression(prettyObject(node["init"]), 0))
		}
		return prettyPrintNode(prettyObject(node["id"]))
	case "SpreadElement":
		return prettyConcat(prettyText("..."), prettyPrintNode(prettyObject(node["argument"])))
	case "AssignmentPattern":
		return prettyConcat(prettyPrintNode(prettyObject(node["left"])), prettyText(" = "),
			prettyPrintNode(prettyObject(node["right"])))
	case "MemberExpression":
		if prettyBool(node, "computed") {
			return prettyConcat(prettyPrintExpression(prettyObject(node["object"]), 20), prettyText("["),
				prettyPrintExpression(prettyObject(node["property"]), 0), prettyText("]"))
		}
		return prettyConcat(prettyPrintExpression(prettyObject(node["object"]), 20), prettyText("."),
			prettyPrintNode(prettyObject(node["property"])))
	case "CallExpression":
		args := prettyArray(node["arguments"])
		if len(args) == 1 && prettyObject(args[0])["type"] == "ArrowFunctionExpression" {
			arrow := prettyObject(args[0])
			return prettyGroup(prettyConcat(
				prettyPrintExpression(prettyObject(node["callee"]), 20), prettyText("("),
				prettyParameterList(prettyArray(arrow["params"])), prettyText(" =>"),
				prettyIndent(prettyConcat(prettyLine, prettyPrintExpression(prettyObject(arrow["body"]), 0))),
				prettyIfBreak(prettyText(",")), prettySoftline, prettyText(")"),
			))
		}
		return prettyConcat(prettyPrintExpression(prettyObject(node["callee"]), 20), prettyArgumentList(args))
	case "NewExpression":
		return prettyConcat(prettyText("new "), prettyPrintExpression(prettyObject(node["callee"]), 20),
			prettyArgumentList(prettyArray(node["arguments"])))
	case "BinaryExpression", "LogicalExpression":
		precedence := prettyExpressionPrecedence(node)
		left := prettyObject(node["left"])
		if node["type"] == "BinaryExpression" && node["operator"] == "+" &&
			left["type"] == "BinaryExpression" && left["operator"] == "+" {
			return prettyAdditiveChainDoc(node)
		}
		increment := 0
		if node["operator"] == "&&" || node["operator"] == "||" {
			increment = 1
		}
		return prettyGroup(prettyConcat(
			prettyPrintExpression(left, precedence), prettyText(" "+prettyString(node["operator"])),
			prettyIndent(prettyConcat(prettyLine,
				prettyPrintExpression(prettyObject(node["right"]), precedence+increment))),
		))
	case "UnaryExpression":
		if node["operator"] == "!" {
			return prettyConcat(prettyText("!"), prettyPrintExpression(prettyObject(node["argument"]), 20))
		}
		return prettyConcat(prettyText(prettyString(node["operator"])+" "),
			prettyPrintExpression(prettyObject(node["argument"]), 20))
	case "AssignmentExpression":
		return prettyConcat(prettyPrintExpression(prettyObject(node["left"]), 2),
			prettyText(" "+prettyString(node["operator"])+" "),
			prettyPrintExpression(prettyObject(node["right"]), 1))
	case "ArrowFunctionExpression":
		return prettyConcat(prettyParameterList(prettyArray(node["params"])), prettyText(" => "),
			prettyPrintExpression(prettyObject(node["body"]), 1))
	case "FunctionDeclaration":
		prefix := "function "
		if prettyBool(node, "async") {
			prefix = "async " + prefix
		}
		generator := ""
		if prettyBool(node, "generator") {
			generator = "*"
		}
		body := prettyObject(node["body"])
		return prettyConcat(prettyText(prefix), prettyText(generator), prettyPrintNode(prettyObject(node["id"])),
			prettyParameterList(prettyArray(node["params"])), prettyText(" "), prettyBlockDoc(prettyArray(body["body"])))
	case "ClassDeclaration":
		return prettyConcat(prettyText("class "), prettyPrintNode(prettyObject(node["id"])),
			prettyText(" "), prettyPrintNode(prettyObject(node["body"])))
	case "ClassBody":
		body := prettyArray(node["body"])
		if len(body) == 0 {
			return prettyText("{}")
		}
		return prettyConcat(prettyText("{"),
			prettyIndent(prettyConcat(prettyHardline, prettyJoin(prettyHardline, prettyDocs(body, prettyPrintNode)))),
			prettyHardline, prettyText("}"))
	case "ClassMethod":
		static, asynchronous, generator := "", "", ""
		if prettyBool(node, "static") {
			static = "static "
		}
		if prettyBool(node, "async") {
			asynchronous = "async "
		}
		if prettyBool(node, "generator") {
			generator = "*"
		}
		body := prettyObject(node["body"])
		return prettyConcat(prettyText(static), prettyText(asynchronous), prettyText(generator),
			prettyNodeKey(node), prettyParameterList(prettyArray(node["params"])), prettyText(" "),
			prettyBlockDoc(prettyArray(body["body"])))
	default:
		return prettyPrintStatement(node)
	}
}

func prettyPrintStatement(node prettyNode) *prettyDoc {
	if node == nil {
		return prettyText("")
	}
	switch node["type"] {
	case "VariableDeclaration":
		return prettyVariableDoc(node, true)
	case "ReturnStatement":
		argument := prettyText("")
		if node["argument"] != nil {
			argument = prettyConcat(prettyText(" "), prettyPrintNode(prettyObject(node["argument"])))
		}
		return prettyConcat(prettyText("return"), argument, prettyText(";"))
	case "ExpressionStatement":
		return prettyConcat(prettyPrintNode(prettyObject(node["expression"])), prettyText(";"))
	case "BlockStatement":
		return prettyBlockDoc(prettyArray(node["body"]))
	case "IfStatement":
		consequent := prettyObject(node["consequent"])
		alternate := prettyObject(node["alternate"])
		if consequent["type"] == "BlockStatement" {
			elseDoc := prettyText("")
			if alternate != nil {
				elseDoc = prettyConcat(prettyText(" else "), prettyPrintStatement(alternate))
			}
			return prettyConcat(prettyText("if ("), prettyPrintExpression(prettyObject(node["test"]), 0),
				prettyText(") "), prettyPrintStatement(consequent), elseDoc)
		}
		elseDoc := prettyText("")
		if alternate != nil {
			elseDoc = prettyIndent(prettyConcat(prettyLine, prettyText("else"), prettyLine,
				prettyPrintStatement(alternate)))
		}
		return prettyGroup(prettyConcat(prettyText("if ("), prettyPrintExpression(prettyObject(node["test"]), 0),
			prettyText(")"), prettyIndent(prettyConcat(prettyLine, prettyPrintStatement(consequent))), elseDoc))
	case "ForOfStatement":
		return prettyConcat(prettyText("for ("), prettyVariableDoc(prettyObject(node["left"]), false),
			prettyText(" of "), prettyPrintNode(prettyObject(node["right"])), prettyText(") "),
			prettyPrintStatement(prettyObject(node["body"])))
	case "FunctionDeclaration", "ClassDeclaration":
		return prettyPrintNode(node)
	case "ExportNamedDeclaration":
		if node["declaration"] != nil {
			return prettyConcat(prettyText("export "), prettyPrintStatement(prettyObject(node["declaration"])))
		}
		return prettyConcat(prettyText("export { "),
			prettyJoin(prettyText(", "), prettyDocs(prettyArray(node["specifiers"]), prettyPrintNode)), prettyText(" };"))
	case "ExportSpecifier":
		local, exported := prettyObject(node["local"]), prettyObject(node["exported"])
		if local["name"] == exported["name"] {
			return prettyPrintNode(local)
		}
		return prettyConcat(prettyPrintNode(local), prettyText(" as "), prettyPrintNode(exported))
	default:
		panic("unsupported pretty statement: " + prettyString(node["type"]))
	}
}

func prettyPrintProgram(program prettyNode) string {
	return prettyRenderDoc(prettyConcat(
		prettyJoin(prettyHardline, prettyDocs(prettyArray(program["body"]), prettyPrintStatement)),
		prettyHardline,
	))
}

func runPrettierAST() bool {
	data, err := os.ReadFile("test/benchmark/text/prettier_ast.json")
	if err != nil {
		return false
	}
	var ast prettyNode
	if err := json.Unmarshal(data, &ast); err != nil {
		return false
	}
	started := time.Now()
	formatted := ""
	for iteration := 0; iteration < 256; iteration++ {
		formatted = prettyPrintProgram(ast)
	}
	elapsedMs := float64(time.Since(started).Nanoseconds()) / 1e6
	checksum := 0
	for _, char := range formatted {
		checksum = (checksum*31 + int(char)) % 1000000007
	}
	fmt.Print(formatted)
	ok := checksum == 56483873
	if ok {
		fmt.Printf("prettier_ast: CHECKSUM:%d\n", checksum)
	} else {
		fmt.Printf("prettier_ast: FAIL checksum=%d\n", checksum)
	}
	fmt.Printf("__TIMING__:%.6f\n", elapsedMs)
	return ok
}
