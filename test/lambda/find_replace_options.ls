// find()/replace() options test suite

'===== FIND REPLACE OPTIONS TESTS ====='

// literal find limit windows
'Test 1: Literal Find Limits'

1; find("ab--ab--ab--ab", "ab", {limit: 2})
2; find("ab--ab--ab--ab", "ab", {limit: -2})

// literal find ignore_case
'Test 2: Literal Find Ignore Case'

3; find("ab--AB--aB", "ab", {ignore_case: true})
4; find("ab--AB--aB", "ab", {ignore_case: true, limit: -1})

// literal replace options
'Test 3: Literal Replace Limits'

5; replace("ab--ab--ab--ab", "ab", "X", {limit: 2})
6; replace("ab--ab--ab--ab", "ab", "X", {limit: -2})
7; replace("ab--ab--ab--ab", "ab", "X", {limit: 0})
8; replace("ab--AB--aB", "ab", "X", {ignore_case: true, limit: -1})

// pattern find/replace options
'Test 4: Pattern Options'

type abpat = "ab"

9; find("ab--AB--aB", abpat, {ignore_case: true, limit: -2})
10; find("ab--ab--ab", abpat, {limit: 0})
11; replace("ab--AB--aB", abpat, "Y", {ignore_case: true, limit: 2})
12; replace("ab--AB--aB", abpat, "Y", {ignore_case: true, limit: -1})
