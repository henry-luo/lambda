import nav: lambda.navigation

let source=nav.grid([64,2]);
let first=nav.path(source,[0,0],[63,1],{heuristic:'zero'})^;
let pressure=[for (index in 0 to 399) {id:index,items:[index,index+1,index+2]}];
let second=nav.path(source,[0,0],[63,1],{heuristic:'zero'})^;
let unicode=nav.graph([nav.node("起点",[0,0]),nav.node("終点",[1,0])],[nav.edge("起点","終点",0.25)]);
let names=nav.path(unicode,"起点","終点")^;
let checks={
    retained:len(pressure)==400 and first==second,
    copied:len(first.nodes)==65 and first.cost==64.0 and first.nodes[0]==[0,0] and first.nodes[64]==[63,1],
    points:first.points[0]==[0.0,0.0] and first.points[64]==[63.0,1.0],
    names:names.nodes==["起点","終点"] and names.cost==0.25,
    source_still_valid:nav.validate(source)^
};
[for (key,passed in checks where passed!=true) string(key)]
