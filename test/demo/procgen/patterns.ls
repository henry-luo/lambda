// Run: ./lambda.exe view test/demo/procgen/patterns.ls
import workshop: .mod_workshop
import patterns: .mod_patterns

workshop.page({id:"patterns",title:"Decorative patterns",eyebrow:"LAMBDA / PROCEDURAL ORNAMENT",
    heading:"Simple marks. Intricate arrangements.",description:"Build radial mandalas, star tiles, looping arcs, woven ribbons, mosaics and spirals.",
    catalog:patterns.CATALOG,colorways:patterns.COLORWAYS,generate:patterns.generate,draw:patterns.draw,
    detail_label:"Complexity",overlay_label:"Construction guides",
    help:"Repeat and rotate a motif. Complexity adds marks; guides reveal the underlying frame.",
    method:"Rotation, tiling, interlacing and Voronoi cells turn small rules into an ornament. Independent seed keys preserve the design when its colorway changes."})
