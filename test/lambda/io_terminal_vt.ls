import layout: lambda.io.terminal.layout
import display: lambda.io.terminal.display
import vt: lambda.io.terminal.vt

let first = layout.layout_frame({prompt: "> ", text: ""}, 0, 8, 4)
let first_bytes = vt.encode(null, display.plan(null, first))
let same_bytes = vt.encode(first, display.plan(first, first))
let wrap = layout.layout_frame({prompt: "> ", text: "ab界"}, 3, 5, 4)
let wrap_bytes = vt.encode(first, display.plan(first, wrap))
let shrink = layout.layout_frame({prompt: "> ", text: "a"}, 1, 5, 4)
let shrink_bytes = vt.encode(wrap, display.plan(wrap, shrink));
[
  first_bytes == chr(27) ++ "[?7l\r" ++ chr(27) ++ "[2K> \r" ++ chr(27) ++ "[2C" ++ chr(27) ++ "[?7h",
  same_bytes == "",
  contains(wrap_bytes, "\r\n") and contains(wrap_bytes, "界"),
  contains(shrink_bytes, chr(27) ++ "[2K") and not contains(shrink_bytes, "界"),
  vt.finish_frame(first) == "\r\n",
  vt.finish_frame(wrap) == "\r\n"
]
