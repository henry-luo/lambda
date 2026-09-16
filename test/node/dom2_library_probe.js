var dom2ProbeFs = require('fs');

function dom2_probe(name, bundlePath, smoke) {
  try {
    (0, eval)(dom2ProbeFs.readFileSync(bundlePath, 'utf8'));
    console.log(name + ':load:ok');
  } catch (error) {
    console.log(name + ':load:error:' + String(error));
    return;
  }

  try {
    var result = smoke();
    console.log(name + ':smoke:' + (result ? 'ok' : 'false'));
  } catch (error) {
    console.log(name + ':smoke:error:' + String(error));
  }
}
