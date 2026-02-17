#lang racket/base
;; R7RS Benchmark: ack
;; Ackermann function - ack(3, 8) = 2045

(define (ack m n)
  (cond
    ((= m 0) (+ n 1))
    ((= n 0) (ack (- m 1) 1))
    (else (ack (- m 1) (ack m (- n 1))))))

(define (benchmark)
  (ack 3 8))

(define start-time (current-inexact-milliseconds))
(define result (benchmark))
(define elapsed (- (current-inexact-milliseconds) start-time))
(if (= result 2045)
    (displayln "ack: PASS")
    (begin (display "ack: FAIL result=") (displayln result)))
(display "TIME_MS=") (displayln elapsed)
