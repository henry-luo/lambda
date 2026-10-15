// S7.7.3: an error-admitting argument can become the call's error value.
fn render(prefix: string, title: string) string => prefix ++ title
fn positional(title: any) => render("", title)
fn named(title: any) => render(title: title, prefix: "")
fn piped(title: any) => title |> render("suffix")
fn declared(title: any) string => render("", title)
fn consume(value) int => 1
fn implicit(value: any) => consume(value)
positional("hello")
