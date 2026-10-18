import scenes: ._scenes
import ui: ._ui

ui.page("salvage",scenes.salvage(),{
    title:"Salvage bay",
    description:"Nine asymmetric assemblies float inside a containment chamber. Apply an off-centre pulse, then turn gravity on to see how their shape and mass distribution affect the tumble.",
    actions:[{id:"pulse",label:"Off-centre pulse",primary:true,play:true,
        request:{operation:"apply",commands:scenes.salvage_commands()}},
      {id:"gravity-on",label:"Gravity on",play:true,request:{operation:"apply",commands:[{type:"gravity",value:[0.0,-9.81,0.0]}]}},
      {id:"gravity-off",label:"Zero gravity",request:{operation:"apply",commands:[{type:"gravity",value:[0.0,0.0,0.0]}]}}],
    labels:["COMPOUND BODIES","AVERAGE SPIN","TRANSLATIONAL ENERGY"],
    note:"Dumbbells, L shapes and crosses each move as one compound body. The larger dumbbell end is denser; the connecting rods also contribute mass."
})
