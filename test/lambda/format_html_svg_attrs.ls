fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

let tree = <div class: "wrap";
    <svg width: 100, height: 50;
        <rect width: 10, height: 5, fill: "red">
    >
>

let html = format(tree, 'html')

{
    div_class: has(html, "<div class=\"wrap\">"),
    svg_width: has(html, "<svg width=\"100\" height=\"50\">"),
    rect_attrs: has(html, "<rect width=\"10\" height=\"5\" fill=\"red\">")
}
