;; R7RS Benchmark: sumfp
;; Sum of floats from 0.0 to 100000.0

(define (run n)
  (let loop ((i n) (s 0.0))
    (if (< i 0.0)
        s
        (loop (- i 1.0) (+ s i)))))

(define (benchmark)
  (run 100000.0))

(let* ((start (get-internal-real-time))
       (result (benchmark))
       (elapsed-ms (* 1000.0 (/ (- (get-internal-real-time) start) internal-time-units-per-second))))
  (if (< (abs (- result 5000050000.0)) 1.0)
      (display "sumfp: PASS\n")
      (begin (display "sumfp: FAIL result=") (display result) (newline)))
  (display "TIME_MS=") (display elapsed-ms) (newline))
