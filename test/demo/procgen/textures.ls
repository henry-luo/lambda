// Run: ./lambda.exe view test/demo/procgen/textures.ls
import workshop: .mod_workshop
import textures: .mod_textures

workshop.page({id:"textures",title:"Textures & materials",eyebrow:"LAMBDA / PROCEDURAL SURFACES",
    heading:"Surfaces, from a seed.",description:"Explore wood fibers, mineral veins, masonry, woven cloth and glazed ceramic surfaces.",
    catalog:textures.CATALOG,colorways:textures.COLORWAYS,generate:textures.generate,draw:textures.draw,
    detail_label:"Pattern scale",overlay_label:"2 × 2 tile preview",
    help:"Change the grain scale or colorway. Tile preview repeats the generated swatch without regenerating it.",
    method:"Periodic coherent noise and repeating cell recipes create each surface. The generated pixels are encoded as a PNG in memory."})
