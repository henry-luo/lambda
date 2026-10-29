type R {name:string, value?:int}
fn build(v) => <R name:"r", value:v>
build(null)
