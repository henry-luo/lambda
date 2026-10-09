import ui: lambda.ui.dtna
import tree: lambda.ui.dtna.tree
let items = [{key:'root',label:"Root",children:[{key:1,label:"Alpha"},{key:2,label:"Beta"},
    {key:'disabled',disabled:true,children:[{key:'isolated',label:"Isolated"}]}]},{key:'tail',label:"Tail"}]
let rows = tree.flatten(items)^
let full = tree.checks(rows,['root'])
let partial = tree.toggle_check(rows,full.checked_keys,1);
[[for (row in rows) [row.key,row.level,row.parent]],
 [for (row in tree.visible(rows,['root'])) row.key],full,partial,
 tree.toggle_check(rows,partial.checked_keys,1),tree.checks(rows,['root'],true),
 tree.toggle_check(rows,full.checked_keys,'disabled'),tree.checks(rows,['isolated']),
 (ui.tree({id:"ok",items:items,checkable:true}) or null) != null,
 (ui.tree({id:"bad",items:[{key:1,children:[{key:1}]}]}) or null) == null,
 (ui.tree({id:"bad",items:[{key:1,children:"bad"}]}) or null) == null,
 (ui.tree({id:"bad",items:items,checked_keys:['unknown']}) or null) == null,
 (ui.tree({id:"bad",items:items,selected_keys:[1,2]}) or null) == null,
 (ui.tree({id:"bad",items:items,check_strictly:1}) or null) == null]
