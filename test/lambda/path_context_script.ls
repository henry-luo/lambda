import helper: .mod_context_script
~script is file
\.~script is path
;(\.~script)# is file
helper.source_file() is file
helper.source_ref() == \.~cwd.test.lambda.'mod_context_script.ls'
;(helper.source_ref())# is file
