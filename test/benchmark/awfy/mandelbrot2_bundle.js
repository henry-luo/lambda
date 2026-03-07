// AWFY Benchmark: Mandelbrot (standalone bundle)
// Auto-generated from official AWFY source — no require() needed

// --- benchmark.js ---
// @ts-check
// This code is derived from the SOM benchmarks, see AUTHORS.md file.
//
// Copyright (c) 2015-2016 Stefan Marr <git@stefan-marr.de>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the 'Software'), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

class Benchmark {
  innerBenchmarkLoop(innerIterations) {
    for (let i = 0; i < innerIterations; i += 1) {
      if (!this.verifyResult(this.benchmark())) {
        return false;
      }
    }
    return true;
  }

  benchmark() {
    throw new Error('subclass responsibility');
  }

  verifyResult() {
    throw new Error('subclass responsibility');
  }
}



// --- mandelbrot.js ---
// @ts-check
// This benchmark is adapted to match the SOM version.
//
// Copyright © 2004-2013 Brent Fulgham
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * Neither the name of "The Computer Language Benchmarks Game" nor the name
//     of "The Computer Language Shootout Benchmarks" nor the names of its
//     contributors may be used to endorse or promote products derived from this
//     software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The Computer Language Benchmarks Game
// http://benchmarksgame.alioth.debian.org
//
//  contributed by Karl von Laudermann
//  modified by Jeremy Echols
//  modified by Detlef Reichl
//  modified by Joseph LaFata
//  modified by Peter Zotov

// http://benchmarksgame.alioth.debian.org/u64q/program.php?test=mandelbrot&lang=yarv&id=3


class Mandelbrot extends Benchmark {
  verifyResult(result, innerIterations) {
    if (innerIterations === 500) { return result === 191; }
    if (innerIterations === 750) { return result === 50; }
    if (innerIterations === 1) { return result === 128; }

    process.stdout.write(`No verification result for ${innerIterations} found\n`);
    process.stdout.write(`Result is: ${result}\n`);
    return false;
  }

  mandelbrot(size) {
    let sum = 0;
    let byteAcc = 0;
    let bitNum = 0;

    let y = 0;

    while (y < size) {
      const ci = ((2.0 * y) / size) - 1.0;
      let x = 0;

      while (x < size) {
        let zrzr = 0.0;
        let zi = 0.0;
        let zizi = 0.0;
        const cr = ((2.0 * x) / size) - 1.5;

        let z = 0;
        let notDone = true;
        let escape = 0;
        while (notDone && z < 50) {
          const zr = zrzr - zizi + cr;
          zi = 2.0 * zr * zi + ci;

          // preserve recalculation
          zrzr = zr * zr;
          zizi = zi * zi;

          if (zrzr + zizi > 4.0) {
            notDone = false;
            escape = 1;
          }
          z += 1;
        }

        byteAcc = (byteAcc << 1) + escape;
        bitNum += 1;

        // Code is very similar for these cases, but using separate blocks
        // ensures we skip the shifting when it's unnecessary, which is most cases.
        if (bitNum === 8) {
          sum ^= byteAcc;
          byteAcc = 0;
          bitNum = 0;
        } else if (x === size - 1) {
          byteAcc <<= (8 - bitNum);
          sum ^= byteAcc;
          byteAcc = 0;
          bitNum = 0;
        }
        x += 1;
      }
      y += 1;
    }
    return sum;
  }

  innerBenchmarkLoop(innerIterations) {
    return this.verifyResult(this.mandelbrot(innerIterations), innerIterations);
  }
}



// --- timing harness ---
const bench = new Mandelbrot();
const __t0 = process.hrtime.bigint();
const ok = bench.innerBenchmarkLoop(500);
const __t1 = process.hrtime.bigint();
process.stdout.write("Mandelbrot: " + (ok ? "PASS" : "FAIL") + "\n");
process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
