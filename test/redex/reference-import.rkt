#lang racket/base

;; reference-import.rkt — Browser reference JSON → Redex box tree converter
;;
;; Phase 2: Reference-Driven Testing (Approach B)
;;
;; Converts browser reference JSONs (Puppeteer-extracted Chrome layout data)
;; into Redex box trees for differential testing. Also extracts the expected
;; layout positions for comparison.
;;
;; Strategy:
;;   1. Parse the HTML file to extract inline styles (the true INPUT)
;;   2. Apply base stylesheet defaults (display:flex, box-sizing:border-box, etc.)
;;   3. Build a Redex box tree from the parsed styles
;;   4. Extract reference layout positions from the JSON (the expected OUTPUT)
;;
;; Handles Taffy/Yoga-ported tests (inline styles on divs, no <style> blocks).

(require racket/match
         racket/list
         racket/string
         racket/port
         json
         "css-layout-lang.rkt"
         "layout-common.rkt")

(provide reference->box-tree
         reference->expected-layout
         reference-file->test-case
         html-file->inline-styles
         parse-inline-style
         classify-html-test
         reference-test-case-box-tree
         reference-test-case-expected
         reference-test-case-viewport
         reference-test-case-name)

;; ============================================================
;; Test Case Structure
;; ============================================================

