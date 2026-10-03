// @benchmark-include micro_common.js
if (typeof runJuliaMicro === "undefined") { var runJuliaMicro = require("./micro_common.js").runJuliaMicro; }
runJuliaMicro("parse_integers");
