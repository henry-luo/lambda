# Citation resource provenance

The Lambda `.ls` processor and test wrappers are project code. No citeproc implementation source is copied into this package.

The following unmodified data files are redistributed from the Citation Style Language project under [Creative Commons Attribution-ShareAlike 3.0 Unported](https://creativecommons.org/licenses/by-sa/3.0/). Original contributor names and rights notices are retained in the XML. Attribution is to the named style authors and the Citation Style Language project; locale contributors are credited in the locale files and upstream repository history. This notice and the resource files must accompany redistribution. The resource license does not change the license of the independently written Lambda processor.

| Upstream | Version/commit | Included files |
|---|---|---|
| [citation-style-language/styles](https://github.com/citation-style-language/styles) | `v1.0.2`, `590f2615e62a2b4172382fd1419ff00b4d359fc3` | `styles/ieee.csl`, `styles/apa.csl`, `styles/chicago-author-date.csl`, `styles/chicago-fullnote-bibliography.csl` |
| [citation-style-language/locales](https://github.com/citation-style-language/locales) | `v1.0.2`, `8353f0623998ad020bb2f52b59eeb175408cb107` | `locales/locales-en-US.xml`, `locales/locales-de-DE.xml`, `locales/locales-fr-FR.xml` |

Every exact source URL and SHA-256 is recorded in [resources.json](resources.json). Update the manifest when deliberately updating a resource; do not patch upstream files in place.

The optional development oracle uses [Juris-M/citeproc-js](https://github.com/Juris-M/citeproc-js), commit `cc9153c45293af878de08cafddbefe6ea150c380`, SHA-256 `db98d3341d39eb7a9f166231b6c51118ed499f79821daa3e41f1ea8d7f8d0cbb`. Its own license notices apply to that downloaded tool (CPAL/AGPL licensing options). It stays under `./temp/` and is neither redistributed here nor required at runtime. The official [CSL test suite](https://github.com/citation-style-language/test-suite), commit `6eefc5b07c6969ab8999e48542acbcc131cba864`, is likewise an external development input; its fixtures are not copied into this package.

Algorithm and specification references: [CSL 1.0.2 specification](https://docs.citationstyles.org/en/stable/specification.html), [jgm/citeproc](https://github.com/jgm/citeproc), [citeproc-js documentation](https://citeproc-js.readthedocs.io/), and [citeproc-lua](https://github.com/zepinglee/citeproc-lua). These were references for behavior and decomposition, not sources of translated implementation code.
