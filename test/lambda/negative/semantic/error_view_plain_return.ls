// S11.4.2v3: a declared clean view result cannot return an ordinary error.
view plain: <view_plain>() int { error("undeclared view error") }
true
