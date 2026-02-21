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
         "layout-intrinsic.rkt"
         "layout-table.rkt"
         "font-metrics.rkt")

(provide layout
         layout-document)

;; ============================================================
;; Layout Dispatch — Main Entry Point
;; ============================================================

;; depth guard: prevents exponential blowup on deeply nested flex/grid measurement.
;; each layout call increments the depth; if it exceeds max-layout-depth, we return
;; a zero-size fallback view instead of recursing further.
(define max-layout-depth 12)
(define current-layout-depth (make-parameter 0))

;; dispatch layout based on box type.
;; box: a Box term from css-layout-lang
;; avail: an AvailableSpace term: (avail AvailWidth AvailHeight)
;; returns: a View term
(define (layout box avail)
  ;; depth guard: bail out on excessive nesting to prevent exponential blowup
  (if (> (current-layout-depth) max-layout-depth)
      (let ([id (match box [`(,_ ,id . ,_) id] [_ 'overflow])])
        (make-empty-view id))
      (parameterize ([current-layout-depth (add1 (current-layout-depth))])
        (layout-impl box avail))))

;; the actual layout dispatch implementation
(define (layout-impl box avail)
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
             ;; shrink-w is border-box width; resolve-block-width with auto width
             ;; computes: content-w = containing-width - padding+border - margin
             ;; so we must add margins to avoid double-subtraction
             (define bm-float (extract-box-model styles (or avail-w 0)))
             (define final-avail
               `(avail (definite ,(+ shrink-w (horizontal-margin bm-float)))
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

      ;; table container
      [`(table ,id ,styles ,table-children)
       (layout-table box avail layout)]

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
      view))  ;; close layout-impl

;; ============================================================
;; Document-Level Layout
;; ============================================================

;; lay out a document root box at a given viewport size.
;; viewport-w, viewport-h in pixels.
;;
;; The root element has no parent to apply its margin positioning.
;; In the browser, the root element is inside <body>, and:
;;   - horizontal margins don't collapse, so root x = margin-left relative to body
;;   - vertical margins collapse through body (body has no border/padding),
;;     so root y = 0 relative to body
;; Our comparison uses body-relative coordinates, so we apply margin-left + body padding to x.
(define (layout-document root-box viewport-w viewport-h)
  (define root-styles (get-box-styles root-box))
  (define root-bm (extract-box-model root-styles viewport-w))
  ;; extract body padding if injected by reference-import
  (define body-pad-left (get-style-prop root-styles '__body-padding-left 0))

  ;; CSS 2.2 §9.5: preceding float context from reference-import.
  ;; When the test HTML has float siblings before the root div, we create a
  ;; wrapper block containing a phantom float + root so the layout engine
  ;; can position the root beside the float via BFC avoidance.
  (define pf-side (get-style-prop root-styles '__preceding-float-side #f))
  (define pf-w (get-style-prop root-styles '__preceding-float-w 0))
  (define pf-h (get-style-prop root-styles '__preceding-float-h 0))

  ;; CSS 2.2 §17.5.2: tables with auto width use shrink-to-fit sizing.
  ;; When the root element is a table (e.g., body { display: table }),
  ;; use shrink-to-fit width instead of full viewport width.
  (define is-table-root? (match root-box [`(table . ,_) #t] [_ #f]))
  (define avail
    (if is-table-root?
        ;; table root: lay out at max-content first to determine shrink-to-fit width,
        ;; but cap at viewport width
        `(avail (definite ,viewport-w) (definite ,viewport-h))
        `(avail (definite ,viewport-w) (definite ,viewport-h))))
  (define view
    (cond
      ;; --- preceding float: create wrapper with phantom float + root ---
      [pf-side
       (let* ([phantom-float
               `(block __phantom-float
                       (style (float ,pf-side)
                              (width (px ,pf-w))
                              (height (px ,pf-h))
                              (margin-top 0) (margin-right 0)
                              (margin-bottom 0) (margin-left 0)
                              (padding-top 0) (padding-right 0)
                              (padding-bottom 0) (padding-left 0)
                              (border-top-width 0) (border-right-width 0)
                              (border-bottom-width 0) (border-left-width 0))
                       ())]
              [wrapper `(block __body-wrapper (style) (,phantom-float ,root-box))]
              [wrapper-view (layout wrapper avail)]
              [wrapper-children (view-children wrapper-view)]
              [root-view
               (for/first ([c (in-list wrapper-children)]
                           #:when (not (equal? (view-id c) '__phantom-float)))
                 c)])
         (or root-view wrapper-view))]

      ;; --- table root with auto width: shrink-to-fit ---
      [is-table-root?
       (let ()
         (define css-width (get-style-prop root-styles 'width 'auto))
         (if (eq? css-width 'auto)
             ;; shrink-to-fit: measure at max-content, then cap
             (let* ([max-avail `(avail av-max-content indefinite)]
                    [max-view (layout root-box max-avail)]
                    [preferred-w (view-width max-view)]
                    [min-avail `(avail av-min-content indefinite)]
                    [min-view (layout root-box min-avail)]
                    [min-w (view-width min-view)]
                    [shrink-w (min preferred-w (max min-w viewport-w))]
                    [final-avail `(avail (definite ,shrink-w) (definite ,viewport-h))])
               (layout root-box final-avail))
             (layout root-box avail)))]

      ;; --- normal block root ---
      [else
       (layout root-box avail)]))
  ;; CSS 2.2 §9.7: if the root box itself is floated, position it as if
  ;; <body> were its parent block formatting context.
  ;; §9.7 also: position:absolute/fixed → float computes to none.
  ;; float:right → place at right edge of body content area;
  ;; float:left → stays at x=0 (normal).
  (define root-float (get-style-prop root-styles 'float #f))
  (define root-position (get-style-prop root-styles 'position #f))
  (define effective-float
    (if (memq root-position '(absolute fixed))
        #f  ;; §9.7: absolutely positioned elements compute float:none
        root-float))
  (define base-x
    (cond
      [(eq? effective-float 'float-right)
       (- viewport-w (view-width view) (box-model-margin-right root-bm))]
      ;; CSS 2.2 §9.3.1: absolutely positioned root with explicit 'left'
      ;; positions relative to the initial containing block (viewport).
      ;; Convert to body-relative coords: x = left_value - body_margin.
      [(and (eq? root-position 'absolute)
            (let ([lv (get-style-prop root-styles 'left #f)])
              (and lv (not (eq? lv 'auto)))))
       (let* ([left-raw (get-style-prop root-styles 'left 0)]
              [left-px (cond [(and (list? left-raw) (eq? (car left-raw) 'px))
                              (cadr left-raw)]
                             [(number? left-raw) left-raw]
                             [else 0])]
              [body-margin (get-style-prop root-styles '__body-margin-left 8)])
         (- left-px body-margin))]
      [else
       (box-model-margin-left root-bm)]))
  ;; when preceding float context exists, preserve the x from BFC avoidance;
  ;; otherwise use normal body margin positioning.
  ;; always add view-x to preserve relative positioning offset (0 for static elements)
  (define final-x
    (if pf-side
        (+ body-pad-left (view-x view))
        (+ body-pad-left base-x (view-x view))))
  ;; Zero the root y: reference->expected-layout zeroes root y for CSS2.1
  ;; tests (preceding <p> description text is not modeled).  Match that
  ;; by discarding position:relative offset on the root element.
  (set-view-pos view final-x 0))

;; ============================================================
;; Text Layout
;; ============================================================

;; Ahem font character width: most visible chars = 1em,
;; Unicode space characters have typographic fractional-em widths,
;; zero-width characters = 0.
(define (ahem-char-width ch font-size)
  (cond
    [(or (char=? ch #\u200B) (char=? ch #\u200C)
         (char=? ch #\u200D) (char=? ch #\uFEFF))
     0]
    [(char=? ch #\u2000) (* 0.5 font-size)]    ;; en quad
    [(char=? ch #\u2001) font-size]             ;; em quad
    [(char=? ch #\u2002) (* 0.5 font-size)]    ;; en space
    [(char=? ch #\u2003) font-size]             ;; em space
    [(char=? ch #\u2004) (* 0.333 font-size)]  ;; three-per-em space
    [(char=? ch #\u2005) (* 0.25 font-size)]   ;; four-per-em space
    [(char=? ch #\u2006) (* 0.167 font-size)]  ;; six-per-em space
    [(char=? ch #\u2007) (* 0.5 font-size)]    ;; figure space
    [(char=? ch #\u2008) (* 0.333 font-size)]  ;; punctuation space
    [(char=? ch #\u2009) (* 0.2 font-size)]    ;; thin space
    [(char=? ch #\u200A) (* 0.1 font-size)]    ;; hair space
    [else font-size]))

(define (layout-text box avail)
  (match box
    [`(text ,id ,styles ,content ,measured-w)
     (define bm (extract-box-model styles))
     (define avail-w (avail-width->number (cadr avail)))

     ;; detect intrinsic sizing mode: min-content forces maximum wrapping (avail-w → 0)
     (define is-min-content? (eq? (cadr avail) 'av-min-content))
     ;; CSS 2.2 §16.6: white-space:nowrap prevents text from wrapping
     (define ws-val (get-style-prop styles 'white-space #f))
     (define is-nowrap? (eq? ws-val 'nowrap))
     (define effective-avail-w
       (cond
         [is-min-content? 0]
         [is-nowrap? #f]  ;; no width constraint → text won't wrap
         [else avail-w]))

     ;; detect font type: Ahem (Taffy tests) vs proportional (CSS2.1 tests)
     (define font-type (get-style-prop styles 'font-type 'ahem))
     (define is-proportional? (eq? font-type 'proportional))

     ;; font metrics depend on font type
     (define font-size
       (cond
         [is-proportional?
          ;; use actual font-size from styles (CSS default is 16px)
          (get-style-prop styles 'font-size CSS-DEFAULT-FONT-SIZE)]
         ;; check for explicit font-size in styles (e.g., CSS2.1 tests with non-default Ahem size)
         [else (get-style-prop styles 'font-size 10)]))

     ;; CSS: font-size ≤ 0 → text has zero dimensions (invisible)
     ;; font-size:0 / font-size:0px / font-size:0em all produce zero-size text
     (cond
       [(and (number? font-size) (<= font-size 0))
        (make-text-view id 0 0 0 0 content)]
       [else
        (layout-text-inner id styles content measured-w bm avail-w
                           effective-avail-w is-proportional? font-size)])]

    [_ (error 'layout-text "expected text box, got: ~a" box)]))

;; main text layout logic — extracted so layout-text can early-return for font-size:0
(define (layout-text-inner id styles content measured-w bm avail-w
                           effective-avail-w is-proportional? font-size)
     ;; detect font-metrics: 'times (serif normal-weight) or 'arial (sans-serif/bold)
     ;; set by reference-import based on font-family + font-weight
     (define font-metrics-val (get-style-prop styles 'font-metrics 'times))
     (define use-arial? (eq? font-metrics-val 'arial))
     ;; select line-height ratio from JSON-loaded font metrics
     ;; JSON metrics provide Chrome-compatible normal line-height calculation:
     ;;   max(hhea_ascender + |hhea_descender| + hhea_lineGap, win_ascent + win_descent) / unitsPerEm
     ;; This replaces the old hardcoded values (Times 1.107, Arial 1.15) which were
     ;; incorrect for Times (missed hhea.lineGap of 87/2048).
     (define proportional-lh-ratio (font-line-height-ratio font-metrics-val))
     ;; Chrome getClientRects() model for text rects:
     ;; - Text rect height = normal line-height (from font metrics), always
     ;; - Text rect y = 0 when line-height is normal (default)
     ;; - Text rect y = half-leading = (actual-lh - fs)/2 when explicit line-height
     ;; - Stacking/line-contribution uses the actual line-height
     ;; We compute: normal-lh (font metrics), actual-lh (with explicit override),
     ;;   text-view-h (= normal-lh), text-view-y (= half-leading or 0)
     (define normal-lh
       (if is-proportional?
           (chrome-mac-line-height font-metrics-val font-size)
           font-size))  ;; Ahem: normal-lh = font-size
     (define line-height  ;; actual line-height (may differ if explicit)
       (cond
         [is-proportional?
          (let ([lh-prop (get-style-prop styles 'line-height #f)])
            (if lh-prop
                (if (number? lh-prop) lh-prop normal-lh)
                normal-lh))]
         [else
          ;; Ahem: check for explicit line-height, default to font-size
          (let ([lh-prop (get-style-prop styles 'line-height #f)])
            (if (and lh-prop (number? lh-prop))
                lh-prop
                font-size))]))
     ;; half-leading: offset when explicit line-height differs from normal
     ;; CSS 2.2 §10.6.1: half-leading = (line-height - content-area) / 2
     ;; Chrome getClientRects(): text rect y = (actual-lh - normal-lh) / 2
     ;; where normal-lh is the font's natural line-height from metrics.
     (define has-explicit-lh?
       (let ([lh-prop (get-style-prop styles 'line-height #f)])
         (and lh-prop (number? lh-prop) (not (= lh-prop normal-lh)))))
     (define text-view-y
       (if has-explicit-lh?
           (/ (- line-height normal-lh) 2)
           0))
     (define text-view-h normal-lh)  ;; Chrome always reports normal-lh as text rect height
     ;; Per-character width function from JSON-loaded font metrics.
     ;; Uses full glyph advance width tables (2000+ glyphs) instead of
     ;; the old ~80-character hardcoded ratio tables, eliminating the
     ;; need for the crude -0.025 uppercase kerning hack on Times.
     (define json-char-width-fn (make-char-width-fn font-metrics-val))
     (define (char-width ch fs)
       (json-char-width-fn ch fs))
     ;; fallback category-based widths (used for Ahem font path)
     (define space-w-char (if is-proportional? (char-width #\space font-size) font-size))

     ;; CSS 2.2 §16.6: white-space:pre preserves newlines in source text.
     ;; check for pre-formatted text with explicit line breaks (newlines).
     ;; each newline forces a new line regardless of available width.
     (define has-newlines? (string-contains? content "\n"))

     (cond
       ;; pre-formatted text with newlines: split on \n to count lines
       [(and has-newlines? is-proportional?)
        (define lines (string-split content "\n" #:trim? #f))
        (define num-lines (length lines))
        ;; measure the width of each line
        (define max-line-w
          (for/fold ([mx 0]) ([line (in-list lines)])
            (define lw
              (for/fold ([total 0]) ([ch (in-string line)])
                (+ total (char-width ch font-size))))
            (max mx lw)))
        (define text-w max-line-w)
        ;; Chrome getClientRects() bounding box: y = half-leading (first line offset),
        ;; height spans from first-line top to last-line bottom:
        ;;   (n-1) * actual-lh + normal-lh
        (define text-h (+ (* (sub1 num-lines) line-height) text-view-h))
        (make-text-view id 0 text-view-y text-w text-h content)]

       ;; no wrapping needed: text fits or no constraint
       [(or (not effective-avail-w) (<= measured-w effective-avail-w))
        ;; Chrome getClientRects(): text rect height = normal-lh (font metrics),
        ;; y = half-leading when explicit line-height is set, else 0.
        (make-text-view id 0 text-view-y measured-w text-view-h content)]
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
           ;; Chrome getClientRects(): bounding box spans from first-line-top
           ;; (at half-leading) to last-line-bottom: (n-1)*actual-lh + normal-lh
           (define text-h (+ (* (sub1 num-lines) line-height) text-view-h))
           (make-text-view id 0 text-view-y text-w text-h content)]

          ;; Ahem font with newlines: handle pre-formatted line breaks
          [(and has-newlines? (not is-proportional?))
           (define phys-lines (string-split content "\n" #:trim? #f))
           (define num-lines (length phys-lines))
           (define max-line-w
             (for/fold ([mx 0]) ([line (in-list phys-lines)])
               (define lw
                 (for/sum ([ch (in-string line)])
                   (ahem-char-width ch font-size)))
               (max mx lw)))
           (define text-w max-line-w)
           (define text-h (+ (* (sub1 num-lines) line-height) text-view-h))
           (make-text-view id 0 text-view-y text-w text-h content)]

          ;; Ahem font: split on spaces first, then zero-width spaces
          ;; CSS 2.2 §16.6: normal white-space processing allows breaks at spaces
          [else
           (define space-split (string-split content " "))
           (define words
             (if (> (length space-split) 1)
                 space-split  ;; text has regular spaces → use as break points
                 (string-split content "\u200B")))  ;; fallback to ZWSP
           (define has-spaces? (> (length space-split) 1))
           ;; Ahem space character has width = font-size (same as any glyph)
           (define ahem-space-w (ahem-char-width #\space font-size))
           (define word-ws
             (for/list ([w (in-list words)])
               ;; use ahem-char-width for proper Unicode space handling
               (for/sum ([ch (in-string w)])
                 (ahem-char-width ch font-size))))
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
                  ;; when splitting on spaces, account for space width between words
                  (define new-line-w
                    (if (and has-spaces? (> line-w 0))
                        (+ line-w ahem-space-w ww)
                        (+ line-w ww)))
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
           ;; report actual content width (max line width), not container width
           (define text-w max-line-w)
           ;; text height: bounding box from first-line-top to last-line-bottom
           (define text-h (+ (* (sub1 num-lines) line-height) text-view-h))
           (make-text-view id 0 text-view-y text-w text-h content)])]))

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
