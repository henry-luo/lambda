#lang racket/base
;; R7RS Benchmark: sumfp
;; Sum of floats from 0.0 to 100000.0

(define (run n)
  (let loop ((i n) (s 0.0))
    (if (< i 0.0)
        s
        (loop (- i 1.0) (+ s i)))))

(define (benchmark)
  (run 100000.0))

(define start-time (current-inexact-milliseconds))
(define result (benchmark))
(define elapsed (- (current-inexact-milliseconds) start-time))
(if (< (abs (- result 5000050000.0)) 1.0)
    (displayln "sumfp: PASS")
    (begin (display "sumfp: FAIL result=") (displayln result)))
(display "TIME_MS=") (displayln elapsed)
