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

(let* ((start (get-internal-real-time))
       (result (benchmark))
       (elapsed-ms (* 1000.0 (/ (- (get-internal-real-time) start) internal-time-units-per-second))))
  (if (= result 50005000)
      (display "sum: PASS\n")
      (begin (display "sum: FAIL result=") (display result) (newline)))
  (display "TIME_MS=") (display elapsed-ms) (newline))
