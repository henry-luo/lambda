// TG3a: leading `as T` is the ordinary non-error parameter domain with a binder.
fn selected(value: as T, ...) type => T;
fn selected_explicit(value: any ! error as T) type => T;

[
  selected(1),
  selected("text"),
  selected_explicit(2.5)
]
