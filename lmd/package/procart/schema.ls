// standalone structural envelope; scene.normalize enforces references and attribute facets (S11.4.6).
type Seed = int | string
// drawing tags can name rules from this document; only runtime admission knows that vocabulary.
pub type Node = element
pub type Variant = <variant name:string,weight:number?,Node+>
pub type Definition = <recipe id:string,kind:string,generator:string,version:int,parameters:map> |
    <geometry id:string,type:string,positions:number[]?,indices:number[]?,normals:number[]?> |
    <rule name:string,Variant+>
pub type Document = <procart version:int,seed:Seed,settings:map?,<resources Definition*>,Node+>
