// Test script for sys.proc.* fields
// Tests process information access via sys paths

"=== sys.proc.self fields ==="

"Process ID (pid):"
sys.proc.self.pid

"Current working directory (cwd):"
sys.proc.self.cwd

"Command line arguments (args):"
sys.proc.self.args

"Args count:"
len(sys.proc.self.args)

"=== sys.proc structure ==="
"sys.proc:"
sys.proc

"sys.proc.self:"
sys.proc.self

"=== Environment variables (sample) ==="
"HOME:"
sys.proc.self.env.HOME

"USER:"
sys.proc.self.env.USER

"PATH (truncated):"
sys.proc.self.env.PATH

"SHELL:"
sys.proc.self.env.SHELL

"PWD:"
sys.proc.self.env.PWD

"=== Full environment map ==="
sys.proc.self.env
