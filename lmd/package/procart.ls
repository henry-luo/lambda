// lambda.procart facade (Lambda_Pkg_Proc_Art.md §4): the common entry points for
// a quick script. Larger scripts import lambda.procart.<module> directly.
import rng: lambda.procart.rng
import noise: lambda.procart.noise

pub fn key(seed) u64 => rng.key(seed)
pub fn derive(k: u64, path) u64 => rng.derive(k, path)
pub fn unit(k: u64) float => rng.unit(k)

// one call from a key and a noise spec to a field of `width` x `height` samples.
pub fn noise_field(k: u64, spec, width: int, height: int) array^ => noise.field(noise.make(k, spec)^, width, height)
