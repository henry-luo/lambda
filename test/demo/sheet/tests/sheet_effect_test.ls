import f: ~~.mod_formula
import m: ~~.mod_model
import cf: ~~.mod_conditional
import dates: ~~.mod_formula_date
// Each test formula is in A1 unless its row is supplied.
fn call_count(st,row=0) any^ => len(st.cache.samples.sheet1["0"][string(row)]["0"] or {})
function() {
    let model=m.initial(m.blank())^
    let data=m.paste(m.select(model,0,0),"10\t20\n30\t40")
    let sheet={title:"Sheet1",rows:10,cols:10,cells:[{row:0,col:0,raw:"=1+2",style:{}},
        {row:1,col:0,raw:"=TODAY()",style:{}},{row:2,col:0,raw:"=NOW()",style:{}},
        {row:3,col:0,raw:"=RAND()",style:{}},{row:4,col:0,raw:"=IF(TRUE,TODAY(),1/0)",style:{}},
        {row:5,col:0,raw:"=LET(x,5,x+1)",style:{}},{row:6,col:0,raw:"=SUM(1,2)",style:{}},
        {row:7,col:0,raw:"=TODAY()+1",style:{}},{row:8,col:0,raw:"=A1+1",style:{}},
        {row:9,col:0,raw:"=RANDOM()",style:{}},{row:0,col:1,raw:"=TODAY(1)",style:{}},
        {row:1,col:1,raw:"=NOW(1)",style:{}},{row:2,col:1,raw:"=RAND(1)",style:{}},
        {row:3,col:1,raw:"=IF(FALSE,1/0,RAND())",style:{}},
        {row:4,col:1,raw:"=IFS(FALSE,1/0,TRUE,TODAY())",style:{}},
        {row:5,col:1,raw:"=CHOOSE(2,1/0,TODAY())",style:{}},
        {row:6,col:1,raw:"=SWITCH(2,1,1/0,2,TODAY())",style:{}},
        {row:7,col:1,raw:"=RAND()+RAND()",style:{}},
        {row:8,col:1,raw:"=IFERROR(1/0,TODAY())",style:{}},
        {row:9,col:1,raw:"=LAMBDA(x,x+TODAY())(1)",style:{}}]}
    let today_value=f.display(sheet,1,0)^
    let now_value=f.display(sheet,2,0)^
    let local=now().local
    let expected_today=dates.date_value(false,local.year,local.month,local.day)^
    let random=f.display(sheet,3,0)^
    let second_random=f.display(sheet,3,0)^
    let random_sum=f.display(sheet,7,1)^
    let volatile_cell=m.set_value(m.select(model,0,0),"=RAND()")^
    let volatile_dependency=m.set_value(m.select(volatile_cell,0,1),"=A1")^
    let volatile_sheet={*:m.indexed(volatile_cell),conditional_rules:[{operator:"expression",formulas:["A1>=0.5"],
        anchor:{row:0,col:0},ranges:[{top:0,left:0,bottom:0,right:0}],style:{bold:true},stop:false}]}
    let volatile_style=cf.style(volatile_sheet,0,0,volatile_cell.cache.values.A1,{})^
    let random_rule={*:m.indexed(model),conditional_rules:[{operator:"expression",formulas:["RAND()>=0"],
        anchor:{row:0,col:0},ranges:[{top:0,left:0,bottom:0,right:0}],style:{bold:true},stop:false}]}
    let sampled_style=cf.style(random_rule,0,0,0,{})^
    let recovered=m.set_value(m.select(volatile_cell,0,1),"=IFERROR(A1/0,A1)")^
    let lambda_samples=m.set_value(m.select(model,0,0),"=LET(f,LAMBDA(x,RAND()),f(1)=f(1))")^
    let direct_samples=m.set_value(m.select(model,0,0),"=RAND()=RAND()")^
    let mixed_random=m.set_value(m.select(model,0,0),"=RAND()+RANDOM()")^
    let clock_calls=m.set_value(m.select(model,0,0),"=TODAY()+NOW()+NOW()")^
    let lambda_clock=m.set_value(m.select(model,0,0),"=LET(f,LAMBDA(x,NOW()),f(1)+f(2))")^
    let clock_cell=m.set_value(m.select(model,0,0),"=NOW()")^
    let clock_dependency=m.set_value(m.select(clock_cell,0,1),"=A1")^
    let skipped_second_clock=m.set_value(m.select(model,0,0),"=IF(TRUE,NOW(),TODAY())")^
    let many_book={*:m.blank(),sheets:[{*:m.empty_sheet(),rows:128,
        cells:[for (row in 0 to 95) {row:row,col:0,raw:if (row%2==0) "=RAND()" else "=NOW()"}]}]}
    let many=m.initial(many_book)^
    let sum_random=m.set_value(m.select(model,0,0),"=SUM(RAND(),1)")^
    let iserror_random=m.set_value(m.select(model,0,0),"=ISERROR(RAND())")^
    let condition_random=m.set_value(m.select(model,0,0),"=IFS(RAND()>=0,1,TRUE,0)")^
    let skipped_random=m.set_value(m.select(model,0,0),"=IF(FALSE,RAND(),1)")^
    let skipped_clock=m.set_value(m.select(model,0,0),"=IFERROR(1,NOW())")^
    let skipped_after_error=m.set_value(m.select(model,0,0),"=IFERROR(1/0+RAND(),1)")^
    let aggregate_branch=m.set_value(m.select(data,0,2),"=AGGREGATE(9,6,IF(A1,1+RAND(),RANDOM()))")^
    let subtotal_book={*:m.blank(),sheets:[{*:m.empty_sheet(),cells:[{row:0,col:0,raw:"=RAND()"},
        {row:1,col:0,raw:"=2"},{row:2,col:0,raw:"=SUBTOTAL(9,A1:A2)"}]}]}
    let subtotal=m.initial(subtotal_book)^
    let volatile_cycle=m.set_value(m.select(model,0,0),"=SEQUENCE(1,2,B1+RAND())")^
    let source={*:m.empty_sheet("Source"),cells:[{row:0,col:0,raw:"=RAND()"}]}
    let dependent={*:m.empty_sheet("Dependent"),cells:[{row:0,col:0,raw:"=Source!A1"},
        {row:0,col:1,raw:"=Source!A1"}]}
    let cross_book={*:m.blank(),sheets:[dependent,source]}
    let cross=m.initial(cross_book)^
    let cross_subtotal_book={*:m.blank(),sheets:[{*:m.empty_sheet("Report"),cells:[
        {row:0,col:0,raw:"=SUBTOTAL(9,Source!A1:A2)"}]},
        {*:m.empty_sheet("Source"),cells:[{row:0,col:0,raw:"=RAND()"},{row:1,col:0,raw:"=2"}]}]}
    let cross_subtotal=m.initial(cross_subtotal_book)^
    let spill_book={*:m.blank(),sheets:[{*:m.empty_sheet("Report"),cells:[
        {row:0,col:0,raw:"=Source!A2"},{row:1,col:0,raw:"=Source!A1+1"}]},
        {*:m.empty_sheet("Source"),cells:[{row:0,col:0,raw:"=SEQUENCE(2)+C1"},{row:0,col:2,raw:"=RAND()"}]}]}
    let cross_spill=m.initial(spill_book)^
    let conditional_sheet={*:sheet,conditional_rules:[{operator:"expression",formulas:["TODAY()>0"],
        anchor:{row:0,col:0},ranges:[{top:0,left:0,bottom:0,right:0}],style:{bold:true},stop:false}]};
    [model is map,data is map,f.display(sheet,0,0)^==3,today_value is number,now_value is number,floor(now_value)==today_value,
        today_value==expected_today,
        random is number,random>=0,random<1,f.display(sheet,4,0)^==today_value,
        f.display(sheet,5,0)^==6,f.display(sheet,6,0)^==3,
        f.display(sheet,7,0)^==today_value+1,f.display(sheet,8,0)^==4,
        f.display(sheet,9,0)^ is number,f.display(sheet,0,1)^=="#VALUE!",
        f.display(sheet,1,1)^=="#VALUE!",f.display(sheet,2,1)^=="#VALUE!",
        f.display(sheet,3,1)^ is number,
        f.display({*:sheet,date1904:true},1,0)^==today_value-1462,
        f.display(sheet,4,1)^==today_value,f.display(sheet,5,1)^==today_value,f.display(sheet,6,1)^==today_value,
        cf.style(conditional_sheet,0,0,3,{})^.bold==true,
        random!=second_random,random_sum>=0 and random_sum<2,
        f.display(sheet,8,1)^==today_value,f.display(sheet,9,1)^==today_value+1,
        volatile_dependency.cache.values.A1==volatile_dependency.cache.values.B1,
        volatile_dependency.cache.values.A1!=volatile_cell.cache.values.A1,
        f.display(m.indexed(volatile_dependency),0,0)^==volatile_dependency.cache.values.A1,
        (volatile_style.bold==true)==(volatile_cell.cache.values.A1>=0.5),
        sampled_style.bold==true,
        cross.cache.values.A1==cross.cache.values.B1,
        recovered.cache.values.A1==recovered.cache.values.B1,
        lambda_samples.cache.values.A1==false,direct_samples.cache.values.A1==false,
        call_count(lambda_samples)^==2,call_count(direct_samples)^==2,
        call_count(mixed_random)^==2,call_count(clock_calls)^==3,
        call_count(lambda_clock)^==2,call_count(clock_dependency)^==1,
        clock_dependency.cache.values.A1==clock_dependency.cache.values.B1,
        call_count(skipped_second_clock)^==1,call_count(volatile_dependency)^==1,
        len(many.cache.samples.sheet1["0"])==96,len(unique([for (row in 0 to 94 where row%2==0) many.cache.values[f.address(row,0)^]]))==48,
        sum_random.cache.values.A1>=1 and sum_random.cache.values.A1<2,
        iserror_random.cache.values.A1==false,condition_random.cache.values.A1==1,
        skipped_random.cache.samples==null,skipped_clock.cache.samples==null,
        skipped_after_error.cache.values.A1==1 and skipped_after_error.cache.samples==null,
        aggregate_branch.cache.values.C1>=1 and aggregate_branch.cache.values.C1<2,
        subtotal.cache.values.A3==subtotal.cache.values.A1+2,
        cross_subtotal.cache.values.A1>=2 and cross_subtotal.cache.values.A1<3,
        abs(cross_spill.cache.values.A1-cross_spill.cache.values.A2)<0.000001,
        volatile_cycle.cache.values.A1=="#CYCLE!",
        m.set_value(m.select(model,0,0),"=SEQUENCE(2)+RAND()")^.cache.values.A1 is number]
}
