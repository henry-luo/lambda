#lang racket/base

;; test-grid.rkt — Unit tests for CSS Grid layout
;;
;; Tests grid track sizing, item placement, fr units,
;; and gap handling.

(require rackunit
         racket/list
         "../css-layout-lang.rkt"
         "../layout-common.rkt"
         "../layout-dispatch.rkt")

(printf "Running grid layout tests...\n")

;; ============================================================
;; Test 1: Basic 2-column grid with fixed tracks
;; ============================================================

(test-case "basic 2-column grid with px tracks"
  (define box
    '(grid container
           (style (width (px 400)))
           (grid-def () ((px 200) (px 200)))
           ((block item1 (style (height (px 50))) ())
            (block item2 (style (height (px 50))) ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))
  (check-equal? (length children) 2)
  (check-= (view-width view) 400 0.01))

;; ============================================================
;; Test 2: Grid with fr units
;; ============================================================

(test-case "grid with fr units distributes space"
  (define box
    '(grid container
           (style (width (px 400)))
           (grid-def () ((fr 1) (fr 1)))
           ((block item1 (style (height (px 50))) ())
            (block item2 (style (height (px 50))) ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))
  (check-equal? (length children) 2)
  ;; each column should be 200px (400/2)
  (check-= (view-width (first children)) 200 1))

;; ============================================================
;; Test 3: Grid with mixed px and fr tracks
;; ============================================================

(test-case "grid with mixed px and fr tracks"
  (define box
    '(grid container
           (style (width (px 500)))
           (grid-def () ((px 100) (fr 1) (fr 2)))
           ((block item1 (style (height (px 50))) ())
            (block item2 (style (height (px 50))) ())
            (block item3 (style (height (px 50))) ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))
  (check-equal? (length children) 3))

;; ============================================================
;; Test 4: Grid with gap
;; ============================================================

(test-case "grid with column-gap"
  (define box
    '(grid container
           (style (width (px 420)) (column-gap 20))
           (grid-def () ((px 200) (px 200)))
           ((block item1 (style (height (px 50))) ())
            (block item2 (style (height (px 50))) ()))))
  (define view (layout-document box 800 600))
  (check-= (view-width view) 420 0.01))

;; ============================================================
;; Test 5: Grid with explicit placement
;; ============================================================

(test-case "grid items with explicit placement"
  (define box
    '(grid container
           (style (width (px 400)))
           (grid-def ((px 50) (px 50)) ((px 200) (px 200)))
           ((block item1
                   (style (height (px 50))
                          (grid-row-start (line 1))
                          (grid-row-end (line 2))
                          (grid-column-start (line 1))
                          (grid-column-end (line 2)))
                   ())
            (block item2
                   (style (height (px 50))
                          (grid-row-start (line 1))
                          (grid-row-end (line 2))
                          (grid-column-start (line 2))
                          (grid-column-end (line 3)))
                   ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))
  (check-equal? (length children) 2))

;; ============================================================
;; Test 6: Grid with auto rows
;; ============================================================

(test-case "grid with auto rows sizes to content"
  (define box
    '(grid container
           (style (width (px 200)))
           (grid-def (auto) ((px 200)))
           ((block item1 (style (height (px 80))) ()))))
  (define view (layout-document box 800 600))
  ;; auto row should size to the item height
  (check-true (>= (view-height view) 80)))

;; ============================================================
;; Test 7: Grid with row-gap
;; ============================================================

(test-case "grid with row-gap"
  (define box
    '(grid container
           (style (width (px 200)) (row-gap 10))
           (grid-def ((px 50) (px 50)) ((px 200)))
           ((block item1 (style (height (px 50))) ())
            (block item2 (style (height (px 50))) ()))))
  (define view (layout-document box 800 600))
  ;; total height: 50 + 10 + 50 = 110
  (check-= (view-height view) 110 1))

(printf "Grid layout tests complete.\n")
