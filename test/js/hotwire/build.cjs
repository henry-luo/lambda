const packages = require('./package.json').dependencies;
require('../library_build.cjs')(__dirname, [
  ['hotwire_stimulus', 'stimulus.js', 'Stimulus ' + packages['@hotwired/stimulus']],
  ['hotwire_turbo_streams', 'streams.js', 'Turbo Streams ' + packages['@hotwired/turbo']],
  ['hotwire/http.bundle', 'http.js', 'Hotwire HTTP ' + packages['@hotwired/turbo']]
]);
