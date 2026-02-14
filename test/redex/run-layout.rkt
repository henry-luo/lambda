#lang racket/base

;; run-layout.rkt — CLI entry point for Redex layout
;;
;; Usage:
;;   racket run-layout.rkt <box-tree.json> [--output layout.json] [--viewport WxH]
;;
;; Reads a box tree from JSON (exported by Radiant's --export-box-tree),
;; runs the Redex layout model, and outputs the view tree as JSON.

(require racket/cmdline
         json
         "layout-dispatch.rkt"
         "json-bridge.rkt")

(define output-file (make-parameter #f))
(define viewport-w (make-parameter 800))
(define viewport-h (make-parameter 600))

(define input-file
  (command-line
   #:program "redex-layout"
   #:once-each
   [("-o" "--output") file
    "Output layout JSON file"
    (output-file file)]
   [("-v" "--viewport") viewport
    "Viewport size as WxH (default: 800x600)"
    (define parts (regexp-split #rx"x" viewport))
    (when (>= (length parts) 2)
      (viewport-w (string->number (car parts)))
      (viewport-h (string->number (cadr parts))))]
   #:args (input)
   input))

;; read input box tree
(define box-tree (json-file->box-tree input-file))

;; run layout
(define view
  (layout-document box-tree (viewport-w) (viewport-h)))

;; output result
(define result-json (view->json view))

(cond
  [(output-file)
   (view->json-file view (output-file))
   (printf "Layout written to ~a\n" (output-file))]
  [else
   (write-json result-json)
   (newline)])
