// Excel workbook date systems use a fictitious 1900-02-29 at serial 60.
import c: .mod_formula_common
let DAY_MS=86400000
let EPOCH_1900= -2209075200000
let EPOCH_1904= -2082844800000

fn datetime_for(serial,date1904) any^ {
    let days=floor(c.numeric(serial)^);
    if (days<0 or days>(if (date1904) 2957003 else 2958465)) raise error("#NUM!")
    else if (not date1904 and days==60) {year:1900,month:2,day:29}
    else if (not date1904 and days==0) {year:1900,month:1,day:0}
    else {
        let offset=if (not date1904 and days>60) days-1 else days
        let dt=datetime((if (date1904) EPOCH_1904 else EPOCH_1900)+offset*DAY_MS)^;
        if (dt.year>9999 or dt.year<1900) raise error("#NUM!") else {year:dt.year,month:dt.month,day:dt.day}
    }
}
fn serial_for(year,month,day,date1904) any^ {
    let y=trunc(c.numeric(year)^)
    let m=trunc(c.numeric(month)^)
    let d=trunc(c.numeric(day)^)
    let actual_year=if (y>=0 and y<1900) y+1900 else y
    let adjusted_year=actual_year+floor((m-1)/12)
    let adjusted_month=m-12*floor((m-1)/12);
    if (y<0 or y>=10000 or adjusted_year<1900 or adjusted_year>9999) raise error("#NUM!") else {
        let anchor=date(adjusted_year,adjusted_month,1)^
        let epoch=if (date1904) EPOCH_1904 else EPOCH_1900
        let base=(anchor.unix-epoch)/DAY_MS
        // Anchor after February receives Excel's phantom leap day before adding the day offset.
        let serial=base+(if (not date1904 and (adjusted_year>1900 or adjusted_month>2)) 1 else 0)+d-1;
        if (serial<0 or serial>(if (date1904) 2957003 else 2958465)) raise error("#NUM!") else serial
    }
}
pub fn date_value(date1904,year,month,day) any^ => serial_for(year,month,day,date1904)^
pub fn year(date1904,serial) any^ => datetime_for(serial,date1904)^.year
pub fn month(date1904,serial) any^ => datetime_for(serial,date1904)^.month
pub fn day(date1904,serial) any^ => datetime_for(serial,date1904)^.day
pub fn days(finish,start) any^ => floor(c.numeric(finish)^)-floor(c.numeric(start)^)
pub fn time_value(hour,minute,second) any^ {
    let h=trunc(c.numeric(hour)^)
    let m=trunc(c.numeric(minute)^)
    let s=trunc(c.numeric(second)^);
    if (h<0 or m<0 or s<0 or h>32767 or m>32767 or s>32767) raise error("#NUM!")
    else ((h*3600+m*60+s)%86400)/86400.0
}
fn seconds(serial) any^ {
    let value=c.numeric(serial)^;
    if (value<0) raise error("#NUM!") else int(floor((value-floor(value))*86400+0.0000001))%86400
}
pub fn hour(serial) any^ => seconds(serial)^ div 3600
pub fn minute(serial) any^ => (seconds(serial)^ div 60)%60
pub fn second(serial) any^ => seconds(serial)^%60
pub fn weekday(date1904,serial,kind=1) any^ {
    let days=floor(c.numeric(serial)^)
    let mode=trunc(c.numeric(kind)^);
    if (days<0) raise error("#NUM!") else {
        let sunday=(days+(if (date1904) 5 else 6))%7
        let start=if (mode==1 or mode==17) 0 else if (mode==2 or mode==3 or mode==11) 1
            else if (mode>=12 and mode<=16) mode-10 else -1;
        if (start<0) raise error("#NUM!") else ((sunday-start+7)%7)+(if (mode==3) 0 else 1)
    }
}
fn shifted(date1904,serial,months,end_of_month) any^ {
    let parts=datetime_for(serial,date1904)^
    let delta=trunc(c.numeric(months)^)
    let total=parts.year*12+parts.month-1+delta
    let year=floor(total/12)
    let month=total-12*year+1;
    if (year<1900 or year>9999) raise error("#NUM!") else {
        let first=date(year,month,1)^
        let maxday=if (year==1900 and month==2 and not date1904) 29 else first.days_in_month;
        serial_for(year,month,if (end_of_month) maxday else min(parts.day,maxday),date1904)^
    }
}
pub fn edate(date1904,serial,months) any^ => shifted(date1904,serial,months,false)^
pub fn eomonth(date1904,serial,months) any^ => shifted(date1904,serial,months,true)^
// Text-date admission deliberately accepts unambiguous ISO date and time forms.
fn digits(value) any^ => value!="" and all([for (ch in ((split(value,"")) ^ { [] })) contains("0123456789",ch)])
pub fn datevalue(date1904,input) any^ {
    if (not (input is string) or len(input)!=10 or slice(input,4,5)!="-" or slice(input,7,8)!="-" or
        not digits(slice(input,0,4)) or not digits(slice(input,5,7)) or not digits(slice(input,8,10))) raise error("#VALUE!")
    else {
        let year=int(slice(input,0,4))^
        let month=int(slice(input,5,7))^
        let day=int(slice(input,8,10))^;
        if (year<1900 or month<1 or month>12 or day<1) raise error("#VALUE!")
        else if (not date1904 and year==1900 and month==2 and day==29) 60
        else {
            let anchor=date(year,month,1)^;
            if (day>anchor.days_in_month) raise error("#VALUE!") else serial_for(year,month,day,date1904)^
        }
    }
}
pub fn timevalue(input) any^ {
    if (not (input is string)) raise error("#VALUE!") else {
        let parts=split(input,":")^;
        if ((len(parts)!=2 and len(parts)!=3) or not all([for (part in parts) digits(part)])) raise error("#VALUE!")
        else {
            let h=int(parts[0])^
            let m=int(parts[1])^
            let s=if (len(parts)==3) int(parts[2])^ else 0;
            if (h>23 or m>59 or s>59) raise error("#VALUE!") else time_value(h,m,s)^
        }
    }
}
