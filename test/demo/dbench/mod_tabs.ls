// S13.1.3v2/S13.3.1v2: each tab retains its own task and result generation.
pub let LIMIT=4
pub fn get(tabs,id) any^ => ([for (tab in tabs where tab.id==id) tab])[0]
pub fn update(tabs,id,changes) array^ => [for (tab in tabs) if (tab.id==id) {*:tab,*:changes} else tab]
pub fn remove(tabs,id) array^ => [for (tab in tabs where tab.id!=id) tab]
pub fn accepts(tab,message) bool^ => tab!=null and message.tab==tab.id and message.generation==tab.state.generation
pub fn neighbor(tabs,id,step) any^ {
    let indexes=[for (i,tab in tabs where tab.id==id) i];
    if (len(indexes)==0 or len(tabs)==0) null else tabs[(indexes[0]+step+len(tabs))%len(tabs)].id
}
