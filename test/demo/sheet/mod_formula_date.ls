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

// Weekend masks are Monday-first, matching Excel's seven-character convention.
fn weekend_mask(weekend) any^ {
    if (weekend==null) "0000011"
    else if (weekend is string) {
        if (len(weekend)!=7 or not all([for (ch in ((split(weekend,"")) ^ { [] })) ch=="0" or ch=="1"])) raise error("#VALUE!")
        else weekend
    } else {
        let n=trunc(c.numeric(weekend)^)
        let masks=["0000011","1000001","1100000","0110000","0011000","0001100","0000110"]
        let singles=["0000001","1000000","0100000","0010000","0001000","0000100","0000010"];
        if (n>=1 and n<=7) masks[n-1] else if (n>=11 and n<=17) singles[n-11] else raise error("#NUM!")
    }
}
fn holiday_days(holidays,date1904) any^ {
    let values=c.range_values(holidays)
    let numbers=sort([for (v in values where v is number) floor(v)]);
    if (any([for (v in numbers) v<0 or v>(if (date1904) 2957003 else 2958465)])) raise error("#NUM!")
    else [for (i,v in numbers where i==0 or numbers[i-1]!=v) v]
}
fn is_workday(day,date1904,mask) any^ {
    let weekday_index=weekday(date1904,day,2)^-1;
    slice(mask,weekday_index,weekday_index+1)=="0"
}
fn workdays_between(first,end_day,date1904,mask,holidays) any^ {
    let count=end_day-first+1
    let per_week=len([for (ch in ((split(mask,"")) ^ { [] }) where ch=="0") ch])
    let weeks=count div 7
    let remainder=count%7
    let regular=weeks*per_week+len([for (i in 0 to 6 where i<remainder and is_workday(first+i,date1904,mask)^) i])
    let excluded=len([for (day in holidays where day>=first and day<=end_day and is_workday(day,date1904,mask)^) day]);
    regular-excluded
}
fn checked_day(value,date1904) any^ {
    let day=floor(c.numeric(value)^)
    let checked=datetime_for(day,date1904)^;
    day
}
pub fn networkdays_intl(date1904,start,finish,weekend=1,holidays=[]) any^ {
    let a=checked_day(start,date1904)^
    let b=checked_day(finish,date1904)^
    let mask=weekend_mask(weekend)^
    let excluded=holiday_days(holidays,date1904)^;
    (if (a<=b) 1 else -1)*workdays_between(min(a,b),max(a,b),date1904,mask,excluded)^
}
pub fn networkdays(date1904,start,finish,holidays=[]) any^ => networkdays_intl(date1904,start,finish,1,holidays)^
// Binary search prevents long workday offsets from iterating over every date.
fn workday_search(start,direction,wanted,low,high,date1904,mask,holidays) any^ {
    if (low>=high) start+direction*low else {
        let mid=(low+high) div 2
        let end=start+direction*mid
        let first=if (direction>0) start+1 else end
        let end_day=if (direction>0) end else start-1
        let count=workdays_between(first,end_day,date1904,mask,holidays)^;
        if (count>=wanted) workday_search(start,direction,wanted,low,mid,date1904,mask,holidays)^
        else workday_search(start,direction,wanted,mid+1,high,date1904,mask,holidays)^
    }
}
pub fn workday_intl(date1904,start,days,weekend=1,holidays=[]) any^ {
    let first=checked_day(start,date1904)^
    let offset=trunc(c.numeric(days)^)
    let mask=weekend_mask(weekend)^
    let excluded=holiday_days(holidays,date1904)^
    let direction=if (offset<0) -1 else 1
    let bound=if (direction>0) (if (date1904) 2957003 else 2958465)-first else first;
    if (mask=="1111111") raise error("#VALUE!") else if (offset==0) first else if (bound==0) raise error("#NUM!") else {
        let end_day=first+direction*bound
        let available=workdays_between(if (direction>0) first+1 else end_day,if (direction>0) end_day else first-1,date1904,mask,excluded)^;
        if (available<abs(offset)) raise error("#NUM!")
        else workday_search(first,direction,abs(offset),1,bound,date1904,mask,excluded)^
    }
}
pub fn workday(date1904,start,days,holidays=[]) any^ => workday_intl(date1904,start,days,1,holidays)^

// Calendar differences use workbook serials so both Excel date systems remain valid.
pub fn datedif(date1904,start,finish,unit) any^ {
    let a=checked_day(start,date1904)^
    let b=checked_day(finish,date1904)^
    let first=datetime_for(a,date1904)^
    let finish_parts=datetime_for(b,date1904)^
    let months=(finish_parts.year-first.year)*12+finish_parts.month-first.month-(if (finish_parts.day<first.day) 1 else 0)
    let mode=upper(c.text(unit)^);
    if (a>b) raise error("#NUM!")
    else if (mode=="D") b-a
    else if (mode=="M") months
    else if (mode=="Y") months div 12
    else if (mode=="YM") months%12
    else if (mode=="MD") {
        let base=if (finish_parts.day>=first.day) serial_for(finish_parts.year,finish_parts.month,first.day,date1904)^
            else serial_for(finish_parts.year,finish_parts.month-1,first.day,date1904)^;
        b-base
    }
    else if (mode=="YD") {
        let base=serial_for(finish_parts.year-(if (finish_parts.month<first.month or (finish_parts.month==first.month and finish_parts.day<first.day)) 1 else 0),first.month,first.day,date1904)^;
        b-base
    }
    else raise error("#NUM!")
}
pub fn weeknum(date1904,serial,kind=1) any^ {
    let day=checked_day(serial,date1904)^
    let mode=trunc(c.numeric(kind)^)
    let parts=datetime_for(day,date1904)^;
    if (mode==21) isoweeknum(date1904,day)^
    else if (not contains([1,2,11,12,13,14,15,16,17],mode)) raise error("#NUM!")
    else {
        let first=serial_for(parts.year,1,1,date1904)^
        let weekday_mode=if (mode==2) 11 else mode
        let offset=weekday(date1904,first,weekday_mode)^-1;
        floor((day-first+offset)/7)+1
    }
}
pub fn isoweeknum(date1904,serial) any^ {
    let day=checked_day(serial,date1904)^
    let thursday=day+4-weekday(date1904,day,2)^
    let parts=datetime_for(thursday,date1904)^
    let jan4=serial_for(parts.year,1,4,date1904)^
    let first=jan4-weekday(date1904,jan4,2)^+1;
    floor((day-first)/7)+1
}

