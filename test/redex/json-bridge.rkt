#lang racket/base

;; json-bridge.rkt — JSON import/export for box trees and layout results
;;
;; Provides bidirectional conversion between JSON and Redex terms.
;; Used for differential testing: Radiant exports box-tree.json,
;; Redex imports it, runs layout, and exports layout.json for comparison.

(require racket/match
         racket/string
         json
         "css-layout-lang.rkt"
         "layout-common.rkt")

(provide json->box-tree
         box-tree->json
         view->json
         json-file->box-tree
         view->json-file
         json->avail)

;; ============================================================
;; JSON → Box Tree (Import)
;; ============================================================

;; convert a JSON hash-table to a Box term
(define (json->box-tree jsobj)
  (define type (hash-ref jsobj 'type "block"))
  (define id (string->symbol (hash-ref jsobj 'id "anon")))
  (define styles (json->styles (hash-ref jsobj 'styles (hash))))
  (define children-json (hash-ref jsobj 'children '()))

  (case (string->symbol type)
    [(block)
     `(block ,id ,styles ,(map json->box-tree children-json))]
    [(inline)
     `(inline ,id ,styles ,(map json->inline-content children-json))]
    [(inline-block)
     `(inline-block ,id ,styles ,(map json->box-tree children-json))]
    [(flex)
     `(flex ,id ,styles ,(map json->box-tree children-json))]
    [(grid)
     (define grid-def (json->grid-def jsobj))
     `(grid ,id ,styles ,grid-def ,(map json->box-tree children-json))]
    [(table)
     `(table ,id ,styles ,(map json->table-row-group children-json))]
    [(text)
     (define content (hash-ref jsobj 'content ""))
     (define measured-w (hash-ref jsobj 'measuredWidth 0))
     `(text ,id ,styles ,content ,measured-w)]
    [(replaced)
     (define iw (hash-ref jsobj 'intrinsicWidth 0))
     (define ih (hash-ref jsobj 'intrinsicHeight 0))
     `(replaced ,id ,styles ,iw ,ih)]
    [(none)
     `(none ,id)]
    [else
     `(block ,id ,styles ,(map json->box-tree children-json))]))

;; convert inline content from JSON
(define (json->inline-content jsobj)
  (define type (hash-ref jsobj 'type "block"))
  (if (equal? type "text")
      (let ([id (string->symbol (hash-ref jsobj 'id "anon"))]
            [styles (json->styles (hash-ref jsobj 'styles (hash)))]
            [content (hash-ref jsobj 'content "")]
            [measured-w (hash-ref jsobj 'measuredWidth 0)])
        `(text ,id ,styles ,content ,measured-w))
      (json->box-tree jsobj)))

;; convert grid definition from JSON
(define (json->grid-def jsobj)
  (define rows (hash-ref jsobj 'gridTemplateRows '()))
  (define cols (hash-ref jsobj 'gridTemplateColumns '()))
  `(grid-def ,(map json->track-size rows)
             ,(map json->track-size cols)))

;; convert a track size from JSON
(define (json->track-size ts)
  (cond
    [(hash? ts)
     (cond
       [(hash-has-key? ts 'px) `(px ,(hash-ref ts 'px))]
       [(hash-has-key? ts 'fr) `(fr ,(hash-ref ts 'fr))]
       [(hash-has-key? ts 'percent) `(% ,(hash-ref ts 'percent))]
       [(hash-has-key? ts 'minmax)
        (define mm (hash-ref ts 'minmax))
        `(minmax ,(json->size-value (car mm))
                 ,(json->size-value (cadr mm)))]
       [else 'auto])]
    [(equal? ts "auto") 'auto]
    [(equal? ts "min-content") 'min-content]
    [(equal? ts "max-content") 'max-content]
    [else 'auto]))

;; convert table row groups from JSON
(define (json->table-row-group jsobj)
  (define type (hash-ref jsobj 'type "row"))
  (cond
    [(equal? type "row-group")
     (define id (string->symbol (hash-ref jsobj 'id "anon")))
     (define styles (json->styles (hash-ref jsobj 'styles (hash))))
     (define rows (map json->table-row (hash-ref jsobj 'children '())))
     `(row-group ,id ,styles ,rows)]
    [else (json->table-row jsobj)]))

(define (json->table-row jsobj)
  (define id (string->symbol (hash-ref jsobj 'id "anon")))
  (define styles (json->styles (hash-ref jsobj 'styles (hash))))
  (define cells (map json->table-cell (hash-ref jsobj 'children '())))
  `(row ,id ,styles ,cells))

(define (json->table-cell jsobj)
  (define id (string->symbol (hash-ref jsobj 'id "anon")))
  (define styles (json->styles (hash-ref jsobj 'styles (hash))))
  (define colspan (hash-ref jsobj 'colspan 1))
  (define children (map json->box-tree (hash-ref jsobj 'children '())))
  `(cell ,id ,styles ,colspan ,children))

;; ============================================================
;; JSON → Styles
;; ============================================================

(define (json->styles jsstyles)
  (define props '())

  (define (add-prop! name val)
    (set! props (cons `(,name ,val) props)))

  ;; box model
  (when (hash-has-key? jsstyles 'width)
    (add-prop! 'width (json->size-value (hash-ref jsstyles 'width))))
  (when (hash-has-key? jsstyles 'height)
    (add-prop! 'height (json->size-value (hash-ref jsstyles 'height))))
  (when (hash-has-key? jsstyles 'minWidth)
    (add-prop! 'min-width (json->size-value (hash-ref jsstyles 'minWidth))))
  (when (hash-has-key? jsstyles 'minHeight)
    (add-prop! 'min-height (json->size-value (hash-ref jsstyles 'minHeight))))
  (when (hash-has-key? jsstyles 'maxWidth)
    (add-prop! 'max-width (json->size-value (hash-ref jsstyles 'maxWidth))))
  (when (hash-has-key? jsstyles 'maxHeight)
    (add-prop! 'max-height (json->size-value (hash-ref jsstyles 'maxHeight))))

  ;; edges
  (when (hash-has-key? jsstyles 'margin)
    (add-prop! 'margin (json->edges (hash-ref jsstyles 'margin))))
  (when (hash-has-key? jsstyles 'padding)
    (add-prop! 'padding (json->edges (hash-ref jsstyles 'padding))))
  (when (hash-has-key? jsstyles 'borderWidth)
    (add-prop! 'border-width (json->edges (hash-ref jsstyles 'borderWidth))))

  ;; box-sizing
  (when (hash-has-key? jsstyles 'boxSizing)
    (add-prop! 'box-sizing (string->symbol (hash-ref jsstyles 'boxSizing))))

  ;; positioning
  (when (hash-has-key? jsstyles 'position)
    (add-prop! 'position (string->symbol (hash-ref jsstyles 'position))))
  (for ([prop '(top right bottom left)])
    (when (hash-has-key? jsstyles prop)
      (add-prop! prop (json->size-value (hash-ref jsstyles prop)))))

  ;; flex
  (when (hash-has-key? jsstyles 'flexDirection)
    (add-prop! 'flex-direction (string->symbol (hash-ref jsstyles 'flexDirection))))
  (when (hash-has-key? jsstyles 'flexWrap)
    (add-prop! 'flex-wrap (string->symbol (hash-ref jsstyles 'flexWrap))))
  (when (hash-has-key? jsstyles 'justifyContent)
    (add-prop! 'justify-content (string->symbol (hash-ref jsstyles 'justifyContent))))
  (when (hash-has-key? jsstyles 'alignItems)
    (add-prop! 'align-items (string->symbol (hash-ref jsstyles 'alignItems))))
  (when (hash-has-key? jsstyles 'alignContent)
    (add-prop! 'align-content (string->symbol (hash-ref jsstyles 'alignContent))))
  (when (hash-has-key? jsstyles 'flexGrow)
    (add-prop! 'flex-grow (hash-ref jsstyles 'flexGrow)))
  (when (hash-has-key? jsstyles 'flexShrink)
    (add-prop! 'flex-shrink (hash-ref jsstyles 'flexShrink)))
  (when (hash-has-key? jsstyles 'flexBasis)
    (add-prop! 'flex-basis (json->size-value (hash-ref jsstyles 'flexBasis))))
  (when (hash-has-key? jsstyles 'alignSelf)
    (add-prop! 'align-self (string->symbol (hash-ref jsstyles 'alignSelf))))
  (when (hash-has-key? jsstyles 'order)
    (add-prop! 'order (hash-ref jsstyles 'order)))
  (when (hash-has-key? jsstyles 'rowGap)
    (add-prop! 'row-gap (hash-ref jsstyles 'rowGap)))
  (when (hash-has-key? jsstyles 'columnGap)
    (add-prop! 'column-gap (hash-ref jsstyles 'columnGap)))

  ;; grid
  (when (hash-has-key? jsstyles 'gridRowStart)
    (add-prop! 'grid-row-start (json->grid-line (hash-ref jsstyles 'gridRowStart))))
  (when (hash-has-key? jsstyles 'gridRowEnd)
    (add-prop! 'grid-row-end (json->grid-line (hash-ref jsstyles 'gridRowEnd))))
  (when (hash-has-key? jsstyles 'gridColumnStart)
    (add-prop! 'grid-column-start (json->grid-line (hash-ref jsstyles 'gridColumnStart))))
  (when (hash-has-key? jsstyles 'gridColumnEnd)
    (add-prop! 'grid-column-end (json->grid-line (hash-ref jsstyles 'gridColumnEnd))))

  ;; text
  (when (hash-has-key? jsstyles 'textAlign)
    (add-prop! 'text-align (string->symbol (hash-ref jsstyles 'textAlign))))

  `(style ,@(reverse props)))

(define (json->size-value val)
  (cond
    [(hash? val)
     (cond
       [(hash-has-key? val 'px) `(px ,(hash-ref val 'px))]
       [(hash-has-key? val 'percent) `(% ,(hash-ref val 'percent))]
       [(hash-has-key? val 'em) `(em ,(hash-ref val 'em))]
       [(hash-has-key? val 'fr) `(fr ,(hash-ref val 'fr))]
       [else 'auto])]
    [(equal? val "auto") 'auto]
    [(equal? val "none") 'none]
    [(equal? val "min-content") 'min-content]
    [(equal? val "max-content") 'max-content]
    [(equal? val "fit-content") 'fit-content]
    [(number? val) `(px ,val)]
    [else 'auto]))

(define (json->edges val)
  (cond
    [(hash? val)
     `(edges ,(hash-ref val 'top 0)
             ,(hash-ref val 'right 0)
             ,(hash-ref val 'bottom 0)
             ,(hash-ref val 'left 0))]
    [(list? val)
     (match val
       [(list t r b l) `(edges ,t ,r ,b ,l)]
       [(list t r) `(edges ,t ,r ,t ,r)]
       [(list a) `(edges ,a ,a ,a ,a)]
       [_ `(edges 0 0 0 0)])]
    [else `(edges 0 0 0 0)]))

(define (json->grid-line val)
  (cond
    [(number? val) `(line ,val)]
    [(equal? val "auto") 'grid-auto]
    [(hash? val)
     (cond
       [(hash-has-key? val 'span) `(span ,(hash-ref val 'span))]
       [(hash-has-key? val 'line) `(line ,(hash-ref val 'line))]
       [else 'grid-auto])]
    [else 'grid-auto]))

;; ============================================================
;; JSON → AvailableSpace
;; ============================================================

(define (json->avail jsobj)
  (define w (hash-ref jsobj 'width 800))
  (define h (hash-ref jsobj 'height #f))
  `(avail (definite ,w) ,(if h `(definite ,h) 'indefinite)))

;; ============================================================
;; Box Tree → JSON (Export)
;; ============================================================

(define (box-tree->json box)
  (match box
    [`(,type ,id ,styles ,children ...)
     (define h (make-hash))
     (hash-set! h 'type (symbol->string type))
     (hash-set! h 'id (symbol->string id))
     (hash-set! h 'styles (styles->json styles))
     (when (pair? children)
       (hash-set! h 'children
                  (map box-tree->json (car children))))
     h]
    [_ (make-hash)]))

(define (styles->json styles)
  (define h (make-hash))
  (match styles
    [`(style . ,props)
     (for ([p (in-list props)]
           #:when (pair? p))
       (define key (car p))
       (define val (cadr p))
       (hash-set! h (symbol->camel-case key) (size-value->json val)))]
    [_ (void)])
  h)

(define (size-value->json val)
  (match val
    [`(px ,n) (make-hash `((px . ,n)))]
    [`(% ,n) (make-hash `((percent . ,n)))]
    [`(em ,n) (make-hash `((em . ,n)))]
    [`(fr ,n) (make-hash `((fr . ,n)))]
    [`(edges ,t ,r ,b ,l) (make-hash `((top . ,t) (right . ,r) (bottom . ,b) (left . ,l)))]
    ['auto "auto"]
    ['none "none"]
    [(? number?) val]
    [(? symbol?) (symbol->string val)]
    [_ val]))

;; ============================================================
;; View → JSON (Export)
;; ============================================================

(define (view->json view)
  (match view
    [`(view ,id ,x ,y ,w ,h (,children ...))
     (define jh (make-hash))
     (hash-set! jh 'id (if (symbol? id) (symbol->string id) (~a id)))
     (hash-set! jh 'x (exact->inexact x))
     (hash-set! jh 'y (exact->inexact y))
     (hash-set! jh 'width (exact->inexact w))
     (hash-set! jh 'height (exact->inexact h))
     (hash-set! jh 'children (map view->json children))
     jh]
    [`(view-text ,id ,x ,y ,w ,h ,text)
     (define jh (make-hash))
     (hash-set! jh 'id (if (symbol? id) (symbol->string id) (~a id)))
     (hash-set! jh 'x (exact->inexact x))
     (hash-set! jh 'y (exact->inexact y))
     (hash-set! jh 'width (exact->inexact w))
     (hash-set! jh 'height (exact->inexact h))
     (hash-set! jh 'text text)
     jh]
    [_ (make-hash)]))

;; ============================================================
;; File I/O
;; ============================================================

(define (json-file->box-tree filepath)
  (define jsobj
    (call-with-input-file filepath
      (λ (in) (read-json in))))
  (json->box-tree jsobj))

(define (view->json-file view filepath)
  (define jsobj (view->json view))
  (call-with-output-file filepath
    (λ (out) (write-json jsobj out))
    #:exists 'replace))

;; ============================================================
;; Utilities
;; ============================================================

;; convert kebab-case symbol to camelCase string
(define (symbol->camel-case sym)
  (define parts (string-split (symbol->string sym) "-"))
  (string->symbol
   (string-append (car parts)
                  (apply string-append
                         (map string-titlecase (cdr parts))))))

(define (~a v)
  (format "~a" v))
