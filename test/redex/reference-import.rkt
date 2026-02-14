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

(define base-defaults
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

;; ============================================================
;; HTML Inline Style Parser
;; ============================================================

;; parse a CSS inline style string into an alist of (property . value)
;; e.g. "width: 100px; display: block;" → ((width . "100px") (display . "block"))
(define (parse-inline-style style-str)
  (if (or (not style-str) (string=? style-str ""))
      '()
      (let ([decls (string-split style-str ";")])
        (filter-map
         (lambda (decl)
           (let ([parts (string-split (string-trim decl) ":")])
             (if (>= (length parts) 2)
                 (cons (string->symbol (string-trim (car parts)))
                       (string-trim (string-join (cdr parts) ":")))
                 #f)))
         decls))))

;; extract the element tree from an HTML file body.
;; returns a nested structure: (element tag attrs children)
;; where attrs is an alist including 'style if present.
;; only handles div elements (sufficient for Taffy tests).
(define (html-file->inline-styles html-path)
  (define html-content (file->string html-path))
  (parse-html-body html-content))

;; parse body content to extract element tree
(define (parse-html-body html-str)
  ;; extract body content
  (define body-match
    (regexp-match #rx"(?i:<body[^>]*>(.*)</body>)" html-str))
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
      ;; find next opening tag
      (define tag-match
        (regexp-match-positions
         #rx"<(div|span|img)([^>]*)>"
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
        (define inline-style
          (if style-match
              (parse-inline-style (cadr style-match))
              '()))

        ;; check for self-closing
        (define self-closing?
          (or (string=? tag-name "img")
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
(define (parse-children-until html-str start tag-name)
  (define children '())
  (define pos start)
  (define len (string-length html-str))
  (define close-rx (regexp (string-append "</" tag-name ">")))

  (let loop ()
    (when (< pos len)
      ;; look for either an opening tag or a closing tag
      (define open-match
        (regexp-match-positions
         #rx"<(div|span|img)([^>]*)>"
         html-str pos))
      (define close-match
        (regexp-match-positions close-rx html-str pos))

      (cond
        ;; closing tag comes first (or no more opens)
        [(and close-match
              (or (not open-match)
                  (<= (caar close-match) (caar open-match))))
         ;; done with this element
         (set! pos (cdar close-match))]

        ;; opening tag found before close
        [open-match
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
         (define inline-style
           (if style-match (parse-inline-style (cadr style-match)) '()))

         (define self-closing?
           (or (string=? child-tag "img")
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

;; classify an HTML test file into: 'simple (inline-only divs),
;; 'style-block (has <style>), 'complex (spans, text, images, etc.)
(define (classify-html-test html-path)
  (define content (file->string html-path))
  (cond
    [(regexp-match? #rx"<style" content) 'style-block]
    [(regexp-match? #rx"<span" content) 'complex]
    [(regexp-match? #rx"<img" content) 'complex]
    ;; check for non-whitespace text content between > and < on the same line
    ;; this matches actual text nodes like ">some text<" but not ">\n  <"
    [(let ([body-match (regexp-match #rx"(?i:<body[^>]*>(.*)</body>)" content)])
       (and body-match
            (regexp-match? #rx">[^ \t\r\n<][^<]*<" (cadr body-match))))
     'complex]
    [else 'simple]))

;; ============================================================
;; Inline Styles → Redex Styles
;; ============================================================

;; build Redex styles from parsed inline styles + base defaults.
;; is-root?: whether this is the body > * element (gets position:absolute)
(define (inline-styles->redex-styles inline-alist is-root? has-class)
  (define props '())

  (define (add! name val)
    (set! props (cons `(,name ,val) props)))

  ;; determine display type
  (define display-str
    (or (cdr-or-false 'display inline-alist)
        (hash-ref base-defaults 'display)))

  ;; determine box-sizing (check class for .content-box / .border-box)
  (define box-sizing
    (cond
      [(and has-class (string-contains? has-class "content-box")) "content-box"]
      [(and has-class (string-contains? has-class "border-box")) "border-box"]
      [(cdr-or-false 'box-sizing inline-alist)]
      [else (hash-ref base-defaults 'box-sizing)]))

  (add! 'box-sizing (string->symbol box-sizing))

  ;; position: root gets absolute, others get inline or base default
  (define position
    (cond
      [is-root? "absolute"]
      [(cdr-or-false 'position inline-alist)]
      [else (hash-ref base-defaults 'position)]))
  (add! 'position (string->symbol position))

  ;; width/height
  (for ([prop '(width height min-width min-height max-width max-height)])
    (define val (cdr-or-false prop inline-alist))
    (when val
      (add! prop (parse-css-length val))))

  ;; margin
  (define margin-vals (parse-edge-shorthand inline-alist 'margin))
  (when margin-vals
    (add! 'margin margin-vals))

  ;; padding
  (define padding-vals (parse-edge-shorthand inline-alist 'padding))
  (when padding-vals
    (add! 'padding padding-vals))

  ;; border-width
  (define border-vals (parse-border-shorthand inline-alist))
  (when border-vals
    (add! 'border-width border-vals))

  ;; flex container properties
  (let ([v (cdr-or-false 'flex-direction inline-alist)])
    (when v (add! 'flex-direction (string->symbol v))))
  (let ([v (cdr-or-false 'flex-wrap inline-alist)])
    (when v (add! 'flex-wrap (string->symbol v))))
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
    (when v
      (let ([gap-px (parse-css-px v)])
        (when gap-px
          (add! 'row-gap gap-px)
          (add! 'column-gap gap-px)))))
  (let ([v (cdr-or-false 'row-gap inline-alist)])
    (when v
      (let ([gap-px (parse-css-px v)])
        (when gap-px (add! 'row-gap gap-px)))))
  (let ([v (cdr-or-false 'column-gap inline-alist)])
    (when v
      (let ([gap-px (parse-css-px v)])
        (when gap-px (add! 'column-gap gap-px)))))

  ;; overflow
  (let ([v (cdr-or-false 'overflow inline-alist)])
    (when v (add! 'overflow (string->symbol v))))

  ;; position offsets
  (for ([prop '(top right bottom left)])
    (define val (cdr-or-false prop inline-alist))
    (when val (add! prop (parse-css-length val))))

  ;; flex shorthand: flex: <grow> <shrink> <basis>
  (let ([v (cdr-or-false 'flex inline-alist)])
    (when v (parse-flex-shorthand! v add!)))

  `(style ,@(reverse props)))

;; ============================================================
;; CSS Value Parsers
;; ============================================================

;; parse a CSS length value to a Redex SizeValue
(define (parse-css-length str)
  (define trimmed (string-trim str))
  (cond
    [(string=? trimmed "auto") 'auto]
    [(string=? trimmed "none") 'none]
    [(string=? trimmed "0") '(px 0)]
    [(string=? trimmed "min-content") 'min-content]
    [(string=? trimmed "max-content") 'max-content]
    [(regexp-match #rx"^(-?[0-9.]+)px$" trimmed) =>
     (lambda (m) `(px ,(string->number (cadr m))))]
    [(regexp-match #rx"^(-?[0-9.]+)%$" trimmed) =>
     (lambda (m) `(% ,(string->number (cadr m))))]
    [(regexp-match #rx"^(-?[0-9.]+)em$" trimmed) =>
     (lambda (m) `(em ,(string->number (cadr m))))]
    [(regexp-match #rx"^(-?[0-9.]+)$" trimmed) =>
     (lambda (m) `(px ,(string->number (cadr m))))]
    [else 'auto]))

;; parse a plain px value, returning just the number or #f
(define (parse-css-px str)
  (define trimmed (string-trim str))
  (cond
    [(string=? trimmed "0") 0]
    [(regexp-match #rx"^(-?[0-9.]+)px$" trimmed) =>
     (lambda (m) (string->number (cadr m)))]
    [(regexp-match #rx"^(-?[0-9.]+)$" trimmed) =>
     (lambda (m) (string->number (cadr m)))]
    [else #f]))

;; parse edge shorthand from inline alist.
;; handles margin/padding with individual sides or shorthand.
(define (parse-edge-shorthand alist prop-name)
  (define top-key (string->symbol (format "~a-top" prop-name)))
  (define right-key (string->symbol (format "~a-right" prop-name)))
  (define bottom-key (string->symbol (format "~a-bottom" prop-name)))
  (define left-key (string->symbol (format "~a-left" prop-name)))

  ;; check individual sides first
  (define t (cdr-or-false top-key alist))
  (define r (cdr-or-false right-key alist))
  (define b (cdr-or-false bottom-key alist))
  (define l (cdr-or-false left-key alist))

  ;; also check shorthand
  (define shorthand (cdr-or-false prop-name alist))

  (cond
    ;; any individual side → build edges
    [(or t r b l)
     (define tv (if t (or (parse-css-px t) 0) 0))
     (define rv (if r (or (parse-css-px r) 0) 0))
     (define bv (if b (or (parse-css-px b) 0) 0))
     (define lv (if l (or (parse-css-px l) 0) 0))
     `(edges ,tv ,rv ,bv ,lv)]

    ;; shorthand present
    [shorthand
     (parse-margin-shorthand shorthand)]

    [else #f]))

;; parse a margin/padding shorthand value: "10px", "10px 20px", "10px 20px 30px", "10px 20px 30px 40px"
(define (parse-margin-shorthand str)
  (define parts (string-split (string-trim str)))
  (define vals (map (lambda (p) (or (parse-css-px p) 0)) parts))
  (match vals
    [(list a) `(edges ,a ,a ,a ,a)]
    [(list tb lr) `(edges ,tb ,lr ,tb ,lr)]
    [(list t lr b) `(edges ,t ,lr ,b ,lr)]
    [(list t r b l) `(edges ,t ,r ,b ,l)]
    [_ #f]))

;; parse border shorthand to extract border-width edges
(define (parse-border-shorthand alist)
  (define bt (cdr-or-false 'border-top-width alist))
  (define br (cdr-or-false 'border-right-width alist))
  (define bb (cdr-or-false 'border-bottom-width alist))
  (define bl (cdr-or-false 'border-left-width alist))
  (define border (cdr-or-false 'border alist))
  (define border-width (cdr-or-false 'border-width alist))

  (cond
    ;; individual sides
    [(or bt br bb bl)
     (define tv (if bt (or (parse-css-px bt) 0) 0))
     (define rv (if br (or (parse-css-px br) 0) 0))
     (define bv (if bb (or (parse-css-px bb) 0) 0))
     (define lv (if bl (or (parse-css-px bl) 0) 0))
     `(edges ,tv ,rv ,bv ,lv)]

    ;; border shorthand (e.g. "1px solid black")
    [border
     (define m (regexp-match #rx"([0-9.]+)px" border))
     (if m
         (let ([w (string->number (cadr m))])
           `(edges ,w ,w ,w ,w))
         #f)]

    ;; border-width shorthand
    [border-width
     (parse-margin-shorthand border-width)]

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
     (when (>= (length parts) 1)
       (let ([g (string->number (car parts))])
         (when g (add! 'flex-grow g))))
     (when (>= (length parts) 2)
       (let ([s (string->number (cadr parts))])
         (when s (add! 'flex-shrink s))))
     (when (>= (length parts) 3)
       (add! 'flex-basis (parse-css-length (caddr parts))))]))

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
    [(string=? v "start") 'flex-start]
    [(string=? v "end") 'flex-end]
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

;; convert a parsed element tree to a Redex box tree.
;; elem: (element tag-name id class inline-alist children)
;; counter: box counter for generating unique ids
;; is-root?: whether this is the body > * element
(define (element->box-tree elem counter is-root?)
  (match elem
    [`(element ,tag ,id ,class ,inline-alist ,children)
     (define box-id
       (if id
           (string->symbol id)
           (string->symbol (format "box~a" (unbox counter)))))
     (set-box! counter (add1 (unbox counter)))

     (define styles (inline-styles->redex-styles inline-alist is-root? class))

     ;; determine display type from inline styles or base default
     (define display-str
       (or (cdr-or-false 'display inline-alist)
           "flex"))  ; base stylesheet default: div { display: flex }

     (define child-boxes
       (map (lambda (c) (element->box-tree c counter #f)) children))

     (define display-sym (string->symbol display-str))
     (case display-sym
       [(flex) `(flex ,box-id ,styles ,child-boxes)]
       [(block) `(block ,box-id ,styles ,child-boxes)]
       [(inline) `(inline ,box-id ,styles ,child-boxes)]
       [(inline-block) `(inline-block ,box-id ,styles ,child-boxes)]
       [(none) `(none ,box-id)]
       ;; grid requires grid-def — skip for now, treat as flex
       [(grid) `(flex ,box-id ,styles ,child-boxes)]
       [else `(flex ,box-id ,styles ,child-boxes)])]
    [_ `(none anon)]))

;; ============================================================
;; Reference JSON → Expected Layout
;; ============================================================

;; extract the expected layout positions from a reference JSON layout tree.
;; skips html, head, body wrappers — starts from the test root (body > div).
;; returns: (expected id x y width height children)
(define (reference->expected-layout ref-json)
  (define layout-tree (hash-ref ref-json 'layout_tree (hash)))
  ;; navigate: html → body → first div (test root)
  (define test-root (find-test-root layout-tree))
  (if test-root
      (layout-node->expected test-root)
      #f))

;; find the test root element (body > div#test-root or first body > div)
(define (find-test-root html-node)
  (define html-children (hash-ref html-node 'children '()))
  ;; find body
  (define body-node
    (for/first ([c (in-list html-children)]
                #:when (equal? (hash-ref c 'tag #f) "body"))
      c))
  (if body-node
      ;; find first div child of body
      (for/first ([c (in-list (hash-ref body-node 'children '()))]
                  #:when (and (equal? (hash-ref c 'nodeType #f) "element")
                              (equal? (hash-ref c 'tag #f) "div")))
        c)
      #f))

;; convert a reference layout node to expected layout structure
;; returns: (expected id x y width height (children ...))
(define (layout-node->expected node)
  (define layout-data (hash-ref node 'layout (hash)))
  (define node-id (or (hash-ref node 'id #f) "anon"))
  (define x (hash-ref layout-data 'x 0))
  (define y (hash-ref layout-data 'y 0))
  (define w (hash-ref layout-data 'width 0))
  (define h (hash-ref layout-data 'height 0))

  ;; recursively process children, skipping non-element nodes
  (define children
    (filter-map
     (lambda (c)
       (and (equal? (hash-ref c 'nodeType #f) "element")
            (not (member (hash-ref c 'tag #f) '("script" "style" "link" "meta" "title")))
            (layout-node->expected c)))
     (hash-ref node 'children '())))

  `(expected ,(if (string? node-id) (string->symbol node-id) 'anon)
             ,x ,y ,w ,h
             ,children))

;; ============================================================
;; Reference JSON → Redex Box Tree
;; ============================================================

;; build a Redex box tree from an HTML file (for the input)
;; and a reference JSON (for the expected output).
;; html-path: path to the HTML test file
;; returns: a Redex Box term
(define (reference->box-tree html-path)
  (define elements (html-file->inline-styles html-path))
  (define counter (box 0))
  (if (null? elements)
      ;; empty test — shouldn't happen
      `(flex root (style) ())
      ;; take the first body-level element as root
      (element->box-tree (car elements) counter #t)))

;; ============================================================
;; Full Test Case Builder
;; ============================================================

;; build a complete test case from an HTML file and its reference JSON.
;; html-path: path to HTML file
;; ref-path: path to reference JSON file
;; returns: reference-test-case struct
(define (reference-file->test-case html-path ref-path)
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

  (reference-test-case name box-tree expected (cons vp-w vp-h)))

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
