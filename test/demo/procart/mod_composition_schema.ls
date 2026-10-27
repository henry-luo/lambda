// structural shape and shared attribute facet; semantic checks live in the resolver (S11.4.6).
type Seed = int | string
type KeyPart = int | string
type Attributes = {id:string,recipe:string?,seed:Seed?,key_path:KeyPart[]?,settings:map?,count:int?,spread:number[]?,
    position:number[]?,rotation:number[]?,scale:number[]?,matrix:number[]?}
pub type Node = (<group Node*> | <flower> | <tree> | <stone> | <grass>) that (map(~) is Attributes)
pub type Recipe = <recipe id:string,kind:string,generator:string,version:int,parameters:map>
pub type Document = <meadow version:int,seed:Seed,width:number,depth:number,relief:number,terrain:map?,path_width:number?,
    <resources Recipe+>, Node*>
