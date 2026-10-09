import ui: lambda.ui.dtna
import collection: lambda.ui.core.collection
let items = [{key:'a',label:"A"},{key:'b',label:"B",disabled:true},{key:'c',label:"C"}]
let rows = [{n:3},{n:1},{n:2}];
[collection.move(items,'a',"ArrowRight"),collection.move(items,'a',"ArrowLeft"),
    collection.move(items,'c',"Home"),collection.move(items,'a',"End"),
    collection.page_count(93,10),collection.page_rows([1,2,3,4,5],2,2)^,
    [for (row in collection.order_rows(rows,'n','asc')) row.n],
    (ui.tabs({items:items}) or null) == null,
    (ui.tabs({id:"dup",items:[{key:1},{key:"1"}]}) or null) == null,
    (ui.pagination({page_size:0}) or null) == null,
    (ui.tabs({id:"bad-flag",items:[{key:1,disabled:"yes"}]}) or null) == null,
    (ui.tabs({id:"missing",items:items,value:'missing'}) or null) == null,
    (ui.tabs({id:"inactive",items:items,default_value:'b'}) or null) == null]
