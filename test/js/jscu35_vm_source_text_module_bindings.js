const vm = require('vm');
let dependency_source = '';
let imports = '';

for (let index = 0; index < 33; index++) {
    dependency_source += 'export const value' + index + ' = ' + index + ';';
    if (index > 0) imports += ',';
    imports += 'value' + index + ' as alias' + index;
}

const dependency = new vm.SourceTextModule(dependency_source);
const consumer = new vm.SourceTextModule(
    'import {' + imports + '} from "dependency";' +
    'export const result = alias0 + alias32;');

consumer.link(() => dependency)
    .then(() => consumer.evaluate())
    .then(() => console.log(consumer.namespace.result));
