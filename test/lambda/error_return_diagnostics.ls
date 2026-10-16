// Each repair recommended by E208 keeps an explicit error boundary (S7.7.3).
fn render(title: string) string => title
fn narrowed(title: string) => render(title)
fn recovered(title: any) => render(title) ^ { "fallback" }
fn as_data(title: any) string | error => render(title)
fn propagated(title: any) string^ => render(title)^;
[narrowed("hello"), recovered(error("input")),
    as_data(error("input")) is error, propagated("hello") ^ { "fallback" }]
