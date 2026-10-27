// D8.1.1 / S8.2.1v4: literal multi-axis reads work on returned numeric arrays.
import image: lambda.image
fn matrix() => [[1.0, 2.0], [3.0, 4.0]];
[matrix()[0, 1], matrix()[1, 0], matrix()[3, 0]];
let raster = image.from_array([[[255u8, 0u8, 0u8, 255u8], [0u8, 255u8, 0u8, 255u8]]])^;
image.sample(raster, 1, 0)^
