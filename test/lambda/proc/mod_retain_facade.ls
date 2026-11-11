// helper module for var_retained_import.ls: forwarding wrappers, as a package facade has.
import core: .mod_retain_core

pub fn keep_via(x) map => core.keep(x)
pub fn size_via(x) int^ => core.size(x)^
pub fn first_via(x) => core.keep(x).held
pub fn part_via(x) map => core.part(x)
