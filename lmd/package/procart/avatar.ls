// PG17 / S12.1.1v3: generation versions preserve saved recipes while new studios use sculpted (v2) or chibi (v3) forms.
import c: ._common
import old: ._avatar_v1
import forms: ._avatar_forms
import motion: ._avatar_motion
import s: lambda.scene3d
import color: lambda.color

pub let FORMS=old.FORMS
pub let ACTIONS=old.ACTIONS
pub let DEFAULTS=old.DEFAULTS
pub let PALETTE={*:old.PALETTE,eyes:"#27333a",trim:"#b99861"}
pub let HAIR_STYLES=["none","short","bob","long","bun","braid","waves"]
pub fn normalize(doc) element^ {
 if (doc is element and doc.generator_version==1) old.normalize(doc)^
 else {
  let valid=c.require(doc is element and name(doc)=='avatar' and doc.version==1 and (doc.generator_version==2 or doc.generator_version==3) and
   c.fields(doc,["version","generator_version","seed","form","ear_shape","outfit","accessory","marking","hair_style","parameters","appearance"]) and
   len(content(doc))==0 and doc.appearance is map and c.fields(doc.appearance,[for (k,v in PALETTE) string(k)]),"avatar: invalid version 2 or 3 recipe")^
  let appearance={*:PALETTE,*:doc.appearance}
  let legacy=old.normalize(<avatar *:{*:map([for (k,v in map(doc) where string(k)!="hair_style") (string(k),v)]),generator_version:1,
   appearance:map([for (k,v in appearance where contains([for (n,p in old.PALETTE) string(n)],string(k))) (string(k),v)])}>)^
  let hair=c.value(doc.hair_style,if (doc.form=="human") "short" else if (doc.form=="fairy") "bun" else "none")
  let checked=c.require(contains(HAIR_STYLES,hair),"avatar: unknown hairstyle")^
  let paints=[for (k,v in appearance) color.parse(v)^];
  <avatar *:{*:map(legacy),generator_version:doc.generator_version,hair_style:hair,appearance:appearance}>
 }
}
// version two remains the default upgrade; version three (chibi) is selected explicitly.
pub fn upgrade(description,target=2) element^ => normalize(<avatar *:{*:map(description),generator_version:target}>)^
pub fn generate(description) map^ {
 let doc=normalize(description)^;
 if (doc.generator_version==1) old.generate(doc)^ else forms.generate(doc)^
}
pub fn resources(models=null) element {
 let all=if (models==null) [] else if (models is array) models else [models]
 let resources=[*content(old.resources()),*[for (m in all) *c.value(m.resources,[])]];
 s.resources([for (r in resources group by r.id as id into batch) batch[0]])
}
pub fn nodes(model,action="idle",playing=true,prefix="avatar",phase=0.0,pace=1.0) array^ {
 let valid=c.require(contains(ACTIONS,action) and playing is bool and c.identifier(prefix) and c.bounded(phase,0.0,1.0) and c.bounded(pace,0.2,3.0),"avatar: invalid playback")^;
 if (model.document.generator_version==1) old.nodes(model,action,playing,prefix,phase,pace)^ else motion.nodes(model,action,playing,prefix,phase,pace)^
}
