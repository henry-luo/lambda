// discharged open call results must join native Boolean selections on every tier.
fn natural(value) => value is int and value >= 0 and value < 9007199254740991
fn selected(record) any^ => false or not natural(record.total)^ or false
fn selected_all(record) any^ => false or not all([for (value in record.values) natural(value)^]) or false
fn discharged(value:any) any^ => not value^
fn observed(value:any) any^ => not value
fn skipped_or(value:any) any^ => true or not value^
fn skipped_and(value:any) any^ => false and not value^;
[selected({total:1})^ == false,
 selected({total:"bad"})^ == true,
 selected({total:error("call-error")}) ^ { ^.message == "call-error" },
 discharged(null)^ == true,discharged(0)^ == false,
 discharged(error("discharged-error")) ^ { ^.message == "discharged-error" },
 observed(error("observed-error")) ^ { ^.message == "observed-error" },
 selected_all({values:[1,2]})^ == false,
 selected_all({values:[1,"bad"]})^ == true,
 skipped_or(error("must skip"))^ == true,
 skipped_and(error("must skip"))^ == false]
