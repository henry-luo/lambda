// @test-permission
const fs = require('fs');

fs.readFile('test/node/path_basic.js', (error) => {
  console.log(error.code);
});
