#lang racket/base
;; R7RS Benchmark: tak
;; Takeuchi function - tak(18, 12, 6) = 7

(define (tak x y z)
  (if (>= y x)
      z
      (tak (tak (- x 1) y z)
           (tak (- y 1) z x)
           (tak (- z 1) x y))))

(define (benchmark)
  (tak 18 12 6))

(define start-time (current-inexact-milliseconds))
(define result (benchmark))
(define elapsed (- (current-inexact-milliseconds) start-time))
(if (= result 7)
    (displayln "tak: PASS")
    (begin (display "tak: FAIL result=") (displayln result)))
(display "TIME_MS=") (displayln elapsed)
