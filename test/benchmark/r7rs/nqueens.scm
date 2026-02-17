;; R7RS Benchmark: nqueens
;; Count all solutions to N-Queens problem - nqueens(8) = 92

(define (ok? row dist placed)
  (cond
    ((null? placed) #t)
    ((= (car placed) (+ row dist)) #f)
    ((= (car placed) (- row dist)) #f)
    (else (ok? row (+ dist 1) (cdr placed)))))

(define (iota-from n)
  (let loop ((i n) (acc '()))
    (if (= i 0) acc (loop (- i 1) (cons i acc)))))

(define (nqueens n)
  (let solve ((candidates (iota-from n))
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

(let* ((start (get-internal-real-time))
       (result (benchmark))
       (elapsed-ms (* 1000.0 (/ (- (get-internal-real-time) start) internal-time-units-per-second))))
  (if (= result 92)
      (display "nqueens: PASS\n")
      (begin (display "nqueens: FAIL result=") (display result) (newline)))
  (display "TIME_MS=") (display elapsed-ms) (newline))
