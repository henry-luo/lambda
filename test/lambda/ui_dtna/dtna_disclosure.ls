import ui: lambda.ui.dtna
import collection: lambda.ui.core.collection
let items = [{key:'a',label:"A"},{key:2,label:"B",disabled:true},{key:'c',label:"C"}];
[((ui.collapse({id:"ok",items:items,default_active_keys:['a']}) ^ { null }) or null) != null,
 ((ui.collapse({items:items}) ^ { null }) or null) == null,
 ((ui.collapse({id:"bad",items:items,active_keys:['unknown']}) ^ { null }) or null) == null,
 ((ui.collapse({id:"bad",items:items,active_keys:['a'],default_active_keys:[]}) ^ { null }) or null) == null,
 ((ui.collapse({id:"bad",items:items,accordion:true,active_keys:['a','c']}) ^ { null }) or null) == null,
 ((ui.collapse({id:"bad",items:items,accordion:1}) ^ { null }) or null) == null,
 ((ui.tabs({id:"bad",items:items,keep_mounted:"yes"}) ^ { null }) or null) == null,
 collection.toggle(['a'],'c'),collection.toggle(['a'],'c',false),collection.toggle(['a'],'a'),
 collection.set_keys(['a'],['a','c'],true),collection.set_keys(['a','c'],['a'],false)]
