// module that imports from mod_vars (chained import test)
import .test.lambda.mod_vars

pub fn full_greeting() { get_name() ++ " says: " ++ get_greeting() }
pub combined_name = name ++ " v" ++ string(version)
