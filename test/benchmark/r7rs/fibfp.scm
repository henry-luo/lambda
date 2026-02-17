;; R7RS Benchmark: fibfp
;; Fibonacci using floating-point arithmetic - fibfp(27.0) = 196418.0

(define (fibfp n)
  (if (< n 2.0)
      n
      (+ (fibfp (- n 1.0)) (fibfp (- n 2.0)))))

(define (benchmark)
  (fibfp 27.0))

(let* ((start (get-internal-real-time))
       (result (benchmark))
       (elapsed-ms (* 1000.0 (/ (- (get-internal-real-time) start) internal-time-units-per-second))))
  (if (= result 196418.0)
      (display "fibfp: PASS\n")
      (begin (display "fibfp: FAIL result=") (display result) (newline)))
  (display "TIME_MS=") (display elapsed-ms) (newline))
