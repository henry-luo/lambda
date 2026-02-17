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
  (define avail `(avail (definite ,viewport-w) (definite ,viewport-h)))
  (define view (layout root-box avail))
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
      [else
       (box-model-margin-left root-bm)]))
  (set-view-pos view
                (+ body-pad-left base-x)
                (view-y view)))

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
         [(or (char=? ch #\space) (char=? ch #\u00A0)) 0.278]
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
         [(or (char=? ch #\space) (char=? ch #\u00A0)) 0.250]
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
        ;; CSS 2.2 §10.6.1: text rect = em-box spanning first to last line
        ;; height = (n-1)*lh + fs, y = half-leading
        (define half-leading (/ (- line-height font-size) 2))
        (define text-h (+ (* (sub1 num-lines) line-height) font-size))
        (make-text-view id 0 half-leading text-w text-h content)]

       ;; no wrapping needed: text fits or no constraint
       [(or (not effective-avail-w) (<= measured-w effective-avail-w))
        ;; CSS 2.2 §10.6.1: text rect height = font-size (em-box), y = half-leading.
        ;; Browser Range.getClientRects() returns em-box dimensions, not the full
        ;; line-box height. The half-leading offsets the text visually within the
        ;; line box. Stacking uses line-height (restored via layout-block stacking code).
        (let ([half-leading (/ (- line-height font-size) 2)])
          (make-text-view id 0 half-leading measured-w font-size content))]
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
           ;; CSS 2.2 §10.6.1: text rect = em-box spanning first to last line
           ;; height = (n-1)*lh + fs, y = half-leading
           (define half-leading (/ (- line-height font-size) 2))
           (define text-h (+ (* (sub1 num-lines) line-height) font-size))
           (make-text-view id 0 half-leading text-w text-h content)]

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
           (define half-leading (/ (- line-height font-size) 2))
           (define text-h (+ (* (sub1 num-lines) line-height) font-size))
           (make-text-view id 0 half-leading text-w text-h content)]

          ;; Ahem font: split on zero-width spaces
          [else
           (define words (string-split content "\u200B"))
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
           (make-text-view id 0 half-leading text-w text-h content)])]))

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

;; CSS 2.2 §17.5.2: compute auto-width for a table from column intrinsic sizes.
;; Examines all cells across all rows, tracks the maximum preferred width
;; per column, then returns the sum of column widths (capped by available width).
(define (compute-table-auto-width all-rows styles avail-w avail)
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
              (cell-preferred-width cs children avail)]
             [`(block ,cid ,cs ,children)
              (cell-preferred-width cs (if (list? children) children (list children)) avail)]
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

