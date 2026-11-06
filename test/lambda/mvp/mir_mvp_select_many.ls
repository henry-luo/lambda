// S12.3.1/S13.1.3v2: select packages its handles into one array at the
// 16-source-operand limit. Repeated handles keep completion stable.
pn ready() { return "selected" }
pn main() {
    let h = start(ready)
    let selected = select(h, h, h, h, h,
                          h, h, h, h, h,
                          h, h, h, h, h, timeout: 60000)^
    print(wait(selected)^)
}
