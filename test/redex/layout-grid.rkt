#lang racket/base

;; layout-grid.rkt — CSS Grid layout algorithm
;;
;; Implements CSS Grid Layout Level 1 (https://www.w3.org/TR/css-grid-1/)
;; Covers:
;;   - Track definition and sizing (px, fr, auto, minmax)
;;   - Explicit and auto item placement
;;   - Fr unit distribution
;;   - Grid alignment (justify/align items/content)
;;
;; Corresponds to Radiant's layout_grid_multipass.cpp.

(require racket/match
         racket/list
         "css-layout-lang.rkt"
         "layout-common.rkt")

(provide layout-grid)

;; ============================================================
;; Grid Item — internal representation
;; ============================================================

(struct grid-item
  (box styles bm
   row-start row-end     ; 0-based track indices
   col-start col-end
   view                  ; laid out view
   )
  #:transparent #:mutable)

;; ============================================================
;; Track — represents a single row or column track
;; ============================================================

(struct track
  (index
   track-size             ; original TrackSize spec
   base-size              ; resolved base size
   growth-limit           ; resolved growth limit
   )
  #:transparent #:mutable)

;; ============================================================
;; Grid Layout — Main Entry Point
;; ============================================================

(define (layout-grid box avail dispatch-fn)
  (match box
    [`(grid ,id ,styles (grid-def (,row-defs ...) (,col-defs ...)) (,children ...))
     (define avail-w (avail-width->number (cadr avail)))
     (define avail-h (avail-height->number (caddr avail)))
     (define bm (extract-box-model styles))

     ;; extract grid properties
     (define row-gap (get-style-prop styles 'row-gap 0))
     (define col-gap (get-style-prop styles 'column-gap 0))
     (define justify-items (get-style-prop styles 'justify-items 'align-stretch))
     (define align-items (get-style-prop styles 'align-items 'align-stretch))

     ;; resolve container content width
     (define content-w (if avail-w (resolve-block-width styles avail-w) 0))
     (define explicit-h (resolve-block-height styles avail-h))

     ;; === Phase 1: Create tracks from definitions ===
     (define col-tracks (create-tracks col-defs))
     (define row-tracks (create-tracks row-defs))

     ;; === Phase 2: Place items into grid cells ===
     (define items (place-grid-items children styles
                                     (length row-tracks) (length col-tracks)))

     ;; ensure enough implicit tracks for placed items
     (define max-row (apply max 1 (map grid-item-row-end items)))
     (define max-col (apply max 1 (map grid-item-col-end items)))
     (set! row-tracks (ensure-tracks row-tracks max-row))
     (set! col-tracks (ensure-tracks col-tracks max-col))

     ;; === Phase 3: Resolve column track sizes ===
     (define total-col-gaps (* col-gap (max 0 (sub1 (length col-tracks)))))
     (define col-available (- content-w total-col-gaps))
     (resolve-track-sizes! col-tracks col-available items 'col dispatch-fn avail)

     ;; === Phase 4: Resolve row track sizes ===
     (define total-row-gaps (* row-gap (max 0 (sub1 (length row-tracks)))))
     (define row-available
       (if explicit-h
           (- explicit-h total-row-gaps)
           +inf.0))
     (resolve-track-sizes! row-tracks row-available items 'row dispatch-fn avail)

     ;; === Phase 5: Position items ===
     (define offset-x (+ (box-model-padding-left bm) (box-model-border-left bm)))
     (define offset-y (+ (box-model-padding-top bm) (box-model-border-top bm)))

     (define child-views
       (for/list ([item (in-list items)])
         (define col-start (grid-item-col-start item))
         (define col-end (grid-item-col-end item))
         (define row-start (grid-item-row-start item))
         (define row-end (grid-item-row-end item))

         ;; compute item area position and size
         (define item-x (track-offset col-tracks col-start col-gap))
         (define item-y (track-offset row-tracks row-start row-gap))
         (define item-w (track-span-size col-tracks col-start col-end col-gap))
         (define item-h (track-span-size row-tracks row-start row-end row-gap))

         ;; lay out the item within its cell
         (define child-avail
           `(avail (definite ,item-w) (definite ,item-h)))
         (define child-view (dispatch-fn (grid-item-box item) child-avail))
         (set-view-pos child-view (+ offset-x item-x) (+ offset-y item-y))))

     ;; compute total grid size
     (define total-col-size (+ (tracks-total-size col-tracks) total-col-gaps))
     (define total-row-size (+ (tracks-total-size row-tracks) total-row-gaps))

     (define final-content-w (max content-w total-col-size))
     (define final-content-h (or explicit-h total-row-size))

     (define border-box-w (compute-border-box-width bm final-content-w))
     (define border-box-h (compute-border-box-height bm final-content-h))

     (make-view id 0 0 border-box-w border-box-h child-views)]

    [_ (error 'layout-grid "expected grid box, got: ~a" box)]))

;; ============================================================
;; Track Creation
;; ============================================================

(define (create-tracks defs)
  (for/list ([def (in-list defs)]
             [i (in-naturals)])
    (track i def 0 +inf.0)))

;; ensure we have at least n tracks (add auto tracks if needed)
(define (ensure-tracks tracks n)
  (define current (length tracks))
  (if (>= current n)
      tracks
      (append tracks
              (for/list ([i (in-range current n)])
                (track i 'auto 0 +inf.0)))))

;; ============================================================
;; Track Size Resolution
;; ============================================================

(define (resolve-track-sizes! tracks available items axis dispatch-fn avail)
  ;; Phase 1: Initialize track sizes
  (for ([t (in-list tracks)])
    (define ts (track-track-size t))
    (match ts
      [`(px ,n)
       (set-track-base-size! t n)
       (set-track-growth-limit! t n)]
      [`(% ,pct)
       (define resolved (* (/ pct 100) available))
       (set-track-base-size! t resolved)
       (set-track-growth-limit! t resolved)]
      ['auto
       (set-track-base-size! t 0)
       (set-track-growth-limit! t +inf.0)]
      ['min-content
       (set-track-base-size! t 0)
       (set-track-growth-limit! t 0)]
      ['max-content
       (set-track-base-size! t 0)
       (set-track-growth-limit! t +inf.0)]
      [`(minmax ,min-sv ,max-sv)
       (define min-v (or (resolve-size-value min-sv available) 0))
       (define max-v (or (resolve-size-value max-sv available) +inf.0))
       (set-track-base-size! t min-v)
       (set-track-growth-limit! t max-v)]
      [`(fr ,_)
       (set-track-base-size! t 0)
       (set-track-growth-limit! t +inf.0)]
      [_ (void)]))

  ;; Phase 2: Size items spanning single tracks
  (for ([item (in-list items)])
    (define start (if (eq? axis 'col) (grid-item-col-start item) (grid-item-row-start item)))
    (define end (if (eq? axis 'col) (grid-item-col-end item) (grid-item-row-end item)))
    (when (= (- end start) 1)
      ;; measure item
      (define item-min-size (measure-grid-item-min item axis dispatch-fn avail))
      (define t (list-ref tracks start))
      (when (> item-min-size (track-base-size t))
        (set-track-base-size! t item-min-size))))

  ;; Phase 3: Distribute fr units
  (define fixed-total
    (for/sum ([t (in-list tracks)]
              #:when (not (fr-track? t)))
      (track-base-size t)))

  (define fr-space (max 0 (- available fixed-total)))
  (define total-fr
    (for/sum ([t (in-list tracks)]
              #:when (fr-track? t))
      (match (track-track-size t)
        [`(fr ,n) n]
        [_ 0])))

  (when (> total-fr 0)
    (define fr-size (/ fr-space total-fr))
    (for ([t (in-list tracks)]
          #:when (fr-track? t))
      (match (track-track-size t)
        [`(fr ,n)
         (define size (* n fr-size))
         (set-track-base-size! t size)
         (set-track-growth-limit! t size)]
        [_ (void)])))

  ;; Phase 4: Maximize auto tracks with remaining space
  (define auto-tracks
    (filter (λ (t) (and (eq? (track-track-size t) 'auto)
                       (= (track-base-size t) 0)))
            tracks))
  (when (and (not (null? auto-tracks))
             (> (- available (tracks-total-size tracks)) 0))
    (define remaining (- available (tracks-total-size tracks)))
    (define per-track (/ remaining (length auto-tracks)))
    (for ([t (in-list auto-tracks)])
      (set-track-base-size! t per-track))))

(define (fr-track? t)
  (match (track-track-size t)
    [`(fr ,_) #t]
    [_ #f]))

;; measure minimum size of a grid item in the given axis
(define (measure-grid-item-min item axis dispatch-fn avail)
  (define child-avail
    `(avail av-min-content av-min-content))
  (define view (dispatch-fn (grid-item-box item) child-avail))
  (if (eq? axis 'col) (view-width view) (view-height view)))

;; ============================================================
;; Item Placement
;; ============================================================

(define (place-grid-items children container-styles num-rows num-cols)
  (define auto-row 0)
  (define auto-col 0)

  (for/list ([child (in-list children)]
             [idx (in-naturals)])
    (define styles (get-box-styles child))
    (define bm (extract-box-model styles))

    ;; read grid placement properties
    (define rs (get-grid-line styles 'grid-row-start))
    (define re (get-grid-line styles 'grid-row-end))
    (define cs (get-grid-line styles 'grid-column-start))
    (define ce (get-grid-line styles 'grid-column-end))

    ;; resolve to 0-based track indices
    (define-values (row-start row-end)
      (resolve-grid-placement rs re num-rows auto-row))
    (define-values (col-start col-end)
      (resolve-grid-placement cs ce num-cols auto-col))

    ;; advance auto placement
    (set! auto-col col-end)
    (when (>= auto-col num-cols)
      (set! auto-col 0)
      (set! auto-row (add1 auto-row)))

    (grid-item child styles bm row-start row-end col-start col-end #f)))

(define (get-grid-line styles prop-name)
  (get-style-prop styles prop-name 'grid-auto))

;; resolve CSS grid line to 0-based track index
(define (resolve-grid-placement start-line end-line num-tracks auto-pos)
  (define s
    (match start-line
      [`(line ,n) (sub1 (if (> n 0) n (+ num-tracks 1 n)))]
      [`(span ,_) auto-pos]
      ['grid-auto auto-pos]
      [_ auto-pos]))
  (define e
    (match end-line
      [`(line ,n) (sub1 (if (> n 0) n (+ num-tracks 1 n)))]
      [`(span ,n) (+ s n)]
      ['grid-auto (add1 s)]
      [_ (add1 s)]))
  (values (max 0 s) (max (add1 s) e)))

;; ============================================================
;; Track Utilities
;; ============================================================

;; total size of all tracks
(define (tracks-total-size tracks)
  (for/sum ([t (in-list tracks)])
    (track-base-size t)))

;; offset of a track from the start
(define (track-offset tracks index gap)
  (for/sum ([t (in-list tracks)]
            [i (in-naturals)]
            #:break (= i index))
    (+ (track-base-size t) (if (> i 0) gap 0))))

;; size of tracks spanning [start, end)
(define (track-span-size tracks start end gap)
  (define span-tracks
    (for/list ([t (in-list tracks)]
               [i (in-naturals)]
               #:when (and (>= i start) (< i end)))
      t))
  (+ (for/sum ([t (in-list span-tracks)])
       (track-base-size t))
     (* gap (max 0 (sub1 (length span-tracks))))))

;; ============================================================
;; Helpers
;; ============================================================

(define (get-box-styles box)
  (match box
    [`(block ,_ ,styles ,_) styles]
    [`(inline ,_ ,styles ,_) styles]
    [`(inline-block ,_ ,styles ,_) styles]
    [`(flex ,_ ,styles ,_) styles]
    [`(grid ,_ ,styles ,_ ,_) styles]
    [`(table ,_ ,styles ,_) styles]
    [`(text ,_ ,styles ,_ ,_) styles]
    [`(replaced ,_ ,styles ,_ ,_) styles]
    [`(none ,_) '(style)]
    [_ '(style)]))

(define (set-view-pos view x y)
  (match view
    [`(view ,id ,_ ,_ ,w ,h ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,_ ,_ ,w ,h ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))
