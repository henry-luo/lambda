;; R7RS Benchmark: mbrot
;; Mandelbrot set generation - 75x75 grid
;; Expected: count at (0,0) = 5

(define (count r i step x y)
  (let* ((max-count 64)
         (radius2 16.0)
         (cr (+ r (* x step)))
         (ci (+ i (* y step))))
    (let loop ((zr cr) (zi ci) (c 0))
      (if (>= c max-count)
          max-count
          (let ((zr2 (* zr zr))
                (zi2 (* zi zi)))
            (if (> (+ zr2 zi2) radius2)
                c
                (loop (+ (- zr2 zi2) cr)
                      (+ (* 2.0 zr zi) ci)
                      (+ c 1))))))))

(define (mbrot matrix r i step n)
  (let loop-y ((y (- n 1)))
    (when (>= y 0)
      (let loop-x ((x (- n 1)))
        (when (>= x 0)
          (vector-set! (vector-ref matrix x) y
                       (count r i step (exact->inexact x) (exact->inexact y)))
          (loop-x (- x 1))))
      (loop-y (- y 1)))))

(define (test n)
  (let ((matrix (let loop ((i 0) (acc '()))
                  (if (= i n)
                      (list->vector (reverse acc))
                      (loop (+ i 1) (cons (make-vector n 0) acc))))))
    (mbrot matrix -1.0 -0.5 0.005 n)
    (vector-ref (vector-ref matrix 0) 0)))

(define (benchmark)
  (test 75))

(let* ((start (get-internal-real-time))
       (result (benchmark))
       (elapsed-ms (* 1000.0 (/ (- (get-internal-real-time) start) internal-time-units-per-second))))
  (if (= result 5)
      (display "mbrot: PASS\n")
      (begin (display "mbrot: FAIL result=") (display result) (newline)))
  (display "TIME_MS=") (display elapsed-ms) (newline))
