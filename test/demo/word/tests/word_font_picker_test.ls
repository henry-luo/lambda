import picker: ~~.mod_font_picker
let fonts = [{value:"Arial",label:"Arial",key:"arial"},{value:"Georgia",label:"Georgia",key:"georgia"},
    {value:"思源宋体",label:"思源宋体",key:"思源宋体"}];
[
    picker.window_size==12 and picker.preview_batch==2,
    picker.window_start(0,0)==0,
    picker.window_start(5,10000)==0,
    picker.window_start(10000,0)==0,
    picker.window_start(10000,3200)==98,
    picker.window_start(10000,320000)==9988,
    picker.matching(fonts,"")==[0,1,2],
    picker.matching(fonts,"  ARI ")==[0],
    picker.matching(fonts,"宋")==[2],
    picker.matching(fonts,"missing")==[],
    picker.next_index(0,"ArrowUp",10000)==0,
    picker.next_index(0,"PageDown",10000)==8,
    picker.next_index(0,"End",10000)==9999,
    picker.next_index(9999,"ArrowDown",10000)==9999,
    picker.next_index(3,"Home",10000)==0,
    picker.next_index(0,"ArrowDown",0)== -1,
    picker.revealed_y(8,0)==32,
    picker.revealed_y(0,320)==0,
    picker.revealed_y(2,0,64)==32,
    picker.retained(["Arial","Missing"],fonts)==["Arial"],
    picker.next_ready([],fonts)==["Arial","Georgia"],
    picker.next_ready(["Arial"],fonts)==["Arial","Georgia","思源宋体"],
    picker.next_ready(["Arial","Georgia","思源宋体"],fonts)==["Arial","Georgia","思源宋体"]
]
