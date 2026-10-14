import math;
fn close(a,b) => max(abs(a-b)) < 1e-9

'=== pivoted solve vector and multiple RHS ==='
let a = [[0,2],[1,3]];
[close(solve(a,[4,7]), [1,2]),
 close(solve(a,[[4,2],[7,4]]), [[1,1],[2,1]]),
 shape(solve(a,[[4],[7]])), shape(solve([[2]],[4])), a]

'=== views and decimal conversion ===';
[close(solve(transpose(a),[2,8]), [1,2]),
 close(solve([[2.0m,0.0m],[0.0m,4.0m]], [6.0m,8.0m]), [3,2])]

'=== least squares with residual and column pivot ==='
let design = [[1,0],[1,1],[1,2],[1,3]]
let y = [1,2,2,4]
let fitted = lstsq(design,y);
[close(fitted,[0.9,0.9]), close(matmul(transpose(design),matmul(design,fitted)-y),[0,0]),
 close(lstsq(design,[[1,2],[2,4],[2,4],[4,8]]),[[0.9,1.8],[0.9,1.8]])]

'=== scaling avoids avoidable overflow and underflow ===';
[close(solve([[2e200,0],[0,4e200]],[6e200,8e200]),[3,2]),
 close(solve([[2e-200,0],[0,4e-200]],[6e-200,8e-200]),[3,2]),
 close(lstsq([[1e150,0],[0,2e150],[1e150,2e150]],[3e150,8e150,11e150]),[3,4])]

'=== empty systems and null ===';
[solve([],[]), lstsq([],[]), lstsq([[],[]],[1,2]),
 solve(null,[1]), lstsq([[1]],null)]

'=== shape domain and rank errors ===';
[solve([[1,2],[2,4]],[1,2]) is error,
 lstsq([[1,2],[2,4],[3,6]],[1,2,3]) is error,
 lstsq([[1,2]],[1]) is error, solve([[1,2]],[1]) is error,
 solve([[1]],[1,2]) is error, solve([1,2],[1,2]) is error,
 solve([[1],[2,3]],[1,2]) is error, solve([[1j]],[1]) is error,
 solve([[inf]],[1]) is error, solve([[1]],[null]) is error]
