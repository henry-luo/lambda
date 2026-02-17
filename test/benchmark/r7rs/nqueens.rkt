#lang racket/base
;; R7RS Benchmark: nqueens
;; Count all solutions to N-Queens problem - nqueens(8) = 92

(define (ok? row dist placed)
  (cond
    ((null? placed) #t)
    ((= (car placed) (+ row dist)) #f)
    ((= (car placed) (- row dist)) #f)
    (else (ok? row (+ dist 1) (cdr placed)))))

(define (nqueens n)
  (let solve ((candidates (build-list n (lambda (i) (+ i 1))))
              (rest '())
              (placed '()))
    (if (null? candidates)
        (if (null? rest) 1 0)
        (let ((row (car candidates))
              (remaining (append (cdr candidates) rest)))
          (+ (if (ok? row 1 placed)
                 (solve remaining '() (cons row placed))
                 0)
             (solve (cdr candidates) (cons row rest) placed))))))

(define (benchmark)
  (nqueens 8))

(define start-time (current-inexact-milliseconds))
(define result (benchmark))
(define elapsed (- (current-inexact-milliseconds) start-time))
(if (= result 92)
    (displayln "nqueens: PASS")
    (begin (display "nqueens: FAIL result=") (displayln result)))
(display "TIME_MS=") (displayln elapsed)
