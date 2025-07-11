package tree_sitter_lambda_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_lambda "github.com/tree-sitter/tree-sitter-lambda/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_lambda.Language())
	if language == nil {
		t.Errorf("Error loading Lambda grammar")
	}
}
