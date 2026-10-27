// Run: ./lambda.exe view test/demo/procgen/rocks.ls
import workshop: .mod_workshop
import rocks: .mod_minerals

workshop.page({id:"rocks",title:"Rocks & crystals",eyebrow:"LAMBDA / PROCEDURAL GEOLOGY",
    heading:"Facets, fractures, and time.",description:"Grow a weathered outcrop, hexagonal columns, a quartz cluster or an open amethyst geode.",
    catalog:rocks.CATALOG,colorways:rocks.COLORWAYS,generate:rocks.generate,draw:rocks.draw,detail_label:"Form complexity",overlay_label:"Facet edges",
    help:"Shape a seeded specimen, then orbit its facets and compare formations.",
    method:"Seeded vertices make irregular rocks. Hexagonal prisms build columns and crystal points; a hollow shell reveals the geode's interior."})
