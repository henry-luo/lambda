#lang racket/base

;; test-all.rkt — Run all Redex layout tests

(printf "========================================\n")
(printf " Redex Layout — Test Suite\n")
(printf "========================================\n\n")

(require "tests/test-block.rkt")
(newline)
(require "tests/test-flex.rkt")
(newline)
(require "tests/test-grid.rkt")
(newline)
(require "tests/test-invariants.rkt")

(printf "\n========================================\n")
(printf " All tests complete.\n")
(printf "========================================\n")
