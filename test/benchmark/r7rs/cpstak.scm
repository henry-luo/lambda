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

(let* ((start (get-internal-real-time))
       (result (benchmark))
       (elapsed-ms (* 1000.0 (/ (- (get-internal-real-time) start) internal-time-units-per-second))))
  (if (= result 7)
      (display "cpstak: PASS\n")
      (begin (display "cpstak: FAIL result=") (display result) (newline)))
  (display "TIME_MS=") (display elapsed-ms) (newline))
