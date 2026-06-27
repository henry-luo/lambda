const assert = require('assert');

const warnings = [];
process.on('warning', (warning) => {
  warnings.push(warning.name + ':' + warning.message.split('\n')[0]);
});

function fail() {
  assert.fail('timer should have been cleared');
}

const nanTimeout = setTimeout(fail, NaN);
clearTimeout(nanTimeout);

const nanInterval = setInterval(fail, NaN);
clearInterval(nanInterval);

const negativeTimeout = setTimeout(fail, -1);
clearTimeout(negativeTimeout);

const negativeInterval = setInterval(fail, -1);
clearInterval(negativeInterval);

const overflowTimeout = setTimeout(fail, Math.pow(2, 31));
clearTimeout(overflowTimeout);

const overflowInterval = setInterval(fail, Math.pow(2, 31));
clearInterval(overflowInterval);

for (const warning of warnings) {
  console.log(warning);
}
