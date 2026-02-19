#lang racket/base

;; layout-table.rkt — CSS Table Layout
;;
;; Implements simplified CSS 2.2 §17 table layout algorithm.
;; Extracted from layout-dispatch.rkt to match the pattern of
;; layout-flex.rkt and layout-grid.rkt.
;;
;; Handles:
;; - Table auto-width computation from column intrinsic sizes
;; - Fixed and auto table-layout algorithms
;; - Border-spacing (separated borders model)
;; - Row groups (thead, tbody, tfoot), captions, column groups
;; - Cell vertical alignment and height stretching
;; - Floated table children

(require racket/match
         racket/list
         racket/math
         "css-layout-lang.rkt"
         "layout-common.rkt")

(provide layout-table)

;; ============================================================
;; Table Layout — Public Entry Point
;; ============================================================

;; layout-fn: the recursive layout dispatch function (avoids circular dependency)
(define (layout-table box avail layout-fn)
  (layout-table-simple box avail layout-fn))

;; ============================================================
;; Table Auto-Width Computation
;; ============================================================

;; CSS 2.2 §17.5.2: compute auto-width for a table from column intrinsic sizes.
;; Examines all cells across all rows, tracks the maximum preferred width
;; per column, then returns the sum of column widths (capped by available width).
(define (compute-table-auto-width all-rows styles avail-w avail layout-fn)
  ;; CSS 2.2 §17.6.1: border-spacing contributes to table auto width
  (define border-collapse (get-style-prop styles 'border-collapse #f))
  (define is-collapse? (eq? border-collapse 'collapse))
  (define bs-h (if is-collapse? 0 (get-style-prop styles 'border-spacing-h 0)))

  (define col-widths (make-hash))  ;; column-index → max-width

  (for ([row (in-list all-rows)])
    (match row
      [`(row ,_ ,_ (,cells ...))
       ;; filter out floated cells
       (define regular-cells
         (filter (lambda (cell)
                   (match cell
                     [`(block ,_ ,s ,_)
                      (let ([fv (get-style-prop s 'float #f)])
                        (not (and fv (not (eq? fv 'float-none)))))]
                     [_ #t]))
                 cells))
       (for ([cell (in-list regular-cells)]
             [col (in-naturals)])
         (define cell-pref-w
           (match cell
             [`(cell ,cid ,cs ,colspan (,children ...))
              (cell-preferred-width cs children avail layout-fn)]
             [`(block ,cid ,cs ,children)
              (cell-preferred-width cs (if (list? children) children (list children)) avail layout-fn)]
             [_ 0]))
         (define old-w (hash-ref col-widths col 0))
         (hash-set! col-widths col (max old-w cell-pref-w)))]
      [_ (void)]))

  (define num-cols (hash-count col-widths))
  (define total-cell-w
    (for/sum ([col (in-range num-cols)])
      (hash-ref col-widths col 0)))
  ;; CSS 2.2 §17.6.1: total width includes (num-cols + 1) horizontal spacing gaps
  (define total-w (+ total-cell-w (* (+ num-cols 1) bs-h)))
  ;; build per-column width list (ordered by column index)
  (define col-widths-list
    (for/list ([col (in-range num-cols)])
      (hash-ref col-widths col 0)))
  ;; return total width and per-column widths
  (values (max total-w 0) col-widths-list))

;; ============================================================
;; Cell Preferred Width
;; ============================================================

;; compute the preferred width of a single table cell.
;; if the cell has an explicit width, use it.
;; otherwise, lay out with shrink-to-fit to determine intrinsic width.
(define (cell-preferred-width cell-styles children avail layout-fn)
  (define explicit-w (get-style-prop cell-styles 'width #f))
  (cond
    [(and explicit-w (not (eq? explicit-w 'auto)))
     (define raw-w
       (cond
         [(number? explicit-w) explicit-w]
         [(and (pair? explicit-w) (eq? (car explicit-w) 'px)) (cadr explicit-w)]
         [else 0]))
     ;; CSS 2.2 §17.5.2.2: column widths use the cell's border-box width.
     ;; The CSS 'width' property is content-box by default (or border-box if
     ;; box-sizing: border-box). Convert to content width first (handles both
     ;; box-sizing modes), then compute border-box width to include padding+border.
     (define bm (extract-box-model cell-styles))
     (compute-border-box-width bm (compute-content-width bm raw-w))]
    [else
     ;; intrinsic width: lay out with max-content constraint
     (define cell-box `(block __cell-measure ,cell-styles (,@children)))
     (define cell-view (layout-fn cell-box `(avail av-max-content ,(caddr avail))))
     (view-width cell-view)]))

;; ============================================================
;; Table Layout — Main Algorithm
;; ============================================================

;; CSS 2.2 §17.6.1: Table layout with border-spacing support
;; border-spacing adds gaps between adjacent cells and at table edges.
;; In the separated borders model (border-collapse: separate, the default),
;; spacing is applied as: edge | cell | gap | cell | gap | cell | edge
(define (layout-table-simple box avail layout-fn)
  (match box
    [`(table ,id ,styles (,row-groups ...))
     (define avail-w (avail-width->number (cadr avail)))
     (define bm (extract-box-model styles avail-w))
     (define offset-x (+ (box-model-padding-left bm) (box-model-border-left bm)))
     (define offset-y (+ (box-model-padding-top bm) (box-model-border-top bm)))

     ;; CSS 2.2 §17.6.1: extract border-spacing
     ;; When border-collapse is 'collapse, spacing is 0.
     ;; Default border-spacing is 0 (CSS initial value).
     (define border-collapse (get-style-prop styles 'border-collapse #f))
     (define is-collapse? (eq? border-collapse 'collapse))
     (define bs-h (if is-collapse? 0 (get-style-prop styles 'border-spacing-h 0)))
     (define bs-v (if is-collapse? 0 (get-style-prop styles 'border-spacing-v 0)))

     ;; CSS 2.2 §17.5.2: determine table width
     ;; If the table has an explicit width, use it.
     ;; If width:auto, compute from column intrinsic widths (shrink-to-fit).
     (define explicit-width (get-style-prop styles 'width #f))
     (define has-explicit-width?
       (and explicit-width (not (eq? explicit-width 'auto))))
     
     ;; Collect all rows from all row-groups (needed for auto-width computation)
     (define all-rows
       (apply append (for/list ([g row-groups]) (extract-table-rows g))))

     ;; CSS 2.2 §17.5.2: table-layout determines the algorithm.
     ;; table-layout: fixed — use the specified width directly (if any).
     ;; table-layout: auto (default) — used width = max(specified, content).
     (define table-layout (get-style-prop styles 'table-layout 'auto))
     (define is-fixed-layout? (eq? table-layout 'fixed))

     ;; compute per-column preferred widths from content
     (define-values (auto-w auto-col-widths)
       (compute-table-auto-width all-rows styles avail-w avail layout-fn))

     (define content-w
       (cond
         ;; Fixed layout with explicit width: use specified width as-is
         [(and is-fixed-layout? has-explicit-width?)
          (if avail-w (resolve-block-width styles avail-w) 0)]
         ;; Auto layout with explicit width: max(specified, content)
         [has-explicit-width?
          (let ([specified-w (if avail-w (resolve-block-width styles avail-w) 0)])
            (max specified-w auto-w))]
         ;; No explicit width: compute from content
         [else auto-w]))

     ;; Determine the number of data columns from the rows.
     ;; This is needed for column width sizing.
     (define num-columns
       (apply max 0
              (for/list ([row (in-list all-rows)])
                (match row
                  [`(row ,_ ,_ (,cells ...))
                   (define regular-cells
                     (filter (lambda (cell)
                               (match cell
                                 [`(block ,_ ,s ,_)
                                  (let ([fv (get-style-prop s 'float #f)])
                                    (not (and fv (not (eq? fv 'float-none)))))]
                                 [_ #t]))
                             cells))
                   (length regular-cells)]
                  [_ 0]))))
     ;; CSS 2.2 §17.6.1: cell width accounts for border-spacing.
     ;; Total horizontal spacing = (num-columns + 1) * bs-h
     ;; Available width for cells = content-w - total horizontal spacing
     (define total-h-spacing (* (+ num-columns 1) bs-h))
     (define cell-available-w (max 0 (- content-w total-h-spacing)))

     ;; CSS 2.2 §17.5.2.1: Fixed table layout algorithm.
     ;; 1. Columns with explicit widths (from first-row cells) get those widths.
     ;; 2. Remaining space distributed equally among auto-width columns.
     ;; For auto layout, distribute proportionally to content widths.
     (define col-widths-list
       (cond
         ;; fixed layout: use first-row cell widths per CSS 2.2 §17.5.2.1
         [(and is-fixed-layout? (> num-columns 0))
          ;; extract first-row cell widths from all-rows
          (define first-row-cells
            (if (null? all-rows) '()
                (match (car all-rows)
                  [`(row ,_ ,_ (,cells ...)) cells]
                  [_ '()])))
          ;; get explicit width from each first-row cell
          (define first-row-widths
            (for/list ([cell (in-list first-row-cells)]
                       [i (in-naturals)])
              (match cell
                [`(block ,_ ,s ,_)
                 (define w-prop (get-style-prop s 'width #f))
                 (cond
                   [(and (number? w-prop) (> w-prop 0)) w-prop]
                   ;; percentage width stored as (% num): resolve against cell-available-w
                   [(and (list? w-prop) (eq? (car w-prop) '%))
                    (define pct (/ (cadr w-prop) 100.0))
                    (* cell-available-w pct)]
                   [else #f])]
                [_ #f])))
          ;; pad/trim to match num-columns
          (define padded-widths
            (cond
              [(= (length first-row-widths) num-columns) first-row-widths]
              [(< (length first-row-widths) num-columns)
               (append first-row-widths
                       (make-list (- num-columns (length first-row-widths)) #f))]
              [else (take first-row-widths num-columns)]))
          ;; count auto columns and remaining space
          (define fixed-total
            (for/sum ([w (in-list padded-widths)])
              (if w w 0)))
          (define auto-count
            (for/sum ([w (in-list padded-widths)])
              (if w 0 1)))
          (define remaining (max 0 (- cell-available-w fixed-total)))
          (define auto-w-each
            (if (> auto-count 0) (/ remaining auto-count) 0))
          ;; assign widths
          (for/list ([w (in-list padded-widths)])
            (if w w auto-w-each))]
         ;; no columns or empty: nothing
         [(= num-columns 0) '()]
         ;; auto-col-widths empty: uniform distribution
         [(null? auto-col-widths)
          (define col-w (/ cell-available-w num-columns))
          (make-list num-columns col-w)]
         [else
          ;; auto layout: distribute proportionally to content widths
          ;; Pad or trim auto-col-widths to match num-columns
          (define padded-col-widths
            (cond
              [(= (length auto-col-widths) num-columns) auto-col-widths]
              [(< (length auto-col-widths) num-columns)
               (append auto-col-widths
                       (make-list (- num-columns (length auto-col-widths)) 0))]
              [else (take auto-col-widths num-columns)]))
          (define total-pref (apply + padded-col-widths))
          (if (> total-pref 0)
              ;; distribute available width proportionally to preferred widths
              (for/list ([pw (in-list padded-col-widths)])
                (* cell-available-w (/ pw total-pref)))
              ;; all zero preferred: uniform distribution
              (make-list num-columns (/ cell-available-w num-columns)))]))

     ;; CSS 2.2 §17.2: row groups must be rendered in order:
     ;; 1. table-caption (above table grid, or below if caption-side: bottom)
     ;; 2. table-column / table-column-group (invisible, but positioned)
     ;; 3. table-header-group (thead — always first in the table grid)
     ;; 4. table-row-group / table-row (tbody — middle)
     ;; 5. table-footer-group (tfoot — always last)
     ;; We lay out in this order (to get correct y positions) but
     ;; then place views back in source order for tree comparison.
     (define (group-sort-key group)
       (define gs
         (match group [`(block ,_ ,s ,_) s] [`(inline ,_ ,s ,_) s] [_ '(style)]))
       (define disp (get-style-prop gs 'display #f))
       (define fv (get-style-prop gs 'float #f))
       (define is-float? (and fv (not (eq? fv 'float-none))))
       (define caption-side-val (get-style-prop gs 'caption-side #f))
       (cond
         [is-float? 0]             ; floats keep original position
         ;; CSS 2.2 §17.4.1: caption-side:bottom renders after tfoot
         [(and (eq? disp 'table-caption) (eq? caption-side-val 'bottom)) 6]
         [(eq? disp 'table-caption) 1]
         [(or (eq? disp 'table-column) (eq? disp 'table-column-group)) 2]
         [(eq? disp 'table-header-group) 3]
         [(or (eq? disp 'table-row-group) (eq? disp 'table-row) (not disp)) 4]
         [(eq? disp 'table-footer-group) 5]
         [else 4]))  ; default: treat like tbody

     ;; Create render-order list of (index . group) pairs
     (define indexed-groups (for/list ([g row-groups] [i (in-naturals)]) (cons i g)))
     (define sorted-indexed-groups
       (sort indexed-groups < #:key (lambda (ig) (group-sort-key (cdr ig)))))

     ;; Lay out in render order, collecting (source-index . view) pairs
     ;; Track row-group IDs and direct-row IDs separately for height distribution.
     ;; row-group-ids: views that wrap rows (tbody/thead/tfoot)
     ;; direct-row-ids: rows placed directly in the table (no row-group wrapper)
     (define-values (indexed-views total-h max-float-bottom column-view-ids
                                   row-group-ids direct-row-ids cell-va-map)
       (let loop ([groups sorted-indexed-groups]
                  [y 0]
                  [acc '()]
                  [mfb 0]
                  [col-ids '()]  ;; track (column-id . column-index) pairs
                  [col-counter 0]
                  [rg-ids '()]   ;; row-group view IDs
                  [dr-ids '()]   ;; direct row view IDs
                  [va-acc '()])  ;; (cell-id . va-value) pairs from all rows
         (cond
           [(null? groups) (values (reverse acc) y mfb col-ids rg-ids dr-ids va-acc)]
           [else
            (define src-idx (car (car groups)))
            (define group (cdr (car groups)))
            ;; check if this child is a floated block (blockified from table-*)
            (define group-styles
              (match group
                [`(block ,_ ,s ,_) s]
                [`(inline ,_ ,s ,_) s]
                [_ '(style)]))
            (define float-val (get-style-prop group-styles 'float #f))
            (define is-float? (and float-val (not (eq? float-val 'float-none))))
            ;; check if this is a table-column or table-column-group
            (define group-display (get-style-prop group-styles 'display #f))
            (define is-column?
              (or (eq? group-display 'table-column)
                  (eq? group-display 'table-column-group)))
            (define group-id
              (match group
                [`(block ,id ,_ ,_) id]
                [`(inline ,id ,_ ,_) id]
                [_ #f]))
            (cond
              [is-float?
               ;; floated child: shrink-to-fit width, position at float edge
               ;; CSS 2.2 §17.2: table establishes BFC, floats are contained.
               (define child-view (layout-fn group `(avail av-max-content ,(caddr avail))))
               (define child-bm (extract-box-model group-styles content-w))
               (define cw (view-width child-view))
               (define ch (view-height child-view))
               (define float-side float-val)
               (define float-x
                 (if (eq? float-side 'float-right)
                     (+ offset-x (- content-w cw (box-model-margin-right child-bm)))
                     (+ offset-x (box-model-margin-left child-bm))))
               (define float-y (+ offset-y y))
               (define positioned (set-view-pos child-view float-x float-y))
               (loop (cdr groups) (+ y ch)
                     (cons (cons src-idx positioned) acc)
                     (max mfb (+ y ch))
                     col-ids col-counter rg-ids dr-ids va-acc)]
              [(and is-column? (eq? group-display 'table-column-group))
               ;; CSS 2.2 §17.5.1: table-column-group wraps its child columns.
               (define child-columns
                 (match group [`(block ,_ ,_ ,children) children] [_ '()]))
               (define span-count
                 (if (null? child-columns) 1 (length child-columns)))
               (define child-col-views
                 (if (null? child-columns)
                     '()
                     (for/list ([cc (in-list child-columns)]
                                [ci (in-naturals)])
                       (define cc-id
                         (match cc [`(block ,cid ,_ ,_) cid] [_ 'anon]))
                       (define actual-col-idx (+ col-counter ci))
                       (define ccw (if (< actual-col-idx (length col-widths-list))
                                       (list-ref col-widths-list actual-col-idx)
                                       0))
                       (define cc-x
                         (for/sum ([j (in-range ci)])
                           (define jcol (+ col-counter j))
                           (+ (if (< jcol (length col-widths-list))
                                  (list-ref col-widths-list jcol) 0)
                              bs-h)))
                       (make-view cc-id cc-x 0 ccw 0 '()))))
               (define group-col-x
                 (+ bs-h (for/sum ([j (in-range col-counter)])
                           (+ (if (< j (length col-widths-list))
                                  (list-ref col-widths-list j) 0)
                              bs-h))))
               (define group-col-w
                 (+ (for/sum ([j (in-range span-count)])
                      (define jcol (+ col-counter j))
                      (if (< jcol (length col-widths-list))
                          (list-ref col-widths-list jcol) 0))
                    (* (max 0 (- span-count 1)) bs-h)))
               (define col-group-view
                 (make-view group-id group-col-x offset-y group-col-w 0
                            child-col-views))
               (define new-col-ids
                 (append (for/list ([cc (in-list child-columns)])
                           (match cc [`(block ,cid ,_ ,_) cid] [_ 'anon]))
                         (cons group-id col-ids)))
               (loop (cdr groups) y
                     (cons (cons src-idx col-group-view) acc) mfb
                     new-col-ids (+ col-counter span-count) rg-ids dr-ids va-acc)]
              [is-column?
               ;; CSS 2.2 §17.5.1: single table-column
               (define this-col-w (if (< col-counter (length col-widths-list))
                                      (list-ref col-widths-list col-counter)
                                      0))
               (define col-x
                 (+ offset-x bs-h
                    (for/sum ([j (in-range col-counter)])
                      (+ (if (< j (length col-widths-list))
                             (list-ref col-widths-list j) 0)
                         bs-h))))
               (define col-view (make-view group-id col-x offset-y this-col-w 0 '()))
               (loop (cdr groups) y
                     (cons (cons src-idx col-view) acc) mfb
                     (cons group-id col-ids) (+ col-counter 1) rg-ids dr-ids va-acc)]
              [else
               (define is-row-group?
                 (or (eq? group-display 'table-row-group)
                     (eq? group-display 'table-header-group)
                     (eq? group-display 'table-footer-group)))
               (define is-direct-row? (eq? group-display 'table-row))
               (cond
                 [is-row-group?
                  (define inner-rows (extract-table-rows group))
                  (define group-w (max 0 (- content-w (* 2 bs-h))))
                  (if (null? inner-rows)
                      (loop (cdr groups) y acc mfb col-ids col-counter rg-ids dr-ids va-acc)
                      (let-values ([(row-views row-h group-cell-va)
                                    (layout-table-rows inner-rows group-w 0
                                                       0 0 avail bs-h bs-v
                                                       col-widths-list layout-fn)])
                        (define group-view
                          (make-view group-id (+ offset-x bs-h) (+ offset-y y bs-v)
                                     group-w row-h row-views))
                        (loop (cdr groups) (+ y bs-v row-h)
                              (cons (cons src-idx group-view) acc) mfb
                              col-ids col-counter
                              (cons group-id rg-ids) dr-ids
                              (append group-cell-va va-acc))))]
                 [is-direct-row?
                  (define direct-row-w (max 0 (- content-w (* 2 bs-h))))
                  (define direct-start-y (+ y bs-v))
                  (define rows (list `(row ,group-id ,group-styles
                                           ,(match group [`(block ,_ ,_ ,c) c] [_ '()]))))
                  (define-values (row-views row-h direct-cell-va)
                    (layout-table-rows rows direct-row-w direct-start-y
                                       (+ offset-x bs-h) offset-y avail bs-h bs-v
                                       col-widths-list layout-fn))
                  (define new-dr-ids
                    (append (map (lambda (rv) (view-id rv)) row-views) dr-ids))
                  (loop (cdr groups) (+ y bs-v row-h)
                        (append (map (lambda (rv) (cons src-idx rv)) (reverse row-views)) acc) mfb
                        col-ids col-counter rg-ids new-dr-ids
                        (append direct-cell-va va-acc))]
                 [else
                  ;; non-table child (e.g. block, text): lay out as block
                  (define child-view (layout-fn group `(avail (definite ,content-w) indefinite)))
                  (define ch (view-height child-view))
                  (define positioned (set-view-pos child-view offset-x (+ offset-y y)))
                  (loop (cdr groups) (+ y ch)
                        (cons (cons src-idx positioned) acc) mfb
                        col-ids col-counter rg-ids dr-ids va-acc)])])])))
     ;; Restore source order
     (define views
       (map cdr (sort indexed-views < #:key car)))

     ;; table establishes BFC: height includes float overflow
     ;; CSS 2.2 §17.6.1: add trailing bs-v after the last row in the table grid
     (define has-table-rows? (> (length all-rows) 0))
     (define total-with-trailing-spacing (if has-table-rows? (+ total-h bs-v) total-h))
     (define content-h-from-children (max total-with-trailing-spacing max-float-bottom))
     (define containing-h (avail-height->number (caddr avail)))
     (define explicit-h (resolve-block-height styles containing-h avail-w))
     (define final-h
       (if explicit-h
           (max content-h-from-children explicit-h)
           content-h-from-children))
     (define total-row-count (length all-rows))
     (define extra-height (max 0 (- final-h content-h-from-children)))

     ;; Helper: stretch a cell to match the new row height, applying VA.
     (define (stretch-cell-in-row cell new-row-h)
       (if (or (not (pair? cell)) (eq? (car cell) 'view-text))
           cell
           (let* ([cell-id (view-id cell)]
                  [old-h (view-height cell)]
                  [va (let ([pair (assoc cell-id cell-va-map)])
                        (if pair (cdr pair) 'va-baseline))]
                  [extra (- new-row-h old-h)]
                  [cell-kids (view-children cell)])
             (if (or (<= extra 0) (not (list? cell-kids)))
                 (make-view cell-id (view-x cell) (view-y cell)
                            (view-width cell) new-row-h cell-kids)
                 (let* ([child-offset
                         (cond
                           [(eq? va 'va-top) 0]
                           [(eq? va 'va-bottom) extra]
                           [(eq? va 'va-middle) (/ extra 2)]
                           [else 0])]
                        [shifted-kids
                         (if (= child-offset 0)
                             cell-kids
                             (for/list ([c (in-list cell-kids)])
                               (if (and (pair? c) (eq? (car c) 'view-text))
                                   `(view-text ,(view-id c) ,(view-x c)
                                               ,(+ (view-y c) child-offset)
                                               ,(view-width c) ,(view-height c)
                                               ,(list-ref c 6))
                                   (make-view (view-id c) (view-x c)
                                              (+ (view-y c) child-offset)
                                              (view-width c) (view-height c)
                                              (view-children c)))))])
                   (make-view cell-id (view-x cell) (view-y cell)
                              (view-width cell) new-row-h shifted-kids))))))

     ;; Helper: stretch a single row view by per-row extra, adjusting y by y-shift
     (define (stretch-row row-view per-row y-shift)
       (define new-row-h (+ (view-height row-view) per-row))
       (define new-row-y (+ (view-y row-view) y-shift))
       (define row-kids (view-children row-view))
       (define new-row-children
         (if (list? row-kids)
             (for/list ([cc (in-list row-kids)])
               (stretch-cell-in-row cc new-row-h))
             row-kids))
       (make-view (view-id row-view) (view-x row-view) new-row-y
                  (view-width row-view) new-row-h new-row-children))

     (define adjusted-row-views
       (if (and (> extra-height 0) (> total-row-count 0))
           (let ([per-row (/ extra-height total-row-count)])
             (define y-shift 0)
             (for/list ([v (in-list views)])
               (cond
                 [(member (view-id v) row-group-ids)
                  (define group-children (view-children v))
                  (define inner-y-shift 0)
                  (define new-children
                    (for/list ([c (in-list
                                   (if (list? group-children) group-children '()))])
                      (if (or (not (pair? c)) (eq? (car c) 'view-text))
                          c
                          (let ([stretched (stretch-row c per-row inner-y-shift)])
                            (set! inner-y-shift (+ inner-y-shift per-row))
                            stretched))))
                  (define group-row-count
                    (length (filter (lambda (c) (and (pair? c)
                                                     (not (eq? (car c) 'view-text))))
                                    (if (list? group-children) group-children '()))))
                  (define group-extra (* group-row-count per-row))
                  (define new-group-h (+ (view-height v) group-extra))
                  (define new-group-y (+ (view-y v) y-shift))
                  (set! y-shift (+ y-shift group-extra))
                  (make-view (view-id v) (view-x v) new-group-y
                             (view-width v) new-group-h new-children)]
                 [(member (view-id v) direct-row-ids)
                  (define stretched (stretch-row v per-row y-shift))
                  (set! y-shift (+ y-shift per-row))
                  stretched]
                 [else
                  (if (> y-shift 0)
                      (make-view (view-id v) (view-x v) (+ (view-y v) y-shift)
                                 (view-width v) (view-height v) (view-children v))
                      v)])))
           views))
     ;; CSS 2.2 §17.5.1: set column/column-group heights to table content height
     (define adjusted-views-final
       (if (null? column-view-ids)
           adjusted-row-views
           (for/list ([v (in-list adjusted-row-views)])
             (if (member (view-id v) column-view-ids)
                 (let ([new-children
                        (for/list ([c (in-list (view-children v))])
                          (make-view (view-id c) (view-x c) (view-y c)
                                     (view-width c) final-h (view-children c)))])
                   (make-view (view-id v) (view-x v) (view-y v)
                              (view-width v) final-h new-children))
                 v))))
     (define border-box-w (compute-border-box-width bm content-w))
     (define border-box-h (compute-border-box-height bm final-h))
     (make-view id 0 0 border-box-w border-box-h adjusted-views-final)]

    [_ (error 'layout-table-simple "expected table box, got: ~a" box)]))

;; ============================================================
;; Table Row Extraction
;; ============================================================

(define (extract-table-rows group)
  (match group
    [`(row-group ,_ ,_ (,rows ...)) rows]
    [`(row ,_ ,_ ,_) (list group)]
    [`(block ,id ,styles ,children)
     (define disp (get-style-prop styles 'display #f))
     (cond
       [(or (eq? disp 'table-row-group)
            (eq? disp 'table-header-group)
            (eq? disp 'table-footer-group))
        (apply append (for/list ([c children]) (extract-table-rows c)))]
       [(eq? disp 'table-row)
        (list `(row ,id ,styles ,children))]
       [else '()])]
    [_ '()]))

;; ============================================================
;; Table Row Layout
;; ============================================================

(define (layout-table-rows rows content-w y offset-x offset-y avail
                            [bs-h 0] [bs-v 0] [col-widths-list '()] [layout-fn #f])
  ;; CSS 2.2 §17.6.1: border-spacing in the separated borders model.
  ;; Returns: (values views total-height cell-va-map)
  (define row-x offset-x)
  (define row-w content-w)
  (let loop ([remaining rows]
             [current-y y]
             [views '()]
             [first? #t]
             [va-map '()])
    (cond
      [(null? remaining)
       (values (reverse views) (- current-y y) va-map)]
      [else
       (match (car remaining)
         [`(row ,row-id ,row-styles (,cells ...))
          ;; add bs-v spacing BETWEEN rows (not before first row)
          (define row-top-y (if first? current-y (+ current-y bs-v)))
          ;; separate floated children from regular cells
          (define-values (float-cells regular-cells)
            (table-partition
             (lambda (cell)
               (match cell
                 [`(block ,_ ,s ,_)
                  (let ([fv (get-style-prop s 'float #f)])
                    (and fv (not (eq? fv 'float-none))))]
                 [_ #f]))
             cells))
          (define num-cells (length regular-cells))
          (define use-per-col? (and (not (null? col-widths-list))
                                    (>= (length col-widths-list) num-cells)))
          (define between-cell-spacing (* (max 0 (- num-cells 1)) bs-h))
          (define uniform-cell-w (if (> num-cells 0)
                            (/ (max 0 (- row-w between-cell-spacing)) num-cells)
                            row-w))
          (define (get-col-w col)
            (if use-per-col?
                (list-ref col-widths-list col)
                uniform-cell-w))
          (define (get-col-x col)
            (if use-per-col?
                (for/sum ([j (in-range col)])
                  (+ (list-ref col-widths-list j) bs-h))
                (* col (+ uniform-cell-w bs-h))))
          (define row-h 0)
          (define cell-views
            (for/list ([cell (in-list regular-cells)]
                       [col (in-naturals)])
              (define cell-x (get-col-x col))
              (match cell
                [`(cell ,cell-id ,cell-styles ,colspan (,children ...))
                 (define cw
                   (if use-per-col?
                       (let ([span (max 1 colspan)])
                         (+ (for/sum ([j (in-range col (min (+ col span) (length col-widths-list)))])
                              (list-ref col-widths-list j))
                            (* (max 0 (- span 1)) bs-h)))
                       (* uniform-cell-w (max 1 colspan))))
                 (define effective-cell-styles
                   (if use-per-col?
                       `(style (width ,cw) ,@(cdr cell-styles))
                       cell-styles))
                 (define cell-box `(block ,cell-id ,effective-cell-styles (,@children)))
                 (define cell-avail `(avail (definite ,cw) indefinite))
                 (define cell-view (layout-fn cell-box cell-avail))
                 (define ch (view-height cell-view))
                 (set! row-h (max row-h ch))
                 (set-view-pos cell-view cell-x 0)]
                [`(block ,cell-id ,cell-styles ,children)
                 (define cw (get-col-w col))
                 (define effective-cell-styles
                   (if use-per-col?
                       `(style (width ,cw) ,@(cdr cell-styles))
                       cell-styles))
                 (define cell-box `(block ,cell-id ,effective-cell-styles ,children))
                 (define cell-avail `(avail (definite ,cw) indefinite))
                 (define cell-view (layout-fn cell-box cell-avail))
                 (define ch (view-height cell-view))
                 (set! row-h (max row-h ch))
                 (set-view-pos cell-view cell-x 0)]
                [_ (make-empty-view 'cell)])))
          ;; lay out floated children
          (define float-views
            (for/list ([fc (in-list float-cells)])
              (match fc
                [`(block ,fc-id ,fc-styles ,fc-children)
                 (define fc-view (layout-fn fc `(avail av-max-content ,(caddr avail))))
                 (define fc-bm (extract-box-model fc-styles row-w))
                 (define fcw (view-width fc-view))
                 (define fch (view-height fc-view))
                 (define fc-float (get-style-prop fc-styles 'float 'float-none))
                 (define fc-x
                   (if (eq? fc-float 'float-right)
                       (- row-w fcw (box-model-margin-right fc-bm))
                       (box-model-margin-left fc-bm)))
                 (set! row-h (max row-h fch))
                 (set-view-pos fc-view fc-x 0)]
                [_ (make-empty-view 'float)])))
          (define row-explicit-h (resolve-block-height row-styles #f row-w))
          (define final-row-h
            (if row-explicit-h (max row-h row-explicit-h) row-h))
          ;; CSS 2.2 §17.5.4: stretch cells to match row height
          ;; and apply vertical alignment within cells.
          (define cell-va-pairs
            (for/list ([cv (in-list cell-views)]
                       [cell (in-list regular-cells)])
              (define cell-styles-tmp
                (match cell
                  [`(cell ,_ ,s ,_ ,_) s]
                  [`(block ,_ ,s ,_) s]
                  [_ '(style)]))
              (cons (view-id cv)
                    (get-style-prop cell-styles-tmp 'vertical-align 'va-baseline))))
          (define stretched-cell-views
            (for/list ([cv (in-list cell-views)]
                       [cell (in-list regular-cells)])
              (define is-text-view? (and (pair? cv) (eq? (car cv) 'view-text)))
              (if is-text-view?
                  cv
                  (let ()
              (define cell-view-h (view-height cv))
              (define cell-actual-h final-row-h)
              (define cell-children (view-children cv))
              (define cell-styles-for-va
                (match cell
                  [`(cell ,_ ,s ,_ ,_) s]
                  [`(block ,_ ,s ,_) s]
                  [_ '(style)]))
              (define cell-bm (extract-box-model cell-styles-for-va (view-width cv)))
              (define pt (box-model-padding-top cell-bm))
              (define pb (box-model-padding-bottom cell-bm))
              (define bt (box-model-border-top cell-bm))
              (define bb (box-model-border-bottom cell-bm))
              (define vert-chrome (+ pt pb bt bb))
              (define orig-content-area (max 0 (- cell-view-h vert-chrome)))
              (define new-content-area (max 0 (- cell-actual-h vert-chrome)))
              (define actual-content-h
                (if (list? cell-children)
                    (let ([content-bottom
                           (apply max 0
                                  (for/list ([c (in-list cell-children)])
                                    (+ (view-y c) (view-height c))))])
                      (max 0 (- content-bottom (+ bt pt))))
                    orig-content-area))
              (define needs-alignment? (> new-content-area actual-content-h))
              (if needs-alignment?
                  (let* ([va (get-style-prop cell-styles-for-va 'vertical-align 'va-baseline)]
                         [extra (- new-content-area actual-content-h)]
                         [child-offset-y
                          (cond
                            [(eq? va 'va-top) 0]
                            [(eq? va 'va-bottom) extra]
                            [(eq? va 'va-middle) (/ extra 2)]
                            [else 0])]
                         [shifted-children
                          (if (list? cell-children)
                              (for/list ([c (in-list cell-children)])
                                (if (and (pair? c) (eq? (car c) 'view-text))
                                    `(view-text ,(view-id c) ,(view-x c)
                                                ,(+ (view-y c) child-offset-y)
                                                ,(view-width c) ,(view-height c)
                                                ,(list-ref c 6))
                                    (make-view (view-id c) (view-x c)
                                               (+ (view-y c) child-offset-y)
                                               (view-width c) (view-height c)
                                               (view-children c))))
                              cell-children)])
                    (make-view (view-id cv) (view-x cv) (view-y cv)
                               (view-width cv) cell-actual-h shifted-children))
                  (if (not (= cell-actual-h cell-view-h))
                      (make-view (view-id cv) (view-x cv) (view-y cv)
                                 (view-width cv) cell-actual-h cell-children)
                      cv))))))
          (define row-view
            (make-view row-id row-x (+ offset-y row-top-y)
                       row-w final-row-h
                       (append stretched-cell-views float-views)))
          (loop (cdr remaining)
                (+ row-top-y final-row-h)
                (cons row-view views)
                #f
                (append cell-va-pairs va-map))]
         [_ (loop (cdr remaining) current-y views first? va-map)])])))

;; ============================================================
;; Helpers
;; ============================================================

;; partition a list into two based on predicate
(define (table-partition pred lst)
  (let loop ([lst lst] [yes '()] [no '()])
    (cond
      [(null? lst) (values (reverse yes) (reverse no))]
      [(pred (car lst)) (loop (cdr lst) (cons (car lst) yes) no)]
      [else (loop (cdr lst) yes (cons (car lst) no))])))
