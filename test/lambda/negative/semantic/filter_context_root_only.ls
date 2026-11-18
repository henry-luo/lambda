// @expect-error: E238
// @description: a context root is not a filter's focal item (S2.4.8).
[1, 2] |: ~proc.pid > 0
