// helper module for var_retained_import.ls: one function keeps its argument, one reads it.
pub fn keep(x) map => {held: x}
pub fn size(x) int^ => len(x)
pub fn part(x) map => {inner: x.child}
