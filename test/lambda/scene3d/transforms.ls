import s: lambda.scene3d
let identity = s.transform()^
let parent = s.transform([10.0, 20.0, 30.0])^
let child = s.transform([1.0, 2.0, 3.0], [0.0, 0.0, 0.0], [2.0, 3.0, 4.0])^
let world = s.multiply(parent, child)^
let rotated = s.transform([0.0, 0.0, 0.0], [0.0, 0.0, 1.5707963267948966])^
let p = s.point(rotated, [1.0, 0.0, 0.0])^;
[len(identity) == 16, s.point(identity, [2.0, 3.0, 4.0])^ == [2.0, 3.0, 4.0],
 s.point(world, [1.0, 1.0, 1.0])^ == [13.0, 25.0, 37.0], abs(p[0]) < 0.000001, abs(p[1] - 1.0) < 0.000001]
