// module with various typed pub arrays for cross-module indexing tests
pub int_arr = [10, 20, 30, 40, 50]
pub str_arr = ["alpha", "beta", "gamma", "delta"]
pub float_arr = [1.1, 2.2, 3.3, 4.4]
pub mixed_arr = [1, "two", 3.0, true]
pub nested_arr = [[1, 2], [3, 4], [5, 6]]
pub items_map = {a: 1, b: 2, c: 3}

pub fn get_item(arr, i) => arr[i]
