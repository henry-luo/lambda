#lang racket/base
(require "font-metrics.rkt")

(load-font-metrics!)
(printf "Metrics loaded: ~a\n" (font-metrics-loaded?))
(printf "Times line-height ratio: ~a\n" (font-line-height-ratio 'times))
(printf "Arial line-height ratio: ~a\n" (font-line-height-ratio 'arial))
(printf "Times ascender ratio: ~a\n" (font-ascender-ratio 'times))
(printf "Times descender ratio: ~a\n" (font-descender-ratio 'times))
(printf "Arial ascender ratio: ~a\n" (font-ascender-ratio 'arial))
(printf "Arial descender ratio: ~a\n" (font-descender-ratio 'arial))
(printf "Char A width (Times, 16px): ~a\n" (char-width-from-metrics #\A 16 'times))
(printf "Char a width (Times, 16px): ~a\n" (char-width-from-metrics #\a 16 'times))
(printf "Char A width (Arial, 16px): ~a\n" (char-width-from-metrics #\A 16 'arial))
(printf "Space width  (Times, 16px): ~a\n" (char-width-from-metrics #\space 16 'times))
(printf "Measure 'Hello' (Times, 16px): ~a\n" (measure-text-with-metrics "Hello" 16 'times))
(printf "Measure 'Hello' (Arial, 16px): ~a\n" (measure-text-with-metrics "Hello" 16 'arial))
(printf "\nComparison with old hardcoded values:\n")
(printf "  Times LH ratio: old=1.107, new=~a (Δ=~a)\n"
        (font-line-height-ratio 'times)
        (- (font-line-height-ratio 'times) 1.107))
(printf "  Arial LH ratio: old=1.15,  new=~a (Δ=~a)\n"
        (font-line-height-ratio 'arial)
        (- (font-line-height-ratio 'arial) 1.15))
