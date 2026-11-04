// provider for import_init_error_driver.ls: its init fails at run time (D9.3.2)
fn g(x) => x
pub let C: int = g("s")
// D9.3.2/S7.7.2v2: the module explicitly delivers its bound validation error.
C
