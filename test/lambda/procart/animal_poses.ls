// PG16 / S12.1.1v3: family activity changes articulated presentation without regenerating morphology.
import animal: lambda.procart.animal
import s: lambda.scene3d
fn description(family) element {
 let attrs={version:1,generator_version:1,seed:2026,parameters:{}}
 if (family=="bird") <bird *:attrs> else if (family=="rabbit") <rabbit *:attrs> else if (family=="frog") <frog *:attrs>
 else if (family=="fish") <fish *:attrs> else if (family=="bee") <bee *:attrs> else <butterfly *:attrs>
}
let models=[for (family in animal.FAMILIES) animal.generate(description(family))^]
let poses=[for (model in models) animal.pose_nodes(model,{state:"rest",speed:0.0},0.25,false,model.family)^];
[all([for (pose in poses) s.validate(s.scene([s.camera(),*pose]))^]),
 all([for (i,model in models) poses[i]==animal.pose_nodes(model,{state:"rest",speed:0.0},0.25,false,model.family)^]),
 all([for (i,model in models) content(poses[i][0])==model.resources]),
 all([for (model in models) animal.pose_nodes(model,{state:if (model.family=="fish") "swim" else if (model.family=="rabbit") "hop" else "fly",speed:0.6},0.25)^!=
  animal.pose_nodes(model,{state:"rest",speed:0.0},0.25)^]),
 animal.pose_nodes(models[0],{state:"fly",speed:1.0},0.0)^!=animal.pose_nodes(models[0],{state:"fly",speed:1.0},0.25)^,
 animal.pose_nodes(models[1],{state:"unknown",speed:0.0}) ^ { true },
 animal.pose_nodes(models[1],{state:"rest",speed:100.0}) ^ { true }]
