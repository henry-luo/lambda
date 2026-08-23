// module with various typed pub arrays for cross-module indexing tests
pub let int_arr = [10, 20, 30, 40, 50]
pub let str_arr = ["alpha", "beta", "gamma", "delta"]
pub let float_arr = [1.1, 2.2, 3.3, 4.4]
pub let mixed_arr = [1, "two", 3.0, true]
pub let nested_arr = [[1, 2], [3, 4], [5, 6]]
pub let items_map = {a: 1, b: 2, c: 3}

pub fn get_item(arr, i) => arr[i]
