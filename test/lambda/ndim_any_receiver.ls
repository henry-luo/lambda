// S8.2.4v3 / S7.1.1v3: dynamic receivers retain the complete N-D key list.
fn read(a:any) => a[1,2]
fn read_first(a:any) => a[0,0]
let pixels = [[1,2,3],[4,5,6]]
;[read(pixels),read("invalid"),read_first(null)]
