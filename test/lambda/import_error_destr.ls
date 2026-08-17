// Test: error destructuring at module scope for imported modules
import m: .mod_error_destr

"1. safe_val:"; m.safe_val
"2. fail_val:"; if (m.fail_val is error) null else m.fail_val
"3. safe_val is error:"; m.safe_val is error
"4. fail_val is error:"; m.fail_val is error
