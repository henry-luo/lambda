// Artwork shared by file-browser surfaces; callers supply their own entries.
let strokes = {
    back:"M15 5l-7 7 7 7",forward:"M9 5l7 7-7 7",up:"M5 14l7-7 7 7",
    icons:"M3 3h6v6H3z M15 3h6v6h-6z M3 15h6v6H3z M15 15h6v6h-6z",
    list:"M8 5h13 M8 12h13 M8 19h13 M3 5h1 M3 12h1 M3 19h1",
    columns:"M3 4h18v16H3z M9 4v16 M15 4v16",
    actions:"M5 12h1 M11 12h1 M17 12h1",newfolder:"M3 7V4h7l3 3h8v13H3z M12 10v7 M8.5 13.5h7",
    preview:"M3 4h18v16H3z M15 4v16",search:"M10 3a7 7 0 1 0 0 14 7 7 0 0 0 0-14 M15 15l6 6",
    refresh:"M20 9a8 8 0 1 0 0 6 M20 3v6h-6",go:"M3 8h18v12H3z M3 8V5h7l3 3 M12 11v6 M9 14l3 3 3-3",
    home:"M3 10l9-7 9 7 M5 9v12h14V9 M9 21v-8h6v8",
    desktop:"M3 4h18v13H3z M12 17v4 M8 21h8",
    downloads:"M12 3v12 M7 10l5 5 5-5 M4 17v4h16v-4",
    documents:"M6 3h8l4 4v14H6z M14 3v5h4 M9 12h6 M9 16h6",
    pictures:"M3 4h18v16H3z M3 17l6-7 5 5 3-3 4 4 M15 8h1",
    folder:"M3 7V4h7l3 3h8v13H3z",computer:"M3 4h18v13H3z M8 21h8 M12 17v4",
    eye:"M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z M15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0",
    close:"M6 6l12 12 M18 6L6 18"
}
pub fn icon(graphic) => <svg width:"20",height:"20",viewBox:"0 0 24 24",'aria-hidden':"true",
    <path d:strokes[graphic] or strokes.folder,fill:"none",stroke:"currentColor",'stroke-width':1.6,
        'stroke-linecap':"round",'stroke-linejoin':"round">>
pub fn file_icon(entry) => <svg class:"file-icon",width:"76",height:"68",viewBox:"0 0 80 72",'aria-hidden':"true",
    (if (entry.dir) <g
        <path d:"M5 16Q5 11 10 11H30L37 18H70Q75 18 75 23V61Q75 65 70 65H10Q5 65 5 60Z",fill:"#48a6df">
        <path d:"M5 26Q5 22 10 22H70Q75 22 75 27V60Q75 65 70 65H10Q5 65 5 60Z",fill:"#80cef5">
        <path d:"M10 24H70",stroke:"#b7e5fc",'stroke-width':1.5>
    > else <g
        <path d:"M19 6H49L63 20V63Q63 67 59 67H21Q17 67 17 63V10Q17 6 19 6Z",fill:"#ffffff",stroke:"#d4d9df",'stroke-width':1.2>
        <path d:"M49 6V20H63",fill:"#edf0f4",stroke:"#d4d9df",'stroke-width':1.2>
        <path d:"M26 30H53 M26 36H53 M26 42H47",stroke:"#dce1e7",'stroke-width':2.5>
        <rect x:"23",y:"48",width:"35",height:"13",rx:"3",fill:if (entry.kind == "Image") "#82aa98" else if (entry.kind == "Source code") "#9b90c5" else "#819bb9">
        <text x:"40",y:"57",'text-anchor':"middle",'font-family':"Arial, sans-serif",'font-size':"7",'font-weight':"bold",fill:"#ffffff",
            upper(slice(entry.extension or "FILE",0,5))>
    >)>
