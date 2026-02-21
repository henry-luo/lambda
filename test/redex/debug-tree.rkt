#lang racket
(require "reference-import.rkt" "layout-dispatch.rkt" "layout-common.rkt")

(define tc (reference-file->test-case
            "/Users/jane/Projects/Lambda/test/layout/data/css2.1/html4/border-spacing-003.htm"
            "/Users/jane/Projects/Lambda/test/layout/reference/border-spacing-003.json"))
(define box (reference-test-case-box-tree tc))
(define view (layout-document box 1200 800))

(define (show-view v depth)
  (printf "~aid=~a x=~a y=~a w=~a h=~a\n"
          (make-string (* 2 depth) #\space)
          (view-id v) (view-x v) (view-y v) (view-width v) (view-height v))
  (for ([c (in-list (view-children v))])
    (show-view c (add1 depth))))

(show-view view 0)
