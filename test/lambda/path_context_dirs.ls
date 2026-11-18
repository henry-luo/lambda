let home = \.~home
let cwd = \.~cwd
let temp_path = \.~temp
home is path
cwd is path
temp_path is path
home == \.~home
cwd == \.~cwd
temp_path == \.~temp
;(home ++ \.missing_context_entry) == \.~home.missing_context_entry
~cwd.test.lambda.'path_context_proc.ls' is file
;(cwd ++ \.test.lambda.'path_context_proc.ls')# is file
~cwd[\.test.lambda.'path_context_proc.ls'] is file
