// S12.1.1v3/D7.2.4v4: navigation descriptions and bounded queries are pure values.
pub fn node(id,position,options = {}) map => {id:id,position:position,*:options}
pub fn edge(from,to,cost = 1.0,options = {}) map => {from:from,to:to,cost:cost,*:options}
pub fn link(id,start,end,options = {}) map => {id:id,start:start,end:end,*:options}
pub fn graph(nodes,edges,options = {}) element => <'navigation-graph' version:1,*:options,nodes:nodes,edges:edges>
pub fn grid(size,options = {}) element => <'navigation-grid' version:1,*:options,size:size>
pub fn triangle(vertices,options = {}) map => {vertices:vertices,*:options}
pub fn mesh(vertices,triangles,options = {}) element => <'navigation-mesh' version:1,*:options,vertices:vertices,triangles:triangles>
pub fn bake(triangles,settings,options = {}) element => <'navigation-bake' version:1,*:options,triangles:triangles,settings:settings>
pub fn validate(source) bool^ => lambda.sys.navigation_validate(source)^
pub fn path(source,start,goal,options = {}) map^ => lambda.sys.navigation_path(source,start,goal,options)^
