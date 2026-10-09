// compare native evaluation with the exact upstream dependency in package-lock.json.
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const crypto = require('crypto');
const {execFileSync} = require('child_process');
const spec = require('@maplibre/maplibre-gl-style-spec');
const root = path.resolve(__dirname, '../..');
const binary = path.resolve(process.env.LAMBDA_BIN || path.join(root, 'lambda.exe'));
const corpusPath = path.join(__dirname, 'expression_cases.json');
const corpus = JSON.parse(fs.readFileSync(corpusPath));
const output = path.join(root, 'temp/map-expression-reference');
fs.mkdirSync(output, {recursive:true});
const script = path.join(output, '_evaluate.ls');
fs.writeFileSync(script, `import expressions: lambda.map.expression
pn main() {
let corpus = input("test/map/expression_cases.json",'json')^;
print(format([for (sample in corpus.cases,
    let layer=<layer id:"reference",type:"circle",paint:sample.paint,filter:sample.filter>,
    let valid=not (expressions.validate(layer) is error))
    {valid:valid,paint:if (valid) expressions.evaluate(layer,corpus.feature,
        if (sample.zoom == null) 1.5 else sample.zoom) else null}],'json'))
}
`);
const native = JSON.parse(execFileSync(binary, ['run','--no-log',script], {
    cwd:root,encoding:'utf8',timeout:120000,env:{...process.env,LAMBDA_HOME:path.join(root,'lmd')}
}).trim());
const feature = {...corpus.feature, type:corpus.feature.geometry.type.replace(/^Multi/,'')};
function property(key, paint, zoom) {
    const definition = spec.latest.paint_circle[key];
    const expression = spec.createPropertyExpression(paint[key] ?? definition.default,key,definition);
    assert.equal(expression.result,'success',`${key}: ${expression.value.map?.(error=>error.message)}`);
    return expression.value.evaluate({zoom},feature);
}
const rows = corpus.cases.map((sample,index) => {
    const zoom=sample.zoom ?? 1.5,paint=sample.paint || {};
    const valid=Object.entries(paint).every(([key,value])=>
        spec.createPropertyExpression(value,key,spec.latest.paint_circle[key]).result==='success');
    assert.equal(valid,!sample.reject,`${sample.name}: reference validity`);
    assert.equal(native[index].valid,valid,`${sample.name}: native validity`);
    if (!valid) return {name:sample.name,valid};
    const visible=!!spec.featureFilter(sample.filter,'filter').filter({zoom:Math.floor(zoom)},feature);
    let expected={visible,color:'#000000',alpha:255,size:5};
    if (visible) {
        const color=property('circle-color',paint,zoom);
        const opacity=property('circle-opacity',paint,zoom);
        // upstream Color stores premultiplied channels; native paint exposes straight RGB.
        expected={visible,color:'#'+[color.r,color.g,color.b].map(channel=>Math.round(color.a?channel*255/color.a:0).toString(16).padStart(2,'0')).join(''),
            alpha:Math.round(color.a*opacity*255),size:property('circle-radius',paint,zoom)};
    }
    const actual=Object.fromEntries(Object.keys(expected).map(key=>[key,native[index].paint[key]]));
    assert.deepEqual(actual,expected,sample.name);
    return {name:sample.name,expected,actual};
});
fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({
    reference:'@maplibre/maplibre-gl-style-spec@26.4.4',
    corpus_sha256:crypto.createHash('sha256').update(fs.readFileSync(corpusPath)).digest('hex'),
    binary_sha256:crypto.createHash('sha256').update(fs.readFileSync(binary)).digest('hex'),rows
},null,2)+'\n');
console.log(`${rows.length}/${rows.length} native expression cases match MapLibre style-spec 26.4.4`);
