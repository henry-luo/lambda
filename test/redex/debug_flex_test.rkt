#lang racket/base
(require "reference-import.rkt"
         "layout-dispatch.rkt"
         "layout-common.rkt"
         racket/pretty)

(define html-path "/Users/henryluo/Projects/Lambda/test/layout/data/flex/measure_child_constraint.html")
(define ref-path "/Users/henryluo/Projects/Lambda/test/layout/reference/measure_child_constraint.json")
(define tc (reference-file->test-case html-path ref-path))
(define box-tree (reference-test-case-box-tree tc))
(define viewport-w (car (reference-test-case-viewport tc)))
(define viewport-h (cdr (reference-test-case-viewport tc)))

(printf "=== Box Tree ===\n")
(pretty-print box-tree)

;; Extract the inner flex container
(define flex-child (car (cadddr box-tree)))

;; Test: layout the inner flex at av-min-content
(printf "\n=== Inner flex at av-min-content ===\n")
(define min-view (layout flex-child '(avail av-min-content (definite 800))))
(printf "min-content: ~a\n\n" min-view)

;; Test: layout the inner flex with definite width 100
(printf "=== Inner flex at (definite 100) ===\n")
(define def-view (layout flex-child '(avail (definite 100) (definite 800))))
(printf "definite-100: ~a\n\n" def-view)

(printf "=== Full layout ===\n")
(define view-tree (layout-document box-tree viewport-w viewport-h))
(pretty-print view-tree)
