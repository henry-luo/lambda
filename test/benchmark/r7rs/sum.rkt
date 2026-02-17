#lang racket/base
;; R7RS Benchmark: sum
;; Sum of integers from 0 to 10000, repeated 100 times

(define (run n)
  (let loop ((i n) (s 0))
    (if (< i 0)
        s
        (loop (- i 1) (+ s i)))))

(define (benchmark)
  (let iter ((count 0) (result 0))
    (if (>= count 100)
        result
        (iter (+ count 1) (run 10000)))))

(define start-time (current-inexact-milliseconds))
(define result (benchmark))
(define elapsed (- (current-inexact-milliseconds) start-time))
(if (= result 50005000)
    (displayln "sum: PASS")
    (begin (display "sum: FAIL result=") (displayln result)))
(display "TIME_MS=") (displayln elapsed)
