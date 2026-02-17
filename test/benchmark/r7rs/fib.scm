;; R7RS Benchmark: fib
;; Naive recursive Fibonacci - fib(27) = 196418

(define (fib n)
  (if (< n 2)
      n
      (+ (fib (- n 1)) (fib (- n 2)))))

(define (benchmark)
  (fib 27))

(let* ((start (get-internal-real-time))
       (result (benchmark))
       (elapsed-ms (* 1000.0 (/ (- (get-internal-real-time) start) internal-time-units-per-second))))
  (if (= result 196418)
      (display "fib: PASS\n")
      (begin (display "fib: FAIL result=") (display result) (newline)))
  (display "TIME_MS=") (display elapsed-ms) (newline))
