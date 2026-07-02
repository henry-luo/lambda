// Stage 4C — in-engine test harness shim.
//
// A tiny vitest-compatible surface so the editor's `.test.ts` files can run
// UNMODIFIED on the LambdaJS runtime (`lambda.exe js`), where vitest cannot.
// The Stage-4C build (tools/build-conformance.mjs) aliases `vitest` to this
// module and bundles the tests to a classic IIFE. Only the API the suite
// actually uses is implemented (surveyed: describe/it/it.skip/expect +
// {toBe,toEqual,toBeNull,toContain,toHaveLength,toBeTruthy,toContainEqual,
// toBeCloseTo,toBeGreaterThan,not.*} + beforeEach/afterEach + vi.fn).
//
// After all test files are imported, the bundle entry calls __harnessRun(),
// which executes every registered case and prints a machine-readable summary
// ("HARNESS pass=N fail=M skip=K") plus one "FAIL <name>: <msg>" per failure.

type Fn = () => void | Promise<void>

interface Case { name: string; fn: Fn; skip: boolean; before: Fn[]; after: Fn[] }
interface Scope { prefix: string; before: Fn[]; after: Fn[] }

const cases: Case[] = []
const scopeStack: Scope[] = [{ prefix: '', before: [], after: [] }]

function curScope(): Scope { return scopeStack[scopeStack.length - 1] as Scope }

export function describe(name: string, fn: () => void): void {
  const parent = curScope()
  scopeStack.push({
    prefix: parent.prefix ? parent.prefix + ' > ' + name : name,
    before: [...parent.before],
    after: [...parent.after]
  })
  try { fn() } finally { scopeStack.pop() }
}

function register(name: string, fn: Fn, skip: boolean): void {
  const s = curScope()
  // Snapshot the enclosing describe's hooks now — at run time scopeStack is
  // back at the root, so hooks must be captured per-case at registration.
  cases.push({ name: s.prefix ? s.prefix + ' > ' + name : name, fn, skip, before: [...s.before], after: [...s.after] })
}

interface ItFn { (name: string, fn: Fn): void; skip: (name: string, fn?: Fn) => void; only: (name: string, fn: Fn) => void }
export const it = ((name: string, fn: Fn) => register(name, fn, false)) as ItFn
it.skip = (name: string, _fn?: Fn) => register(name, () => {}, true)
it.only = (name: string, fn: Fn) => register(name, fn, false)  // no isolation in-engine; runs like it
export const test = it

export function beforeEach(fn: Fn): void { curScope().before.push(fn) }
export function afterEach(fn: Fn): void { curScope().after.push(fn) }

// --- vi (minimal) --------------------------------------------------------
interface MockFn { (...args: any[]): any; mock: { calls: any[][] } }
export const vi = {
  fn(impl?: (...a: any[]) => any): MockFn {
    const f = ((...args: any[]) => { f.mock.calls.push(args); return impl ? impl(...args) : undefined }) as MockFn
    f.mock = { calls: [] }
    return f
  }
}

// --- deep equality (vitest toEqual semantics: structural, undefined≈absent) -
function deepEqual(a: any, b: any): boolean {
  if (Object.is(a, b)) return true
  if (a === null || b === null || typeof a !== 'object' || typeof b !== 'object') return false
  if (Array.isArray(a) || Array.isArray(b)) {
    if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) return false
    for (let i = 0; i < a.length; i++) if (!deepEqual(a[i], b[i])) return false
    return true
  }
  const keys = new Set<string>([...Object.keys(a), ...Object.keys(b)])
  for (const k of keys) {
    const av = a[k], bv = b[k]
    if (av === undefined && bv === undefined) continue  // undefined ≈ absent
    if (!deepEqual(av, bv)) return false
  }
  return true
}

function fmt(v: any): string {
  try { return JSON.stringify(v) } catch { return String(v) }
}

class AssertionError extends Error {}

