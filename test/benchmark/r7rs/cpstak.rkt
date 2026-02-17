#lang racket/base
;; R7RS Benchmark: cpstak
;; CPS Takeuchi function - cpstak(18, 12, 6) = 7

(define (cpstak x y z k)
  (if (>= y x)
      (k z)
      (cpstak (- x 1) y z
              (lambda (v1)
                (cpstak (- y 1) z x
                        (lambda (v2)
                          (cpstak (- z 1) x y
                                  (lambda (v3)
                                    (cpstak v1 v2 v3 k)))))))))

(define (benchmark)
  (cpstak 18 12 6 (lambda (a) a)))

(define start-time (current-inexact-milliseconds))
(define result (benchmark))
(define elapsed (- (current-inexact-milliseconds) start-time))
(if (= result 7)
    (displayln "cpstak: PASS")
    (begin (display "cpstak: FAIL result=") (displayln result)))
(display "TIME_MS=") (displayln elapsed)
