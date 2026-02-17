;; R7RS Benchmark: tak
;; Takeuchi function - tak(18, 12, 6) = 7

(define (tak x y z)
  (if (>= y x)
      z
      (tak (tak (- x 1) y z)
           (tak (- y 1) z x)
           (tak (- z 1) x y))))

(define (benchmark)
  (tak 18 12 6))

(let* ((start (get-internal-real-time))
       (result (benchmark))
       (elapsed-ms (* 1000.0 (/ (- (get-internal-real-time) start) internal-time-units-per-second))))
  (if (= result 7)
      (display "tak: PASS\n")
      (begin (display "tak: FAIL result=") (display result) (newline)))
  (display "TIME_MS=") (display elapsed-ms) (newline))