function makeExpect(actual: any, negate: boolean) {
  const ok = (pass: boolean, msg: string) => {
    if (pass === negate) throw new AssertionError(msg)
  }
  return {
    toBe(expected: any) { ok(Object.is(actual, expected), `expected ${fmt(actual)} ${negate ? 'not ' : ''}to be ${fmt(expected)}`) },
    toEqual(expected: any) { ok(deepEqual(actual, expected), `expected ${fmt(actual)} ${negate ? 'not ' : ''}toEqual ${fmt(expected)}`) },
    toStrictEqual(expected: any) { ok(deepEqual(actual, expected), `expected ${fmt(actual)} ${negate ? 'not ' : ''}toStrictEqual ${fmt(expected)}`) },
    toBeNull() { ok(actual === null, `expected ${fmt(actual)} ${negate ? 'not ' : ''}to be null`) },
    toBeUndefined() { ok(actual === undefined, `expected ${fmt(actual)} ${negate ? 'not ' : ''}to be undefined`) },
    toBeDefined() { ok(actual !== undefined, `expected value ${negate ? '' : 'not '}to be undefined`) },
    toBeTruthy() { ok(!!actual, `expected ${fmt(actual)} ${negate ? 'not ' : ''}to be truthy`) },
    toBeFalsy() { ok(!actual, `expected ${fmt(actual)} ${negate ? 'not ' : ''}to be falsy`) },
    toContain(item: any) {
      const pass = typeof actual === 'string' ? actual.indexOf(item) >= 0 : Array.isArray(actual) && actual.indexOf(item) >= 0
      ok(pass, `expected ${fmt(actual)} ${negate ? 'not ' : ''}to contain ${fmt(item)}`)
    },
    toContainEqual(item: any) {
      const pass = Array.isArray(actual) && actual.some((e: any) => deepEqual(e, item))
      ok(pass, `expected ${fmt(actual)} ${negate ? 'not ' : ''}to contain equal ${fmt(item)}`)
    },
    toHaveLength(n: number) { ok(actual != null && actual.length === n, `expected length ${actual == null ? 'null' : actual.length} ${negate ? 'not ' : ''}to be ${n}`) },
    toBeGreaterThan(n: number) { ok(actual > n, `expected ${fmt(actual)} ${negate ? 'not ' : ''}to be > ${n}`) },
    toBeLessThan(n: number) { ok(actual < n, `expected ${fmt(actual)} ${negate ? 'not ' : ''}to be < ${n}`) },
    toBeCloseTo(n: number, digits = 2) { ok(Math.abs(actual - n) < Math.pow(10, -digits) / 2, `expected ${fmt(actual)} ${negate ? 'not ' : ''}to be close to ${n}`) },
    toMatch(re: RegExp | string) { const r = typeof re === 'string' ? new RegExp(re) : re; ok(r.test(actual), `expected ${fmt(actual)} ${negate ? 'not ' : ''}to match ${re}`) },
    toThrow(expected?: any) {
      let threw = false, err: any = null
      try { (actual as Fn)() } catch (e) { threw = true; err = e }
      let pass = threw
      if (threw && expected !== undefined) {
        const m = err && err.message ? err.message : String(err)
        pass = typeof expected === 'string' ? m.indexOf(expected) >= 0 : (expected instanceof RegExp ? expected.test(m) : true)
      }
      ok(pass, `expected function ${negate ? 'not ' : ''}to throw${expected !== undefined ? ' ' + fmt(expected) : ''}`)
    },
    // vi.fn() mock matchers — read the recorded calls off actual.mock.calls
    toHaveBeenCalled() {
      const calls = actual && actual.mock ? actual.mock.calls : null
      ok(!!calls && calls.length > 0, `expected mock ${negate ? 'not ' : ''}to have been called`)
    },
    toHaveBeenCalledTimes(n: number) {
      const calls = actual && actual.mock ? actual.mock.calls : null
      ok(!!calls && calls.length === n, `expected mock ${negate ? 'not ' : ''}to have been called ${n} time(s), got ${calls ? calls.length : 'non-mock'}`)
    },
    toHaveBeenCalledWith(...args: any[]) {
      const calls = actual && actual.mock ? actual.mock.calls : null
      const pass = !!calls && calls.some((c: any[]) => deepEqual(c, args))
      ok(pass, `expected mock ${negate ? 'not ' : ''}to have been called with ${fmt(args)}`)
    }
  }
}

export function expect(actual: any): any {
  const base = makeExpect(actual, false) as any
  base.not = makeExpect(actual, true)
  return base
}

// --- runner --------------------------------------------------------------
declare const console: { log: (...a: any[]) => void }

export function __harnessRun(): void {
  let pass = 0, fail = 0, skip = 0
  const failures: string[] = []
  for (const c of cases) {
    if (c.skip) { skip++; continue }
    try {
      for (const b of c.before) b()
      const r = c.fn()
      if (r && typeof (r as any).then === 'function') {
        // Synchronous harness: async cases are not supported in-engine yet.
        throw new AssertionError('async test not supported in-engine harness')
      }
      for (const a of c.after) a()
      pass++
    } catch (e: any) {
      fail++
      failures.push('FAIL ' + c.name + ': ' + (e && e.message ? e.message : String(e)))
    }
  }
  for (const f of failures) console.log(f)
  console.log('HARNESS pass=' + pass + ' fail=' + fail + ' skip=' + skip)
}
