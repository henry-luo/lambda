#lang racket/base
(require "reference-import.rkt"
         "layout-dispatch.rkt"
         "layout-common.rkt"
         "compare-layouts.rkt"
         racket/pretty)

;; Load the grid_fit_content_points_argument test
(define data-dir "/Users/henryluo/Projects/Lambda/test/layout/data/flex/")
(define ref-dir "/Users/henryluo/Projects/Lambda/test/layout/reference/")
(define test-name "measure_child_constraint")

(define html-path (string-append data-dir test-name ".html"))
(define ref-path (string-append ref-dir test-name ".json"))
(define tc (reference-file->test-case html-path ref-path))
(define box-tree (reference-test-case-box-tree tc))
(define viewport-w (car (reference-test-case-viewport tc)))
(define viewport-h (cdr (reference-test-case-viewport tc)))

;; first just test the text node min-content measurement
(printf "=== Testing text min-content directly ===\n")
(define text-box 
  (cadddr (cadr (cadddr box-tree))))  ;; extract the text child

(printf "Text box: ~a\n" (list (car text-box) (cadr text-box) (caddr text-box) "..." (list-ref text-box 4)))

;; measure text at min-content
(define min-view (layout `(block "test-block" (style) (,text-box)) '(avail av-min-content (definite 800))))
(printf "Text min-content view: ~a\n" min-view)

;; measure text at max-content
(define max-view (layout `(block "test-block" (style) (,text-box)) '(avail av-max-content (definite 800))))
(printf "Text max-content view: ~a\n" max-view)

(define html-path (string-append data-dir test-name ".html"))
(define ref-path (string-append ref-dir test-name ".json"))
(define tc (reference-file->test-case html-path ref-path))
(define box-tree (reference-test-case-box-tree tc))
(define viewport-w (car (reference-test-case-viewport tc)))
(define viewport-h (cdr (reference-test-case-viewport tc)))

(printf "=== Box Tree ===\n")
(pretty-print box-tree)

(printf "\n=== Running Layout (viewport ~a x ~a) ===\n" viewport-w viewport-h)
(define view-tree (layout-document box-tree viewport-w viewport-h))

(printf "\n=== View Tree ===\n")
(pretty-print view-tree)
