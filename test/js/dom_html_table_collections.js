var table = document.getElementById('table');
var bodies = table.tBodies;
var rows = table.rows;
var bodyRows = bodies[0].rows;

console.log(bodies.length, bodies[0].id, bodies[1].id);
console.log(table.tHead.id, table.tFoot.id);
console.log(rows.length, rows[0].id, rows[4].id);
console.log(bodyRows.length, bodyRows[1].id);
console.log(table.tHead.rows[0].cells.length, table.tHead.rows[0].cells[1].id);

var appended = document.createElement('tr');
appended.id = 'body-row-3';
bodies[0].appendChild(appended);
console.log(rows.length, bodyRows.length);
