// D7.2.2 (LR07-17): a module whose init ends in an error never becomes
// importable, so this importer must not run on any tier
import bad: .import_init_error_mod

"DRIVER_" ++ "RAN"
