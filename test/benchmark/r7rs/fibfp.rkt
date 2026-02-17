#lang racket/base
;; R7RS Benchmark: fibfp
;; Fibonacci using floating-point arithmetic - fibfp(27.0) = 196418.0

(define (fibfp n)
  (if (< n 2.0)
      n
      (+ (fibfp (- n 1.0)) (fibfp (- n 2.0)))))

(define (benchmark)
  (fibfp 27.0))

(define start-time (current-inexact-milliseconds))
(define result (benchmark))
(define elapsed (- (current-inexact-milliseconds) start-time))
(if (= result 196418.0)
    (displayln "fibfp: PASS")
    (begin (display "fibfp: FAIL result=") (displayln result)))
(display "TIME_MS=") (displayln elapsed)
