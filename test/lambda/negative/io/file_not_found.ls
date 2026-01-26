// @expect-error: E401
// @description: File not found when trying to read

let data = read("nonexistent_file_12345.json")
