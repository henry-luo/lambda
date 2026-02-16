#lang racket/base

;; layout-dispatch.rkt — Top-level layout dispatch
;;
;; Routes box types to their respective layout algorithms.
;; This is the single entry point for all layout operations.
;; Corresponds to Radiant's layout_block.cpp top-level dispatch.

(require racket/match
         racket/list
         racket/string
         racket/math
         "css-layout-lang.rkt"
         "layout-common.rkt"
         "layout-block.rkt"
         "layout-inline.rkt"
         "layout-flex.rkt"
         "layout-grid.rkt"
         "layout-positioned.rkt"
         "layout-intrinsic.rkt")

(provide layout
         layout-document)

;; ============================================================
;; Layout Dispatch — Main Entry Point
;; ============================================================

;; dispatch layout based on box type.
;; box: a Box term from css-layout-lang
;; avail: an AvailableSpace term: (avail AvailWidth AvailHeight)
;; returns: a View term
(define (layout box avail)
  (define styles (get-box-styles box))
  (define position (get-style-prop styles 'position 'static))

  ;; handle positioned elements (absolute/fixed are pulled from flow)
  ;; they are laid out by their containing block, not here.
  ;; for absolute/fixed, we call layout-positioned directly.

  (define view
    (cond
      ;; absolute or fixed: lay out via layout-positioned
      [(or (eq? position 'absolute) (eq? position 'fixed))
       (define avail-w (avail-width->number (cadr avail)))
       (define avail-h (avail-height->number (caddr avail)))
       (layout-positioned box (or avail-w 0) (or avail-h 0) layout)]
      [else
       (match box
      ;; display:none
      [`(none ,id)
       (make-empty-view id)]

      ;; block container
      ;; CSS 2.2 §10.3.5: floated block with auto width → shrink-to-fit
      [`(block ,id ,styles ,children)
       (define float-val (get-style-prop styles 'float #f))
       (define css-width (get-style-prop styles 'width 'auto))
       (if (and float-val (not (eq? float-val 'float-none)) (eq? css-width 'auto))
           ;; shrink-to-fit: same algorithm as inline-block
           (let ()
             (define avail-w (avail-width->number (cadr avail)))
             (define avail-h (avail-height->number (caddr avail)))
             (define max-avail `(avail av-max-content indefinite))
             (define max-view (layout-block box max-avail layout))
             (define preferred-w (view-width max-view))
             (define min-avail `(avail av-min-content indefinite))
             (define min-view (layout-block box min-avail layout))
             (define min-w (view-width min-view))
             (define shrink-w
               (min preferred-w (max min-w (or avail-w preferred-w))))
             ;; re-layout at computed width
             (define final-avail
               `(avail (definite ,shrink-w)
                       ,(if avail-h `(definite ,avail-h) 'indefinite)))
             (layout-block box final-avail layout))
           ;; normal block: use available width
           (layout-block box avail layout))]

      ;; inline container
      [`(inline ,id ,styles ,children)
       (layout-inline box avail layout)]

      ;; inline-block: shrink-to-fit when width is auto (CSS 2.2 §10.3.9)
      [`(inline-block ,id ,styles ,children)
       ;; check if width is auto (shrink-to-fit) vs explicit
       (define css-width (get-style-prop styles 'width 'auto))
       (if (eq? css-width 'auto)
           ;; shrink-to-fit: use intrinsic sizing to find preferred width
           ;; then lay out at that width
           (let ()
             (define avail-w (avail-width->number (cadr avail)))
             (define avail-h (avail-height->number (caddr avail)))
             ;; measure max-content (preferred width)
             (define max-avail `(avail av-max-content indefinite))
             (define max-view (layout-block `(block ,id ,styles ,children)
                                            max-avail layout))
             (define preferred-w (view-width max-view))
             ;; measure min-content (minimum width)
             (define min-avail `(avail av-min-content indefinite))
             (define min-view (layout-block `(block ,id ,styles ,children)
                                            min-avail layout))
             (define min-w (view-width min-view))
             ;; shrink-to-fit = min(max(minimum, available), preferred)
             (define shrink-w
               (min preferred-w (max min-w (or avail-w preferred-w))))
             ;; apply min-width / max-width constraints
             (define bm (extract-box-model styles avail-w))
             (define min-width-val (get-style-prop styles 'min-width 'auto))
             (define max-width-val (get-style-prop styles 'max-width 'none))
             (define min-width-raw (or (resolve-size-value min-width-val avail-w) 0))
             (define max-width-raw (or (resolve-size-value max-width-val avail-w) +inf.0))
             ;; convert to border-box if needed
             (define effective-min-w
               (if (and (> min-width-raw 0) (eq? (box-model-box-sizing bm) 'border-box))
                   min-width-raw
                   (+ min-width-raw (horizontal-pb bm))))
             (define effective-max-w
               (if (and (not (infinite? max-width-raw)) (eq? (box-model-box-sizing bm) 'border-box))
                   max-width-raw
                   (+ max-width-raw (horizontal-pb bm))))
             (define final-w (max effective-min-w (min effective-max-w shrink-w)))
             ;; re-layout at computed width
             (define final-avail
               `(avail (definite ,final-w)
                       ,(if avail-h `(definite ,avail-h) 'indefinite)))
             (layout-block `(block ,id ,styles ,children) final-avail layout))
           ;; explicit width: just lay out as block
           (layout-block `(block ,id ,styles ,children) avail layout))]

      ;; flex container
      [`(flex ,id ,styles ,children)
       (layout-flex box avail layout)]

      ;; grid container
      [`(grid ,id ,styles ,grid-def ,children)
       (layout-grid box avail layout)]

      ;; table container (simplified: treat as block)
      [`(table ,id ,styles ,table-children)
       (layout-table-simple box avail)]

      ;; text leaf
      [`(text ,id ,styles ,content ,measured-w)
       (layout-text box avail)]

      ;; replaced element (img, etc.)
      [`(replaced ,id ,styles ,intrinsic-w ,intrinsic-h)
       (layout-replaced box avail)]

      [_ (error 'layout "unknown box type: ~a" box)])]))

  ;; apply relative positioning offset
  ;; pass containing block dimensions for percentage resolution
  (if (eq? position 'relative)
      (let ([cb-w (avail-width->number (cadr avail))]
            [cb-h (avail-height->number (caddr avail))])
        (apply-relative-offset view styles cb-w cb-h))
      view))

;; ============================================================
;; Document-Level Layout
;; ============================================================

;; lay out a document root box at a given viewport size.
;; viewport-w, viewport-h in pixels.
(define (layout-document root-box viewport-w viewport-h)
  (define avail `(avail (definite ,viewport-w) (definite ,viewport-h)))
  (layout root-box avail))

;; ============================================================
;; Text Layout
;; ============================================================

(define (layout-text box avail)
  (match box
    [`(text ,id ,styles ,content ,measured-w)
     (define bm (extract-box-model styles))
     (define avail-w (avail-width->number (cadr avail)))

     ;; detect intrinsic sizing mode: min-content forces maximum wrapping (avail-w → 0)
     (define is-min-content? (eq? (cadr avail) 'av-min-content))
     (define effective-avail-w (if is-min-content? 0 avail-w))

     ;; detect font type: Ahem (Taffy tests) vs proportional (CSS2.1 tests)
     (define font-type (get-style-prop styles 'font-type 'ahem))
     (define is-proportional? (eq? font-type 'proportional))

     ;; font metrics depend on font type
     (define font-size
       (cond
         [is-proportional?
          ;; use actual font-size from styles (CSS default is 16px)
          (get-style-prop styles 'font-size 16)]
         ;; check for explicit font-size in styles (e.g., CSS2.1 tests with non-default Ahem size)
         [else (get-style-prop styles 'font-size 10)]))
     ;; detect font-metrics: 'times (serif normal-weight) or 'arial (sans-serif/bold)
     ;; set by reference-import based on font-family + font-weight
     (define font-metrics-val (get-style-prop styles 'font-metrics 'times))
     (define use-arial? (eq? font-metrics-val 'arial))
     ;; select line-height ratio based on font: Times 1.107, Arial 1.15
     (define proportional-lh-ratio (if use-arial? 1.15 1.107))
     (define line-height
       (cond
         [is-proportional?
          ;; browser default 'normal' line-height from hhea metrics:
          ;; Times: (ascender+|descender|)/unitsPerEm = (891+216)/1000 ≈ 1.107
          ;; Arial: (1854+434+67)/2048 ≈ 1.15
          (let ([lh-prop (get-style-prop styles 'line-height #f)])
            (if lh-prop
                (if (number? lh-prop) lh-prop (* proportional-lh-ratio font-size))
                (* proportional-lh-ratio font-size)))]
         [else
          ;; Ahem: check for explicit line-height, default to font-size
          (let ([lh-prop (get-style-prop styles 'line-height #f)])
            (if (and lh-prop (number? lh-prop))
                lh-prop
                font-size))]))
     ;; per-character Arial TrueType glyph advance width ratios (used for bold text)
     (define (arial-char-ratio ch)
       (cond
         [(char=? ch #\space) 0.278]
         [(char-upper-case? ch)
          (case ch
            [(#\I) 0.278] [(#\J) 0.500] [(#\L) 0.556] [(#\M) 0.833]
            [(#\W) 0.944] [(#\F #\T #\Z) 0.611]
            [(#\C #\O #\Q #\G) 0.778]
            [(#\D #\H #\N #\R #\U) 0.722]
            [(#\A #\B #\E #\K #\P #\S #\V #\X #\Y) 0.667]
            [else 0.667])]
         [(char-lower-case? ch)
          (case ch
            [(#\f #\t) 0.278] [(#\i #\j #\l) 0.222] [(#\r) 0.333]
            [(#\c #\k #\s #\v #\x #\y #\z) 0.500] [(#\m) 0.833] [(#\w) 0.722]
            [(#\a #\b #\d #\e #\g #\h #\n #\o #\p #\q #\u) 0.556]
            [else 0.556])]
         [(char-numeric? ch) 0.556]
         [else
          (case ch
            [(#\. #\, #\: #\; #\! #\/) 0.278]
            [(#\- #\( #\)) 0.333]
            [(#\? #\&) 0.556]
            [else 0.556])]))
     ;; per-character Times (serif) TrueType glyph advance width ratios
     ;; from macOS Times.dfont hhea table (unitsPerEm=1000)
     ;; uppercase ratios include -0.025em average kerning correction
     (define (times-char-ratio ch)
       (cond
         [(char=? ch #\space) 0.250]
         [(char-upper-case? ch)
          ;; apply kerning correction: Times has significant pair kerning for
          ;; uppercase letters (e.g., F-A, T-o) which we don't model individually
          (- (case ch
               [(#\I) 0.333] [(#\J) 0.389] [(#\M) 0.889] [(#\W) 0.944]
               [(#\A #\V) 0.722] [(#\B #\R) 0.667]
               [(#\C #\G #\O #\Q) 0.722]
               [(#\D #\H #\K #\N #\U #\X #\Y) 0.722]
               [(#\E #\L #\Z) 0.611]
               [(#\F) 0.556] [(#\P) 0.556] [(#\S) 0.556] [(#\T) 0.611]
               [else 0.667])
             0.025)]
         [(char-lower-case? ch)
          (case ch
            [(#\f) 0.333] [(#\i #\j #\l) 0.278] [(#\t) 0.278]
            [(#\r) 0.333] [(#\s) 0.389]
            [(#\m) 0.722] [(#\w) 0.667]
            [(#\a #\c #\e #\z) 0.444]
            [(#\b #\d #\g #\h #\k #\n #\o #\p #\q #\u #\v #\x #\y) 0.500]
            [else 0.500])]
         [(char-numeric? ch) 0.500]
         [else
          (case ch
            [(#\. #\, #\: #\; #\!) 0.278]
            [(#\/ #\-) 0.333] [(#\( #\)) 0.333]
            [(#\?) 0.444] [(#\&) 0.778]
            [(#\") 0.444] [(#\') 0.333]
            [else 0.500])]))
     ;; select char-width function based on font-metrics (arial vs times)
     (define (char-width ch fs)
       (* (if use-arial? (arial-char-ratio ch) (times-char-ratio ch)) fs))
     ;; fallback category-based widths (used for Ahem font path)
     (define upper-w (if is-proportional? (* 0.667 font-size) font-size))
     (define lower-w (if is-proportional? (* 0.556 font-size) font-size))
     (define space-w-char (if is-proportional? (* (if use-arial? 0.278 0.250) font-size) font-size))
     (define default-w (if is-proportional? (* 0.556 font-size) font-size))

     (cond
       ;; no wrapping needed: text fits or no constraint
       [(or (not effective-avail-w) (<= measured-w effective-avail-w))
        ;; For Ahem: display height = font-size, y = half-leading
        ;; For proportional: display height = line-height (matches Chrome getBoundingClientRect)
        (if is-proportional?
            (make-text-view id 0 0 measured-w line-height content)
            (let ([half-leading (/ (- line-height font-size) 2)])
              (make-text-view id 0 half-leading measured-w font-size content)))]
       [else
        (cond
          ;; proportional font: split on spaces for word wrapping
          [is-proportional?
           (define words (string-split content " "))
           ;; compute word widths using per-character Arial metrics
           (define (word-width w)
             (for/fold ([total 0]) ([ch (in-string w)])
               (+ total (char-width ch font-size))))
           (define word-ws
             (for/list ([w (in-list words)])
               (word-width w)))
           (define space-w space-w-char)
           (define-values (num-lines max-line-w)
             (let loop ([remaining-words word-ws]
                        [line-w 0]
                        [lines 1]
                        [max-w 0])
               (cond
                 [(null? remaining-words)
                  (values lines (max max-w line-w))]
                 [else
                  (define ww (car remaining-words))
                  (define new-line-w
                    (if (= line-w 0) ww (+ line-w space-w ww)))
                  (cond
                    ;; first word on line always fits
                    [(= line-w 0)
                     (loop (cdr remaining-words) ww lines max-w)]
                    ;; word fits on current line
                    [(<= new-line-w effective-avail-w)
                     (loop (cdr remaining-words) new-line-w lines max-w)]
                    ;; word doesn't fit → start new line
                    [else
                     (loop (cdr remaining-words) ww (+ lines 1) (max max-w line-w))])])))
           ;; for proportional text, report max line width (not container width)
           (define text-w max-line-w)
           (define text-h (* num-lines line-height))
           (make-text-view id 0 0 text-w text-h content)]

          ;; Ahem font: split on zero-width spaces
          [else
           (define words (string-split content "\u200B"))
           (define word-ws
             (for/list ([w (in-list words)])
               ;; each visible char = font-size px wide
               (for/sum ([ch (in-string w)])
                 (if (or (char=? ch #\u200B) (char=? ch #\u200C)
                         (char=? ch #\u200D) (char=? ch #\uFEFF))
                     0 font-size))))
           (define-values (num-lines max-line-w)
             (let loop ([remaining-words word-ws]
                        [line-w 0]
                        [lines 1]
                        [max-w 0])
               (cond
                 [(null? remaining-words)
                  (values lines (max max-w line-w))]
                 [else
                  (define ww (car remaining-words))
                  (define new-line-w (+ line-w ww))
                  (cond
                    ;; first word on line always fits
                    [(= line-w 0)
                     (loop (cdr remaining-words) new-line-w lines max-w)]
                    ;; word fits on current line
                    [(<= new-line-w effective-avail-w)
                     (loop (cdr remaining-words) new-line-w lines max-w)]
                    ;; word doesn't fit → start new line
                    [else
                     (loop (cdr remaining-words) ww (+ lines 1) (max max-w line-w))])])))
           ;; report actual content width (max line width), not container width
           (define text-w max-line-w)
           ;; Ahem: display height spans from first-line half-leading to last-line bottom
           ;; = (num-lines - 1) * line-height + font-size
           (define half-leading (/ (- line-height font-size) 2))
           (define text-h (+ (* (sub1 num-lines) line-height) font-size))
           (make-text-view id 0 half-leading text-w text-h content)])])]

    [_ (error 'layout-text "expected text box, got: ~a" box)]))

;; ============================================================
;; Replaced Element Layout (img, svg, etc.)
;; ============================================================

(define (layout-replaced box avail)
  (match box
    [`(replaced ,id ,styles ,intrinsic-w ,intrinsic-h)
     (define avail-w (avail-width->number (cadr avail)))
     (define avail-h (avail-height->number (caddr avail)))
     (define bm (extract-box-model styles avail-w))

     ;; resolve explicit width/height
     (define css-w (get-style-prop styles 'width 'auto))
     (define css-h (get-style-prop styles 'height 'auto))
     (define resolved-w (resolve-size-value css-w (or avail-w intrinsic-w)))
     ;; CSS 2.2 §10.5: percentage heights resolve only when containing block
     ;; has a definite (explicit) height; otherwise treat as auto
     (define resolved-h
       (cond
         [(and (pair? css-h) (eq? (car css-h) '%) (not avail-h))
          #f]  ;; percentage with indefinite CB height → auto
         [else (resolve-size-value css-h (or avail-h intrinsic-h))]))

     ;; determine final size maintaining aspect ratio
     (define aspect (if (> intrinsic-h 0)
                       (/ intrinsic-w intrinsic-h)
                       1))
     (define-values (final-w final-h)
       (cond
         ;; both specified
         [(and resolved-w resolved-h)
          (values resolved-w resolved-h)]
         ;; only width specified: derive height from aspect ratio
         [resolved-w
          (values resolved-w (/ resolved-w aspect))]
         ;; only height specified: use intrinsic width if available (CSS 2.2 §10.3.2)
         ;; browsers prefer intrinsic width over aspect-ratio derivation for elements
         ;; with concrete intrinsic dimensions (iframe, img, video)
         [resolved-h
          (if (> intrinsic-w 0)
              (values intrinsic-w resolved-h)
              (values (* resolved-h aspect) resolved-h))]
         ;; neither specified: use intrinsic
         [else
          (values intrinsic-w intrinsic-h)]))

     ;; apply min/max constraints
     (define min-w (or (resolve-size-value (get-style-prop styles 'min-width 'auto) avail-w) 0))
     (define max-w (or (resolve-size-value (get-style-prop styles 'max-width 'none) avail-w) +inf.0))
     (define min-h (or (resolve-size-value (get-style-prop styles 'min-height 'auto) avail-h) 0))
     (define max-h (or (resolve-size-value (get-style-prop styles 'max-height 'none) avail-h) +inf.0))

     (define clamped-w (max min-w (min max-w final-w)))
     (define clamped-h (max min-h (min max-h final-h)))

     (define border-box-w (compute-border-box-width bm clamped-w))
     (define border-box-h (compute-border-box-height bm clamped-h))

     (make-view id 0 0 border-box-w border-box-h '())]

    [_ (error 'layout-replaced "expected replaced box, got: ~a" box)]))

;; ============================================================
;; Table Layout (simplified)
;; ============================================================

;; simplified table: treat as vertical block stacking
;; a proper implementation would handle column sizing,
;; border-spacing, caption placement, etc.
(define (layout-table-simple box avail)
  (match box
    [`(table ,id ,styles (,row-groups ...))
     (define avail-w (avail-width->number (cadr avail)))
     (define bm (extract-box-model styles avail-w))
     (define content-w (if avail-w (resolve-block-width styles avail-w) 0))
     (define offset-x (+ (box-model-padding-left bm) (box-model-border-left bm)))
     (define offset-y (+ (box-model-padding-top bm) (box-model-border-top bm)))

     (define-values (views total-h)
       (let loop ([groups row-groups]
                  [y 0]
                  [acc '()])
         (cond
           [(null? groups) (values (reverse acc) y)]
           [else
            (define group (car groups))
            (define rows (extract-table-rows group))
            (define-values (row-views row-h)
              (layout-table-rows rows content-w y offset-x offset-y avail))
            (loop (cdr groups) (+ y row-h)
                  (append (reverse row-views) acc))])))

     (define border-box-w (compute-border-box-width bm content-w))
     (define border-box-h (compute-border-box-height bm total-h))
     (make-view id 0 0 border-box-w border-box-h views)]

    [_ (error 'layout-table-simple "expected table box, got: ~a" box)]))

(define (extract-table-rows group)
  (match group
    [`(row-group ,_ ,_ (,rows ...)) rows]
    [`(row ,_ ,_ ,_) (list group)]
    [_ '()]))

(define (layout-table-rows rows content-w y offset-x offset-y avail)
  (let loop ([remaining rows]
             [current-y y]
             [views '()])
    (cond
      [(null? remaining)
       (values (reverse views) (- current-y y))]
      [else
       (match (car remaining)
         [`(row ,row-id ,row-styles (,cells ...))
          (define num-cells (length cells))
          (define cell-w (if (> num-cells 0) (/ content-w num-cells) content-w))
          (define row-h 0)
          (define cell-views
            (for/list ([cell (in-list cells)]
                       [col (in-naturals)])
              (match cell
                [`(cell ,cell-id ,cell-styles ,colspan (,children ...))
                 (define cw (* cell-w (max 1 colspan)))
                 (define cell-box `(block ,cell-id ,cell-styles (,@children)))
                 (define cell-avail `(avail (definite ,cw) indefinite))
                 (define cell-view (layout cell-box cell-avail))
                 (define ch (view-height cell-view))
                 (set! row-h (max row-h ch))
                 (set-view-pos cell-view
                              (+ offset-x (* col cell-w))
                              (+ offset-y current-y))]
                [_ (make-empty-view 'cell)])))
          (loop (cdr remaining)
                (+ current-y row-h)
                (append (reverse cell-views) views))]
         [_ (loop (cdr remaining) current-y views)])])))

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
    [`(view ,id ,_ ,_ ,w ,h ,children ,baseline)
     `(view ,id ,x ,y ,w ,h ,children ,baseline)]
    [`(view ,id ,_ ,_ ,w ,h ,children)
     `(view ,id ,x ,y ,w ,h ,children)]
    [`(view-text ,id ,_ ,_ ,w ,h ,text)
     `(view-text ,id ,x ,y ,w ,h ,text)]
    [_ view]))
