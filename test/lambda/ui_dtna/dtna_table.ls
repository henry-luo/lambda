import ui: lambda.ui.dtna
import table: lambda.ui.dtna.table
let props = {id:"people",page_size:2,columns:[{key:'name',title:"Name"},{key:'age',title:"Age",sortable:true},
    {key:'status',title:"Status",filters:[{value:'active',label:"Active"},{value:'paused',label:"Paused"}]}],
    rows:[{key:1,name:"Cora",age:30,status:'active'},{key:2,name:"Ada",age:20,status:'active',disabled:true},
        {key:3,name:"Ben",age:25,status:'paused'},{key:4,name:"Dana",age:20,status:'active'}]}
let sorted = table.model(props,{key:'age',direction:'asc'})
let filtered = table.model(props,null,{status:'paused'},9);
[[for (entry in sorted.rows) entry.key],[for (entry in table.model(props,{key:'age',direction:'asc'},{},2).rows) entry.key],
 filtered.current,filtered.total,[for (entry in filtered.rows) entry.key],
 [for (entry in table.model({*:props,pagination:false}).rows) entry.key],
 (ui.table(props) or null) != null,
 (ui.table({*:props,rows:[{key:1},{key:"1"}]}) or null) == null,
 (ui.table({*:props,columns:[]}) or null) == null,
 (ui.table({*:props,sort:{key:'name',direction:'asc'}}) or null) == null,
 (ui.table({*:props,filters:{status:'missing'}}) or null) == null,
 (ui.table({*:props,default_current:1,current:2}) or null) == null,
 (ui.table({*:props,page_size:0}) or null) == null,
 (ui.table({*:props,selected_keys:[9]}) or null) == null,
 (ui.table({*:props,selection:'single',selected_keys:[1,2]}) or null) == null,
 (ui.table({*:props,columns:[{key:'name',render:42}]}) or null) == null,
 (ui.table({*:props,columns:[{key:'name',filters:"bad"}]}) or null) == null,
 (ui.table({*:props,columns:[{key:'name',render:(value,row,index) => <b value>}]}) or null) != null]
