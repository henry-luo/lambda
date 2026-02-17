#lang racket/base
;; R7RS Benchmark: fib
;; Naive recursive Fibonacci - fib(27) = 196418

(define (fib n)
  (if (< n 2)
      n
      (+ (fib (- n 1)) (fib (- n 2)))))

(define (benchmark)
  (fib 27))

(define start-time (current-inexact-milliseconds))
(define result (benchmark))
(define elapsed (- (current-inexact-milliseconds) start-time))
(if (= result 196418)
    (displayln "fib: PASS")
    (begin (display "fib: FAIL result=") (displayln result)))
(display "TIME_MS=") (displayln elapsed)
