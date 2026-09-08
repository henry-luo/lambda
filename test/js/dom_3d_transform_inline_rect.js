// @document ../layout/data/baseline/transform_3d_inline_fragment_coordinates.html
var link = document.getElementById("link").getBoundingClientRect();
var atomic = document.getElementById("atomic").getBoundingClientRect();
console.log(Math.round(link.y) + "," + Math.round(atomic.y));
