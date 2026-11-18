let proc = ~proc
proc.pid == sys.proc.self.pid
~proc.cwd == sys.proc.self.cwd
~proc.argv == sys.proc.self.argv
~proc.env.PATH == sys.proc.self.env.PATH
proc.env.PATH == ~proc.env.PATH
proc.env.Path == ~proc.env.Path
proc.env.LAMBDA_CONTEXT_DUPLICATE == ~proc.env.LAMBDA_CONTEXT_DUPLICATE
;(\.~proc ++ \.cwd)# == sys.proc.self.cwd
;(\.~proc)#.pid == ~proc.pid
