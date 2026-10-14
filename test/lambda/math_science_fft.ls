// Independent DFT oracle checks sign, ordering, and arbitrary-length transforms.
import m:math;
fn close(a, b) => len(a) == len(b) and max([for (i in 0 to len(a)-1) abs(a[i]-b[i])]) < 1e-9
fn dft(x) {
    let n = len(x);
    [for (k in 0 to n-1)
        reduce([for (j in 0 to n-1)
            complex(x[j]) * complex(m.cos(-2*m.pi*k*j/n), m.sin(-2*m.pi*k*j/n))], (a,b) => a+b)]
}
fn samples(n) => [for (j in 0 to n-1) complex(j%3 - 1, j%5 - 2)]

'=== forward and inverse conventions ===';
[close(m.fft([1,0,0,0]), [1,1,1,1]),
 close(m.fft([0,1,0,0]), [1,-1j,-1,1j]),
 close(m.ifft([1,1,1,1]), [1,0,0,0])]

'=== prime composite and power-of-two lengths ===';
[for (n in [1,2,3,5,7,8,9,16,17]) {
    let x = samples(n);
    [close(m.fft(x), dft(x)), close(m.ifft(m.fft(x)), x)]
}]

'=== real transforms and explicit odd length ===';
[for (x in [[7], [2,3], [1,2,3,4,5], [2,0,-3,1,2,4]]) {
    let spectrum = m.rfft(x);
    [len(spectrum) == floor(len(x)/2)+1,
     close(spectrum, slice(m.fft(x), 0, len(spectrum))),
     close(m.irfft(spectrum, len(x)), x)]
}]

'=== ranges views and qualified namespace ==='
fn transform_fn(x) any | error => m.fft(x)
let transform = transform_fn;
[close(m.fft(1 to 5), dft([1,2,3,4,5])),
 close(m.fft(subview([99,1,2,3,99],1,4)), dft([1,2,3])),
 close(lambda.math.fft([2,3]), [5,-1]), close(transform([2,3]), [5,-1])]

'=== empty singleton and absence ===';
[m.fft([]), m.ifft([]), m.rfft([]), m.irfft([],0),
 m.fft([2j]), m.irfft([7],1), m.fft(null), m.irfft(null,3)]

'=== invalid spectra shapes and lengths ===';
[m.fft(3) is error, m.fft([[1,2],[3,4]]) is error,
 m.fft([1,null]) is error, m.fft([1,"x"]) is error,
 m.rfft([1j]) is error, m.irfft([1,2],3.5) is error,
 m.irfft([1,2],-1) is error, m.irfft([1,2],4) is error,
 m.irfft([1j,2],2) is error, m.irfft([1,2j],2) is error,
 m.fft([inf]) is error]