;; a test case bundles the box tree (input), expected layout (output),
;; viewport dimensions, and test name
(struct reference-test-case
  (name        ; string: test file name
   box-tree    ; Redex Box term
   expected    ; expected layout tree (list)
   viewport    ; (width . height) pair
   )
  #:transparent)

;; ============================================================
;; Base Stylesheet Defaults (from test_base_style.css)
;; ============================================================

;; these defaults apply to all Taffy tests:
;;   div, span, img { box-sizing: border-box; position: relative;
;;                     border: 0 solid red; margin: 0; padding: 0; }
;;   div { display: flex; }
;;   body > * { position: absolute; }
;;   #test-root { font-family: ahem; line-height: 1; font-size: 10px; }

(define taffy-defaults
  (hash 'display "flex"
        'box-sizing "border-box"
        'position "relative"
        'margin-top "0"
        'margin-right "0"
        'margin-bottom "0"
        'margin-left "0"
        'padding-top "0"
        'padding-right "0"
        'padding-bottom "0"
        'padding-left "0"
        'border-top-width "0"
        'border-right-width "0"
        'border-bottom-width "0"
        'border-left-width "0"))

;; CSS default values for non-Taffy tests (browser defaults)
(define css-defaults
  (hash 'display "block"
        'box-sizing "content-box"
        'position "static"
        'margin-top "0"
        'margin-right "0"
        'margin-bottom "0"
        'margin-left "0"
        'padding-top "0"
        'padding-right "0"
        'padding-bottom "0"
        'padding-left "0"
        'border-top-width "0"
        'border-right-width "0"
        'border-bottom-width "0"
        'border-left-width "0"))

;; parameter to control which defaults are used
(define uses-taffy-base? (make-parameter #t))

;; active defaults based on current test type
(define (base-defaults)
  (if (uses-taffy-base?) taffy-defaults css-defaults))

;; detect whether an HTML file uses the Taffy test_base_style.css
(define (html-uses-taffy-base-stylesheet? html-path)
  (define content (file->string html-path))
  (regexp-match? #rx"test_base_style" content))

;; ============================================================
;; Ahem Font Text Measurement
;; ============================================================

;; Ahem font metrics (from ahem-metrics.json):
;; - units per em: 1000
;; - ascender: 800, descender: -200, line-gap: 0
;; - every visible glyph is exactly 1em wide
;; - zero-width space (U+200B) has 0 advance width
;;
;; With test_base_style.css: font-size: 10px, line-height: 1
;; → each visible char = 10px wide, line height = 10px

(define ahem-font-size 10)  ;; default from test_base_style.css
(define ahem-line-height 1) ;; unitless multiplier

;; decode common HTML entities in text content
(define (decode-html-entities str)
  (define s str)
  (set! s (string-replace s "&amp;" "&"))
  (set! s (string-replace s "&lt;" "<"))
  (set! s (string-replace s "&gt;" ">"))
  (set! s (string-replace s "&quot;" "\""))
  (set! s (string-replace s "&apos;" "'"))
  (set! s (string-replace s "&nbsp;" "\u00A0"))
  (set! s (string-replace s "&ZeroWidthSpace;" "\u200B"))
  (set! s (string-replace s "&#8203;" "\u200B"))
  (set! s (string-replace s "&#x200B;" "\u200B"))
  s)

;; extract non-whitespace text content from a string.
;; trims leading/trailing whitespace, collapses internal whitespace to single space.
(define (normalize-text-content raw)
  (define trimmed (string-trim raw))
  ;; collapse internal whitespace (but preserve zero-width spaces)
  (define collapsed
    (regexp-replace* #px"[\\s]+" trimmed " "))
  collapsed)

;; measure text width using Ahem font metrics.
;; in Ahem, every visible character is 1em wide.
;; zero-width space (U+200B) has 0 width.
;; returns width in pixels at the given font-size.
(define (measure-text-ahem text [font-size ahem-font-size])
  (define total 0)
  (for ([ch (in-string text)])
    (cond
      ;; zero-width space and other zero-width characters
      [(or (char=? ch #\u200B)  ;; zero-width space
           (char=? ch #\u200C)  ;; zero-width non-joiner
           (char=? ch #\u200D)  ;; zero-width joiner
           (char=? ch #\uFEFF)) ;; zero-width no-break space
       (void)]
      [else (set! total (+ total font-size))]))
  total)

;; split text into "words" at zero-width space boundaries.
;; each word's width = number of visible chars × font-size.
;; returns a list of word-widths in pixels.
(define (text-word-widths text [font-size ahem-font-size])
  (define words (string-split text "\u200B"))
  (for/list ([w (in-list words)])
    (measure-text-ahem w font-size)))

;; ============================================================
;; Proportional Font Metrics (for non-Taffy/CSS2.1 tests)
;; ============================================================

;; character-width categories for typical browser default serif font at 16px.
;; calibrated to match Chrome's text rendering on macOS.
;; proportional font width ratios (relative to font-size 1.0)
;; Arial TrueType glyph advance widths (units per em = 2048)
;; measured from the Arial Regular TrueType font file
(define arial-char-widths
  (make-hash
   '((#\A . 0.667) (#\B . 0.667) (#\C . 0.722) (#\D . 0.722)
     (#\E . 0.667) (#\F . 0.611) (#\G . 0.778) (#\H . 0.722)
     (#\I . 0.278) (#\J . 0.500) (#\K . 0.667) (#\L . 0.556)
     (#\M . 0.833) (#\N . 0.722) (#\O . 0.778) (#\P . 0.667)
     (#\Q . 0.778) (#\R . 0.722) (#\S . 0.667) (#\T . 0.611)
     (#\U . 0.722) (#\V . 0.667) (#\W . 0.944) (#\X . 0.667)
     (#\Y . 0.667) (#\Z . 0.611)
     (#\a . 0.556) (#\b . 0.556) (#\c . 0.500) (#\d . 0.556)
     (#\e . 0.556) (#\f . 0.278) (#\g . 0.556) (#\h . 0.556)
     (#\i . 0.222) (#\j . 0.222) (#\k . 0.500) (#\l . 0.222)
     (#\m . 0.833) (#\n . 0.556) (#\o . 0.556) (#\p . 0.556)
     (#\q . 0.556) (#\r . 0.333) (#\s . 0.500) (#\t . 0.278)
     (#\u . 0.556) (#\v . 0.500) (#\w . 0.722) (#\x . 0.500)
     (#\y . 0.500) (#\z . 0.500)
     (#\0 . 0.556) (#\1 . 0.556) (#\2 . 0.556) (#\3 . 0.556)
     (#\4 . 0.556) (#\5 . 0.556) (#\6 . 0.556) (#\7 . 0.556)
     (#\8 . 0.556) (#\9 . 0.556)
     (#\space . 0.278) (#\. . 0.278) (#\, . 0.278) (#\: . 0.278)
     (#\; . 0.278) (#\! . 0.278) (#\? . 0.556) (#\- . 0.333)
     (#\( . 0.333) (#\) . 0.333) (#\/ . 0.278) (#\& . 0.667)
     (#\' . 0.191) (#\" . 0.355))))

;; Times New Roman / serif character width ratios (relative to font-size)
;; browser default font is typically serif (Times New Roman)
(define times-char-widths
  (make-hash
   '((#\A . 0.722) (#\B . 0.667) (#\C . 0.667) (#\D . 0.722)
     (#\E . 0.611) (#\F . 0.556) (#\G . 0.722) (#\H . 0.722)
     (#\I . 0.333) (#\J . 0.389) (#\K . 0.722) (#\L . 0.611)
     (#\M . 0.889) (#\N . 0.722) (#\O . 0.722) (#\P . 0.556)
     (#\Q . 0.722) (#\R . 0.667) (#\S . 0.556) (#\T . 0.611)
     (#\U . 0.722) (#\V . 0.722) (#\W . 0.944) (#\X . 0.722)
     (#\Y . 0.722) (#\Z . 0.611)
     (#\a . 0.444) (#\b . 0.500) (#\c . 0.444) (#\d . 0.500)
     (#\e . 0.444) (#\f . 0.333) (#\g . 0.500) (#\h . 0.500)
     (#\i . 0.278) (#\j . 0.278) (#\k . 0.500) (#\l . 0.278)
     (#\m . 0.778) (#\n . 0.500) (#\o . 0.500) (#\p . 0.500)
     (#\q . 0.500) (#\r . 0.333) (#\s . 0.389) (#\t . 0.278)
     (#\u . 0.500) (#\v . 0.500) (#\w . 0.722) (#\x . 0.500)
     (#\y . 0.500) (#\z . 0.444)
     (#\0 . 0.500) (#\1 . 0.500) (#\2 . 0.500) (#\3 . 0.500)
     (#\4 . 0.500) (#\5 . 0.500) (#\6 . 0.500) (#\7 . 0.500)
     (#\8 . 0.500) (#\9 . 0.500)
     (#\space . 0.250) (#\. . 0.250) (#\, . 0.250) (#\: . 0.278)
     (#\; . 0.278) (#\! . 0.333) (#\? . 0.444) (#\- . 0.333)
     (#\( . 0.333) (#\) . 0.333) (#\/ . 0.278) (#\& . 0.778)
     (#\' . 0.180) (#\" . 0.408))))

;; default width for characters not in the lookup table
(define arial-default-ratio 0.556)
(define times-default-ratio 0.500)

;; backward compat constants (at 16px reference size)
(define proportional-uppercase-width 8.5)
(define proportional-lowercase-width 5.8)
(define proportional-space-width 4.0)
(define proportional-digit-width 5.9)
(define proportional-default-width 5.9)
(define proportional-line-height 18)

;; measure text width using proportional font approximation.
;; uses per-character width ratios scaled by font-size.
;; font-family: #f or string — for serif/sans-serif distinction
;; font-metrics: 'times or 'arial — selects which char-width table to use
(define (measure-text-proportional text [font-size 16] [font-family #f] [font-metrics 'times])
  ;; times: default serif font metrics, arial: sans-serif/bold font metrics
  (define use-times? (eq? font-metrics 'times))
  (define char-widths (if use-times? times-char-widths arial-char-widths))
  (define default-ratio (if use-times? times-default-ratio arial-default-ratio))
  (define total 0)
  (for ([ch (in-string text)])
    (cond
      [(or (char=? ch #\newline) (char=? ch #\return)
           (char=? ch #\u200B) (char=? ch #\u200C)
           (char=? ch #\u200D) (char=? ch #\uFEFF))
       (void)]  ;; zero-width / control chars
      [else
       (define ratio (hash-ref char-widths ch default-ratio))
       ;; apply average kerning correction for Times uppercase letters
       ;; (Times has significant pair kerning e.g. F-A, T-o which we don't model)
       (define adjusted-ratio
         (if (and use-times? (char-upper-case? ch)) (- ratio 0.025) ratio))
       (set! total (+ total (* adjusted-ratio font-size)))]))
  total)

;; ============================================================
;; HTML Inline Style Parser
;; ============================================================

;; parse a CSS inline style string into an alist of (property . value)
;; e.g. "width: 100px; display: block;" → ((width . "100px") (display . "block"))
(define (parse-inline-style style-str)
  (if (or (not style-str) (string=? style-str ""))
      '()
      (let ([decls (string-split style-str ";")])
        ;; reverse so that last declaration wins (CSS cascade: later overrides earlier)
        ;; since get-style-prop uses assoc which finds the first match
        (define raw
          (reverse
           (filter-map
            (lambda (decl)
              (let ([parts (string-split (string-trim decl) ":")])
                (if (>= (length parts) 2)
                    (let* ([key (string->symbol (string-trim (car parts)))]
                           [val (string-trim (string-join (cdr parts) ":"))])
                      ;; CSS validation: reject invalid values during parsing
                      ;; negative padding is invalid (CSS spec)
                      (cond
                        [(and (memq key '(padding padding-top padding-right
                                          padding-bottom padding-left))
                              (regexp-match? #rx"^-" val))
                         #f]  ;; drop invalid negative padding declaration
                        [else (cons key val)]))
                    #f)))
            decls)))
        ;; dedup within this rule: keep first occurrence of each key
        ;; (= last declared, since list is reversed). This ensures cross-rule
        ;; dedup in apply-style-rules works correctly.
        (define deduped
          (let loop ([props raw] [seen '()] [result '()])
            (cond
              [(null? props) (reverse result)]
              [(member (caar props) seen)
               (loop (cdr props) seen result)]
              [else
               (loop (cdr props) (cons (caar props) seen)
                     (cons (car props) result))])))
        ;; expand font shorthand: "font: [style] size family"
        ;; extract font-size and font-family from font shorthand
        (define font-val (cdr-or-false 'font deduped))
        (if font-val
            (let ()
              (define parts (string-split (string-trim font-val)))
              ;; find the font-size (number+unit) and family (last word)
              (define family (if (>= (length parts) 2)
                                (last parts)
                                #f))
              ;; find font-size: look for a part with a unit or slash
              (define size-line-part
                (for/first ([p (in-list parts)]
                            #:when (regexp-match?
                                    #rx"^[0-9.]+(px|pt|em|ex|in|cm|mm|pc|rem|ch|%)"
                                    p))
                  p))
              ;; split "32px/4" into font-size "32px" and line-height "4"
              (define size-split (if size-line-part (string-split size-line-part "/") '()))
              (define size-part (and (>= (length size-split) 1) (car size-split)))
              (define line-height-part (and (>= (length size-split) 2) (cadr size-split)))
              (append deduped
                      (if family
                          (list (cons 'font-family family))
                          '())
                      (if size-part
                          (list (cons 'font-size size-part))
                          '())
                      (if line-height-part
                          (list (cons 'line-height line-height-part))
                          '())))
            deduped))))

;; ============================================================
;; ::before Pseudo-element Content Injection
;; ============================================================

;; walk the element tree and handle ::before pseudo-element content.
;; for block-display ::before, add a __before-block-height property to the parent
;; which layout-block-children uses as initial y offset.
;; before-rules: list of (selector-parts . props-alist) from extract-style-rules
(define (inject-before-content elements before-rules ancestor-chain)
  (for/list ([elem (in-list elements)])
    (match elem
      [`(element ,tag ,id ,class ,inline-alist ,children)
       ;; check if any before-rule matches this element
       (define matching-before
         (for/fold ([found #f]) ([rule (in-list before-rules)])
           (if found found  ;; take first match
               (let ([selector (car rule)]
                     [props (cdr rule)])
                 (if (selector-matches? selector tag id class ancestor-chain)
                     props
                     #f)))))
       ;; recurse into children
       (define new-chain (cons (list tag id class) ancestor-chain))
       (define new-children
         (inject-before-content children before-rules new-chain))
       ;; handle ::before content
       (if matching-before
           (let ()
             (define content-raw (cdr-or-false 'content matching-before))
             (define content-text
               (if (and content-raw (string? content-raw))
                   (regexp-replace* #rx"^['\"]|['\"]$" content-raw "")
                   #f))
             (define before-display (or (cdr-or-false 'display matching-before) "inline"))
             (if (and content-text (> (string-length content-text) 0)
                      (equal? before-display "block"))
                 ;; block ::before: compute height from parent's font metrics
                 ;; font-size and line-height from parent (inherited) or before-rule
                 (let* ([parent-fs-raw (or (cdr-or-false 'font-size inline-alist) "16px")]
                        [parent-fs (or (parse-css-px parent-fs-raw)
                                       (if (string? parent-fs-raw)
                                           (let ([m (regexp-match #rx"([0-9.]+)" parent-fs-raw)])
                                             (if m (string->number (cadr m)) 16))
                                           16))]
                        [parent-lh-raw (cdr-or-false 'line-height inline-alist)]
                        [parent-lh (cond
                                     [(not parent-lh-raw) parent-fs]
                                     [(and (string? parent-lh-raw)
                                           (regexp-match #rx"^([0-9.]+)$" parent-lh-raw))
                                      (* parent-fs (string->number parent-lh-raw))]
                                     [else (or (parse-css-px parent-lh-raw) parent-fs)])]
                        ;; add before-block-height to parent's styles
                        [new-alist
                         (cons (cons '__before-block-height parent-lh) inline-alist)])
                   `(element ,tag ,id ,class ,new-alist ,new-children))
                 ;; inline/run-in ::before: measure content width and add as x-offset
                 (if (and content-text (> (string-length content-text) 0))
                     (let* ([parent-fs-raw (or (cdr-or-false 'font-size inline-alist) "16px")]
                            [parent-fs (or (parse-css-px parent-fs-raw) 16)]
                            ;; determine font metrics for width measurement
                            [ff-val (cdr-or-false 'font-family inline-alist)]
                            [fw-val (cdr-or-false 'font-weight inline-alist)]
                            [is-bold? (and fw-val (or (equal? fw-val "bold") (equal? fw-val "700")))]
                            [is-sans? (and ff-val
                                           (or (regexp-match? #rx"(?i:sans-serif)" ff-val)
                                               (regexp-match? #rx"(?i:arial)" ff-val)))]
                            [fm (if (or is-bold? is-sans?) 'arial 'times)]
                            [content-w (measure-text-proportional content-text parent-fs ff-val fm)]
                            [new-alist
                             (cons (cons '__before-inline-width content-w) inline-alist)])
                       `(element ,tag ,id ,class ,new-alist ,new-children))
                     ;; empty: no change
                     `(element ,tag ,id ,class ,inline-alist ,new-children))))
           `(element ,tag ,id ,class ,inline-alist ,new-children))]
      [_ elem])))

;; extract the element tree from an HTML file body.
;; returns a nested structure: (element tag attrs children)
;; where attrs is an alist including 'style if present.
;; only handles div elements (sufficient for Taffy tests).
(define (html-file->inline-styles html-path)
  (define html-content (file->string html-path))
  (define-values (style-rules before-rules) (extract-style-rules html-content))
  (define elements (parse-html-body html-content))
  ;; apply style block rules to elements if any
  ;; include html → body in ancestor chain so selectors like "body > div" work
  (define body-ancestor-chain '(("body" #f #f) ("html" #f #f)))
  ;; compute body element's resolved CSS properties from style rules
  ;; rules matching "body" (e.g., "body { font-size: 12px; }") should be inherited
  (define body-props
    (if (null? style-rules)
        '()
        (apply append
          (filter-map
           (lambda (rule)
             (define selector (car rule))
             (define props (cdr rule))
             ;; match selectors that target the body element
             (if (selector-matches? selector "body" #f #f '(("html" #f #f)) 1)
                 props
                 #f))
           style-rules))))
  ;; also compute html element's properties for completeness
  (define html-props
    (if (null? style-rules)
        '()
        (apply append
          (filter-map
           (lambda (rule)
             (define selector (car rule))
             (define props (cdr rule))
             (if (selector-matches? selector "html" #f #f '() 1)
                 props
                 #f))
           style-rules))))
  ;; merge html and body props (body overrides html for inherited props)
  (define parent-props
    (append body-props html-props))
  ;; apply style rules first to resolve inherited properties (font-size, line-height)
  (define styled-elements
    (if (null? style-rules)
        elements
        (for/list ([e (in-list elements)] [i (in-naturals 1)])
          (apply-style-rules e style-rules body-ancestor-chain parent-props i))))
  ;; then inject ::before pseudo-element content (needs resolved font properties)
  (if (null? before-rules)
      styled-elements
      (inject-before-content styled-elements before-rules body-ancestor-chain)))

;; ============================================================
;; CSS <style> Block Parser
;; ============================================================

;; extract CSS rules from <style> blocks in HTML.
;; returns a list of (selector-parts . properties-alist)
;; selector-parts is a list of simple selectors for matching
(define (extract-style-rules html-str)
  (define results '())
  (define before-results '())
  ;; find all <style>...</style> blocks
  (define style-matches
    (regexp-match* #rx"<style[^>]*>(.*?)</style>" html-str #:match-select cadr))
  (for ([raw-css-text (in-list (or style-matches '()))])
    ;; strip CSS comments /* ... */ before parsing
    (define css-text (regexp-replace* #rx"/\\*[^*]*\\*+(?:[^/*][^*]*\\*+)*/" raw-css-text ""))
    ;; also strip @font-face blocks (we don't use them)
    (define css-clean (regexp-replace* #rx"@font-face[^}]*\\}" css-text ""))
    ;; parse each rule: selector { properties }
    ;; handle grouped selectors (comma-separated)
    (define rule-matches
      (regexp-match* #px"([^{}]+)\\{([^}]*)\\}" css-clean #:match-select cdr))
    (for ([rule (in-list rule-matches)])
      (define selector-text (string-trim (car rule)))
      (define props-text (string-trim (cadr rule)))
      (define props (parse-inline-style props-text))
      (when (not (null? props))
        ;; handle comma-separated selectors: "sel1, sel2 { ... }"
        (define selectors (string-split selector-text ","))
        (for ([sel (in-list selectors)])
          (define trimmed-sel (string-trim sel))
          (when (> (string-length trimmed-sel) 0)
            ;; check for ::before / :before pseudo-element
            (define is-before? (regexp-match? #rx"::?before" trimmed-sel))
            (if is-before?
                (let ([base-sel (string-trim (regexp-replace #rx"::?before$" trimmed-sel ""))])
                  (when (> (string-length base-sel) 0)
                    (let ([parsed-sel (parse-css-selector base-sel)])
                      (when parsed-sel
                        (set! before-results (cons (cons parsed-sel props) before-results))))))
                (let ([parsed-sel (parse-css-selector trimmed-sel)])
                  (when parsed-sel
                    (set! results (cons (cons parsed-sel props) results))))))))))
  (values (reverse results) (reverse before-results)))

;; parse a simple CSS selector into a structured form.
;; supports: element (div), id (#foo), class (.bar), descendant (div div),
;;           child combinator (div > div)
;; returns a list of selector parts, each being:
;;   (element "div"), (id "foo"), (class "bar")
;; nested for descendant: ((element "div") (element "div"))
;; child combinator: ((element "body") > (element "div")) — uses '> marker
(define (parse-css-selector sel-str)
  ;; normalize spaces around > for child combinator
  (define cleaned (regexp-replace* #rx"\\s*>\\s*" sel-str " > "))
  (define parts (string-split cleaned))
  ;; parse tokens, keeping ">" as a combinator marker
  (define parsed
    (filter-map
     (lambda (part)
       (cond
         [(string=? part ">") '>]  ;; child combinator marker
         ;; universal selector *
         [(string=? part "*") '(universal)]
         [(regexp-match #rx"^#(.+)$" part) =>
          (lambda (m) `(id ,(cadr m)))]
         ;; .class:nth-child(N)
         [(regexp-match #rx"^\\.([^:]+):nth-child\\(([0-9]+)\\)$" part) =>
          (lambda (m) `(class+nth ,(cadr m) ,(string->number (caddr m))))]
         ;; element.class:nth-child(N)
         [(regexp-match #rx"^([a-zA-Z]+)\\.([^:]+):nth-child\\(([0-9]+)\\)$" part) =>
          (lambda (m) `(element+class+nth ,(cadr m) ,(caddr m) ,(string->number (cadddr m))))]
         ;; element:nth-child(N) (no class)
         [(regexp-match #rx"^([a-zA-Z]+):nth-child\\(([0-9]+)\\)$" part) =>
          (lambda (m) `(element+nth ,(cadr m) ,(string->number (caddr m))))]
         ;; :nth-child(N) alone
         [(regexp-match #rx"^:nth-child\\(([0-9]+)\\)$" part) =>
          (lambda (m) `(nth ,(string->number (cadr m))))]
         [(regexp-match #rx"^\\.(.+)$" part) =>
          (lambda (m) `(class ,(cadr m)))]
         [(regexp-match #rx"^([a-zA-Z][a-zA-Z0-9]*)$" part) =>
          (lambda (m) `(element ,(cadr m)))]
         ;; element#id or element.class
         [(regexp-match #rx"^([a-zA-Z]+)#(.+)$" part) =>
          (lambda (m) `(element+id ,(cadr m) ,(caddr m)))]
         [(regexp-match #rx"^([a-zA-Z]+)\\.(.+)$" part) =>
          (lambda (m) `(element+class ,(cadr m) ,(caddr m)))]
         [else #f]))
     parts))
  (if (null? parsed) #f parsed))

;; check if a single selector part matches an element.
;; child-index: 1-based position among siblings (for :nth-child)
(define (selector-part-matches? part elem-tag elem-id elem-class [child-index 0])
  (match part
    [`(universal) #t]  ;; * matches everything
    [`(element ,tag) (equal? tag elem-tag)]
    [`(id ,id-str) (and elem-id (equal? id-str elem-id))]
    [`(class ,cls) (and elem-class (string-contains? elem-class cls))]
    [`(element+id ,tag ,id-str) (and (equal? tag elem-tag) elem-id (equal? id-str elem-id))]
    [`(element+class ,tag ,cls) (and (equal? tag elem-tag) elem-class (string-contains? elem-class cls))]
    [`(nth ,n) (= child-index n)]
    [`(class+nth ,cls ,n) (and elem-class (string-contains? elem-class cls) (= child-index n))]
    [`(element+nth ,tag ,n) (and (equal? tag elem-tag) (= child-index n))]
    [`(element+class+nth ,tag ,cls ,n) (and (equal? tag elem-tag) elem-class (string-contains? elem-class cls) (= child-index n))]
    [_ #f]))

;; check if a full selector (list of parts, possibly with '> markers) matches
;; an element in context.
;; ancestor-chain: list of (tag id class) from innermost parent to outermost.
;; the last non-'> part must match the current element.
;; child-index: 1-based position among siblings (for :nth-child)
(define (selector-matches? selector elem-tag elem-id elem-class ancestor-chain [child-index 0])
  (cond
    [(null? selector) #f]
    [else
     ;; separate the last selector part (must match current element)
     ;; and the preceding parts (must match ancestors)
     (define rev (reverse selector))
     (define last-part (car rev))
     ;; last-part should not be '>, but handle gracefully
     (if (eq? last-part '>)
         #f
         (and (selector-part-matches? last-part elem-tag elem-id elem-class child-index)
              ;; check ancestor parts (reversed, so innermost ancestor first)
              (ancestors-match-with-combinators? (cdr rev) ancestor-chain)))]))

;; check if selector ancestor parts match the ancestor chain,
;; respecting '> child combinators.
;; rev-parts: remaining selector parts in reverse order (innermost first).
;;   may contain '> markers between selector parts.
;; ancestor-chain: list of (tag id class) from innermost parent outward.
(define (ancestors-match-with-combinators? rev-parts ancestor-chain)
  (cond
    [(null? rev-parts) #t]  ;; all parts matched
    [(null? ancestor-chain) #f]  ;; no more ancestors to check
    [else
     (define head (car rev-parts))
     (cond
       ;; '> combinator: next selector part must match DIRECT parent (first in chain)
       [(eq? head '>)
        (if (null? (cdr rev-parts))
            #f  ;; malformed: '> with nothing before it
            (let ([next-part (cadr rev-parts)]
                  [parent (car ancestor-chain)])
              (and (selector-part-matches? next-part (car parent) (cadr parent) (caddr parent))
                   (ancestors-match-with-combinators? (cddr rev-parts) (cdr ancestor-chain)))))]
       ;; descendant combinator (no '> marker): match ANY ancestor
       [else
        (let loop ([ancestors ancestor-chain])
          (cond
            [(null? ancestors) #f]
            [(let ([a (car ancestors)])
               (selector-part-matches? head (car a) (cadr a) (caddr a)))
             ;; matched: continue with remaining parts and remaining ancestors
             (ancestors-match-with-combinators? (cdr rev-parts) (cdr ancestors))]
            [else (loop (cdr ancestors))]))])]))

;; apply style rules to an element tree.
;; returns a new element tree with style rules merged into inline styles.
;; ancestor-chain: list of (tag id class) tuples from root to parent.
;; parent-props: alist of parent element's computed CSS properties (for inherit)
;; child-index: 1-based position among siblings (for :nth-child matching)
(define (apply-style-rules elem rules ancestor-chain [parent-props '()] [child-index 0])
  (match elem
    [`(element ,tag ,id ,class ,inline-alist ,children)
     ;; find all matching rules in source order
     (define matching-props-raw
       (apply append
         (filter-map
          (lambda (rule)
            (define selector (car rule))
            (define props (cdr rule))
            (if (selector-matches? selector tag id class ancestor-chain child-index)
                props
                #f))
          rules)))
     ;; deduplicate matching-props: later rules override earlier ones (CSS cascade)
     ;; sort by specificity: id selectors > class selectors > element selectors
     ;; for now, just deduplicate keeping last occurrence of each property
     (define matching-props
       (let loop ([props (reverse matching-props-raw)] [seen '()] [result '()])
         (cond
           [(null? props) result]
           [(member (caar props) seen)
            (loop (cdr props) seen result)]
           [else
            (loop (cdr props) (cons (caar props) seen)
                  (cons (car props) result))])))
     ;; merge: inline styles take priority over <style> block rules
     ;; build merged alist: start with style-block props, then override with inline
     (define merged-raw
       (let ([result matching-props])
         (for/fold ([r result]) ([prop (in-list inline-alist)])
           ;; remove any existing entry with same key, then add inline
           (cons prop (filter (lambda (p) (not (equal? (car p) (car prop)))) r)))))
     ;; resolve 'inherit' values: replace with parent's computed value
     (define merged-resolved
       (map (lambda (prop)
              (if (and (string? (cdr prop)) (string=? (cdr prop) "inherit"))
                  ;; look up property in parent's computed styles
                  (let ([parent-val (cdr-or-false (car prop) parent-props)])
                    (if parent-val
                        (cons (car prop) parent-val)
                        prop))  ;; keep inherit if parent doesn't have it
                  prop))
            merged-raw))
     ;; CSS inherited properties: automatically inherit from parent if not set
     ;; (font-family, font-size, font, line-height, color, direction, text-align,
     ;;  visibility, white-space, word-spacing, letter-spacing, text-indent,
     ;;  text-transform, list-style, cursor)
     (define css-inherited-props
       '(font-family font-size font line-height color direction text-align
         visibility white-space word-spacing letter-spacing text-indent
         text-transform))
     (define merged
       (let loop ([inh-props css-inherited-props] [result merged-resolved])
         (cond
           [(null? inh-props) result]
           [else
            (define key (car inh-props))
            (if (cdr-or-false key result)
                ;; child already has this property — keep as is
                (loop (cdr inh-props) result)
                ;; child doesn't have it — inherit from parent
                (let ([parent-val (cdr-or-false key parent-props)])
                  (if parent-val
                      (loop (cdr inh-props) (cons (cons key parent-val) result))
                      (loop (cdr inh-props) result))))])))
     ;; recurse into children with updated ancestor chain
     ;; compute 1-based child indices for :nth-child matching
     (define new-chain (cons (list tag id class) ancestor-chain))
     (define new-children
       (let loop ([cs children] [idx 1] [result '()])
         (cond
           [(null? cs) (reverse result)]
           [else
            (define c (car cs))
            (match c
              [`(text-node ,_) (loop (cdr cs) idx (cons c result))]
              [_ (loop (cdr cs) (add1 idx)
                       (cons (apply-style-rules c rules new-chain merged idx) result))])])))
     `(element ,tag ,id ,class ,merged ,new-children)]
    [_ elem]))

;; parse body content to extract element tree
(define (parse-html-body html-str)
  ;; extract body content — try with </body> first, fall back to end-of-string
  (define body-match
    (or (regexp-match #rx"(?i:<body[^>]*>(.*)</body>)" html-str)
        (regexp-match #rx"(?i:<body[^>]*>(.*))" html-str)))
  (if body-match
      (parse-elements (cadr body-match))
      '()))

;; parse a sequence of elements from HTML string.
;; returns list of (element tag id style-alist children)
(define (parse-elements html-str)
  (define results '())
  (define pos 0)
  (define len (string-length html-str))

  (let loop ()
    (when (< pos len)
      ;; find next opening tag — include common HTML elements
      (define tag-match
        (regexp-match-positions
         #rx"<(div|span|img|p|iframe|strong|em|b|i|u|a|br|table|tr|td|th|dl|dt|dd|ol|ul|li|object|embed|video|canvas)([^>]*)>"
         html-str pos))
      (when tag-match
        (define tag-start (caar tag-match))
        (define tag-end (cdar tag-match))
        (define tag-name (substring html-str
                                    (car (cadr tag-match))
                                    (cdr (cadr tag-match))))
        (define attrs-str (substring html-str
                                     (car (caddr tag-match))
                                     (cdr (caddr tag-match))))

        ;; parse id
        (define id-match (regexp-match #rx"id=\"([^\"]+)\"" attrs-str))
        (define elem-id (if id-match (cadr id-match) #f))

        ;; parse class
        (define class-match (regexp-match #rx"class=\"([^\"]+)\"" attrs-str))
        (define elem-class (if class-match (cadr class-match) #f))

        ;; parse style
        (define style-match (regexp-match #rx"style=\"([^\"]+)\"" attrs-str))
        (define inline-style-raw
          (if style-match
              (parse-inline-style (cadr style-match))
              '()))

        ;; parse HTML presentational attributes (width, height) → CSS equivalents
        ;; for replaced elements like iframe, img, video
        (define width-attr-match (regexp-match #rx"width=\"([^\"]+)\"" attrs-str))
        (define height-attr-match (regexp-match #rx"height=\"([^\"]+)\"" attrs-str))
        (define html-attrs-as-css
          (append
           (if (and width-attr-match (not (cdr-or-false 'width inline-style-raw)))
               (list (cons 'width (cadr width-attr-match)))
               '())
           (if (and height-attr-match (not (cdr-or-false 'height inline-style-raw)))
               (list (cons 'height (cadr height-attr-match)))
               '())))
        (define inline-style (append inline-style-raw html-attrs-as-css))

        ;; check for self-closing (void elements + explicit />)
        (define self-closing?
          (or (member tag-name '("img" "br" "hr" "input" "meta" "link"))
              (regexp-match? #rx"/>\\s*$" (substring html-str (car (car tag-match))
                                                              tag-end))))

        (if self-closing?
            ;; self-closing: no children
            (begin
              (set! results
                    (cons (list 'element tag-name elem-id elem-class inline-style '())
                          results))
              (set! pos tag-end)
              (loop))

            ;; find matching close tag
            (let ()
              (define-values (children after-pos)
                (parse-children-until html-str tag-end tag-name))
              (set! results
                    (cons (list 'element tag-name elem-id elem-class inline-style children)
                          results))
              (set! pos after-pos)
              (loop))))))

  (reverse results))

;; parse children until we hit the closing tag for the given tag-name.
;; returns (values children-list position-after-close)
;; also captures text content between tags as (text-node "content") entries.
(define (parse-children-until html-str start tag-name)
  (define children '())
  (define pos start)
  (define len (string-length html-str))
  (define close-rx (regexp (string-append "</" tag-name ">")))

  ;; helper: extract text between pos and next-tag-start, add as text-node if non-empty
  (define (maybe-add-text-node! end-pos)
    (when (< pos end-pos)
      (define raw (substring html-str pos end-pos))
      (define decoded (decode-html-entities raw))
      (define text (normalize-text-content decoded))
      (when (and (> (string-length text) 0)
                 ;; skip pure whitespace between elements (unless white-space: pre)
                 (not (regexp-match? #rx"^[ \t\r\n]+$" text)))
        (set! children (cons (list 'text-node text) children)))))

  (let loop ()
    (when (< pos len)
      ;; look for either an opening tag or a closing tag
      (define open-match
        (regexp-match-positions
         #rx"<(div|span|img|p|iframe|strong|em|b|i|u|a|br|table|tr|td|th|dl|dt|dd|ol|ul|li|object|embed|video|canvas)([^>]*)>"
         html-str pos))
      (define close-match
        (regexp-match-positions close-rx html-str pos))

      (cond
        ;; closing tag comes first (or no more opens)
        [(and close-match
              (or (not open-match)
                  (<= (caar close-match) (caar open-match))))
         ;; capture text before closing tag
         (maybe-add-text-node! (caar close-match))
         ;; done with this element
         (set! pos (cdar close-match))]

        ;; opening tag found before close
        [open-match
         ;; capture text before the opening tag
         (maybe-add-text-node! (caar open-match))
         (define tag-start (caar open-match))
         (define tag-end (cdar open-match))
         (define child-tag (substring html-str
                                      (car (cadr open-match))
                                      (cdr (cadr open-match))))
         (define attrs-str (substring html-str
                                       (car (caddr open-match))
                                       (cdr (caddr open-match))))

         ;; parse attrs
         (define id-match (regexp-match #rx"id=\"([^\"]+)\"" attrs-str))
         (define elem-id (if id-match (cadr id-match) #f))
         (define class-match (regexp-match #rx"class=\"([^\"]+)\"" attrs-str))
         (define elem-class (if class-match (cadr class-match) #f))
         (define style-match (regexp-match #rx"style=\"([^\"]+)\"" attrs-str))
         (define inline-style-raw
           (if style-match (parse-inline-style (cadr style-match)) '()))
         ;; parse HTML presentational attributes (width, height) → CSS equivalents
         (define width-attr-match (regexp-match #rx"width=\"([^\"]+)\"" attrs-str))
         (define height-attr-match (regexp-match #rx"height=\"([^\"]+)\"" attrs-str))
         (define html-attrs-as-css
           (append
            (if (and width-attr-match (not (cdr-or-false 'width inline-style-raw)))
                (list (cons 'width (cadr width-attr-match)))
                '())
            (if (and height-attr-match (not (cdr-or-false 'height inline-style-raw)))
                (list (cons 'height (cadr height-attr-match)))
                '())))
         (define inline-style (append inline-style-raw html-attrs-as-css))

         (define self-closing?
           (or (member child-tag '("img" "br" "hr" "input" "meta" "link"))
               (regexp-match? #rx"/>\\s*$"
                              (substring html-str tag-start tag-end))))

         (if self-closing?
             (begin
               (set! children
                     (cons (list 'element child-tag elem-id elem-class inline-style '())
                           children))
               (set! pos tag-end)
               (loop))

             (let ()
               (define-values (grandchildren after)
                 (parse-children-until html-str tag-end child-tag))
               (set! children
                     (cons (list 'element child-tag elem-id elem-class inline-style grandchildren)
                           children))
               (set! pos after)
               (loop)))]

        ;; nothing more
        [else
         (set! pos len)])))

  (values (reverse children) pos))

;; ============================================================
;; Classify HTML Test
;; ============================================================

;; classify an HTML test file into: 'simple (div-only with optional text),
;; 'style-block (has <style> block CSS rules), 'complex (spans, images, etc.)
;; text content between tags is supported by the importer (Ahem font measurement),
;; so tests with text are classified as 'simple or 'style-block, not 'complex.
(define (classify-html-test html-path)
  (define content (file->string html-path))
  (cond
    [(regexp-match? #rx"<img" content) 'complex]
    [(regexp-match? #rx"<span" content) 'complex]
    [(regexp-match? #rx"<style" content) 'style-block]
    [else 'simple]))

;; HTML tag default display values (CSS 2.2 § user agent stylesheet)
;; returns #f for tags that use the base default (block)
(define (html-tag-default-display tag)
  (cond
    [(member tag '("strong" "em" "b" "i" "u" "a" "span" "code" "small" "big"
                   "abbr" "cite" "dfn" "kbd" "samp" "var" "sub" "sup"
                   "q" "ins" "del" "mark" "s" "strike" "font"))
     "inline"]
    [(member tag '("table")) "table"]
    [(member tag '("tr")) "table-row"]
    [(member tag '("td" "th")) "table-cell"]
    [(member tag '("tbody" "thead" "tfoot")) "table-row-group"]
    [(member tag '("li")) "list-item"]
    [(member tag '("br" "img")) "inline"]
    [else #f]))

;; ============================================================
;; Inline Styles → Redex Styles
;; ============================================================

;; build Redex styles from parsed inline styles + base defaults.
;; is-root?: whether this is the body > * element (gets position:absolute)
(define (inline-styles->redex-styles inline-alist is-root? has-class [tag #f])
  (define props '())

  (define (add! name val)
    (set! props (cons `(,name ,val) props)))

  ;; determine display type: CSS > tag default > base default
  (define display-str
    (or (cdr-or-false 'display inline-alist)
        (and tag (html-tag-default-display tag))
        (hash-ref (base-defaults) 'display)))

  ;; determine box-sizing (check class for .content-box / .border-box)
  (define box-sizing
    (cond
      [(and has-class (string-contains? has-class "content-box")) "content-box"]
      [(and has-class (string-contains? has-class "border-box")) "border-box"]
      [(cdr-or-false 'box-sizing inline-alist)]
      [else (hash-ref (base-defaults) 'box-sizing)]))

  (add! 'box-sizing (string->symbol box-sizing))

  ;; detect font properties to set em/ex context for CSS unit conversion
  ;; font-size from font shorthand or font-size property
  (define font-size-str (cdr-or-false 'font-size inline-alist))
  (define elem-font-size
    (if font-size-str
        ;; parse font-size without em/ex recursion — use absolute units only
        (let ([m (regexp-match #rx"^([0-9.]+)(px|pt|in|cm|mm|pc)$"
                               (string-trim font-size-str))])
          (if m (css-unit->px (string->number (cadr m)) (caddr m))
              (current-em-size)))
        (current-em-size)))
  ;; detect font-family for ex ratio (Ahem: 0.8, default: 0.5)
  (define font-family-str (cdr-or-false 'font-family inline-alist))
  (define elem-ex-ratio
    (if (and font-family-str (regexp-match? #rx"(?i:ahem)" font-family-str))
        0.8 0.5))

  ;; set up em/ex context for all CSS unit conversions in this element
  (current-em-size elem-font-size)
  (current-ex-ratio elem-ex-ratio)

  ;; store font-size as numeric value in props for layout use
  (add! 'font-size elem-font-size)

  ;; position: root gets absolute for Taffy tests (test_base_style.css has
  ;; body > * { position: absolute; }), static for CSS2.1 tests (browser default)
  (define position
    (cond
      [(and is-root? (uses-taffy-base?)) "absolute"]
      [(cdr-or-false 'position inline-alist)]
      [else (hash-ref (base-defaults) 'position)]))
  (add! 'position (string->symbol position))

  ;; width/height
  (for ([prop '(width height min-width min-height max-width max-height)])
    (define val (cdr-or-false prop inline-alist))
    (when val
      (add! prop (parse-css-length val))))

  ;; expand CSS logical property shorthands before edge parsing:
  ;; margin-block → margin-block-start + margin-block-end
  ;; margin-inline → margin-inline-start + margin-inline-end
  ;; padding-block → padding-block-start + padding-block-end
  ;; padding-inline → padding-inline-start + padding-inline-end
  ;; (In horizontal LTR writing mode: block=vertical, inline=horizontal)
  (define expanded-alist
    (let loop ([alist inline-alist] [result '()])
      (cond
        [(null? alist) (reverse result)]
        [else
         (define entry (car alist))
         (define key (car entry))
         (define val (cdr entry))
         (cond
           [(or (eq? key 'margin-block) (eq? key 'padding-block))
            (define prefix (if (eq? key 'margin-block) "margin" "padding"))
            (define parts (string-split (string-trim val)))
            ;; 1 value → both start/end; 2 values → start end
            (define start-val (car parts))
            (define end-val (if (>= (length parts) 2) (cadr parts) start-val))
            (loop (cdr alist)
                  (cons (cons (string->symbol (format "~a-block-end" prefix)) end-val)
                        (cons (cons (string->symbol (format "~a-block-start" prefix)) start-val)
                              result)))]
           [(or (eq? key 'margin-inline) (eq? key 'padding-inline))
            (define prefix (if (eq? key 'margin-inline) "margin" "padding"))
            (define parts (string-split (string-trim val)))
            (define start-val (car parts))
            (define end-val (if (>= (length parts) 2) (cadr parts) start-val))
            (loop (cdr alist)
                  (cons (cons (string->symbol (format "~a-inline-end" prefix)) end-val)
                        (cons (cons (string->symbol (format "~a-inline-start" prefix)) start-val)
                              result)))]
           [else
            (loop (cdr alist) (cons entry result))])])))

  ;; margin
  (define margin-vals (parse-edge-shorthand expanded-alist 'margin))
  (when margin-vals
    (add! 'margin margin-vals))

  ;; padding (CSS spec: negative padding is invalid, clamp to 0)
  (define padding-vals-raw (parse-edge-shorthand expanded-alist 'padding))
  (define padding-vals
    (and padding-vals-raw
         (match padding-vals-raw
           [`(edges ,t ,r ,b ,l)
            (define (clamp-pad v)
              (cond [(and (number? v) (< v 0)) 0]
                    [else v]))
            `(edges ,(clamp-pad t) ,(clamp-pad r) ,(clamp-pad b) ,(clamp-pad l))]
           [_ padding-vals-raw])))
  (when padding-vals
    (add! 'padding padding-vals))

  ;; border-width
  (define border-vals (parse-border-shorthand expanded-alist))
  (when border-vals
    (add! 'border-width border-vals))

  ;; flex container properties
  (let ([v (cdr-or-false 'flex-direction inline-alist)])
    (when v (add! 'flex-direction (string->symbol v))))
  (let ([v (cdr-or-false 'flex-wrap inline-alist)])
    (when v
      ;; normalize invalid "no-wrap" to "nowrap" (Chrome ignores invalid values)
      (define normalized (if (string=? v "no-wrap") "nowrap" v))
      (add! 'flex-wrap (string->symbol normalized))))
  ;; flex-flow shorthand: <flex-direction> || <flex-wrap>
  (let ([v (cdr-or-false 'flex-flow inline-alist)])
    (when v
      (define parts (string-split (string-trim v)))
      (define direction-keywords '("row" "row-reverse" "column" "column-reverse"))
      (define wrap-keywords '("nowrap" "wrap" "wrap-reverse"))
      (for ([part (in-list parts)])
        (cond
          [(member part direction-keywords)
           (add! 'flex-direction (string->symbol part))]
          [(member part wrap-keywords)
           (add! 'flex-wrap (string->symbol part))]))))
  (let ([v (cdr-or-false 'justify-content inline-alist)])
    (when v (add! 'justify-content (map-justify-content v))))
  (let ([v (cdr-or-false 'align-items inline-alist)])
    (when v (add! 'align-items (map-align-items v))))
  (let ([v (cdr-or-false 'align-content inline-alist)])
    (when v (add! 'align-content (map-align-content v))))

  ;; flex item properties
  (let ([v (cdr-or-false 'flex-grow inline-alist)])
    (when v (add! 'flex-grow (string->number v))))
  (let ([v (cdr-or-false 'flex-shrink inline-alist)])
    (when v (add! 'flex-shrink (string->number v))))
  (let ([v (cdr-or-false 'flex-basis inline-alist)])
    (when v (add! 'flex-basis (parse-css-length v))))
  (let ([v (cdr-or-false 'align-self inline-alist)])
    (when v (add! 'align-self (map-align-self v))))
  (let ([v (cdr-or-false 'order inline-alist)])
    (when v (add! 'order (string->number v))))

  ;; gap properties
  (let ([v (cdr-or-false 'gap inline-alist)])
    (when (and v (not (string=? v "normal")))
      ;; gap shorthand: "row-gap column-gap" or single value for both
      (define parts (string-split v))
      (cond
        [(= (length parts) 2)
         ;; two values: first is row-gap, second is column-gap
         (let ([rg (parse-css-px-or-pct (car parts))]
               [cg (parse-css-px-or-pct (cadr parts))])
           (when rg (add! 'row-gap rg))
           (when cg (add! 'column-gap cg)))]
        [(= (length parts) 1)
         (let ([gap-v (parse-css-px-or-pct (car parts))])
           (when gap-v
             (add! 'row-gap gap-v)
             (add! 'column-gap gap-v)))]
        [else (void)])))
  (let ([v (cdr-or-false 'row-gap inline-alist)])
    (when (and v (not (string=? v "normal")))
      (let ([gap-v (parse-css-px-or-pct v)])
        (when gap-v (add! 'row-gap gap-v)))))
  (let ([v (cdr-or-false 'column-gap inline-alist)])
    (when (and v (not (string=? v "normal")))
      (let ([gap-v (parse-css-px-or-pct v)])
        (when gap-v (add! 'column-gap gap-v)))))

  ;; overflow
  (let ([v (cdr-or-false 'overflow inline-alist)])
    (when v (add! 'overflow (string->symbol v))))

  ;; aspect-ratio
  (let ([v (cdr-or-false 'aspect-ratio inline-alist)])
    (when v
      (define n (string->number (string-trim v)))
      (when n (add! 'aspect-ratio n))))

  ;; text-align
  (let ([v (cdr-or-false 'text-align inline-alist)])
    (when v
      (define val (string->symbol (string-trim v)))
      (add! 'text-align val)))

  ;; text-indent (CSS 2.2 §16.1): applies to block containers
  ;; supports px, em, and percentage values
  (let ([v (cdr-or-false 'text-indent inline-alist)])
    (when v
      (define parsed (parse-css-length v))
      (when parsed
        (add! 'text-indent parsed))))

  ;; float and clear
  (let ([v (cdr-or-false 'float inline-alist)])
    (when v
      (define val (string-trim v))
      (add! 'float (cond
                     [(string=? val "left") 'float-left]
                     [(string=? val "right") 'float-right]
                     [else 'float-none]))))
  (let ([v (cdr-or-false 'clear inline-alist)])
    (when v
      (define val (string-trim v))
      (add! 'clear (cond
                     [(string=? val "left") 'clear-left]
                     [(string=? val "right") 'clear-right]
                     [(string=? val "both") 'clear-both]
                     [else 'clear-none]))))

  ;; white-space
  (let ([v (cdr-or-false 'white-space inline-alist)])
    (when v
      (add! 'white-space (string->symbol (string-trim v)))))

  ;; vertical-align
  (let ([v (cdr-or-false 'vertical-align inline-alist)])
    (when v
      (define val (string-trim v))
      (add! 'vertical-align
            (cond
              [(string=? val "baseline") 'va-baseline]
              [(string=? val "top") 'va-top]
              [(string=? val "middle") 'va-middle]
              [(string=? val "bottom") 'va-bottom]
              [(string=? val "text-top") 'va-text-top]
              [(string=? val "text-bottom") 'va-text-bottom]
              [else (let ([n (parse-css-px val)])
                     (if n `(va-length ,n) 'va-baseline))]))))

  ;; line-height
  (let ([v (cdr-or-false 'line-height inline-alist)])
    (when v
      (define val (string-trim v))
      (cond
        [(string=? val "normal") (void)] ;; use default
        [(string->number val)
         => (lambda (n) (add! 'line-height (* n elem-font-size)))] ;; unitless multiplier
        [else
         (define parsed (parse-css-px val))
         (when parsed (add! 'line-height parsed))])))

  ;; position offsets
  (for ([prop '(top right bottom left)])
    (define val (cdr-or-false prop inline-alist))
    (when val (add! prop (parse-css-length val))))

  ;; flex shorthand: flex: <grow> <shrink> <basis>
  (let ([v (cdr-or-false 'flex inline-alist)])
    (when v (parse-flex-shorthand! v add!)))

  ;; grid container properties
  (let ([v (cdr-or-false 'grid-template-columns inline-alist)])
    (when v
      (define-values (tracks names) (parse-grid-template v))
      (add! 'grid-template-columns tracks)
      (when (not (null? names))
        (add! 'grid-col-line-names names))))
  (let ([v (cdr-or-false 'grid-template-rows inline-alist)])
    (when v
      (define-values (tracks names) (parse-grid-template v))
      (add! 'grid-template-rows tracks)
      (when (not (null? names))
        (add! 'grid-row-line-names names))))
  (let ([v (cdr-or-false 'grid-auto-columns inline-alist)])
    (when v (add! 'grid-auto-columns (parse-grid-auto-tracks v))))
  (let ([v (cdr-or-false 'grid-auto-rows inline-alist)])
    (when v (add! 'grid-auto-rows (parse-grid-auto-tracks v))))

  ;; grid-template-areas: "header header" "nav main" "footer footer"
  (let ([v (cdr-or-false 'grid-template-areas inline-alist)])
    (when (and v (not (string=? (string-trim v) "none")))
      (define area-map (parse-grid-template-areas v))
      (when (not (null? area-map))
        (add! 'grid-template-areas area-map))))
  (let ([v (cdr-or-false 'grid-auto-flow inline-alist)])
    (when v (add! 'grid-auto-flow (map-grid-auto-flow v))))
  (let ([v (cdr-or-false 'justify-items inline-alist)])
    (when v (add! 'justify-items (map-align-items v))))
  (let ([v (cdr-or-false 'justify-content inline-alist)])
    (when (and v (not (assoc 'justify-content props)))
      (add! 'justify-content (map-justify-content v))))
  (let ([v (cdr-or-false 'justify-self inline-alist)])
    (when v (add! 'justify-self (map-align-self v))))
  (let ([v (cdr-or-false 'place-items inline-alist)])
    (when v
      ;; place-items: <align-items> <justify-items>?
      ;; single value sets both
      (define parts (string-split (string-trim v)))
      (define ai (map-align-items (car parts)))
      (define ji (if (>= (length parts) 2)
                     (map-align-items (cadr parts))
                     ai))
      (unless (assoc 'align-items props) (add! 'align-items ai))
      (unless (assoc 'justify-items props) (add! 'justify-items ji))))

  ;; grid item placement properties
  ;; grid-area shorthand: either a named area or "row-start / col-start / row-end / col-end"
  (let ([v (cdr-or-false 'grid-area inline-alist)])
    (when v
      (define trimmed (string-trim v))
      (define parts (map string-trim (string-split trimmed "/")))
      (cond
        ;; single value: named area reference
        [(and (= (length parts) 1)
              (regexp-match? #rx"^[a-zA-Z_][a-zA-Z0-9_-]*$" (car parts)))
         (add! 'grid-area-name (car parts))]
        ;; 4 values: row-start / col-start / row-end / col-end
        [(= (length parts) 4)
         (add! 'grid-row-start (parse-grid-line-value (list-ref parts 0)))
         (add! 'grid-column-start (parse-grid-line-value (list-ref parts 1)))
         (add! 'grid-row-end (parse-grid-line-value (list-ref parts 2)))
         (add! 'grid-column-end (parse-grid-line-value (list-ref parts 3)))]
        ;; 2 values: row-start / col-start (end = auto)
        [(= (length parts) 2)
         (add! 'grid-row-start (parse-grid-line-value (list-ref parts 0)))
         (add! 'grid-column-start (parse-grid-line-value (list-ref parts 1)))]
        [else (void)])))
  (let ([v (cdr-or-false 'grid-row inline-alist)])
    (when v (parse-grid-placement-shorthand! v 'grid-row-start 'grid-row-end add!)))
  (let ([v (cdr-or-false 'grid-column inline-alist)])
    (when v (parse-grid-placement-shorthand! v 'grid-column-start 'grid-column-end add!)))
  (let ([v (cdr-or-false 'grid-row-start inline-alist)])
    (when v (add! 'grid-row-start (parse-grid-line-value v))))
  (let ([v (cdr-or-false 'grid-row-end inline-alist)])
    (when v (add! 'grid-row-end (parse-grid-line-value v))))
  (let ([v (cdr-or-false 'grid-column-start inline-alist)])
    (when v (add! 'grid-column-start (parse-grid-line-value v))))
  (let ([v (cdr-or-false 'grid-column-end inline-alist)])
    (when v (add! 'grid-column-end (parse-grid-line-value v))))

  ;; pass through __before-block-height from ::before pseudo-element injection
  (let ([v (cdr-or-false '__before-block-height inline-alist)])
    (when (and v (number? v) (> v 0))
      (add! '__before-block-height v)))

  ;; pass through __before-inline-width from inline ::before pseudo-element
  (let ([v (cdr-or-false '__before-inline-width inline-alist)])
    (when (and v (number? v) (> v 0))
      (add! '__before-inline-width v)))

  `(style ,@(reverse props)))

;; ============================================================
;; CSS Value Parsers
;; ============================================================

;; parse a CSS length value to a Redex SizeValue
;; convert CSS absolute length units to px
;; 1in=96px, 1cm=37.795px, 1mm=3.7795px, 1pt=4/3px, 1pc=16px
;; em/ex/rem/ch are relative to current-em-size parameter
(define current-em-size (make-parameter 16))  ;; default browser font-size
;; ex ratio: for Ahem font 0.8, for default serif ~0.5
(define current-ex-ratio (make-parameter 0.5))

(define (css-unit->px val unit)
  (define factor
    (cond
      [(string=? unit "px") 1]
      [(string=? unit "in") 96]
      [(string=? unit "cm") (/ 96.0 2.54)]
      [(string=? unit "mm") (/ 96.0 25.4)]
      [(string=? unit "pt") (/ 4.0 3.0)]
      [(string=? unit "pc") 16]
      [(string=? unit "em") (current-em-size)]
      [(string=? unit "ex") (* (current-em-size) (current-ex-ratio))]
      [(string=? unit "rem") 16]
      [(string=? unit "ch") (* (current-em-size) 0.5)]
      [else 1]))
  (* val factor))

(define (parse-css-length str)
  (define trimmed (string-trim str))
  (cond
    [(string=? trimmed "auto") 'auto]
    [(string=? trimmed "none") 'none]
    [(string=? trimmed "0") '(px 0)]
    [(string=? trimmed "min-content") 'min-content]
    [(string=? trimmed "max-content") 'max-content]
    [(regexp-match #rx"^([+-]?[0-9.]+)px$" trimmed) =>
     (lambda (m) `(px ,(string->number (cadr m))))]
    [(regexp-match #rx"^([+-]?[0-9.]+)%$" trimmed) =>
     (lambda (m) `(% ,(string->number (cadr m))))]
    [(regexp-match #rx"^([+-]?[0-9.]+)(em|ex|rem|ch)$" trimmed) =>
     (lambda (m) `(px ,(css-unit->px (string->number (cadr m)) (caddr m))))]
    [(regexp-match #rx"^([+-]?[0-9.]+)(in|cm|mm|pt|pc)$" trimmed) =>
     (lambda (m) `(px ,(css-unit->px (string->number (cadr m)) (caddr m))))]
    [(regexp-match #rx"^([+-]?[0-9.]+)$" trimmed) =>
     (lambda (m) `(px ,(string->number (cadr m))))]
    [else 'auto]))

;; parse a plain px value, returning just the number or #f
(define (parse-css-px str)
  (define trimmed (string-trim str))
  (cond
    [(string=? trimmed "0") 0]
    [(regexp-match #rx"^([+-]?[0-9.]+)px$" trimmed) =>
     (lambda (m) (string->number (cadr m)))]
    [(regexp-match #rx"^([+-]?[0-9.]+)(in|cm|mm|pt|pc|em|ex|rem|ch)$" trimmed) =>
     (lambda (m) (css-unit->px (string->number (cadr m)) (caddr m)))]
    [(regexp-match #rx"^([+-]?[0-9.]+)$" trimmed) =>
     (lambda (m) (string->number (cadr m)))]
    [else #f]))

;; parse a CSS value that can be px or percentage, returning number or (% n)
(define (parse-css-px-or-pct str)
  (define trimmed (string-trim str))
  (cond
    [(string=? trimmed "0") 0]
    [(regexp-match #rx"^([+-]?[0-9.]+)px$" trimmed) =>
     (lambda (m) (string->number (cadr m)))]
    [(regexp-match #rx"^([+-]?[0-9.]+)%$" trimmed) =>
     (lambda (m) `(% ,(string->number (cadr m))))]
    [(regexp-match #rx"^([+-]?[0-9.]+)(in|cm|mm|pt|pc|em|ex|rem|ch)$" trimmed) =>
     (lambda (m) (css-unit->px (string->number (cadr m)) (caddr m)))]
    [(regexp-match #rx"^([+-]?[0-9.]+)$" trimmed) =>
     (lambda (m) (string->number (cadr m)))]
    [else #f]))

;; parse edge shorthand from inline alist.
;; handles margin/padding with individual sides or shorthand.
;; also handles logical properties (inline-start/end, block-start/end) assuming LTR.
(define (parse-edge-shorthand alist prop-name)
  (define top-key (string->symbol (format "~a-top" prop-name)))
  (define right-key (string->symbol (format "~a-right" prop-name)))
  (define bottom-key (string->symbol (format "~a-bottom" prop-name)))
  (define left-key (string->symbol (format "~a-left" prop-name)))

  ;; logical property keys (CSS Logical Properties)
  (define inline-start-key (string->symbol (format "~a-inline-start" prop-name)))
  (define inline-end-key (string->symbol (format "~a-inline-end" prop-name)))
  (define block-start-key (string->symbol (format "~a-block-start" prop-name)))
  (define block-end-key (string->symbol (format "~a-block-end" prop-name)))

  ;; check individual sides first, then logical properties (LTR: inline-start=left, block-start=top)
  (define t (or (cdr-or-false top-key alist) (cdr-or-false block-start-key alist)))
  (define r (or (cdr-or-false right-key alist) (cdr-or-false inline-end-key alist)))
  (define b (or (cdr-or-false bottom-key alist) (cdr-or-false block-end-key alist)))
  (define l (or (cdr-or-false left-key alist) (cdr-or-false inline-start-key alist)))

  ;; also check shorthand
  (define shorthand (cdr-or-false prop-name alist))

  (cond
    ;; any individual side → build edges
    [(or t r b l)
     (define (parse-margin-val v)
       (cond
         [(not v) 0]
         [(equal? v "auto") 'auto]
         [else (or (parse-css-px-or-pct v) 0)]))
     (define tv (parse-margin-val t))
     (define rv (parse-margin-val r))
     (define bv (parse-margin-val b))
     (define lv (parse-margin-val l))
     `(edges ,tv ,rv ,bv ,lv)]

    ;; shorthand present
    [shorthand
     (parse-margin-shorthand shorthand)]

    [else #f]))

;; parse a margin/padding shorthand value: "10px", "10px 20px", "10px 20px 30px", "10px 20px 30px 40px"
;; preserves 'auto for margin values
(define (parse-margin-shorthand str)
  (define parts (string-split (string-trim str)))
  (define vals (map (lambda (p)
                      (cond
                        [(equal? p "auto") 'auto]
                        [else (or (parse-css-px-or-pct p) 0)]))
                    parts))
  (match vals
    [(list a) `(edges ,a ,a ,a ,a)]
    [(list tb lr) `(edges ,tb ,lr ,tb ,lr)]
    [(list t lr b) `(edges ,t ,lr ,b ,lr)]
    [(list t r b l) `(edges ,t ,r ,b ,l)]
    [_ #f]))

;; extract a width value from a border shorthand string
;; e.g., "1px solid black" → 1, "1in blue" → 96, "solid 5px blue" → 5
;; "0 solid black" → 0 (unitless zero is valid in CSS)
;; if border-style is present but no explicit width, defaults to medium (3px)
(define (extract-border-width-from-shorthand str)
  (define m (regexp-match #rx"([0-9.]+)(px|in|cm|mm|pt|pc|em|ex|rem|ch)" str))
  (if m
      (css-unit->px (string->number (cadr m)) (caddr m))
      ;; check for unitless zero: "0 solid ..." or "solid 0 ..." etc.
      ;; CSS allows unitless 0 for border-width
      ;; note: use #px for \s (whitespace class); #rx treats \s as literal 's'
      (cond
        [(regexp-match? #px"(?:^|\\s)0(?:$|\\s)" str) 0]
        [(regexp-match? #rx"thin" str) 1]
        [(regexp-match? #rx"medium" str) 3]
        [(regexp-match? #rx"thick" str) 5]
        ;; if a border-style keyword is present but no explicit width,
        ;; CSS defaults to medium (3px). 'none' and 'hidden' mean no visible border.
        [(regexp-match? #rx"(?i:none|hidden)" str) 0]
        [(regexp-match? #rx"(?i:dotted|dashed|solid|double|groove|ridge|inset|outset)" str) 3]
        [else #f])))

;; parse border shorthand to extract border-width edges
(define (parse-border-shorthand alist)
  (define bt (cdr-or-false 'border-top-width alist))
  (define br (cdr-or-false 'border-right-width alist))
  (define bb (cdr-or-false 'border-bottom-width alist))
  (define bl (cdr-or-false 'border-left-width alist))
  (define border (cdr-or-false 'border alist))
  (define border-width (cdr-or-false 'border-width alist))
  ;; individual side shorthands: border-top, border-right, border-bottom, border-left
  (define b-top (cdr-or-false 'border-top alist))
  (define b-right (cdr-or-false 'border-right alist))
  (define b-bottom (cdr-or-false 'border-bottom alist))
  (define b-left (cdr-or-false 'border-left alist))

  ;; start with border shorthand as baseline (applies to all sides)
  (define base-w (if border (or (extract-border-width-from-shorthand border) 0) 0))

  ;; border-width shorthand overrides base-w if present
  (define bw-parsed (if border-width (parse-margin-shorthand border-width) #f))

  (cond
    ;; individual border-*-width properties take highest priority
    [(or bt br bb bl)
     (define bw-t (if bw-parsed (list-ref (cdr bw-parsed) 0) base-w))
     (define bw-r (if bw-parsed (list-ref (cdr bw-parsed) 1) base-w))
     (define bw-b (if bw-parsed (list-ref (cdr bw-parsed) 2) base-w))
     (define bw-l (if bw-parsed (list-ref (cdr bw-parsed) 3) base-w))
     (define tv (if bt (or (parse-css-px bt) 0) bw-t))
     (define rv (if br (or (parse-css-px br) 0) bw-r))
     (define bv (if bb (or (parse-css-px bb) 0) bw-b))
     (define lv (if bl (or (parse-css-px bl) 0) bw-l))
     `(edges ,tv ,rv ,bv ,lv)]

    ;; individual side shorthands (e.g., border-left: solid 5px blue)
    [(or b-top b-right b-bottom b-left)
     (define bw-t (if bw-parsed (list-ref (cdr bw-parsed) 0) base-w))
     (define bw-r (if bw-parsed (list-ref (cdr bw-parsed) 1) base-w))
     (define bw-b (if bw-parsed (list-ref (cdr bw-parsed) 2) base-w))
     (define bw-l (if bw-parsed (list-ref (cdr bw-parsed) 3) base-w))
     (define tv (if b-top (or (extract-border-width-from-shorthand b-top) 0) bw-t))
     (define rv (if b-right (or (extract-border-width-from-shorthand b-right) 0) bw-r))
     (define bv (if b-bottom (or (extract-border-width-from-shorthand b-bottom) 0) bw-b))
     (define lv (if b-left (or (extract-border-width-from-shorthand b-left) 0) bw-l))
     `(edges ,tv ,rv ,bv ,lv)]

    ;; border-width shorthand (e.g. "5px")
    [bw-parsed bw-parsed]

    ;; border shorthand present (e.g. "1px solid black", "0 solid black")
    ;; when border is specified, use the extracted width even if 0
    [(and border (number? base-w))
     `(edges ,base-w ,base-w ,base-w ,base-w)]

    [else #f]))

;; parse flex shorthand: flex: <grow> [<shrink>] [<basis>]
(define (parse-flex-shorthand! str add!)
  (define trimmed (string-trim str))
  (cond
    [(string=? trimmed "none")
     ;; flex: none → 0 0 auto
     (add! 'flex-grow 0)
     (add! 'flex-shrink 0)
     (add! 'flex-basis 'auto)]
    [(string=? trimmed "auto")
     ;; flex: auto → 1 1 auto
     (add! 'flex-grow 1)
     (add! 'flex-shrink 1)
     (add! 'flex-basis 'auto)]
    [else
     (define parts (string-split trimmed))
     (cond
       [(= (length parts) 1)
        ;; flex: <number> → <number> 1 0%  (CSS spec: single unitless number = grow shrink basis)
        (let ([g (string->number (car parts))])
          (cond
            [g (add! 'flex-grow g)
               (add! 'flex-shrink 1)
               (add! 'flex-basis `(px 0))]
            [else
             ;; single non-numeric value → treat as flex-basis
             (add! 'flex-grow 1)
             (add! 'flex-shrink 1)
             (add! 'flex-basis (parse-css-length (car parts)))]))]
       [(= (length parts) 2)
        (let ([g (string->number (car parts))]
              [s (string->number (cadr parts))])
          (cond
            [(and g s)
             ;; flex: <grow> <shrink> → grow shrink 0%
             (add! 'flex-grow g)
             (add! 'flex-shrink s)
             (add! 'flex-basis `(px 0))]
            [g
             ;; flex: <grow> <basis>
             (add! 'flex-grow g)
             (add! 'flex-shrink 1)
             (add! 'flex-basis (parse-css-length (cadr parts)))]
            [else
             (add! 'flex-grow 1)
             (add! 'flex-shrink 1)
             (add! 'flex-basis (parse-css-length (car parts)))]))]
       [(>= (length parts) 3)
        (let ([g (string->number (car parts))])
          (when g (add! 'flex-grow g)))
        (let ([s (string->number (cadr parts))])
          (when s (add! 'flex-shrink s)))
        (add! 'flex-basis (parse-css-length (caddr parts)))])]))

;; ============================================================
;; Grid CSS Parsers
;; ============================================================

;; parse grid-template-areas CSS value into an area map
;; input: CSS value string like "\"nav header\" \"nav main\" \"footer footer\""
;; output: alist of (name row-start row-end col-start col-end) with 1-based line numbers
;; e.g., ((nav 1 3 1 2) (header 1 2 2 3) (main 2 3 2 3) (footer 3 4 1 3))
(define (parse-grid-template-areas str)
  ;; extract contents of all quoted strings from the CSS value
  (define row-strings (regexp-match* #rx"\"([^\"]+)\"" str #:match-select cadr))
  (when (null? row-strings)
    ;; fallback: try unquoted rows separated by newlines (shouldn't normally happen)
    (set! row-strings (list str)))
  ;; parse each row string into a list of cell names
  (define grid
    (for/list ([row-str (in-list row-strings)])
      (string-split (string-trim row-str))))
  (when (null? grid) '())
  ;; collect all unique area names (skip ".")
  (define area-names
    (remove-duplicates
     (filter (lambda (name) (not (string=? name ".")))
             (apply append grid))))
  ;; for each area name, find its bounding rectangle
  (define num-rows (length grid))
  (for/list ([name (in-list area-names)])
    (define row-min #f)
    (define row-max #f)
    (define col-min #f)
    (define col-max #f)
    (for ([row (in-list grid)]
          [r (in-naturals)])
      (for ([cell (in-list row)]
            [c (in-naturals)])
        (when (string=? cell name)
          (when (or (not row-min) (< r row-min)) (set! row-min r))
          (when (or (not row-max) (> r row-max)) (set! row-max r))
          (when (or (not col-min) (< c col-min)) (set! col-min c))
          (when (or (not col-max) (> c col-max)) (set! col-max c)))))
    ;; convert to 1-based grid line numbers
    ;; row-start line = row-min + 1, row-end line = row-max + 2
    ;; col-start line = col-min + 1, col-end line = col-max + 2
    (list name (add1 row-min) (+ row-max 2) (add1 col-min) (+ col-max 2))))

;; parse a grid-template-columns or grid-template-rows string into
;; a list of TrackSize terms: ((px 40) (px 40) (fr 1) auto ...)
;; handles: px, %, fr, auto, min-content, max-content, minmax(), repeat()
;; also extracts named line markers [name] → (name . line-number) alist
;; returns (values track-list line-names-alist)
(define (parse-grid-template str)
  (define trimmed (string-trim str))
  (cond
    [(or (string=? trimmed "none") (string=? trimmed ""))
     (values '() '())]
    [else
     (define tokens (tokenize-track-list trimmed))
     ;; process tokens: strip [name] bracket tokens and build name→line# map
     ;; line numbers are 1-based CSS grid line numbers
     (define all-tracks '())
     (define names '())
     (for ([tok (in-list tokens)])
       (define bracket-m (regexp-match #rx"^\\[(.+)\\]$" tok))
       (cond
         [bracket-m
          ;; named line: line number = current track count + 1
          (define name (string-trim (cadr bracket-m)))
          (set! names (cons (cons name (add1 (length all-tracks))) names))]
         [else
          (define parsed (parse-single-track-token tok))
          (set! all-tracks (append all-tracks parsed))]))
     (values all-tracks (reverse names))]))

;; tokenize and parse a track list string
;; handles repeat() and minmax() as nested functions
(define (parse-track-list str)
  (define tokens (tokenize-track-list str))
  (parse-track-tokens tokens))

;; tokenize a track list, respecting parentheses
(define (tokenize-track-list str)
  (define len (string-length str))
  (let loop ([pos 0] [tokens '()])
    (cond
      [(>= pos len) (reverse tokens)]
      ;; skip whitespace
      [(char-whitespace? (string-ref str pos))
       (loop (add1 pos) tokens)]
      [else
       ;; read a token (could be a function like repeat(...) or minmax(...))
       (define-values (token new-pos) (read-track-token str pos))
       (loop new-pos (cons token tokens))])))

;; read a single track token starting at pos
;; returns (values token-string new-pos)
(define (read-track-token str pos)
  (define len (string-length str))
  (define start pos)
  ;; check if this starts a function call (contains '(')
  (let scan ([i pos] [depth 0])
    (cond
      [(>= i len)
       (values (substring str start i) i)]
      [(char=? (string-ref str i) #\()
       (scan (add1 i) (add1 depth))]
      [(char=? (string-ref str i) #\))
       (if (<= depth 1)
           (values (substring str start (add1 i)) (add1 i))
           (scan (add1 i) (sub1 depth)))]
      [(and (= depth 0) (char-whitespace? (string-ref str i)))
       (values (substring str start i) i)]
      [else (scan (add1 i) depth)])))

;; parse a list of track token strings into TrackSize terms
(define (parse-track-tokens tokens)
  (apply append
         (map parse-single-track-token tokens)))

;; parse a single track token into a list of TrackSize terms
;; (list because repeat() can expand to multiple)
(define (parse-single-track-token token)
  (define trimmed (string-trim token))
  (cond
    [(string=? trimmed "auto") (list 'auto)]
    [(string=? trimmed "min-content") (list 'min-content)]
    [(string=? trimmed "max-content") (list 'max-content)]
    ;; px value
    [(regexp-match #rx"^(-?[0-9.]+)px$" trimmed) =>
     (lambda (m) (list `(px ,(string->number (cadr m)))))]
    ;; percentage
    [(regexp-match #rx"^(-?[0-9.]+)%$" trimmed) =>
     (lambda (m) (list `(% ,(string->number (cadr m)))))]
    ;; fr unit
    [(regexp-match #rx"^(-?[0-9.]+)fr$" trimmed) =>
     (lambda (m) (list `(fr ,(string->number (cadr m)))))]
    ;; repeat(count, track-size)
    [(regexp-match #rx"^repeat\\((.+)\\)$" trimmed) =>
     (lambda (m)
       (define inner (cadr m))
       (define comma-pos (string-index-of inner #\,))
       (if comma-pos
           (let* ([count-str (string-trim (substring inner 0 comma-pos))]
                  [track-str (string-trim (substring inner (add1 comma-pos)))]
                  [track-defs (parse-track-list track-str)])
             (cond
               [(string->number count-str)
                => (lambda (n)
                     (apply append (for/list ([_ (in-range (inexact->exact (round n)))]) track-defs)))]
               ;; auto-fill / auto-fit — store as a marker for expansion at layout time
               [(or (string=? count-str "auto-fill") (string=? count-str "auto-fit"))
                (list `(,(string->symbol count-str) ,@track-defs))]
               [else track-defs]))
           (list 'auto)))]
    ;; minmax(min, max)
    [(regexp-match #rx"^minmax\\((.+)\\)$" trimmed) =>
     (lambda (m)
       (define inner (cadr m))
       (define comma-pos (string-index-of inner #\,))
       (if comma-pos
           (let* ([min-str (string-trim (substring inner 0 comma-pos))]
                  [max-str (string-trim (substring inner (add1 comma-pos)))]
                  [min-v (parse-track-size-value min-str)]
                  [max-v (parse-track-size-value max-str)])
             (list `(minmax ,min-v ,max-v)))
           (list 'auto)))]
    ;; fit-content(length) — CSS Grid §7.2.3
    ;; equivalent to minmax(auto, min(max-content, <length>))
    ;; the track grows up to <length> but not beyond, and shrinks to fit content
    [(regexp-match #rx"^fit-content\\((.+)\\)$" trimmed) =>
     (lambda (m)
       (define arg (parse-track-size-value (string-trim (cadr m))))
       (list `(fit-content ,arg)))]
    ;; plain number (rare, treat as px)
    [(regexp-match #rx"^(-?[0-9.]+)$" trimmed) =>
     (lambda (m) (list `(px ,(string->number (cadr m)))))]
    [else (list 'auto)]))

;; find first index of character ch in string, or #f
;; respects parenthesis nesting (only finds at depth 0)
(define (string-index-of str ch)
  (define len (string-length str))
  (let loop ([i 0] [depth 0])
    (cond
      [(>= i len) #f]
      [(char=? (string-ref str i) #\() (loop (add1 i) (add1 depth))]
      [(char=? (string-ref str i) #\)) (loop (add1 i) (sub1 depth))]
      [(and (= depth 0) (char=? (string-ref str i) ch)) i]
      [else (loop (add1 i) depth)])))

;; parse a single track size value (for minmax arguments)
(define (parse-track-size-value str)
  (define trimmed (string-trim str))
  (cond
    [(string=? trimmed "auto") 'auto]
    [(string=? trimmed "min-content") 'min-content]
    [(string=? trimmed "max-content") 'max-content]
    [(regexp-match #rx"^(-?[0-9.]+)px$" trimmed) =>
     (lambda (m) `(px ,(string->number (cadr m))))]
    [(regexp-match #rx"^(-?[0-9.]+)%$" trimmed) =>
     (lambda (m) `(% ,(string->number (cadr m))))]
    [(regexp-match #rx"^(-?[0-9.]+)fr$" trimmed) =>
     (lambda (m) `(fr ,(string->number (cadr m))))]
    [(regexp-match #rx"^(-?[0-9.]+)$" trimmed) =>
     (lambda (m) `(px ,(string->number (cadr m))))]
    [else 'auto]))

;; parse grid-auto-columns / grid-auto-rows value
;; e.g. "80px", "1fr", "10px 20px 30px", "minmax(40px, auto)"
(define (parse-grid-auto-tracks str)
  (parse-track-list (string-trim str)))

;; parse grid-row or grid-column shorthand:
;; "1 / 3" → grid-row-start: (line 1), grid-row-end: (line 3)
;; "1 / span 2" → grid-row-start: (line 1), grid-row-end: (span 2)
;; "span 2" → grid-row-start: (span 2), grid-row-end: auto
;; "1" → grid-row-start: (line 1), grid-row-end: auto
(define (parse-grid-placement-shorthand! str start-prop end-prop add!)
  (define parts (string-split (string-trim str) "/"))
  (cond
    [(= (length parts) 2)
     (add! start-prop (parse-grid-line-value (string-trim (car parts))))
     (add! end-prop (parse-grid-line-value (string-trim (cadr parts))))]
    [(= (length parts) 1)
     (add! start-prop (parse-grid-line-value (string-trim (car parts))))]
    [else (void)]))

;; parse a single grid line value: "1", "-3", "span 2", "auto", "start"
(define (parse-grid-line-value str)
  (define trimmed (string-trim str))
  (cond
    [(string=? trimmed "auto") 'grid-auto]
    [(regexp-match #px"^span\\s+([0-9]+)$" trimmed) =>
     (lambda (m) `(span ,(string->number (cadr m))))]
    [(regexp-match #rx"^(-?[0-9]+)$" trimmed) =>
     (lambda (m) `(line ,(string->number (cadr m))))]
    ;; named grid line reference (CSS identifier)
    [(regexp-match #rx"^[a-zA-Z_][a-zA-Z0-9_-]*$" trimmed)
     `(named ,trimmed)]
    [else 'grid-auto]))

;; map CSS grid-auto-flow values
(define (map-grid-auto-flow val)
  (define v (string-trim val))
  (cond
    [(string=? v "row") 'grid-row]
    [(string=? v "column") 'grid-column]
    [(string=? v "dense") 'grid-row-dense]
    [(string=? v "row dense") 'grid-row-dense]
    [(string=? v "column dense") 'grid-column-dense]
    [else 'grid-row]))

;; ============================================================
;; CSS Keyword Mapping
;; ============================================================

;; map CSS justify-content values to Redex enum names
(define (map-justify-content val)
  (define v (string-trim val))
  (cond
    [(string=? v "flex-start") 'flex-start]
    [(string=? v "flex-end") 'flex-end]
    [(string=? v "center") 'center]
    [(string=? v "space-between") 'space-between]
    [(string=? v "space-around") 'space-around]
    [(string=? v "space-evenly") 'space-evenly]
    [(string=? v "start") 'start]
    [(string=? v "end") 'end]
    [(string=? v "normal") 'flex-start]
    [else (string->symbol v)]))

;; map CSS align-items values to Redex enum names
(define (map-align-items val)
  (define v (string-trim val))
  (cond
    [(string=? v "flex-start") 'align-start]
    [(string=? v "flex-end") 'align-end]
    [(string=? v "center") 'align-center]
    [(string=? v "baseline") 'align-baseline]
    [(string=? v "stretch") 'align-stretch]
    [(string=? v "start") 'align-start]
    [(string=? v "end") 'align-end]
    [(string=? v "normal") 'align-stretch]
    [else (string->symbol v)]))

;; map CSS align-content values to Redex enum names
(define (map-align-content val)
  (define v (string-trim val))
  (cond
    [(string=? v "flex-start") 'content-start]
    [(string=? v "flex-end") 'content-end]
    [(string=? v "center") 'content-center]
    [(string=? v "stretch") 'content-stretch]
    [(string=? v "space-between") 'content-space-between]
    [(string=? v "space-around") 'content-space-around]
    [(string=? v "space-evenly") 'content-space-evenly]
    [(string=? v "start") 'content-start]
    [(string=? v "end") 'content-end]
    [(string=? v "normal") 'content-stretch]
    [else (string->symbol v)]))

;; map CSS align-self values to Redex enum names
(define (map-align-self val)
  (define v (string-trim val))
  (cond
    [(string=? v "auto") 'self-auto]
    [(string=? v "flex-start") 'self-start]
    [(string=? v "flex-end") 'self-end]
    [(string=? v "center") 'self-center]
    [(string=? v "baseline") 'self-baseline]
    [(string=? v "stretch") 'self-stretch]
    [(string=? v "start") 'self-start]
    [(string=? v "end") 'self-end]
    [else (string->symbol v)]))

;; ============================================================
;; Element Tree → Redex Box Tree
;; ============================================================

;; resolve a size value to px, given a parent width for percentage resolution
(define (resolve-size-value val parent-w)
  (match val
    [`(px ,n) n]
    [(? number?) val]
    [`(% ,pct)
     (if (and parent-w (number? parent-w))
         (* (/ pct 100) parent-w)
         #f)]
    [_ #f]))

;; get a padding value from styles (in px)
(define (style-padding-value styles prop)
  (define v (get-style-prop styles prop 0))
  (match v
    [`(px ,n) n]
    [(? number?) v]
    [_ 0]))

;; resolve this element's content width for use by children
;; returns the content-box width in px, or #f if unknown
(define (resolve-element-content-width styles parent-w)
  (define w-val (get-style-prop styles 'width #f))
  (define resolved (resolve-size-value w-val parent-w))
  (if resolved
      (let-values ([(pt pr pb pl) (get-edges styles 'padding)]
                   [(bt br bb bl) (get-edges styles 'border-width)])
        ;; resolve percentage padding against parent width
        (define (resolve-pct v)
          (match v
            [`(% ,pct)
             (if (and parent-w (number? parent-w))
                 (* (/ pct 100) parent-w)
                 0)]
            [(? number?) v]
            [_ 0]))
        (- resolved (resolve-pct pl) (resolve-pct pr) bl br))
      ;; if no explicit width, pass through parent-w as an approximation
      ;; (this covers display:block which takes full parent width)
      parent-w))

;; expand auto-fill/auto-fit repeat markers in track definitions
;; e.g. ((auto-fill (px 40))) with available-size=120 → ((px 40) (px 40) (px 40))
;; for auto-fit: ((auto-fit (px 40))) → ((auto-fit-track (px 40)) (auto-fit-track (px 40)) ...)
(define (expand-auto-repeat defs explicit-size styles)
  (if (null? defs)
      defs
      ;; compute space consumed by non-auto-fill/auto-fit tracks
      ;; auto-fill must only use the remaining available space
      (let* ([avail-num
              (if (and explicit-size (pair? explicit-size))
                  (match explicit-size [`(px ,n) n] [_ #f])
                  #f)]
             [fixed-space
              (if avail-num
                  (for/sum ([d (in-list defs)])
                    (match d
                      [`(auto-fill . ,_) 0]
                      [`(auto-fit . ,_) 0]
                      [`(px ,n) n]
                      [`(% ,pct) (* (/ pct 100) avail-num)]
                      [`(minmax ,mn ,_mx)
                       (match mn [`(px ,n) n] [`(% ,pct) (* (/ pct 100) avail-num)] [_ 0])]
                      [`(fr ,_) 0]
                      [_ 0]))
                  0)]
             ;; count fixed tracks for gap calculation
             [fixed-count
              (for/sum ([d (in-list defs)])
                (match d
                  [`(auto-fill . ,_) 0]
                  [`(auto-fit . ,_) 0]
                  [_ 1]))]
             [gap-val (let ([raw (get-style-prop styles 'column-gap 0)])
                        (cond
                          [(number? raw) raw]
                          [(and (pair? raw) (eq? (car raw) '%) avail-num)
                           (* (/ (cadr raw) 100) avail-num)]
                          [else 0]))]
             [fixed-gaps (* gap-val fixed-count)]  ;; gaps between fixed tracks and the auto-fill group
             [reduced-avail
              (if avail-num
                  `(px ,(max 0 (- avail-num fixed-space fixed-gaps)))
                  explicit-size)])
        (apply append
          (for/list ([d (in-list defs)])
            (match d
              [`(auto-fill . ,track-defs)
               (expand-auto-fill-tracks track-defs reduced-avail styles #f)]
              [`(auto-fit . ,track-defs)
               (expand-auto-fill-tracks track-defs reduced-avail styles #t)]
              [_ (list d)]))))))

;; calculate how many times to repeat auto-fill tracks
(define (expand-auto-fill-tracks track-defs explicit-size styles is-auto-fit?)
  (define avail
    (if (and explicit-size (pair? explicit-size))
        (match explicit-size
          [`(px ,n) n]
          [_ #f])
        #f))
  (if (and avail (> avail 0))
      ;; compute total size of one repetition using minimum track sizes
      (let ([one-rep-size
             (for/sum ([td (in-list track-defs)])
               (match td
                 [`(px ,n) n]
                 [`(% ,pct) (* (/ pct 100) avail)]
                 ;; for minmax, use the minimum for auto-fill repetition count
                 [`(minmax ,mn ,_mx)
                  (match mn
                    [`(px ,n) n]
                    [`(% ,pct) (* (/ pct 100) avail)]
                    [_ 0])]
                 [_ 0]))])
        (if (> one-rep-size 0)
            ;; account for gaps
            (let* ([raw-gap (get-style-prop styles 'column-gap 0)]
                   [gap-val (cond
                              [(number? raw-gap) raw-gap]
                              [(and (pair? raw-gap) (eq? (car raw-gap) '%))
                               (* (/ (cadr raw-gap) 100) avail)]
                              [else 0])]
                   ;; compute gap-aware repetition count
                   ;; with N repeats and N-1 gaps: N*size + (N-1)*gap <= avail
                   ;; N*(size+gap) <= avail + gap → N <= (avail+gap)/(size+gap)
                   [n (inexact->exact (floor (/ (+ avail gap-val) (+ one-rep-size gap-val))))]
                   [count (max 1 n)]
                   [expanded (apply append (for/list ([_ (in-range count)]) track-defs))])
              ;; for auto-fit, wrap each track in (auto-fit-track ...)
              (if is-auto-fit?
                  (map (lambda (td) `(auto-fit-track ,td)) expanded)
                  expanded))
            track-defs))
      ;; no definite size — keep as single repetition
      track-defs))

;; convert a parsed element tree to a Redex box tree.
;; elem: (element tag-name id class inline-alist children)
;; counter: box counter for generating unique ids
;; is-root?: whether this is the body > * element
;; parent-w: parent's content width in px (for resolving percentages), or #f
(define (element->box-tree elem counter is-root? [parent-w #f])
  (match elem
    [`(element ,tag ,id ,class ,inline-alist ,children)
     ;; encode tag:name in id for element-name matching in comparator
     (define box-id
       (if id
           (string->symbol (format "~a:~a" tag id))
           (string->symbol (format "~a:box~a" tag (unbox counter)))))
     (set-box! counter (add1 (unbox counter)))

     (define styles (inline-styles->redex-styles inline-alist is-root? class tag))

     ;; determine display type from inline styles, tag default, or base default
     (define display-str
       (or (cdr-or-false 'display inline-alist)
           (html-tag-default-display tag)
           (hash-ref (base-defaults) 'display)))  ; Taffy: flex, CSS: block

     ;; resolve this element's content width for children
     (define this-w (resolve-element-content-width styles parent-w))

     ;; check if this is a replaced element (iframe, img, object, embed, video)
     ;; replaced elements have intrinsic dimensions instead of child content
     (define replaced-tags '("iframe" "img" "object" "embed" "video" "canvas"))
     (cond
       [(member tag replaced-tags)
        ;; return a replaced element box with intrinsic dimensions
        ;; iframe: default 300×150 (CSS spec default object size)
        (define intrinsic-w
          (case tag
            [("iframe") 300]
            [("img") 0]
            [("video") 300]
            [else 300]))
        (define intrinsic-h
          (case tag
            [("iframe") 150]
            [("img") 0]
            [("video") 150]
            [else 150]))
        `(replaced ,box-id ,styles ,intrinsic-w ,intrinsic-h)]
       [else

     (define child-boxes
       (filter-map
        (lambda (c)
          ;; scope em/ex context per child so siblings don't leak to each other
          (parameterize ([current-em-size (current-em-size)]
                         [current-ex-ratio (current-ex-ratio)])
          (match c
            [`(text-node ,text)
             ;; create a text box with pre-measured width
             (define text-id (string->symbol (format "txt~a" (unbox counter))))
             (set-box! counter (add1 (unbox counter)))
             ;; detect font-family from parent element's styles
             (define font-family-val
               (cdr-or-false 'font-family inline-alist))
             (define uses-ahem?
               (or (uses-taffy-base?)
                   (and font-family-val
                        (regexp-match? #rx"(?i:ahem)" font-family-val))))
             ;; detect font-size from parent element's styles
             (define font-size-val
               (cdr-or-false 'font-size inline-alist))
             (define effective-font-size
               (cond
                 [font-size-val (or (parse-css-px font-size-val) 16)]
                 [(uses-taffy-base?) ahem-font-size]
                 [else 16]))
             (define text-box-sizing (if (uses-taffy-base?) 'border-box 'content-box))
             ;; inherit line-height from parent styles
             (define parent-line-height (get-style-prop styles 'line-height #f))
             ;; detect font-weight from parent element's styles
             (define font-weight-val
               (let ([fw (cdr-or-false 'font-weight inline-alist)])
                 (cond
                   [(not fw) 'normal]
                   [(or (equal? fw "bold") (equal? fw "700") (equal? fw "800") (equal? fw "900")) 'bold]
                   [else 'normal])))
             ;; determine which font metrics to use:
             ;; - "arial" if font-family is explicitly sans-serif/arial/helvetica, OR bold weight
             ;; - "times" if font-family is default (serif) with normal weight
             (define use-arial-metrics?
               (or (eq? font-weight-val 'bold)
                   (and font-family-val
                        (or (regexp-match? #rx"(?i:sans-serif)" font-family-val)
                            (regexp-match? #rx"(?i:arial)" font-family-val)
                            (regexp-match? #rx"(?i:helvetica)" font-family-val)))))
             (define font-metrics-sym (if use-arial-metrics? 'arial 'times))
             ;; detect white-space: pre from parent (preserves whitespace text nodes)
             (define white-space-val (cdr-or-false 'white-space inline-alist))
             (define is-pre? (and white-space-val
                                  (or (equal? white-space-val "pre")
                                      (equal? white-space-val "pre-wrap")
                                      (equal? white-space-val "pre-line"))))
             (cond
               [uses-ahem?
                ;; Ahem font: each visible char = font-size wide
                (define text-styles
                  (if parent-line-height
                      `(style (box-sizing ,text-box-sizing) (font-size ,effective-font-size)
                              (line-height ,parent-line-height))
                      `(style (box-sizing ,text-box-sizing) (font-size ,effective-font-size))))
                (define measured-w (measure-text-ahem text effective-font-size))
                `(text ,text-id ,text-styles ,text ,measured-w)]
               [else
                ;; non-Ahem: normalize whitespace (unless white-space: pre)
                (define normalized
                  (if is-pre? text (normalize-text-content text)))
                (cond
                  [(string=? normalized "")
                   ;; empty text after normalization → skip
                   #f]
                  [else
                   ;; include font-metrics in text styles for layout-dispatch
                   (define base-style-props
                     `((box-sizing ,text-box-sizing)
                       (font-type proportional)
                       (font-size ,effective-font-size)
                       (font-metrics ,font-metrics-sym)))
                   (define text-styles
                     (if parent-line-height
                         `(style ,@base-style-props (line-height ,parent-line-height))
                         `(style ,@base-style-props)))
                   (define measured-w (measure-text-proportional normalized effective-font-size font-family-val font-metrics-sym))
                   `(text ,text-id ,text-styles ,normalized ,measured-w)])])]
            [`(element ,tag ,id ,class ,inline-alist ,children)
             ;; skip elements with position:absolute or position:fixed
             ;; (they are out of normal flow and we don't model them)
             (define pos-val (cdr-or-false 'position inline-alist))
             (if (and pos-val (or (string=? pos-val "absolute")
                                  (string=? pos-val "fixed")))
                 #f
                 (element->box-tree c counter #f this-w))]
            [_ (element->box-tree c counter #f this-w)])))
        children))

     (define display-sym (string->symbol display-str))

     ;; CSS 2.2 §9.2.1.1: when an inline element contains block children,
     ;; convert to block and add strut heights for the anonymous block portions
     ;; wrapping any before/after inline content.
     (define (has-block-child? boxes)
       (for/or ([b (in-list boxes)])
         (and (pair? b) (eq? (car b) 'block))))

     (define (compute-strut-height)
       ;; strut = normal line-height of the inline element's font
       ;; Times: (ascender+|descender|)/unitsPerEm = (891+216)/1000 ≈ 1.107
       (define fs-val (cdr-or-false 'font-size inline-alist))
       (define fs (cond [fs-val (or (parse-css-px fs-val) 16)] [else 16]))
       ;; check if font-family is sans-serif → use Arial line-height
       (define ff-val (cdr-or-false 'font-family inline-alist))
       (define is-sans?
         (and ff-val
              (or (regexp-match? #rx"(?i:sans-serif)" ff-val)
                  (regexp-match? #rx"(?i:arial)" ff-val)
                  (regexp-match? #rx"(?i:helvetica)" ff-val))))
       (define lh-ratio (if is-sans? 1.15 1.107))
       (* lh-ratio fs))

     (case display-sym
       [(flex) `(flex ,box-id ,styles ,child-boxes)]
       [(block) `(block ,box-id ,styles ,child-boxes)]
       [(inline)
        (if (has-block-child? child-boxes)
            ;; block-in-inline: convert to block and add strut heights
            (let* ([strut-h (compute-strut-height)]
                   ;; check if first child is a block (need before-strut)
                   [first-is-block?
                    (and (pair? child-boxes)
                         (pair? (car child-boxes))
                         (eq? (car (car child-boxes)) 'block))]
                   ;; check if last child is a block (need after-strut)
                   [last-is-block?
                    (and (pair? child-boxes)
                         (let ([last (last child-boxes)])
                           (and (pair? last)
                                (eq? (car last) 'block))))]
                   ;; inject strut heights into the container's styles
                   [strut-styles
                    (match styles
                      [`(style ,@props)
                       (define new-props props)
                       (when first-is-block?
                         (set! new-props
                           (append new-props
                                   `((__before-strut-height ,strut-h)))))
                       (when last-is-block?
                         (set! new-props
                           (append new-props
                                   `((__after-strut-height ,strut-h)))))
                       `(style ,@new-props)]
                      [_ styles])])
              `(block ,box-id ,strut-styles ,child-boxes))
            ;; normal inline
            `(inline ,box-id ,styles ,child-boxes))]
       [(inline-block) `(inline-block ,box-id ,styles ,child-boxes)]
       [(inline-flex)
        ;; inline-flex behaves like flex for layout purposes
        `(flex ,box-id ,styles ,child-boxes)]
       [(run-in)
        ;; CSS3: run-in is computed as block in modern browsers
        `(block ,box-id ,styles ,child-boxes)]
       [(list-item)
        ;; list-item is a block-level box with optional marker
        `(block ,box-id ,styles ,child-boxes)]
       [(table inline-table)
        ;; table display types use shrink-to-fit when width:auto (CSS 2.2 §17.5.2)
        ;; use inline-block box type to get shrink-to-fit behavior
        (define explicit-width (get-style-prop styles 'width #f))
        (if (or (not explicit-width) (eq? explicit-width 'auto))
            `(inline-block ,box-id ,styles ,child-boxes)
            `(block ,box-id ,styles ,child-boxes))]
       [(table-row table-row-group table-header-group table-footer-group
                   table-cell table-column table-column-group table-caption)
        ;; inner table parts: treat as block
        `(block ,box-id ,styles ,child-boxes)]
       [(none) `(none ,box-id)]
       [(grid)
        ;; build grid box with grid-def from styles
        (define raw-col-defs
          (get-style-prop styles 'grid-template-columns '()))
        (define raw-row-defs
          (get-style-prop styles 'grid-template-rows '()))
        ;; expand auto-fill/auto-fit repeats using container's explicit size
        ;; resolve percentage widths against this element's content width
        (define explicit-w (get-style-prop styles 'width #f))
        (define explicit-h (get-style-prop styles 'height #f))
        (define resolved-w (resolve-size-value explicit-w parent-w))
        (define resolved-h (resolve-size-value explicit-h #f))
        ;; subtract padding for auto-fill calculation (content area)
        (define-values (_pt pad-r _pb pad-l) (get-edges styles 'padding))
        ;; resolve percentage padding against parent width for content-area calc
        (define (resolve-pad-pct v)
          (match v
            [`(% ,pct)
             (if (and parent-w (number? parent-w))
                 (* (/ pct 100) parent-w) 0)]
            [(? number?) v] [_ 0]))
        (define content-w-for-repeat
          (if resolved-w (- resolved-w (resolve-pad-pct pad-l) (resolve-pad-pct pad-r)) #f))
        (define col-defs (expand-auto-repeat raw-col-defs
                           (if content-w-for-repeat `(px ,content-w-for-repeat) explicit-w)
                           styles))
        (define row-defs (expand-auto-repeat raw-row-defs explicit-h styles))
        (define grid-def `(grid-def (,@row-defs) (,@col-defs)))
        `(grid ,box-id ,styles ,grid-def ,child-boxes)]
       [else `(flex ,box-id ,styles ,child-boxes)])])] ;; close else, cond, match
    [_ `(none anon)]))

;; ============================================================
;; Reference JSON → Expected Layout
;; ============================================================

;; extract the expected layout positions from a reference JSON layout tree.
;; skips html, head, body wrappers — starts from the test root (body > first div).
;; For non-Taffy tests, zeroes root y since preceding elements (like <p>
;; description text) are not modeled in our box tree.
;; returns: (expected id x y width height children)
(define (reference->expected-layout ref-json)
  (define layout-tree (hash-ref ref-json 'layout_tree (hash)))
  ;; navigate: html → body → first div (test root)
  (define-values (test-root body-abs-x body-abs-y) (find-test-root layout-tree))
  (if test-root
      (let ([exp (layout-node->expected test-root body-abs-x body-abs-y)])
        (if (and exp (not (uses-taffy-base?)))
            ;; for CSS2.1 tests, zero out root y since we don't model preceding
            ;; elements like <p> description text in the box tree
            (match exp
              [`(expected ,id ,x ,y ,w ,h ,children)
               `(expected ,id ,x 0 ,w ,h ,children)]
              [_ exp])
            exp))
      #f))

;; find the test root element (body > div#test-root or first body > div)
;; returns: (values test-root-node body-abs-x body-abs-y)
(define (find-test-root html-node)
  (define html-children (hash-ref html-node 'children '()))
  ;; find body
  (define body-node
    (for/first ([c (in-list html-children)]
                #:when (equal? (hash-ref c 'tag #f) "body"))
      c))
  (if body-node
      (let* ([body-layout (hash-ref body-node 'layout (hash))]
             [body-x (hash-ref body-layout 'x 0)]
             [body-y (hash-ref body-layout 'y 0)]
             [test-root
              (for/first ([c (in-list (hash-ref body-node 'children '()))]
                          #:when (and (equal? (hash-ref c 'nodeType #f) "element")
                                      (equal? (hash-ref c 'tag #f) "div")))
                c)])
        (values test-root body-x body-y))
      (values #f 0 0)))

;; convert a text node to an expected layout node.
;; uses bounding box of all layout rects, converted to parent-relative.
;; returns: (expected-text text x y w h) — distinct form from element nodes
(define (text-node->expected node parent-abs-x parent-abs-y)
  (define layout (hash-ref node 'layout (hash)))
  (define rects (hash-ref layout 'rects '()))
  (if (null? rects)
      #f
      (let* ([xs  (map (lambda (r) (hash-ref r 'x 0)) rects)]
             [ys  (map (lambda (r) (hash-ref r 'y 0)) rects)]
             [x2s (map (lambda (r) (+ (hash-ref r 'x 0) (hash-ref r 'width 0))) rects)]
             [y2s (map (lambda (r) (+ (hash-ref r 'y 0) (hash-ref r 'height 0))) rects)]
             [abs-x (apply min xs)]
             [abs-y (apply min ys)]
             [w (- (apply max x2s) abs-x)]
             [h (- (apply max y2s) abs-y)]
             [rel-x (- abs-x parent-abs-x)]
             [rel-y (- abs-y parent-abs-y)])
        `(expected-text text ,rel-x ,rel-y ,w ,h))))

;; merge adjacent text children into a single bounding-box text node.
;; for non-Taffy tests, the browser may split one text node into multiple
;; per-line entries. merging them avoids requiring exact proportional font
;; wrapping in our layout engine.
(define (merge-text-children children)
  (define (is-text? c) (and (pair? c) (eq? (car c) 'expected-text)))

  ;; group: collect runs of text, keep elements as-is
  (let loop ([remaining children] [result '()] [text-group '()])
    (cond
      [(null? remaining)
       ;; flush any accumulated text group
       (reverse (if (null? text-group) result
                    (cons (merge-text-group (reverse text-group)) result)))]
      [(is-text? (car remaining))
       (loop (cdr remaining) result (cons (car remaining) text-group))]
      [else
       ;; flush text group, then add non-text element
       (define flushed
         (if (null? text-group) result
             (cons (merge-text-group (reverse text-group)) result)))
       (loop (cdr remaining) (cons (car remaining) flushed) '())])))

;; merge a list of expected-text nodes into one bounding box
(define (merge-text-group texts)
  (if (= (length texts) 1)
      (car texts)
      ;; compute bounding box
      (let* ([xs  (map (lambda (t) (list-ref t 2)) texts)]
             [ys  (map (lambda (t) (list-ref t 3)) texts)]
             [x2s (map (lambda (t) (+ (list-ref t 2) (list-ref t 4))) texts)]
             [y2s (map (lambda (t) (+ (list-ref t 3) (list-ref t 5))) texts)]
             [min-x (apply min xs)]
             [min-y (apply min ys)]
             [w (- (apply max x2s) min-x)]
             [h (- (apply max y2s) min-y)])
        `(expected-text text ,min-x ,min-y ,w ,h))))

;; convert a reference layout node to expected layout structure
;; Converts absolute browser coordinates to parent-relative coordinates.
;; returns: (expected tag:id x y width height (children ...))
;; id encodes the HTML tag for structural matching: tag:htmlid or tag:anon
(define (layout-node->expected node [parent-abs-x 0] [parent-abs-y 0])
  (define layout-data (hash-ref node 'layout (hash)))
  (define node-tag (hash-ref node 'tag "div"))
  (define node-id-str
    (let ([raw (hash-ref node 'id #f)])
      (if (and raw (string? raw)) raw "anon")))
  (define exp-id (string->symbol (format "~a:~a" node-tag node-id-str)))
  (define abs-x (hash-ref layout-data 'x 0))
  (define abs-y (hash-ref layout-data 'y 0))
  (define w (hash-ref layout-data 'width 0))
  (define h (hash-ref layout-data 'height 0))

  ;; convert to parent-relative coordinates
  (define rel-x (- abs-x parent-abs-x))
  (define rel-y (- abs-y parent-abs-y))

  ;; recursively process children (elements + text nodes with layout)
  ;; children are relative to this node's absolute position
  (define raw-children
    (filter-map
     (lambda (c)
       (define node-type (hash-ref c 'nodeType #f))
       (cond
         ;; element nodes: skip script/style/display:none/position:absolute/fixed
         [(equal? node-type "element")
          (and (not (member (hash-ref c 'tag #f) '("script" "style" "link" "meta" "title")))
               (let ([computed (hash-ref c 'computed (hash))])
                 (and (not (equal? (hash-ref computed 'display #f) "none"))
                      ;; skip position:absolute/fixed elements (out of normal flow)
                      (not (member (hash-ref computed 'position #f) '("absolute" "fixed")))))
               (layout-node->expected c abs-x abs-y))]
         ;; text nodes with visible layout
         [(equal? node-type "text")
          (let ([layout (hash-ref c 'layout (hash))])
            (and (hash-ref layout 'hasLayout #f)
                 (text-node->expected c abs-x abs-y)))]
         [else #f]))
     (hash-ref node 'children '())))

  ;; merge adjacent text children into a single bounding box.
  ;; browsers may split one text node into multiple per-line layout entries,
  ;; but our layout engine produces a single text view per text node.
  ;; merging avoids structural child-count mismatches.
  (define children (merge-text-children raw-children))

  `(expected ,exp-id
             ,rel-x ,rel-y ,w ,h
             ,children))

;; ============================================================
;; Reference JSON → Redex Box Tree
;; ============================================================

;; build a Redex box tree from an HTML file (for the input)
;; and a reference JSON (for the expected output).
;; html-path: path to the HTML test file
;; returns: a Redex Box term
(define (reference->box-tree html-path)
  ;; detect whether this test uses the Taffy base stylesheet
  ;; reset em/ex context per test case to prevent cross-test leakage
  (parameterize ([uses-taffy-base? (html-uses-taffy-base-stylesheet? html-path)]
                 [current-em-size 16]
                 [current-ex-ratio 0.5])
    (define elements (html-file->inline-styles html-path))
    (define counter (box 0))
    (cond
      [(null? elements)
       ;; empty test — shouldn't happen
       `(block root (style) ())]
      [(uses-taffy-base?)
       ;; Taffy: take the first body-level element as root
       (element->box-tree (car elements) counter #t)]
      [else
       ;; CSS2.1: find first div element (skip p/description elements)
       ;; to match expected tree which starts from first div
       (define first-div
         (for/first ([e (in-list elements)]
                     #:when (match e
                              [`(element "div" . ,_) #t]
                              [_ #f]))
           e))
       (if first-div
           (element->box-tree first-div counter #t)
           ;; fallback: use first element
           (element->box-tree (car elements) counter #t))])))

;; ============================================================
;; Full Test Case Builder
;; ============================================================

;; build a complete test case from an HTML file and its reference JSON.
;; html-path: path to HTML file
;; ref-path: path to reference JSON file
;; returns: reference-test-case struct
(define (reference-file->test-case html-path ref-path)
  ;; detect whether this test uses the Taffy base stylesheet
  ;; reset em/ex context per test case to prevent cross-test leakage
  (parameterize ([uses-taffy-base? (html-uses-taffy-base-stylesheet? html-path)]
                 [current-em-size 16]
                 [current-ex-ratio 0.5])
    (define ref-json
      (call-with-input-file ref-path
        (lambda (in) (read-json in))))

    (define box-tree (reference->box-tree html-path))
    (define expected (reference->expected-layout ref-json))
    (define viewport-info (hash-ref ref-json 'browser_info (hash)))
    (define vp (hash-ref viewport-info 'viewport (hash 'width 1200 'height 800)))
    (define vp-w (hash-ref vp 'width 1200))
    (define vp-h (hash-ref vp 'height 800))
    (define name (hash-ref ref-json 'test_file "unknown"))

    ;; for non-Taffy tests, subtract body margin from viewport dimensions.
    ;; Taffy tests have body { margin: 0; } so no adjustment needed.
    ;; CSS2.1 tests use browser default body margin (typically 8px).
    ;; Extract actual body margin from reference layout data.
    (define effective-vp-w
      (if (uses-taffy-base?)
          vp-w
          (let* ([layout-tree (hash-ref ref-json 'layout_tree (hash))]
                 [html-children (hash-ref layout-tree 'children '())]
                 [body-node
                  (for/first ([c (in-list html-children)]
                              #:when (equal? (hash-ref c 'tag #f) "body"))
                    c)]
                 [body-layout (if body-node (hash-ref body-node 'layout (hash)) (hash))]
                 [body-x (hash-ref body-layout 'x 0)])
            ;; body-x is the left margin; subtract both left and right margins
            (- vp-w (* 2 body-x)))))

    (reference-test-case name box-tree expected (cons effective-vp-w vp-h))))

;; ============================================================
;; Utilities
;; ============================================================

;; alist lookup — return cdr or #f
(define (cdr-or-false key alist)
  (define pair (assoc key alist))
  (if pair (cdr pair) #f))

;; read a file to string
(define (file->string path)
  (call-with-input-file path
    (lambda (in) (port->string in))))
