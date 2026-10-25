const packages = require('./package.json').dependencies;
require('../library_build.cjs')(__dirname, [
  ['react', 'suite.jsx', 'React ' + packages.react],
  ['redux_toolkit', 'redux_toolkit.js', 'Redux Toolkit ' + packages['@reduxjs/toolkit']],
  ['react_redux', 'react_redux.jsx', 'React Redux ' + packages['react-redux']],
  ['zustand', 'zustand.jsx', 'Zustand ' + packages.zustand],
  ['react_router', 'react_router.jsx', 'React Router ' + packages['react-router']]
]);