// Day-count bases follow Excel's US/European 30/360 and actual-day conventions.
fn leap_year(year,date1904) => (year%4==0 and (year%100!=0 or year%400==0)) or (year==1900 and not date1904)
fn february_end(parts,date1904) any^ => parts.month==2 and parts.day==(if (leap_year(parts.year^,date1904)) 29 else 28)
fn contains_leap_day(a,b,year,date1904) any^ => if (not leap_year(year,date1904)) false else {
    let day=serial_for(year,2,29,date1904)^;
    day>=a and day<=b
}
pub fn yearfrac(date1904,start,finish,basis=0) any^ {
    let start_day=checked_day(start,date1904)^
    let end_day=checked_day(finish,date1904)^
    let mode=trunc(c.numeric(if (basis==null) 0 else basis)^)
    let a=min(start_day,end_day)
    let b=max(start_day,end_day)
    let first=datetime_for(a,date1904)^
    let finish_parts=datetime_for(b,date1904)^;
    if (mode<0 or mode>4) raise error("#NUM!")
    else if (a==b) 0
    else if (mode==2 or mode==3) (b-a)/(if (mode==2) 360.0 else 365.0)
    else if (mode==0 or mode==4) {
        let feb1=february_end(first,date1904)
        let feb2=february_end(finish_parts,date1904)
        let d1=if (first.day==31 or (mode==0 and feb1)) 30 else first.day
        let d2=if (finish_parts.day==31 and (mode==4 or first.day>=30)) 30
            else if (mode==0 and feb1 and feb2) 30 else finish_parts.day;
        (360*(finish_parts.year-first.year)+30*(finish_parts.month-first.month)+d2-d1)/360.0
    } else {
        let short_span=first.year==finish_parts.year or (first.year+1==finish_parts.year and
            (first.month>finish_parts.month or (first.month==finish_parts.month and first.day>=finish_parts.day)))
        let leap=if (first.year==finish_parts.year) leap_year(first.year,date1904) else
            contains_leap_day(a,b,first.year,date1904)^ or contains_leap_day(a,b,finish_parts.year,date1904)^
        let years=finish_parts.year-first.year+1
        let average=if (short_span) (if (leap) 366 else 365) else
            sum([for (year in first.year to finish_parts.year) if (leap_year(year,date1904)) 366 else 365])/years;
        (b-a)/average
    }
}

fn comma_groups(digits) any^ => if (len(digits)<=3) digits else
    comma_groups(slice(digits,0,len(digits)-3))^ ++ "," ++ slice(digits,len(digits)-3)
fn grouped_number(value,places) any^ {
    let formatted=c.fixed(value,places)^
    let negative=starts_with(formatted,"-")
    let unsigned=if (negative) slice(formatted,1) else formatted
    let pieces=split(unsigned,".")^;
    (if (negative) "-" else "") ++ comma_groups(pieces[0])^ ++
        (if (len(pieces)==2) "." ++ pieces[1] else "")
}
// TEXT admits an explicit numeric/date subset and rejects unsupported masks.
pub fn text(date1904,input,pattern) any^ {
    let value=c.numeric(input)^
    let fmt=c.text(pattern)^
    let code=lower(fmt);
    if (contains(["yyyy-mm-dd","yyyy/mm/dd","mm/dd/yyyy","dd/mm/yyyy"],code)) {
        let parts=datetime_for(value,date1904)^
        let mm=(if (parts.month<10) "0" else "") ++ string(parts.month)
        let dd=(if (parts.day<10) "0" else "") ++ string(parts.day)
        let yyyy=string(parts.year);
        if (code=="yyyy-mm-dd") yyyy ++ "-" ++ mm ++ "-" ++ dd
        else if (code=="yyyy/mm/dd") yyyy ++ "/" ++ mm ++ "/" ++ dd
        else if (code=="mm/dd/yyyy") mm ++ "/" ++ dd ++ "/" ++ yyyy
        else dd ++ "/" ++ mm ++ "/" ++ yyyy
    } else {
        let percent=ends_with(fmt,"%")
        let currency=starts_with(fmt,"$")
        let mask=slice(fmt,if (currency) 1 else 0,len(fmt)-(if (percent) 1 else 0))
        let pieces=split(mask,".")^
        let digits=if (len(pieces)==2) pieces[1] else ""
        let grouped=pieces[0]=="#,##0";
        if (len(pieces)>2 or not (pieces[0]=="0" or grouped) or len(digits)>10 or
            (len(pieces)==2 and digits=="") or not all([for (ch in ((split(digits,"")) ^ { [] })) ch=="0"])) raise error("#VALUE!")
        else {
            let amount=value*(if (percent) 100 else 1);
            (if (currency) "$" else "") ++ (if (grouped) grouped_number(amount,len(digits))^ else c.fixed(amount,len(digits))^) ++ (if (percent) "%" else "")
        }
    }
}
