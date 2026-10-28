// every generator remains reachable from a directly opened page.
pub fn navigation(active) element => <nav class:"generator-nav",'aria-label':"Procedural generators",
    for (page in [{id:"grammar",title:"Form studies",file:"grammar.ls"},{id:"trees",title:"Tree studio",file:"trees.ls"},{id:"world",title:"Terrain",file:"world.ls"},{id:"meadow",title:"Meadow",file:"meadow.ls"},{id:"plants",title:"Plants & foliage",file:"plants.ls"},
        {id:"rocks",title:"Rocks & crystals",file:"rocks.ls"},{id:"textures",title:"Textures & materials",file:"textures.ls"},
        {id:"patterns",title:"Decorative patterns",file:"patterns.ls"}])
        <a href:page.file,'aria-current':(if (active == page.id) "page" else "false"),page.title>>
