// A native comparison must remain Bool when a function body boxes its result.
fn contains(text: string, needle: string) {
    index_of(text, needle) != null
}

[contains("<path fill=\"rgb(0,0,0)\"></path>", "rgb(0,0,0)"),
 contains("<path fill=\"rgb(0,0,0)\"></path>", "rgb(255,0,0)")]
