// find()/replace() options test suite

'===== FIND REPLACE OPTIONS TESTS ====='

// literal find limit windows
'Test 1: Literal Find Limits'

1; find("ab--ab--ab--ab", "ab", {limit: 2})
2; find("ab--ab--ab--ab", "ab", {last: 2})

// literal find ignore_case
'Test 2: Literal Find Ignore Case'

3; find("ab--AB--aB", "ab", {ignore_case: true})
4; find("ab--AB--aB", "ab", {ignore_case: true, last: 1})

// literal replace options
'Test 3: Literal Replace Limits'

5; replace("ab--ab--ab--ab", "ab", "X", {limit: 2})
6; replace("ab--ab--ab--ab", "ab", "X", {last: 2})
7; replace("ab--ab--ab--ab", "ab", "X", {limit: 0})
8; replace("ab--AB--aB", "ab", "X", {ignore_case: true, last: 1})

// pattern find/replace options
'Test 4: Pattern Options'

type abpat = "ab"

9; find("ab--AB--aB", abpat, {ignore_case: true, last: 2})
10; find("ab--ab--ab", abpat, {limit: 0})
11; replace("ab--AB--aB", abpat, "Y", {ignore_case: true, limit: 2})
12; replace("ab--AB--aB", abpat, "Y", {ignore_case: true, last: 1})

// literal scans share one memchr kernel; replace() of one byte by one byte at
// every match takes a whole-string select instead
'Test 5: Literal Scan Paths'

13; replace("aaaa", "aa", "b")
14; replace("aaa", "aa", "b")
15; find("aaaa", "aa")
16; replace("aaab", "aab", "X")
17; replace("ACGTTGCA", "A", "T")
18; replace("the quick brown fox jumps over the lazy dog, the quick brown fox jumps!", "o", "0")
19; replace('a.b.c', ".", "-")
20; replace("xAxaXa", "a", "-", {ignore_case: true})
21; replace("a.b.c.d", ".", "", {last: 2})
22; replace("naïve café", "é", "e")
23; find("x-y--z", "-", {last: 2})
