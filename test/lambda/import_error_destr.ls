// Test: error destructuring at module scope for imported modules
import m: .mod_error_destr

"1. safe_val:"; m.safe_val
"2. fail_val:"; m.fail_val
"3. safe_err is error:"; ^m.safe_err
"4. fail_err is error:"; ^m.fail_err
