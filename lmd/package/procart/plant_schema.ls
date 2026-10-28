// standalone specimen envelope; runtime admission checks parameters and versions (S11.4.6).
type Seed = int | string
pub type Document = <tree version:int, generator:string, generator_version:int, seed:Seed, parameters:map>
