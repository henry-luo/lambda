'use strict';

const child = require('./regression_cjs_top_level_lexical_capture_child');
const validate = child.expectsError({ name: 'Error' });

console.log(validate(new Error('ok')));
