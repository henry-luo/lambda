#lang racket/base
(require "reference-import.rkt"
         "layout-dispatch.rkt"
         "css-layout-lang.rkt"
         "layout-common.rkt"
         racket/pretty)

(define tc (reference-file->test-case
            "../layout/data/baseline/chrome_issue_325928327.html"
            "../layout/reference/chrome_issue_325928327.json"))
(define bt (reference-test-case-box-tree tc))
(define vp (reference-test-case-viewport tc))

(displayln "=== Box Tree ===")
(pretty-print bt)

(displayln "\n=== Viewport ===")
(displayln vp)

(displayln "\n=== Layout Result ===")
(define view (layout-document bt (car vp) (cdr vp)))
(pretty-print view)
