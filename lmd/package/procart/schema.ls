// standalone structural envelope; scene.normalize enforces references and attribute facets (S11.4.6).
type Seed = int | string
type CoordinateScale = number | number[]
pub type SurfaceBinding = string | {pattern:string,mapping:string?,scale:CoordinateScale?,offset:number[]?,resolution:int?,roughness:number?,metalness:number?}
// drawing tags can name rules from this document; only runtime admission knows that vocabulary.
pub type Node = element
pub type Variant = <variant name:string,weight:number?,Node+>
pub type Pattern = <pattern id:string,generator:string,version:int,seed:Seed?,parameters:map?>
pub type Definition = <recipe id:string,kind:string,generator:string,version:int,parameters:map> |
    <geometry id:string,type:string,positions:number[]?,indices:number[]?,normals:number[]?> |
    <rule name:string,Variant+> | Pattern
pub type Environment = <environment latitude:number?,altitude:number?,day:int?,time:number?,weather:string?,humidity:number?,wind_speed:number?,wind_direction:number?,lighting:map?>
pub type Document = <procart version:int,seed:Seed,settings:map?,surface:SurfaceBinding?,surfaces:map?,(Environment | <resources Definition*> | Node)+>
