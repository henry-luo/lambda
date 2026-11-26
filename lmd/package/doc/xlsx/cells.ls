// Cell display shared by the XLSX preview and the Sheet editor (D9.3.4).
pub let ROW_LIMIT = 1048576
pub let COLUMN_LIMIT = 16384
let LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

pub fn column(col) any^ => if (col<26) slice(LETTERS,col,col+1) else column(int(floor(col/26))-1) ++ column(col%26)
pub fn address(row,col) any^ => column(col) ++ string(row+1)

// Shared fixed-point formatting keeps cell and TEXT rounding consistent.
pub fn fixed(value,places=2) any^ {
    let scale=10**places
    let rounded=int(round(abs(value)*scale))
    let whole=int(floor(rounded/scale))
    let fraction=string(rounded%scale);
    (if (value<0) "-" else "") ++ string(whole) ++
        (if (places==0) "" else "." ++ join(fill(max(0,places-len(fraction)),"0"),"") ++ fraction)
}

fn trim_decimal(text) any^ => if (ends_with(text,"0")) trim_decimal(slice(text,0,len(text)-1))
    else if (ends_with(text,".")) slice(text,0,len(text)-1) else text
// Excel's 1900 calendar includes a fictitious leap day at serial 60.
fn excel_date(value,date1904) any^ {
    let days=int(floor(value));
    if (not date1904 and days==60) "1900-02-29"
    else {
        let epoch=if (date1904) -2082844800000 else -2209075200000
        let offset=if (not date1904 and days>60) days-1 else days
        let rendered=string(date(datetime(epoch+offset*86400000)^)^);
        slice(rendered,2,len(rendered)-1)
    }
}
fn two_digits(value) => (if (value<10) "0" else "") ++ string(value)
// A spreadsheet time is the fractional part of a day, rounded to seconds.
fn excel_time(value) any^ {
    let seconds=int(floor((value-floor(value))*86400+0.5))%86400;
    two_digits(seconds div 3600) ++ ":" ++ two_digits((seconds div 60)%60) ++ ":" ++ two_digits(seconds%60)
}
pub fn formatted(value,style) any^ => if (value is bool) (if (value) "TRUE" else "FALSE")
    else if (not (value is number)) string(value)
    else if (style.format=="date" or style.format=="date1904") excel_date(value,style.format=="date1904")^
    else if (style.format=="time") excel_time(value)^
    else if (style.format=="datetime" or style.format=="datetime1904")
        excel_date(value+0.5/86400,style.format=="datetime1904")^ ++ " " ++ excel_time(value)^
    else if (style.format=="currency") "$" ++ fixed(value)
    else if (style.format=="percent") fixed(value*100,1) ++ "%"
    else if (style.format=="number") fixed(value)
    else if (value==floor(value)) string(int(value)) else trim_decimal(fixed(value,6))
// Default cell declarations live in CSS; keep a stable shape for empty cells.
pub fn cell_style(style,zoom) => (if (style.fill) "background-color:" ++ style.fill ++ ";" else "") ++
    (if (style.color) "color:" ++ style.color ++ ";" else "") ++
    (if (style.font) "font-family:" ++ style.font ++ ";" else "") ++
    (if (style.size) "font-size:" ++ string(style.size*zoom) ++ "px;" else "") ++
    (if (style.bold) "font-weight:700;" else "") ++ (if (style.italic) "font-style:italic;" else "") ++
    (if (style.underline) "text-decoration:underline;" else "") ++
    (if (style.align) "text-align:" ++ style.align ++ ";" else "") ++
    (if (style.wrap) "white-space:normal;" else "") ++ (if (style.border) "border:1px solid #6d8275;" else "")

// Convert SpreadsheetML character widths and point heights to CSS pixels.
pub fn column_width(characters) => max(40,min(1000,round(characters*7+5)))
pub fn row_height(points) => max(27,min(500,round(points*4/3)))