;; compute the preferred width of a single table cell.
;; if the cell has an explicit width, use it.
;; otherwise, lay out with shrink-to-fit to determine intrinsic width.
(define (cell-preferred-width cell-styles children avail)
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
     (define cell-view (layout cell-box `(avail av-max-content ,(caddr avail))))
     (view-width cell-view)]))

;; CSS 2.2 §17.6.1: Table layout with border-spacing support
;; border-spacing adds gaps between adjacent cells and at table edges.
;; In the separated borders model (border-collapse: separate, the default),
;; spacing is applied as: edge | cell | gap | cell | gap | cell | edge
(define (layout-table-simple box avail)
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
       (compute-table-auto-width all-rows styles avail-w avail))

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

     ;; CSS 2.2 §17.5.2.2: distribute available width among columns.
     ;; auto-col-widths has per-column preferred border-box widths from content.
     ;; If columns have preferred widths, distribute extra space proportionally.
     ;; For fixed layout or when auto-col-widths is empty, use uniform distribution.
     (define col-widths-list
       (cond
         ;; fixed layout: all columns share equally
         [(or is-fixed-layout? (null? auto-col-widths) (= num-columns 0))
          (define col-w (if (> num-columns 0) (/ cell-available-w num-columns) 0))
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
               ;; At the table level, floated blocks (blockified from table
               ;; internal types like table-column-group, table-column,
               ;; table-caption) act as block-level out-of-flow boxes.
               ;; In browser behavior, subsequent table rows start below
               ;; these floats, so we advance the y cursor.
               (define child-view (layout group `(avail av-max-content ,(caddr avail))))
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
               ;; Create a group view containing positioned column child views.
               ;; The group and children don't advance the y cursor.
               ;; CSS 2.2 §17.2.1: If a column-group contains no table-column
               ;; children, the element represents a single column.
               (define child-columns
                 (match group [`(block ,_ ,_ ,children) children] [_ '()]))
               (define span-count
                 (if (null? child-columns) 1 (length child-columns)))
               ;; compute per-child column widths and positions using col-widths-list
               (define child-col-views
                 (if (null? child-columns)
                     '()  ;; empty group = 1 implicit column, no child views
                     (for/list ([cc (in-list child-columns)]
                                [ci (in-naturals)])
                       (define cc-id
                         (match cc [`(block ,cid ,_ ,_) cid] [_ 'anon]))
                       (define actual-col-idx (+ col-counter ci))
                       (define ccw (if (< actual-col-idx (length col-widths-list))
                                       (list-ref col-widths-list actual-col-idx)
                                       0))
                       ;; x relative to group start: sum widths + spacing of preceding children
                       (define cc-x
                         (for/sum ([j (in-range ci)])
                           (define jcol (+ col-counter j))
                           (+ (if (< jcol (length col-widths-list))
                                  (list-ref col-widths-list jcol) 0)
                              bs-h)))
                       (make-view cc-id cc-x 0 ccw 0 '()))))
               ;; group x = offset from table edge: bs-h + sum of preceding column widths + spacing
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
               ;; CSS 2.2 §17.5.1: single table-column — positioned at column offset.
               ;; x = bs-h + sum of preceding column widths + spacing
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
               ;; Classify the group child:
               ;; - row-group: wrap rows in a group view
               ;; - direct row: lay out directly
               ;; - other block: lay out as block
               (define is-row-group?
                 (or (eq? group-display 'table-row-group)
                     (eq? group-display 'table-header-group)
                     (eq? group-display 'table-footer-group)))
               (define is-direct-row? (eq? group-display 'table-row))
               (cond
                 [is-row-group?
                  ;; CSS 2.2 §17.2: row-group wraps its contained rows.
                  ;; Extract rows from the group, layout them, then wrap
                  ;; in a group view to preserve the tree structure.
                  ;; CSS 2.2 §17.6.1: the row-group absorbs the border-spacing
                  ;; edge inset. Group x = offset-x + bs-h, group w = content-w - 2*bs-h.
                  ;; Rows within the group are at x=0 relative to the group.
                  (define inner-rows (extract-table-rows group))
                  (define group-w (max 0 (- content-w (* 2 bs-h))))
                  (if (null? inner-rows)
                      ;; empty row-group: skip
                      (loop (cdr groups) y acc mfb col-ids col-counter rg-ids dr-ids va-acc)
                      (let-values ([(row-views row-h group-cell-va)
                                    (layout-table-rows inner-rows group-w 0
                                                       0 0 avail bs-h bs-v
                                                       col-widths-list)])
                        ;; create group wrapper view containing the row views
                        ;; Group is inset by border-spacing from the table edge:
                        ;; x = offset-x + bs-h, y = offset-y + y + bs-v
                        ;; The leading bs-v is absorbed into the group position.
                        (define group-view
                          (make-view group-id (+ offset-x bs-h) (+ offset-y y bs-v)
                                     group-w row-h row-views))
                        ;; advance y past leading bs-v + group height
                        (loop (cdr groups) (+ y bs-v row-h)
                              (cons (cons src-idx group-view) acc) mfb
                              col-ids col-counter
                              (cons group-id rg-ids) dr-ids
                              (append group-cell-va va-acc))))]
                 [is-direct-row?
                  ;; single row directly in the table — apply edge inset here
                  ;; since there's no row-group to absorb it.
                  ;; Leading bs-v is absorbed into the row's starting y.
                  (define direct-row-w (max 0 (- content-w (* 2 bs-h))))
                  (define direct-start-y (+ y bs-v))
                  (define rows (list `(row ,group-id ,group-styles
                                           ,(match group [`(block ,_ ,_ ,c) c] [_ '()]))))
                  (define-values (row-views row-h direct-cell-va)
                    (layout-table-rows rows direct-row-w direct-start-y
                                       (+ offset-x bs-h) offset-y avail bs-h bs-v
                                       col-widths-list))
                  (define new-dr-ids
                    (append (map (lambda (rv) (view-id rv)) row-views) dr-ids))
                  ;; advance y past leading bs-v + row height
                  (loop (cdr groups) (+ y bs-v row-h)
                        (append (map (lambda (rv) (cons src-idx rv)) (reverse row-views)) acc) mfb
                        col-ids col-counter rg-ids new-dr-ids
                        (append direct-cell-va va-acc))]
                 [else
                  ;; non-table child (e.g. block, text): lay out as block
                  (define child-view (layout group `(avail (definite ,content-w) indefinite)))
                  (define ch (view-height child-view))
                  (define positioned (set-view-pos child-view offset-x (+ offset-y y)))
                  (loop (cdr groups) (+ y ch)
                        (cons (cons src-idx positioned) acc) mfb
                        col-ids col-counter rg-ids dr-ids va-acc)])])])))
     ;; Restore source order: sort indexed-views by source index,
     ;; then extract just the views. This ensures our view tree children
     ;; match the reference tree's source-order children while having
     ;; correct y positions from render-order layout.
     (define views
       (map cdr (sort indexed-views < #:key car)))

     ;; table establishes BFC: height includes float overflow
     ;; CSS 2.2 §17.6.1: add trailing bs-v after the last row in the table grid
     (define has-table-rows? (> (length all-rows) 0))
     (define total-with-trailing-spacing (if has-table-rows? (+ total-h bs-v) total-h))
     (define content-h-from-children (max total-with-trailing-spacing max-float-bottom))
     ;; CSS 2.2 §17.5.3: if the table has an explicit height, use the maximum
     ;; of the computed content height and the explicit height.
     ;; Pass containing block height so percentage heights resolve correctly.
     (define containing-h (avail-height->number (caddr avail)))
     (define explicit-h (resolve-block-height styles containing-h avail-w))
     (define final-h
       (if explicit-h
           (max content-h-from-children explicit-h)
           content-h-from-children))
     ;; CSS 2.2 §17.5.3: if the table's explicit height exceeds the sum of
     ;; row heights, distribute the extra height to the rows.
     ;; Rows may be direct children or nested inside row-groups.
     ;; Count all rows (including those inside groups) for distribution.
     (define total-row-count (length all-rows))
     (define extra-height (max 0 (- final-h content-h-from-children)))

     ;; Helper: stretch a cell to match the new row height, applying VA.
     ;; Uses cell-va-map to look up the correct vertical-align for each cell.
     ;; For va-middle: re-center content in the expanded cell.
     ;; For va-baseline/va-top/default: content stays at top (grow at bottom).
     ;; For va-bottom: content moves to bottom.
     (define (stretch-cell-in-row cell new-row-h)
       (if (or (not (pair? cell)) (eq? (car cell) 'view-text))
           cell  ;; not a cell view, pass through
           (let* ([cell-id (view-id cell)]
                  [old-h (view-height cell)]
                  [va (let ([pair (assoc cell-id cell-va-map)])
                        (if pair (cdr pair) 'va-baseline))]
                  [extra (- new-row-h old-h)]
                  [cell-kids (view-children cell)])
             (if (or (<= extra 0) (not (list? cell-kids)))
                 ;; no extra height or no children to shift
                 (make-view cell-id (view-x cell) (view-y cell)
                            (view-width cell) new-row-h cell-kids)
                 ;; apply VA-based offset to children
                 (let* ([child-offset
                         (cond
                           [(eq? va 'va-top) 0]
                           [(eq? va 'va-bottom) extra]
                           [(eq? va 'va-middle) (/ extra 2)]
                           [else 0])]  ;; baseline = top
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
           ;; distribute extra height equally among ALL rows
           (let ([per-row (/ extra-height total-row-count)])
             ;; Walk the view tree:
             ;; - row-group views: stretch contained rows, expand group height
             ;; - direct row views: stretch the row itself
             ;; - other views: shift y by accumulated shift
             (define y-shift 0)
             (for/list ([v (in-list views)])
               (cond
                 ;; row-group: stretch inner rows and expand group
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
                 ;; direct row: stretch the row itself
                 [(member (view-id v) direct-row-ids)
                  (define stretched (stretch-row v per-row y-shift))
                  (set! y-shift (+ y-shift per-row))
                  stretched]
                 [else
                  ;; non-row view: shift y by accumulated shift
                  (if (> y-shift 0)
                      (make-view (view-id v) (view-x v) (+ (view-y v) y-shift)
                                 (view-width v) (view-height v) (view-children v))
                      v)])))
           views))
     ;; CSS 2.2 §17.5.1: table-column and table-column-group elements don't generate
     ;; visible boxes, but browsers report them with dimensions matching the table's
     ;; row area. Post-process to set their height to the total table content height.
     (define adjusted-views-final
       (if (null? column-view-ids)
           adjusted-row-views
           (for/list ([v (in-list adjusted-row-views)])
             (if (member (view-id v) column-view-ids)
                 ;; update the column/column-group view height, and also
                 ;; update any child column view heights (for column-groups)
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

(define (extract-table-rows group)
  (match group
    [`(row-group ,_ ,_ (,rows ...)) rows]
    [`(row ,_ ,_ ,_) (list group)]
    ;; CSS 2.2 §17: inner table elements stored as (block ...) with display style.
    ;; Match block boxes whose display is table-row or table-row-group.
    [`(block ,id ,styles ,children)
     (define disp (get-style-prop styles 'display #f))
     (cond
       [(or (eq? disp 'table-row-group)
            (eq? disp 'table-header-group)
            (eq? disp 'table-footer-group))
        ;; row-group: extract rows from children
        (apply append (for/list ([c children]) (extract-table-rows c)))]
       [(eq? disp 'table-row)
        ;; single row: wrap cells as a synthetic row
        (list `(row ,id ,styles ,children))]
       [else '()])]
    [_ '()]))

(define (layout-table-rows rows content-w y offset-x offset-y avail
                            [bs-h 0] [bs-v 0] [col-widths-list '()])
  ;; CSS 2.2 §17.6.1: border-spacing in the separated borders model.
  ;; This function handles BETWEEN-cell and BETWEEN-row spacing only.
  ;; Edge spacing (inset from table/group edges) is handled by the caller.
  ;; Row position: x = offset-x, width = content-w (caller provides inset values)
  ;; Cell widths come from col-widths-list when available (per-column proportional).
  ;; Cell x within row = sum of preceding column widths + spacing gaps.
  ;; Vertical layout: row1 [bs-v] row2 [bs-v] row3 ...
  ;; bs-v is added BETWEEN rows only (not before first or after last).
  ;; Leading/trailing bs-v is handled by the caller (group or table level).
  ;; Returns: (values views total-height cell-va-map)
  ;; where cell-va-map is a list of (cell-id . va-value) for height distribution.
  (define row-x offset-x)
  (define row-w content-w)
  (let loop ([remaining rows]
             [current-y y]
             [views '()]
             [first? #t]
             [va-map '()])  ;; accumulate (cell-id . va-value) pairs
    (cond
      [(null? remaining)
       (values (reverse views) (- current-y y) va-map)]
      [else
       (match (car remaining)
         [`(row ,row-id ,row-styles (,cells ...))
          ;; add bs-v spacing BETWEEN rows (not before first row)
          (define row-top-y (if first? current-y (+ current-y bs-v)))
          ;; separate floated children (blockified table-cell) from regular cells
          (define-values (float-cells regular-cells)
            (partition
             (lambda (cell)
               (match cell
                 [`(block ,_ ,s ,_)
                  (let ([fv (get-style-prop s 'float #f)])
                    (and fv (not (eq? fv 'float-none))))]
                 [_ #f]))
             cells))
          (define num-cells (length regular-cells))
          ;; CSS 2.2 §17.5.2.2: use per-column widths when available.
          ;; col-widths-list contains border-box widths for each column.
          ;; If not available (empty list), fall back to uniform distribution.
          (define use-per-col? (and (not (null? col-widths-list))
                                    (>= (length col-widths-list) num-cells)))
          ;; Uniform fallback width (for when no per-column data)
          (define between-cell-spacing (* (max 0 (- num-cells 1)) bs-h))
          (define uniform-cell-w (if (> num-cells 0)
                            (/ (max 0 (- row-w between-cell-spacing)) num-cells)
                            row-w))
          ;; Helper: get width for column index
          (define (get-col-w col)
            (if use-per-col?
                (list-ref col-widths-list col)
                uniform-cell-w))
          ;; Helper: get x position for column index (sum of preceding widths + spacing)
          (define (get-col-x col)
            (if use-per-col?
                (for/sum ([j (in-range col)])
                  (+ (list-ref col-widths-list j) bs-h))
                (* col (+ uniform-cell-w bs-h))))
          (define row-h 0)
          ;; lay out regular table cells
          (define cell-views
            (for/list ([cell (in-list regular-cells)]
                       [col (in-naturals)])
              (define cell-x (get-col-x col))
              (match cell
                [`(cell ,cell-id ,cell-styles ,colspan (,children ...))
                 ;; colspan: sum widths of spanned columns + (colspan-1) spacing gaps
                 (define cw
                   (if use-per-col?
                       (let ([span (max 1 colspan)])
                         (+ (for/sum ([j (in-range col (min (+ col span) (length col-widths-list)))])
                              (list-ref col-widths-list j))
                            (* (max 0 (- span 1)) bs-h)))
                       (* uniform-cell-w (max 1 colspan))))
                 (define cell-box `(block ,cell-id ,cell-styles (,@children)))
                 (define cell-avail `(avail (definite ,cw) indefinite))
                 (define cell-view (layout cell-box cell-avail))
                 (define ch (view-height cell-view))
                 (set! row-h (max row-h ch))
                 (set-view-pos cell-view cell-x 0)]
                ;; block child acting as cell (e.g. display:table-cell without float)
                [`(block ,cell-id ,cell-styles ,children)
                 (define cw (get-col-w col))
                 (define cell-avail `(avail (definite ,cw) indefinite))
                 (define cell-view (layout cell cell-avail))
                 (define ch (view-height cell-view))
                 (set! row-h (max row-h ch))
                 (set-view-pos cell-view cell-x 0)]
                [_ (make-empty-view 'cell)])))
          ;; lay out floated children (blockified from table-cell/etc.)
          ;; Float positions are relative to the row.
          (define float-views
            (for/list ([fc (in-list float-cells)])
              (match fc
                [`(block ,fc-id ,fc-styles ,fc-children)
                 (define fc-view (layout fc `(avail av-max-content ,(caddr avail))))
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
          ;; create row view containing all cell and float views
          ;; CSS 2.2 §17.5.3: row height is the maximum of:
          ;; 1. content height from cells
          ;; 2. the row's explicit height property (treated as minimum)
          (define row-explicit-h (resolve-block-height row-styles #f row-w))
          (define final-row-h
            (if row-explicit-h (max row-h row-explicit-h) row-h))
          ;; CSS 2.2 §17.5.4: stretch cells to match row height
          ;; and apply vertical alignment within cells.
          ;; Default vertical-align for table-cell is 'baseline' (CSS initial).
          ;; UA stylesheet sets 'va-middle' for <td>/<th> elements.
          ;; The cell view already includes its padding/border from layout.
          ;; Vertical alignment considers the cell's CONTENT AREA:
          ;; content-area-h = cell-height - padding-top - padding-bottom - border-top - border-bottom
          ;; Content text height = content-area-h (from actual content).
          ;; When the row is taller than the cell, extra space is added to
          ;; the content area, and content is aligned within it.
          ;; Collect per-cell VA values for height distribution re-application.
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
              ;; text views don't need stretching/alignment
              (if is-text-view?
                  cv
                  (let ()
              (define cell-view-h (view-height cv))
              ;; Cell actual height matches row height
              (define cell-actual-h final-row-h)
              (define cell-children (view-children cv))
              ;; Get the cell's padding/border to compute content area
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
              ;; Content area in the original cell
              (define orig-content-area (max 0 (- cell-view-h vert-chrome)))
              ;; Content area after stretching to row height
              (define new-content-area (max 0 (- cell-actual-h vert-chrome)))
              ;; Actual content height from children layout
              (define actual-content-h
                (if (list? cell-children)
                    (let ([content-bottom
                           (apply max 0
                                  (for/list ([c (in-list cell-children)])
                                    (+ (view-y c) (view-height c))))])
                      (max 0 (- content-bottom (+ bt pt))))
                    orig-content-area))
              ;; Vertical alignment applies when new-content-area > actual-content-h
              (define needs-alignment? (> new-content-area actual-content-h))
              (if needs-alignment?
                  ;; apply vertical alignment within content area
                  ;; CSS initial value for vertical-align is 'baseline'. The UA
                  ;; stylesheet sets 'middle' for <td>/<th> which is captured
                  ;; in computed styles as 'va-middle'.
                  (let* ([va (get-style-prop cell-styles-for-va 'vertical-align 'va-baseline)]
                         [extra (- new-content-area actual-content-h)]
                         [child-offset-y
                          (cond
                            [(eq? va 'va-top) 0]
                            [(eq? va 'va-bottom) extra]
                            [(eq? va 'va-middle) (/ extra 2)]
                            [else 0])]  ;; baseline = content at top (default)
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
                  ;; no alignment needed, just set cell height to row height
                  (if (not (= cell-actual-h cell-view-h))
                      (make-view (view-id cv) (view-x cv) (view-y cv)
                                 (view-width cv) cell-actual-h cell-children)
                      cv))))))
          (define row-view
            (make-view row-id row-x (+ offset-y row-top-y)
                       row-w final-row-h
                       (append stretched-cell-views float-views)))
          ;; advance: current-y moves past the spacing + row height
          (loop (cdr remaining)
                (+ row-top-y final-row-h)
                (cons row-view views)
                #f
                (append cell-va-pairs va-map))]
         [_ (loop (cdr remaining) current-y views first? va-map)])])))

;; helper: partition a list into two based on predicate
(define (partition pred lst)
  (let loop ([lst lst] [yes '()] [no '()])
    (cond
      [(null? lst) (values (reverse yes) (reverse no))]
      [(pred (car lst)) (loop (cdr lst) (cons (car lst) yes) no)]
      [else (loop (cdr lst) yes (cons (car lst) no))])))

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
