var module = { exports: {} };
var exports = module.exports;
globalThis.global = globalThis;
globalThis.self = globalThis;
"use strict";

function _readOnlyError(r) { throw new TypeError('"' + r + '" is read-only'); }
function _regenerator() { /*! regenerator-runtime -- Copyright (c) 2014-present, Facebook, Inc. -- license (MIT): https://github.com/babel/babel/blob/main/packages/babel-helpers/LICENSE */ var e, t, r = "function" == typeof Symbol ? Symbol : {}, n = r.iterator || "@@iterator", o = r.toStringTag || "@@toStringTag"; function i(r, n, o, i) { var c = n && n.prototype instanceof Generator ? n : Generator, u = Object.create(c.prototype); return _regeneratorDefine2(u, "_invoke", function (r, n, o) { var i, c, u, f = 0, p = o || [], y = !1, G = { p: 0, n: 0, v: e, a: d, f: d.bind(e, 4), d: function d(t, r) { return i = t, c = 0, u = e, G.n = r, a; } }; function d(r, n) { for (c = r, u = n, t = 0; !y && f && !o && t < p.length; t++) { var o, i = p[t], d = G.p, l = i[2]; r > 3 ? (o = l === n) && (u = i[(c = i[4]) ? 5 : (c = 3, 3)], i[4] = i[5] = e) : i[0] <= d && ((o = r < 2 && d < i[1]) ? (c = 0, G.v = n, G.n = i[1]) : d < l && (o = r < 3 || i[0] > n || n > l) && (i[4] = r, i[5] = n, G.n = l, c = 0)); } if (o || r > 1) return a; throw y = !0, n; } return function (o, p, l) { if (f > 1) throw TypeError("Generator is already running"); for (y && 1 === p && d(p, l), c = p, u = l; (t = c < 2 ? e : u) || !y;) { i || (c ? c < 3 ? (c > 1 && (G.n = -1), d(c, u)) : G.n = u : G.v = u); try { if (f = 2, i) { if (c || (o = "next"), t = i[o]) { if (!(t = t.call(i, u))) throw TypeError("iterator result is not an object"); if (!t.done) return t; u = t.value, c < 2 && (c = 0); } else 1 === c && (t = i["return"]) && t.call(i), c < 2 && (u = TypeError("The iterator does not provide a '" + o + "' method"), c = 1); i = e; } else if ((t = (y = G.n < 0) ? u : r.call(n, G)) !== a) break; } catch (t) { i = e, c = 1, u = t; } finally { f = 1; } } return { value: t, done: y }; }; }(r, o, i), !0), u; } var a = {}; function Generator() {} function GeneratorFunction() {} function GeneratorFunctionPrototype() {} t = Object.getPrototypeOf; var c = [][n] ? t(t([][n]())) : (_regeneratorDefine2(t = {}, n, function () { return this; }), t), u = GeneratorFunctionPrototype.prototype = Generator.prototype = Object.create(c); function f(e) { return Object.setPrototypeOf ? Object.setPrototypeOf(e, GeneratorFunctionPrototype) : (e.__proto__ = GeneratorFunctionPrototype, _regeneratorDefine2(e, o, "GeneratorFunction")), e.prototype = Object.create(u), e; } return GeneratorFunction.prototype = GeneratorFunctionPrototype, _regeneratorDefine2(u, "constructor", GeneratorFunctionPrototype), _regeneratorDefine2(GeneratorFunctionPrototype, "constructor", GeneratorFunction), GeneratorFunction.displayName = "GeneratorFunction", _regeneratorDefine2(GeneratorFunctionPrototype, o, "GeneratorFunction"), _regeneratorDefine2(u), _regeneratorDefine2(u, o, "Generator"), _regeneratorDefine2(u, n, function () { return this; }), _regeneratorDefine2(u, "toString", function () { return "[object Generator]"; }), (_regenerator = function _regenerator() { return { w: i, m: f }; })(); }
function _regeneratorDefine2(e, r, n, t) { var i = Object.defineProperty; try { i({}, "", {}); } catch (e) { i = 0; } _regeneratorDefine2 = function _regeneratorDefine(e, r, n, t) { function o(r, n) { _regeneratorDefine2(e, r, function (e) { return this._invoke(r, n, e); }); } r ? i ? i(e, r, { value: n, enumerable: !t, configurable: !t, writable: !t }) : e[r] = n : (o("next", 0), o("throw", 1), o("return", 2)); }, _regeneratorDefine2(e, r, n, t); }
function asyncGeneratorStep(n, t, e, r, o, a, c) { try { var i = n[a](c), u = i.value; } catch (n) { return void e(n); } i.done ? t(u) : Promise.resolve(u).then(r, o); }
function _asyncToGenerator(n) { return function () { var t = this, e = arguments; return new Promise(function (r, o) { var a = n.apply(t, e); function _next(n) { asyncGeneratorStep(a, r, o, _next, _throw, "next", n); } function _throw(n) { asyncGeneratorStep(a, r, o, _next, _throw, "throw", n); } _next(void 0); }); }; }
function _defineProperty(e, r, t) { return (r = _toPropertyKey(r)) in e ? Object.defineProperty(e, r, { value: t, enumerable: !0, configurable: !0, writable: !0 }) : e[r] = t, e; }
function _slicedToArray(r, e) { return _arrayWithHoles(r) || _iterableToArrayLimit(r, e) || _unsupportedIterableToArray(r, e) || _nonIterableRest(); }
function _nonIterableRest() { throw new TypeError("Invalid attempt to destructure non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method."); }
function _iterableToArrayLimit(r, l) { var t = null == r ? null : "undefined" != typeof Symbol && r[Symbol.iterator] || r["@@iterator"]; if (null != t) { var e, n, i, u, a = [], f = !0, o = !1; try { if (i = (t = t.call(r)).next, 0 === l) { if (Object(t) !== t) return; f = !1; } else for (; !(f = (e = i.call(t)).done) && (a.push(e.value), a.length !== l); f = !0); } catch (r) { o = !0, n = r; } finally { try { if (!f && null != t["return"] && (u = t["return"](), Object(u) !== u)) return; } finally { if (o) throw n; } } return a; } }
function _arrayWithHoles(r) { if (Array.isArray(r)) return r; }
function _createForOfIteratorHelper(r, e) { var t = "undefined" != typeof Symbol && r[Symbol.iterator] || r["@@iterator"]; if (!t) { if (Array.isArray(r) || (t = _unsupportedIterableToArray(r)) || e && r && "number" == typeof r.length) { t && (r = t); var _n = 0, F = function F() {}; return { s: F, n: function n() { return _n >= r.length ? { done: !0 } : { done: !1, value: r[_n++] }; }, e: function e(r) { throw r; }, f: F }; } throw new TypeError("Invalid attempt to iterate non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method."); } var o, a = !0, u = !1; return { s: function s() { t = t.call(r); }, n: function n() { var r = t.next(); return a = r.done, r; }, e: function e(r) { u = !0, o = r; }, f: function f() { try { a || null == t["return"] || t["return"](); } finally { if (u) throw o; } } }; }
function _callSuper(t, o, e) { return o = _getPrototypeOf(o), _possibleConstructorReturn(t, _isNativeReflectConstruct() ? Reflect.construct(o, e || [], _getPrototypeOf(t).constructor) : o.apply(t, e)); }
function _possibleConstructorReturn(t, e) { if (e && ("object" == _typeof(e) || "function" == typeof e)) return e; if (void 0 !== e) throw new TypeError("Derived constructors may only return object or undefined"); return _assertThisInitialized(t); }
function _assertThisInitialized(e) { if (void 0 === e) throw new ReferenceError("this hasn't been initialised - super() hasn't been called"); return e; }
function _superPropGet(t, o, e, r) { var p = _get(_getPrototypeOf(1 & r ? t.prototype : t), o, e); return 2 & r && "function" == typeof p ? function (t) { return p.apply(e, t); } : p; }
function _get() { return _get = "undefined" != typeof Reflect && Reflect.get ? Reflect.get.bind() : function (e, t, r) { var p = _superPropBase(e, t); if (p) { var n = Object.getOwnPropertyDescriptor(p, t); return n.get ? n.get.call(arguments.length < 3 ? e : r) : n.value; } }, _get.apply(null, arguments); }
function _superPropBase(t, o) { for (; !{}.hasOwnProperty.call(t, o) && null !== (t = _getPrototypeOf(t));); return t; }
function _inherits(t, e) { if ("function" != typeof e && null !== e) throw new TypeError("Super expression must either be null or a function"); t.prototype = Object.create(e && e.prototype, { constructor: { value: t, writable: !0, configurable: !0 } }), Object.defineProperty(t, "prototype", { writable: !1 }), e && _setPrototypeOf(t, e); }
function _wrapNativeSuper(t) { var r = "function" == typeof Map ? new Map() : void 0; return _wrapNativeSuper = function _wrapNativeSuper(t) { if (null === t || !_isNativeFunction(t)) return t; if ("function" != typeof t) throw new TypeError("Super expression must either be null or a function"); if (void 0 !== r) { if (r.has(t)) return r.get(t); r.set(t, Wrapper); } function Wrapper() { return _construct(t, arguments, _getPrototypeOf(this).constructor); } return Wrapper.prototype = Object.create(t.prototype, { constructor: { value: Wrapper, enumerable: !1, writable: !0, configurable: !0 } }), _setPrototypeOf(Wrapper, t); }, _wrapNativeSuper(t); }
function _construct(t, e, r) { if (_isNativeReflectConstruct()) return Reflect.construct.apply(null, arguments); var o = [null]; o.push.apply(o, e); var p = new (t.bind.apply(t, o))(); return r && _setPrototypeOf(p, r.prototype), p; }
function _isNativeReflectConstruct() { try { var t = !Boolean.prototype.valueOf.call(Reflect.construct(Boolean, [], function () {})); } catch (t) {} return (_isNativeReflectConstruct = function _isNativeReflectConstruct() { return !!t; })(); }
function _isNativeFunction(t) { try { return -1 !== Function.toString.call(t).indexOf("[native code]"); } catch (n) { return "function" == typeof t; } }
function _setPrototypeOf(t, e) { return _setPrototypeOf = Object.setPrototypeOf ? Object.setPrototypeOf.bind() : function (t, e) { return t.__proto__ = e, t; }, _setPrototypeOf(t, e); }
function _getPrototypeOf(t) { return _getPrototypeOf = Object.setPrototypeOf ? Object.getPrototypeOf.bind() : function (t) { return t.__proto__ || Object.getPrototypeOf(t); }, _getPrototypeOf(t); }
function _toConsumableArray(r) { return _arrayWithoutHoles(r) || _iterableToArray(r) || _unsupportedIterableToArray(r) || _nonIterableSpread(); }
function _nonIterableSpread() { throw new TypeError("Invalid attempt to spread non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method."); }
function _unsupportedIterableToArray(r, a) { if (r) { if ("string" == typeof r) return _arrayLikeToArray(r, a); var t = {}.toString.call(r).slice(8, -1); return "Object" === t && r.constructor && (t = r.constructor.name), "Map" === t || "Set" === t ? Array.from(r) : "Arguments" === t || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(t) ? _arrayLikeToArray(r, a) : void 0; } }
function _iterableToArray(r) { if ("undefined" != typeof Symbol && null != r[Symbol.iterator] || null != r["@@iterator"]) return Array.from(r); }
function _arrayWithoutHoles(r) { if (Array.isArray(r)) return _arrayLikeToArray(r); }
function _arrayLikeToArray(r, a) { (null == a || a > r.length) && (a = r.length); for (var e = 0, n = Array(a); e < a; e++) n[e] = r[e]; return n; }
function _defineProperties(e, r) { for (var t = 0; t < r.length; t++) { var o = r[t]; o.enumerable = o.enumerable || !1, o.configurable = !0, "value" in o && (o.writable = !0), Object.defineProperty(e, _toPropertyKey(o.key), o); } }
function _createClass(e, r, t) { return r && _defineProperties(e.prototype, r), t && _defineProperties(e, t), Object.defineProperty(e, "prototype", { writable: !1 }), e; }
function _toPropertyKey(t) { var i = _toPrimitive(t, "string"); return "symbol" == _typeof(i) ? i : i + ""; }
function _toPrimitive(t, r) { if ("object" != _typeof(t) || !t) return t; var e = t[Symbol.toPrimitive]; if (void 0 !== e) { var i = e.call(t, r || "default"); if ("object" != _typeof(i)) return i; throw new TypeError("@@toPrimitive must return a primitive value."); } return ("string" === r ? String : Number)(t); }
function _classCallCheck(a, n) { if (!(a instanceof n)) throw new TypeError("Cannot call a class as a function"); }
function _typeof(o) { "@babel/helpers - typeof"; return _typeof = "function" == typeof Symbol && "symbol" == typeof Symbol.iterator ? function (o) { return typeof o; } : function (o) { return o && "function" == typeof Symbol && o.constructor === Symbol && o !== Symbol.prototype ? "symbol" : typeof o; }, _typeof(o); }
var yup = function () {
  var __getOwnPropNames = Object.getOwnPropertyNames;
  var __commonJS = function __commonJS(cb, mod) {
    return function __require() {
      return mod || (0, cb[__getOwnPropNames(cb)[0]])((mod = {
        exports: {}
      }).exports, mod), mod.exports;
    };
  };

  // ../../node_modules/property-expr/index.js
  var require_property_expr = __commonJS({
    "../../node_modules/property-expr/index.js": function __node_modules_propertyExpr_indexJs(exports, module) {
      "use strict";

      function Cache(maxSize) {
        this._maxSize = maxSize;
        this.clear();
      }
      Cache.prototype.clear = function () {
        this._size = 0;
        this._values = /* @__PURE__ */Object.create(null);
      };
      Cache.prototype.get = function (key) {
        return this._values[key];
      };
      Cache.prototype.set = function (key, value) {
        this._size >= this._maxSize && this.clear();
        if (!(key in this._values)) this._size++;
        return this._values[key] = value;
      };
      var SPLIT_REGEX = /[^.^\]^[]+|(?=\[\]|\.\.)/g;
      var DIGIT_REGEX = /^\d+$/;
      var LEAD_DIGIT_REGEX = /^\d/;
      var SPEC_CHAR_REGEX = /[~`!#$%\^&*+=\-\[\]\\';,/{}|\\":<>\?]/g;
      var CLEAN_QUOTES_REGEX = /^\s*(['"]?)(.*?)(\1)\s*$/;
      var MAX_CACHE_SIZE = 512;
      var pathCache = new Cache(MAX_CACHE_SIZE);
      var setCache = new Cache(MAX_CACHE_SIZE);
      var getCache = new Cache(MAX_CACHE_SIZE);
      module.exports = {
        Cache: Cache,
        split: split,
        normalizePath: normalizePath,
        setter: function setter(path) {
          var parts = normalizePath(path);
          return setCache.get(path) || setCache.set(path, function setter(obj, value) {
            var index = 0;
            var len = parts.length;
            var data = obj;
            while (index < len - 1) {
              var part = parts[index];
              if (part === "__proto__" || part === "constructor" || part === "prototype") {
                return obj;
              }
              data = data[parts[index++]];
            }
            data[parts[index]] = value;
          });
        },
        getter: function getter(path, safe) {
          var parts = normalizePath(path);
          return getCache.get(path) || getCache.set(path, function getter(data) {
            var index = 0,
              len = parts.length;
            while (index < len) {
              if (data != null || !safe) data = data[parts[index++]];else return;
            }
            return data;
          });
        },
        join: function join(segments) {
          return segments.reduce(function (path, part) {
            return path + (isQuoted(part) || DIGIT_REGEX.test(part) ? "[" + part + "]" : (path ? "." : "") + part);
          }, "");
        },
        forEach: function forEach(path, cb, thisArg) {
          _forEach(Array.isArray(path) ? path : split(path), cb, thisArg);
        }
      };
      function normalizePath(path) {
        return pathCache.get(path) || pathCache.set(path, split(path).map(function (part) {
          return part.replace(CLEAN_QUOTES_REGEX, "$2");
        }));
      }
      function split(path) {
        return path.match(SPLIT_REGEX) || [""];
      }
      function _forEach(parts, iter, thisArg) {
        var len = parts.length,
          part,
          idx,
          isArray,
          isBracket;
        for (idx = 0; idx < len; idx++) {
          part = parts[idx];
          if (part) {
            if (shouldBeQuoted(part)) {
              part = '"' + part + '"';
            }
            isBracket = isQuoted(part);
            isArray = !isBracket && /^\d+$/.test(part);
            iter.call(thisArg, part, isBracket, isArray, idx, parts);
          }
        }
      }
      function isQuoted(str) {
        return typeof str === "string" && str && ["'", '"'].indexOf(str.charAt(0)) !== -1;
      }
      function hasLeadingNumber(part) {
        return part.match(LEAD_DIGIT_REGEX) && !part.match(DIGIT_REGEX);
      }
      function hasSpecialChars(part) {
        return SPEC_CHAR_REGEX.test(part);
      }
      function shouldBeQuoted(part) {
        return !isQuoted(part) && (hasLeadingNumber(part) || hasSpecialChars(part));
      }
    }
  });

  // ../../node_modules/tiny-case/index.js
  var require_tiny_case = __commonJS({
    "../../node_modules/tiny-case/index.js": function __node_modules_tinyCase_indexJs(exports, module) {
      var reWords = /[A-Z\xc0-\xd6\xd8-\xde]?[a-z\xdf-\xf6\xf8-\xff]+(?:['’](?:d|ll|m|re|s|t|ve))?(?=[\xac\xb1\xd7\xf7\x00-\x2f\x3a-\x40\x5b-\x60\x7b-\xbf\u2000-\u206f \t\x0b\f\xa0\ufeff\n\r\u2028\u2029\u1680\u180e\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u202f\u205f\u3000]|[A-Z\xc0-\xd6\xd8-\xde]|$)|(?:[A-Z\xc0-\xd6\xd8-\xde]|[^\ud800-\udfff\xac\xb1\xd7\xf7\x00-\x2f\x3a-\x40\x5b-\x60\x7b-\xbf\u2000-\u206f \t\x0b\f\xa0\ufeff\n\r\u2028\u2029\u1680\u180e\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u202f\u205f\u3000\d+\u2700-\u27bfa-z\xdf-\xf6\xf8-\xffA-Z\xc0-\xd6\xd8-\xde])+(?:['’](?:D|LL|M|RE|S|T|VE))?(?=[\xac\xb1\xd7\xf7\x00-\x2f\x3a-\x40\x5b-\x60\x7b-\xbf\u2000-\u206f \t\x0b\f\xa0\ufeff\n\r\u2028\u2029\u1680\u180e\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u202f\u205f\u3000]|[A-Z\xc0-\xd6\xd8-\xde](?:[a-z\xdf-\xf6\xf8-\xff]|[^\ud800-\udfff\xac\xb1\xd7\xf7\x00-\x2f\x3a-\x40\x5b-\x60\x7b-\xbf\u2000-\u206f \t\x0b\f\xa0\ufeff\n\r\u2028\u2029\u1680\u180e\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u202f\u205f\u3000\d+\u2700-\u27bfa-z\xdf-\xf6\xf8-\xffA-Z\xc0-\xd6\xd8-\xde])|$)|[A-Z\xc0-\xd6\xd8-\xde]?(?:[a-z\xdf-\xf6\xf8-\xff]|[^\ud800-\udfff\xac\xb1\xd7\xf7\x00-\x2f\x3a-\x40\x5b-\x60\x7b-\xbf\u2000-\u206f \t\x0b\f\xa0\ufeff\n\r\u2028\u2029\u1680\u180e\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u202f\u205f\u3000\d+\u2700-\u27bfa-z\xdf-\xf6\xf8-\xffA-Z\xc0-\xd6\xd8-\xde])+(?:['’](?:d|ll|m|re|s|t|ve))?|[A-Z\xc0-\xd6\xd8-\xde]+(?:['’](?:D|LL|M|RE|S|T|VE))?|\d*(?:1ST|2ND|3RD|(?![123])\dTH)(?=\b|[a-z_])|\d*(?:1st|2nd|3rd|(?![123])\dth)(?=\b|[A-Z_])|\d+|(?:[\u2700-\u27bf]|(?:\ud83c[\udde6-\uddff]){2}|[\ud800-\udbff][\udc00-\udfff])[\ufe0e\ufe0f]?(?:[\u0300-\u036f\ufe20-\ufe2f\u20d0-\u20ff]|\ud83c[\udffb-\udfff])?(?:\u200d(?:[^\ud800-\udfff]|(?:\ud83c[\udde6-\uddff]){2}|[\ud800-\udbff][\udc00-\udfff])[\ufe0e\ufe0f]?(?:[\u0300-\u036f\ufe20-\ufe2f\u20d0-\u20ff]|\ud83c[\udffb-\udfff])?)*/g;
      var words = function words(str) {
        return str.match(reWords) || [];
      };
      var upperFirst = function upperFirst(str) {
        return str[0].toUpperCase() + str.slice(1);
      };
      var join = function join(str, d) {
        return words(str).join(d).toLowerCase();
      };
      var camelCase = function camelCase(str) {
        return words(str).reduce(function (acc, next) {
          return "".concat(acc).concat(!acc ? next.toLowerCase() : next[0].toUpperCase() + next.slice(1).toLowerCase());
        }, "");
      };
      var pascalCase = function pascalCase(str) {
        return upperFirst(camelCase(str));
      };
      var snakeCase = function snakeCase(str) {
        return join(str, "_");
      };
      var kebabCase = function kebabCase(str) {
        return join(str, "-");
      };
      var sentenceCase = function sentenceCase(str) {
        return upperFirst(join(str, " "));
      };
      var titleCase = function titleCase(str) {
        return words(str).map(upperFirst).join(" ");
      };
      module.exports = {
        words: words,
        upperFirst: upperFirst,
        camelCase: camelCase,
        pascalCase: pascalCase,
        snakeCase: snakeCase,
        kebabCase: kebabCase,
        sentenceCase: sentenceCase,
        titleCase: titleCase
      };
    }
  });

  // ../../node_modules/toposort/index.js
  var require_toposort = __commonJS({
    "../../node_modules/toposort/index.js": function __node_modules_toposort_indexJs(exports, module) {
      module.exports = function (edges) {
        return toposort(uniqueNodes(edges), edges);
      };
      module.exports.array = toposort;
      function toposort(nodes, edges) {
        var cursor = nodes.length,
          sorted = new Array(cursor),
          visited = {},
          i = cursor,
          outgoingEdges = makeOutgoingEdges(edges),
          nodesHash = makeNodesHash(nodes);
        edges.forEach(function (edge) {
          if (!nodesHash.has(edge[0]) || !nodesHash.has(edge[1])) {
            throw new Error("Unknown node. There is an unknown node in the supplied edges.");
          }
        });
        while (i--) {
          if (!visited[i]) visit(nodes[i], i, /* @__PURE__ */new Set());
        }
        return sorted;
        function visit(node, i2, predecessors) {
          if (predecessors.has(node)) {
            var nodeRep;
            try {
              nodeRep = ", node was:" + JSON.stringify(node);
            } catch (e) {
              nodeRep = "";
            }
            throw new Error("Cyclic dependency" + nodeRep);
          }
          if (!nodesHash.has(node)) {
            throw new Error("Found unknown node. Make sure to provided all involved nodes. Unknown node: " + JSON.stringify(node));
          }
          if (visited[i2]) return;
          visited[i2] = true;
          var outgoing = outgoingEdges.get(node) || /* @__PURE__ */new Set();
          outgoing = Array.from(outgoing);
          if (i2 = outgoing.length) {
            predecessors.add(node);
            do {
              var child = outgoing[--i2];
              visit(child, nodesHash.get(child), predecessors);
            } while (i2);
            predecessors["delete"](node);
          }
          sorted[--cursor] = node;
        }
      }
      function uniqueNodes(arr) {
        var res = /* @__PURE__ */new Set();
        for (var i = 0, len = arr.length; i < len; i++) {
          var edge = arr[i];
          res.add(edge[0]);
          res.add(edge[1]);
        }
        return Array.from(res);
      }
      function makeOutgoingEdges(arr) {
        var edges = /* @__PURE__ */new Map();
        for (var i = 0, len = arr.length; i < len; i++) {
          var edge = arr[i];
          if (!edges.has(edge[0])) edges.set(edge[0], /* @__PURE__ */new Set());
          if (!edges.has(edge[1])) edges.set(edge[1], /* @__PURE__ */new Set());
          edges.get(edge[0]).add(edge[1]);
        }
        return edges;
      }
      function makeNodesHash(arr) {
        var res = /* @__PURE__ */new Map();
        for (var i = 0, len = arr.length; i < len; i++) {
          res.set(arr[i], i);
        }
        return res;
      }
    }
  });

  // ../../package/index.js
  var require_package = __commonJS({
    "../../package/index.js": function __package_indexJs(exports) {
      "use strict";

      Object.defineProperty(exports, "__esModule", {
        value: true
      });
      var propertyExpr = require_property_expr();
      var tinyCase = require_tiny_case();
      var toposort = require_toposort();
      function _interopDefaultLegacy(e) {
        return e && _typeof(e) === "object" && "default" in e ? e : {
          "default": e
        };
      }
      var toposort__default = /* @__PURE__ */_interopDefaultLegacy(toposort);
      var toString = Object.prototype.toString;
      var errorToString = Error.prototype.toString;
      var regExpToString = RegExp.prototype.toString;
      var symbolToString = typeof Symbol !== "undefined" ? Symbol.prototype.toString : function () {
        return "";
      };
      var SYMBOL_REGEXP = /^Symbol\((.*)\)(.*)$/;
      function printNumber(val) {
        if (val != +val) return "NaN";
        var isNegativeZero = val === 0 && 1 / val < 0;
        return isNegativeZero ? "-0" : "" + val;
      }
      function printSimpleValue(val) {
        var quoteStrings = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : false;
        if (val == null || val === true || val === false) return "" + val;
        var typeOf = _typeof(val);
        if (typeOf === "number") return printNumber(val);
        if (typeOf === "string") return quoteStrings ? "\"".concat(val, "\"") : val;
        if (typeOf === "function") return "[Function " + (val.name || "anonymous") + "]";
        if (typeOf === "symbol") return symbolToString.call(val).replace(SYMBOL_REGEXP, "Symbol($1)");
        var tag = toString.call(val).slice(8, -1);
        if (tag === "Date") return isNaN(val.getTime()) ? "" + val : val.toISOString(val);
        if (tag === "Error" || val instanceof Error) return "[" + errorToString.call(val) + "]";
        if (tag === "RegExp") return regExpToString.call(val);
        return null;
      }
      function printValue(value, quoteStrings) {
        var result = printSimpleValue(value, quoteStrings);
        if (result !== null) return result;
        return JSON.stringify(value, function (key, value2) {
          var result2 = printSimpleValue(this[key], quoteStrings);
          if (result2 !== null) return result2;
          return value2;
        }, 2);
      }
      function toArray(value) {
        return value == null ? [] : [].concat(value);
      }
      var _Symbol$toStringTag;
      var _Symbol$hasInstance;
      var _Symbol$toStringTag2;
      var strReg = /\$\{\s*(\w+)\s*\}/g;
      _Symbol$toStringTag = Symbol.toStringTag;
      var ValidationErrorNoStack = /*#__PURE__*/_createClass(function ValidationErrorNoStack(errorOrErrors, value, field, type) {
        var _this = this;
        _classCallCheck(this, ValidationErrorNoStack);
        this.name = void 0;
        this.message = void 0;
        this.value = void 0;
        this.path = void 0;
        this.type = void 0;
        this.params = void 0;
        this.errors = void 0;
        this.inner = void 0;
        this[_Symbol$toStringTag] = "Error";
        this.name = "ValidationError";
        this.value = value;
        this.path = field;
        this.type = type;
        this.errors = [];
        this.inner = [];
        toArray(errorOrErrors).forEach(function (err) {
          if (ValidationError.isError(err)) {
            var _this$errors, _this$inner;
            (_this$errors = _this.errors).push.apply(_this$errors, _toConsumableArray(err.errors));
            var innerErrors = err.inner.length ? err.inner : [err];
            (_this$inner = _this.inner).push.apply(_this$inner, _toConsumableArray(innerErrors));
          } else {
            _this.errors.push(err);
          }
        });
        this.message = this.errors.length > 1 ? "".concat(this.errors.length, " errors occurred") : this.errors[0];
      });
      _Symbol$hasInstance = Symbol.hasInstance;
      _Symbol$toStringTag2 = Symbol.toStringTag;
      var ValidationError = /*#__PURE__*/function (_Error, _Symbol$hasInstance2) {
        function _ValidationError(errorOrErrors, value, field, type, disableStack) {
          var _this2;
          _classCallCheck(this, _ValidationError);
          var errorNoStack = new ValidationErrorNoStack(errorOrErrors, value, field, type);
          if (disableStack) {
            return _possibleConstructorReturn(_this2, errorNoStack);
          }
          _this2 = _callSuper(this, _ValidationError);
          _this2.value = void 0;
          _this2.path = void 0;
          _this2.type = void 0;
          _this2.params = void 0;
          _this2.errors = [];
          _this2.inner = [];
          _this2[_Symbol$toStringTag2] = "Error";
          _this2.name = errorNoStack.name;
          _this2.message = errorNoStack.message;
          _this2.type = errorNoStack.type;
          _this2.value = errorNoStack.value;
          _this2.path = errorNoStack.path;
          _this2.errors = errorNoStack.errors;
          _this2.inner = errorNoStack.inner;
          if (Error.captureStackTrace) {
            Error.captureStackTrace(_this2, _ValidationError);
          }
          return _this2;
        }
        _inherits(_ValidationError, _Error);
        return _createClass(_ValidationError, null, [{
          key: "formatError",
          value: function formatError(message, params) {
            var path = params.label || params.path || "this";
            params = Object.assign({}, params, {
              path: path,
              originalPath: params.path
            });
            if (typeof message === "string") return message.replace(strReg, function (_, key) {
              return printValue(params[key]);
            });
            if (typeof message === "function") return message(params);
            return message;
          }
        }, {
          key: "isError",
          value: function isError(err) {
            return err && err.name === "ValidationError";
          }
        }, {
          key: _Symbol$hasInstance2,
          value: function value(inst) {
            return ValidationErrorNoStack[Symbol.hasInstance](inst) || _superPropGet(_ValidationError, Symbol.hasInstance, this, 2)([inst]);
          }
        }]);
      }(/*#__PURE__*/_wrapNativeSuper(Error), _Symbol$hasInstance);
      var mixed = {
        "default": "${path} is invalid",
        required: "${path} is a required field",
        defined: "${path} must be defined",
        notNull: "${path} cannot be null",
        oneOf: "${path} must be one of the following values: ${values}",
        notOneOf: "${path} must not be one of the following values: ${values}",
        notType: function notType(_ref) {
          var path = _ref.path,
            type = _ref.type,
            value = _ref.value,
            originalValue = _ref.originalValue;
          var castMsg = originalValue != null && originalValue !== value ? " (cast from the value `".concat(printValue(originalValue, true), "`).") : ".";
          return type !== "mixed" ? "".concat(path, " must be a `").concat(type, "` type, but the final value was: `").concat(printValue(value, true), "`") + castMsg : "".concat(path, " must match the configured type. The validated value was: `").concat(printValue(value, true), "`") + castMsg;
        }
      };
      var string = {
        length: "${path} must be exactly ${length} characters",
        min: "${path} must be at least ${min} characters",
        max: "${path} must be at most ${max} characters",
        matches: '${path} must match the following: "${regex}"',
        email: "${path} must be a valid email",
        url: "${path} must be a valid URL",
        uuid: "${path} must be a valid UUID",
        datetime: "${path} must be a valid ISO date-time",
        datetime_precision: "${path} must be a valid ISO date-time with a sub-second precision of exactly ${precision} digits",
        datetime_offset: '${path} must be a valid ISO date-time with UTC "Z" timezone',
        trim: "${path} must be a trimmed string",
        lowercase: "${path} must be a lowercase string",
        uppercase: "${path} must be a upper case string"
      };
      var number = {
        min: "${path} must be greater than or equal to ${min}",
        max: "${path} must be less than or equal to ${max}",
        lessThan: "${path} must be less than ${less}",
        moreThan: "${path} must be greater than ${more}",
        positive: "${path} must be a positive number",
        negative: "${path} must be a negative number",
        integer: "${path} must be an integer"
      };
      var date = {
        min: "${path} field must be later than ${min}",
        max: "${path} field must be at earlier than ${max}"
      };
      var _boolean = {
        isValue: "${path} field must be ${value}"
      };
      var object = {
        noUnknown: "${path} field has unspecified keys: ${unknown}",
        exact: "${path} object contains unknown properties: ${properties}"
      };
      var array = {
        min: "${path} field must have at least ${min} items",
        max: "${path} field must have less than or equal to ${max} items",
        length: "${path} must have ${length} items"
      };
      var tuple = {
        notType: function notType(params) {
          var path = params.path,
            value = params.value,
            spec = params.spec;
          var typeLen = spec.types.length;
          if (Array.isArray(value)) {
            if (value.length < typeLen) return "".concat(path, " tuple value has too few items, expected a length of ").concat(typeLen, " but got ").concat(value.length, " for value: `").concat(printValue(value, true), "`");
            if (value.length > typeLen) return "".concat(path, " tuple value has too many items, expected a length of ").concat(typeLen, " but got ").concat(value.length, " for value: `").concat(printValue(value, true), "`");
          }
          return ValidationError.formatError(mixed.notType, params);
        }
      };
      var locale = Object.assign(/* @__PURE__ */Object.create(null), {
        mixed: mixed,
        string: string,
        number: number,
        date: date,
        object: object,
        array: array,
        "boolean": _boolean,
        tuple: tuple
      });
      var isSchema = function isSchema(obj) {
        return obj && obj.__isYupSchema__;
      };
      var Condition = /*#__PURE__*/function () {
        function _Condition(refs, builder) {
          _classCallCheck(this, _Condition);
          this.fn = void 0;
          this.refs = refs;
          this.refs = refs;
          this.fn = builder;
        }
        return _createClass(_Condition, [{
          key: "resolve",
          value: function resolve(base, options) {
            var values = this.refs.map(function (ref) {
              return (
                // TODO: ? operator here?
                ref.getValue(options == null ? void 0 : options.value, options == null ? void 0 : options.parent, options == null ? void 0 : options.context)
              );
            });
            var schema = this.fn(values, base, options);
            if (schema === void 0 ||
            // @ts-ignore this can be base
            schema === base) {
              return base;
            }
            if (!isSchema(schema)) throw new TypeError("conditions must return a schema object");
            return schema.resolve(options);
          }
        }], [{
          key: "fromOptions",
          value: function fromOptions(refs, config) {
            if (!config.then && !config.otherwise) throw new TypeError("either `then:` or `otherwise:` is required for `when()` conditions");
            var is = config.is,
              then = config.then,
              otherwise = config.otherwise;
            var check = typeof is === "function" ? is : function () {
              for (var _len = arguments.length, values = new Array(_len), _key = 0; _key < _len; _key++) {
                values[_key] = arguments[_key];
              }
              return values.every(function (value) {
                return value === is;
              });
            };
            return new _Condition(refs, function (values, schema) {
              var _branch;
              var branch = check.apply(void 0, _toConsumableArray(values)) ? then : otherwise;
              return (_branch = branch == null ? void 0 : branch(schema)) != null ? _branch : schema;
            });
          }
        }]);
      }();
      var prefixes = {
        context: "$",
        value: "."
      };
      function create$9(key, options) {
        return new Reference(key, options);
      }
      var Reference = /*#__PURE__*/function () {
        function Reference(key) {
          var options = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
          _classCallCheck(this, Reference);
          this.key = void 0;
          this.isContext = void 0;
          this.isValue = void 0;
          this.isSibling = void 0;
          this.path = void 0;
          this.getter = void 0;
          this.map = void 0;
          if (typeof key !== "string") throw new TypeError("ref must be a string, got: " + key);
          this.key = key.trim();
          if (key === "") throw new TypeError("ref must be a non-empty string");
          this.isContext = this.key[0] === prefixes.context;
          this.isValue = this.key[0] === prefixes.value;
          this.isSibling = !this.isContext && !this.isValue;
          var prefix = this.isContext ? prefixes.context : this.isValue ? prefixes.value : "";
          this.path = this.key.slice(prefix.length);
          this.getter = this.path && propertyExpr.getter(this.path, true);
          this.map = options.map;
        }
        return _createClass(Reference, [{
          key: "getValue",
          value: function getValue(value, parent, context) {
            var result = this.isContext ? context : this.isValue ? value : parent;
            if (this.getter) result = this.getter(result || {});
            if (this.map) result = this.map(result);
            return result;
          }
          /**
           *
           * @param {*} value
           * @param {Object} options
           * @param {Object=} options.context
           * @param {Object=} options.parent
           */
        }, {
          key: "cast",
          value: function cast(value, options) {
            return this.getValue(value, options == null ? void 0 : options.parent, options == null ? void 0 : options.context);
          }
        }, {
          key: "resolve",
          value: function resolve() {
            return this;
          }
        }, {
          key: "describe",
          value: function describe() {
            return {
              type: "ref",
              key: this.key
            };
          }
        }, {
          key: "toString",
          value: function toString() {
            return "Ref(".concat(this.key, ")");
          }
        }], [{
          key: "isRef",
          value: function isRef(value) {
            return value && value.__isYupRef;
          }
        }]);
      }();
      Reference.prototype.__isYupRef = true;
      var isAbsent = function isAbsent(value) {
        return value == null;
      };
      function createValidation(config) {
        function validate(_ref2, panic, next) {
          var value = _ref2.value,
            _ref2$path = _ref2.path,
            path = _ref2$path === void 0 ? "" : _ref2$path,
            options = _ref2.options,
            originalValue = _ref2.originalValue,
            schema = _ref2.schema;
          var name = config.name,
            test = config.test,
            params = config.params,
            message = config.message,
            skipAbsent = config.skipAbsent;
          var parent = options.parent,
            context = options.context,
            _options$abortEarly2 = options.abortEarly,
            abortEarly = _options$abortEarly2 === void 0 ? schema.spec.abortEarly : _options$abortEarly2,
            _options$disableStack4 = options.disableStackTrace,
            disableStackTrace = _options$disableStack4 === void 0 ? schema.spec.disableStackTrace : _options$disableStack4;
          var resolveOptions = {
            value: value,
            parent: parent,
            context: context
          };
          function createError() {
            var overrides = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
            var nextParams = resolveParams(Object.assign({
              value: value,
              originalValue: originalValue,
              label: schema.spec.label,
              path: overrides.path || path,
              spec: schema.spec,
              disableStackTrace: overrides.disableStackTrace || disableStackTrace
            }, params, overrides.params), resolveOptions);
            var error = new ValidationError(ValidationError.formatError(overrides.message || message, nextParams), value, nextParams.path, overrides.type || name, nextParams.disableStackTrace);
            error.params = nextParams;
            return error;
          }
          var invalid = abortEarly ? panic : next;
          var ctx = {
            path: path,
            parent: parent,
            type: name,
            from: options.from,
            createError: createError,
            resolve: function resolve(item) {
              return resolveMaybeRef(item, resolveOptions);
            },
            options: options,
            originalValue: originalValue,
            schema: schema
          };
          var handleResult = function handleResult(validOrError) {
            if (ValidationError.isError(validOrError)) invalid(validOrError);else if (!validOrError) invalid(createError());else next(null);
          };
          var handleError = function handleError(err) {
            if (ValidationError.isError(err)) invalid(err);else panic(err);
          };
          var shouldSkip = skipAbsent && isAbsent(value);
          if (shouldSkip) {
            return handleResult(true);
          }
          var result;
          try {
            var _result;
            result = test.call(ctx, value, ctx);
            if (typeof ((_result = result) == null ? void 0 : _result.then) === "function") {
              if (options.sync) {
                throw new Error("Validation test of type: \"".concat(ctx.type, "\" returned a Promise during a synchronous validate. This test will finish after the validate call has returned"));
              }
              return Promise.resolve(result).then(handleResult, handleError);
            }
          } catch (err) {
            handleError(err);
            return;
          }
          handleResult(result);
        }
        validate.OPTIONS = config;
        return validate;
      }
      function resolveParams(params, options) {
        if (!params) return params;
        for (var _i = 0, _Object$keys = Object.keys(params); _i < _Object$keys.length; _i++) {
          var key = _Object$keys[_i];
          params[key] = resolveMaybeRef(params[key], options);
        }
        return params;
      }
      function resolveMaybeRef(item, options) {
        return Reference.isRef(item) ? item.getValue(options.value, options.parent, options.context) : item;
      }
      function getIn(schema, path, value) {
        var context = arguments.length > 3 && arguments[3] !== undefined ? arguments[3] : value;
        var parent, lastPart, lastPartDebug;
        if (!path) return {
          parent: parent,
          parentPath: path,
          schema: schema
        };
        propertyExpr.forEach(path, function (_part, isBracket, isArray) {
          var part = isBracket ? _part.slice(1, _part.length - 1) : _part;
          schema = schema.resolve({
            context: context,
            parent: parent,
            value: value
          });
          var isTuple = schema.type === "tuple";
          var idx = isArray ? parseInt(part, 10) : 0;
          if (schema.innerType || isTuple) {
            if (isTuple && !isArray) throw new Error("Yup.reach cannot implicitly index into a tuple type. the path part \"".concat(lastPartDebug, "\" must contain an index to the tuple element, e.g. \"").concat(lastPartDebug, "[0]\""));
            if (value && idx >= value.length) {
              throw new Error("Yup.reach cannot resolve an array item at index: ".concat(_part, ", in the path: ").concat(path, ". because there is no value at that index. "));
            }
            parent = value;
            value = value && value[idx];
            schema = isTuple ? schema.spec.types[idx] : schema.innerType;
          }
          if (!isArray) {
            if (!schema.fields || !schema.fields[part]) throw new Error("The schema does not contain the path: ".concat(path, ". (failed at: ").concat(lastPartDebug, " which is a type: \"").concat(schema.type, "\")"));
            parent = value;
            value = value && value[part];
            schema = schema.fields[part];
          }
          lastPart = part;
          lastPartDebug = isBracket ? "[" + _part + "]" : "." + _part;
        });
        return {
          schema: schema,
          parent: parent,
          parentPath: lastPart
        };
      }
      function reach(obj, path, value, context) {
        return getIn(obj, path, value, context).schema;
      }
      var ReferenceSet = /*#__PURE__*/function (_Set) {
        function _ReferenceSet() {
          _classCallCheck(this, _ReferenceSet);
          return _callSuper(this, _ReferenceSet, arguments);
        }
        _inherits(_ReferenceSet, _Set);
        return _createClass(_ReferenceSet, [{
          key: "describe",
          value: function describe() {
            var description = [];
            var _iterator = _createForOfIteratorHelper(this.values()),
              _step;
            try {
              for (_iterator.s(); !(_step = _iterator.n()).done;) {
                var item = _step.value;
                description.push(Reference.isRef(item) ? item.describe() : item);
              }
            } catch (err) {
              _iterator.e(err);
            } finally {
              _iterator.f();
            }
            return description;
          }
        }, {
          key: "resolveAll",
          value: function resolveAll(resolve) {
            var result = [];
            var _iterator2 = _createForOfIteratorHelper(this.values()),
              _step2;
            try {
              for (_iterator2.s(); !(_step2 = _iterator2.n()).done;) {
                var item = _step2.value;
                result.push(resolve(item));
              }
            } catch (err) {
              _iterator2.e(err);
            } finally {
              _iterator2.f();
            }
            return result;
          }
        }, {
          key: "clone",
          value: function clone() {
            return new _ReferenceSet(this.values());
          }
        }, {
          key: "merge",
          value: function merge(newItems, removeItems) {
            var next = this.clone();
            newItems.forEach(function (value) {
              return next.add(value);
            });
            removeItems.forEach(function (value) {
              return next["delete"](value);
            });
            return next;
          }
        }]);
      }(/*#__PURE__*/_wrapNativeSuper(Set));
      function _clone(src) {
        var seen = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : /* @__PURE__ */new Map();
        if (isSchema(src) || !src || _typeof(src) !== "object") return src;
        if (seen.has(src)) return seen.get(src);
        var copy;
        if (src instanceof Date) {
          copy = new Date(src.getTime());
          seen.set(src, copy);
        } else if (src instanceof RegExp) {
          copy = new RegExp(src);
          seen.set(src, copy);
        } else if (Array.isArray(src)) {
          copy = new Array(src.length);
          seen.set(src, copy);
          for (var i = 0; i < src.length; i++) copy[i] = _clone(src[i], seen);
        } else if (src instanceof Map) {
          copy = /* @__PURE__ */new Map();
          seen.set(src, copy);
          var _iterator3 = _createForOfIteratorHelper(src.entries()),
            _step3;
          try {
            for (_iterator3.s(); !(_step3 = _iterator3.n()).done;) {
              var _step3$value = _slicedToArray(_step3.value, 2),
                k = _step3$value[0],
                v = _step3$value[1];
              copy.set(k, _clone(v, seen));
            }
          } catch (err) {
            _iterator3.e(err);
          } finally {
            _iterator3.f();
          }
        } else if (src instanceof Set) {
          copy = /* @__PURE__ */new Set();
          seen.set(src, copy);
          var _iterator4 = _createForOfIteratorHelper(src),
            _step4;
          try {
            for (_iterator4.s(); !(_step4 = _iterator4.n()).done;) {
              var _v = _step4.value;
              copy.add(_clone(_v, seen));
            }
          } catch (err) {
            _iterator4.e(err);
          } finally {
            _iterator4.f();
          }
        } else if (src instanceof Object) {
          copy = {};
          seen.set(src, copy);
          for (var _i2 = 0, _Object$entries = Object.entries(src); _i2 < _Object$entries.length; _i2++) {
            var _Object$entries$_i = _slicedToArray(_Object$entries[_i2], 2),
              _k = _Object$entries$_i[0],
              _v2 = _Object$entries$_i[1];
            copy[_k] = _clone(_v2, seen);
          }
        } else {
          throw Error("Unable to clone ".concat(src));
        }
        return copy;
      }
      function createStandardPath(path) {
        if (!(path != null && path.length)) {
          return void 0;
        }
        var segments = [];
        var currentSegment = "";
        var inBrackets = false;
        var inQuotes = false;
        for (var i = 0; i < path.length; i++) {
          var _char = path[i];
          if (_char === "[" && !inQuotes) {
            if (currentSegment) {
              segments.push.apply(segments, _toConsumableArray(currentSegment.split(".").filter(Boolean)));
              currentSegment = "";
            }
            inBrackets = true;
            continue;
          }
          if (_char === "]" && !inQuotes) {
            if (currentSegment) {
              if (/^\d+$/.test(currentSegment)) {
                segments.push(currentSegment);
              } else {
                segments.push(currentSegment.replace(/^"|"$/g, ""));
              }
              currentSegment = "";
            }
            inBrackets = false;
            continue;
          }
          if (_char === '"') {
            inQuotes = !inQuotes;
            continue;
          }
          if (_char === "." && !inBrackets && !inQuotes) {
            if (currentSegment) {
              segments.push(currentSegment);
              currentSegment = "";
            }
            continue;
          }
          currentSegment += _char;
        }
        if (currentSegment) {
          segments.push.apply(segments, _toConsumableArray(currentSegment.split(".").filter(Boolean)));
        }
        return segments;
      }
      function createStandardIssues(error, parentPath) {
        var path = parentPath ? "".concat(parentPath, ".").concat(error.path) : error.path;
        return error.errors.map(function (err) {
          return {
            message: err,
            path: createStandardPath(path)
          };
        });
      }
      function issuesFromValidationError(error, parentPath) {
        var _error$inner;
        if (!((_error$inner = error.inner) != null && _error$inner.length) && error.errors.length) {
          return createStandardIssues(error, parentPath);
        }
        var path = parentPath ? "".concat(parentPath, ".").concat(error.path) : error.path;
        return error.inner.flatMap(function (err) {
          return issuesFromValidationError(err, path);
        });
      }
      var Schema = /*#__PURE__*/function () {
        function Schema(options) {
          var _this3 = this;
          _classCallCheck(this, Schema);
          this.type = void 0;
          this.deps = [];
          this.tests = void 0;
          this.transforms = void 0;
          this.conditions = [];
          this._mutate = void 0;
          this.internalTests = {};
          this._whitelist = new ReferenceSet();
          this._blacklist = new ReferenceSet();
          this.exclusiveTests = /* @__PURE__ */Object.create(null);
          this._typeCheck = void 0;
          this.spec = void 0;
          this.tests = [];
          this.transforms = [];
          this.withMutation(function () {
            _this3.typeError(mixed.notType);
          });
          this.type = options.type;
          this._typeCheck = options.check;
          this.spec = Object.assign({
            strip: false,
            strict: false,
            abortEarly: true,
            recursive: true,
            disableStackTrace: false,
            nullable: false,
            optional: true,
            coerce: true
          }, options == null ? void 0 : options.spec);
          this.withMutation(function (s) {
            s.nonNullable();
          });
        }
        // TODO: remove
        return _createClass(Schema, [{
          key: "_type",
          get: function get() {
            return this.type;
          }
        }, {
          key: "clone",
          value: function clone(spec) {
            if (this._mutate) {
              if (spec) Object.assign(this.spec, spec);
              return this;
            }
            var next = Object.create(Object.getPrototypeOf(this));
            next.type = this.type;
            next._typeCheck = this._typeCheck;
            next._whitelist = this._whitelist.clone();
            next._blacklist = this._blacklist.clone();
            next.internalTests = Object.assign({}, this.internalTests);
            next.exclusiveTests = Object.assign({}, this.exclusiveTests);
            next.deps = _toConsumableArray(this.deps);
            next.conditions = _toConsumableArray(this.conditions);
            next.tests = _toConsumableArray(this.tests);
            next.transforms = _toConsumableArray(this.transforms);
            next.spec = _clone(Object.assign({}, this.spec, spec));
            return next;
          }
        }, {
          key: "label",
          value: function label(_label) {
            var next = this.clone();
            next.spec.label = _label;
            return next;
          }
        }, {
          key: "meta",
          value: function meta() {
            if (arguments.length === 0) return this.spec.meta;
            var next = this.clone();
            next.spec.meta = Object.assign(next.spec.meta || {}, arguments.length <= 0 ? undefined : arguments[0]);
            return next;
          }
        }, {
          key: "withMutation",
          value: function withMutation(fn) {
            var before = this._mutate;
            this._mutate = true;
            var result = fn(this);
            this._mutate = before;
            return result;
          }
        }, {
          key: "concat",
          value: function concat(schema) {
            if (!schema || schema === this) return this;
            if (schema.type !== this.type && this.type !== "mixed") throw new TypeError("You cannot `concat()` schema's of different types: ".concat(this.type, " and ").concat(schema.type));
            var base = this;
            var combined = schema.clone();
            var mergedSpec = Object.assign({}, base.spec, combined.spec);
            combined.spec = mergedSpec;
            combined.internalTests = Object.assign({}, base.internalTests, combined.internalTests);
            combined._whitelist = base._whitelist.merge(schema._whitelist, schema._blacklist);
            combined._blacklist = base._blacklist.merge(schema._blacklist, schema._whitelist);
            combined.tests = base.tests;
            combined.exclusiveTests = base.exclusiveTests;
            combined.withMutation(function (next) {
              schema.tests.forEach(function (fn) {
                next.test(fn.OPTIONS);
              });
            });
            combined.transforms = [].concat(_toConsumableArray(base.transforms), _toConsumableArray(combined.transforms));
            return combined;
          }
        }, {
          key: "isType",
          value: function isType(v) {
            if (v == null) {
              if (this.spec.nullable && v === null) return true;
              if (this.spec.optional && v === void 0) return true;
              return false;
            }
            return this._typeCheck(v);
          }
        }, {
          key: "resolve",
          value: function resolve(options) {
            var schema = this;
            if (schema.conditions.length) {
              var conditions = schema.conditions;
              schema = schema.clone();
              schema.conditions = [];
              schema = conditions.reduce(function (prevSchema, condition) {
                return condition.resolve(prevSchema, options);
              }, schema);
              schema = schema.resolve(options);
            }
            return schema;
          }
        }, {
          key: "resolveOptions",
          value: function resolveOptions(options) {
            var _options$strict, _options$abortEarly, _options$recursive, _options$disableStack;
            return Object.assign({}, options, {
              from: options.from || [],
              strict: (_options$strict = options.strict) != null ? _options$strict : this.spec.strict,
              abortEarly: (_options$abortEarly = options.abortEarly) != null ? _options$abortEarly : this.spec.abortEarly,
              recursive: (_options$recursive = options.recursive) != null ? _options$recursive : this.spec.recursive,
              disableStackTrace: (_options$disableStack = options.disableStackTrace) != null ? _options$disableStack : this.spec.disableStackTrace
            });
          }
          /**
           * Run the configured transform pipeline over an input value.
           */
        }, {
          key: "cast",
          value: function cast(value) {
            var options = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
            var resolvedSchema = this.resolve(Object.assign({}, options, {
              value: value
              // parent: options.parent,
              // context: options.context,
            }));
            var allowOptionality = options.assert === "ignore-optionality";
            var result = resolvedSchema._cast(value, options);
            if (options.assert !== false && !resolvedSchema.isType(result)) {
              if (allowOptionality && isAbsent(result)) {
                return result;
              }
              var formattedValue = printValue(value);
              var formattedResult = printValue(result);
              throw new TypeError("The value of ".concat(options.path || "field", " could not be cast to a value that satisfies the schema type: \"").concat(resolvedSchema.type, "\". \n\nattempted value: ").concat(formattedValue, " \n") + (formattedResult !== formattedValue ? "result of cast: ".concat(formattedResult) : ""));
            }
            return result;
          }
        }, {
          key: "_cast",
          value: function _cast(rawValue, options) {
            var _this4 = this;
            var value = rawValue === void 0 ? rawValue : this.transforms.reduce(function (prevValue, fn) {
              return fn.call(_this4, prevValue, rawValue, _this4, options);
            }, rawValue);
            if (value === void 0) {
              value = this.getDefault(options);
            }
            return value;
          }
        }, {
          key: "_validate",
          value: function _validate(_value) {
            var _this5 = this;
            var options = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
            var panic = arguments.length > 2 ? arguments[2] : undefined;
            var next = arguments.length > 3 ? arguments[3] : undefined;
            var path = options.path,
              _options$originalValu3 = options.originalValue,
              originalValue = _options$originalValu3 === void 0 ? _value : _options$originalValu3,
              _options$strict2 = options.strict,
              strict = _options$strict2 === void 0 ? this.spec.strict : _options$strict2;
            var value = _value;
            if (!strict) {
              value = this._cast(value, Object.assign({
                assert: false
              }, options));
            }
            var initialTests = [];
            for (var _i3 = 0, _Object$values = Object.values(this.internalTests); _i3 < _Object$values.length; _i3++) {
              var test = _Object$values[_i3];
              if (test) initialTests.push(test);
            }
            this.runTests({
              path: path,
              value: value,
              originalValue: originalValue,
              options: options,
              tests: initialTests
            }, panic, function (initialErrors) {
              if (initialErrors.length) {
                return next(initialErrors, value);
              }
              _this5.runTests({
                path: path,
                value: value,
                originalValue: originalValue,
                options: options,
                tests: _this5.tests
              }, panic, next);
            });
          }
          /**
           * Executes a set of validations, either schema, produced Tests or a nested
           * schema validate result.
           */
        }, {
          key: "runTests",
          value: function runTests(runOptions, panic, next) {
            var fired = false;
            var tests = runOptions.tests,
              value = runOptions.value,
              originalValue = runOptions.originalValue,
              path = runOptions.path,
              options = runOptions.options;
            var panicOnce = function panicOnce(arg) {
              if (fired) return;
              fired = true;
              panic(arg, value);
            };
            var nextOnce = function nextOnce(arg) {
              if (fired) return;
              fired = true;
              next(arg, value);
            };
            var count = tests.length;
            var nestedErrors = [];
            if (!count) return nextOnce([]);
            var args = {
              value: value,
              originalValue: originalValue,
              path: path,
              options: options,
              schema: this
            };
            for (var i = 0; i < tests.length; i++) {
              var test = tests[i];
              test(args, panicOnce, function finishTestRun(err) {
                if (err) {
                  Array.isArray(err) ? nestedErrors.push.apply(nestedErrors, _toConsumableArray(err)) : nestedErrors.push(err);
                }
                if (--count <= 0) {
                  nextOnce(nestedErrors);
                }
              });
            }
          }
        }, {
          key: "asNestedTest",
          value: function asNestedTest(_ref3) {
            var _this6 = this;
            var key = _ref3.key,
              index = _ref3.index,
              parent = _ref3.parent,
              parentPath = _ref3.parentPath,
              originalParent = _ref3.originalParent,
              options = _ref3.options;
            var k = key != null ? key : index;
            if (k == null) {
              throw TypeError("Must include `key` or `index` for nested validations");
            }
            var isIndex = typeof k === "number";
            var value = parent[k];
            var testOptions = Object.assign({}, options, _defineProperty(_defineProperty({
              // Nested validations fields are always strict:
              //    1. parent isn't strict so the casting will also have cast inner values
              //    2. parent is strict in which case the nested values weren't cast either
              strict: true,
              parent: parent,
              value: value,
              originalValue: originalParent[k],
              // FIXME: tests depend on `index` being passed around deeply,
              //   we should not let the options.key/index bleed through
              key: void 0
            }, isIndex ? "index" : "key", k), "path", isIndex || k.includes(".") ? "".concat(parentPath || "", "[").concat(isIndex ? k : "\"".concat(k, "\""), "]") : (parentPath ? "".concat(parentPath, ".") : "") + key));
            return function (_, panic, next) {
              return _this6.resolve(testOptions)._validate(value, testOptions, panic, next);
            };
          }
        }, {
          key: "validate",
          value: function validate(value, options) {
            var _options$disableStack2;
            var schema = this.resolve(Object.assign({}, options, {
              value: value
            }));
            var disableStackTrace = (_options$disableStack2 = options == null ? void 0 : options.disableStackTrace) != null ? _options$disableStack2 : schema.spec.disableStackTrace;
            return new Promise(function (resolve, reject) {
              return schema._validate(value, options, function (error, parsed) {
                if (ValidationError.isError(error)) error.value = parsed;
                reject(error);
              }, function (errors, validated) {
                if (errors.length) reject(new ValidationError(errors, validated, void 0, void 0, disableStackTrace));else resolve(validated);
              });
            });
          }
        }, {
          key: "validateSync",
          value: function validateSync(value, options) {
            var _options$disableStack3;
            var schema = this.resolve(Object.assign({}, options, {
              value: value
            }));
            var result;
            var disableStackTrace = (_options$disableStack3 = options == null ? void 0 : options.disableStackTrace) != null ? _options$disableStack3 : schema.spec.disableStackTrace;
            schema._validate(value, Object.assign({}, options, {
              sync: true
            }), function (error, parsed) {
              if (ValidationError.isError(error)) error.value = parsed;
              throw error;
            }, function (errors, validated) {
              if (errors.length) throw new ValidationError(errors, value, void 0, void 0, disableStackTrace);
              result = validated;
            });
            return result;
          }
        }, {
          key: "isValid",
          value: function isValid(value, options) {
            return this.validate(value, options).then(function () {
              return true;
            }, function (err) {
              if (ValidationError.isError(err)) return false;
              throw err;
            });
          }
        }, {
          key: "isValidSync",
          value: function isValidSync(value, options) {
            try {
              this.validateSync(value, options);
              return true;
            } catch (err) {
              if (ValidationError.isError(err)) return false;
              throw err;
            }
          }
        }, {
          key: "_getDefault",
          value: function _getDefault(options) {
            var defaultValue = this.spec["default"];
            if (defaultValue == null) {
              return defaultValue;
            }
            return typeof defaultValue === "function" ? defaultValue.call(this, options) : _clone(defaultValue);
          }
        }, {
          key: "getDefault",
          value: function getDefault(options) {
            var schema = this.resolve(options || {});
            return schema._getDefault(options);
          }
        }, {
          key: "default",
          value: function _default(def) {
            if (arguments.length === 0) {
              return this._getDefault();
            }
            var next = this.clone({
              "default": def
            });
            return next;
          }
        }, {
          key: "strict",
          value: function strict() {
            var isStrict = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : true;
            return this.clone({
              strict: isStrict
            });
          }
        }, {
          key: "nullability",
          value: function nullability(nullable, message) {
            var next = this.clone({
              nullable: nullable
            });
            next.internalTests.nullable = createValidation({
              message: message,
              name: "nullable",
              test: function test(value) {
                return value === null ? this.schema.spec.nullable : true;
              }
            });
            return next;
          }
        }, {
          key: "optionality",
          value: function optionality(optional, message) {
            var next = this.clone({
              optional: optional
            });
            next.internalTests.optionality = createValidation({
              message: message,
              name: "optionality",
              test: function test(value) {
                return value === void 0 ? this.schema.spec.optional : true;
              }
            });
            return next;
          }
        }, {
          key: "optional",
          value: function optional() {
            return this.optionality(true);
          }
        }, {
          key: "defined",
          value: function defined() {
            var message = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : mixed.defined;
            return this.optionality(false, message);
          }
        }, {
          key: "nullable",
          value: function nullable() {
            return this.nullability(true);
          }
        }, {
          key: "nonNullable",
          value: function nonNullable() {
            var message = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : mixed.notNull;
            return this.nullability(false, message);
          }
        }, {
          key: "required",
          value: function required() {
            var message = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : mixed.required;
            return this.clone().withMutation(function (next) {
              return next.nonNullable(message).defined(message);
            });
          }
        }, {
          key: "notRequired",
          value: function notRequired() {
            return this.clone().withMutation(function (next) {
              return next.nullable().optional();
            });
          }
        }, {
          key: "transform",
          value: function transform(fn) {
            var next = this.clone();
            next.transforms.push(fn);
            return next;
          }
          /**
           * Adds a test function to the schema's queue of tests.
           * tests can be exclusive or non-exclusive.
           *
           * - exclusive tests, will replace any existing tests of the same name.
           * - non-exclusive: can be stacked
           *
           * If a non-exclusive test is added to a schema with an exclusive test of the same name
           * the exclusive test is removed and further tests of the same name will be stacked.
           *
           * If an exclusive test is added to a schema with non-exclusive tests of the same name
           * the previous tests are removed and further tests of the same name will replace each other.
           */
        }, {
          key: "test",
          value: function test() {
            var opts;
            if (arguments.length === 1) {
              if (typeof (arguments.length <= 0 ? undefined : arguments[0]) === "function") {
                opts = {
                  test: arguments.length <= 0 ? undefined : arguments[0]
                };
              } else {
                opts = arguments.length <= 0 ? undefined : arguments[0];
              }
            } else if (arguments.length === 2) {
              opts = {
                name: arguments.length <= 0 ? undefined : arguments[0],
                test: arguments.length <= 1 ? undefined : arguments[1]
              };
            } else {
              opts = {
                name: arguments.length <= 0 ? undefined : arguments[0],
                message: arguments.length <= 1 ? undefined : arguments[1],
                test: arguments.length <= 2 ? undefined : arguments[2]
              };
            }
            if (opts.message === void 0) opts.message = mixed["default"];
            if (typeof opts.test !== "function") throw new TypeError("`test` is a required parameters");
            var next = this.clone();
            var validate = createValidation(opts);
            var isExclusive = opts.exclusive || opts.name && next.exclusiveTests[opts.name] === true;
            if (opts.exclusive) {
              if (!opts.name) throw new TypeError("Exclusive tests must provide a unique `name` identifying the test");
            }
            if (opts.name) next.exclusiveTests[opts.name] = !!opts.exclusive;
            next.tests = next.tests.filter(function (fn) {
              if (fn.OPTIONS.name === opts.name) {
                if (isExclusive) return false;
                if (fn.OPTIONS.test === validate.OPTIONS.test) return false;
              }
              return true;
            });
            next.tests.push(validate);
            return next;
          }
        }, {
          key: "when",
          value: function when(keys, options) {
            if (!Array.isArray(keys) && typeof keys !== "string") {
              options = keys;
              keys = ".";
            }
            var next = this.clone();
            var deps = toArray(keys).map(function (key) {
              return new Reference(key);
            });
            deps.forEach(function (dep) {
              if (dep.isSibling) next.deps.push(dep.key);
            });
            next.conditions.push(typeof options === "function" ? new Condition(deps, options) : Condition.fromOptions(deps, options));
            return next;
          }
        }, {
          key: "typeError",
          value: function typeError(message) {
            var next = this.clone();
            next.internalTests.typeError = createValidation({
              message: message,
              name: "typeError",
              skipAbsent: true,
              test: function test(value) {
                if (!this.schema._typeCheck(value)) return this.createError({
                  params: {
                    type: this.schema.type
                  }
                });
                return true;
              }
            });
            return next;
          }
        }, {
          key: "oneOf",
          value: function oneOf(enums) {
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : mixed.oneOf;
            var next = this.clone();
            enums.forEach(function (val) {
              next._whitelist.add(val);
              next._blacklist["delete"](val);
            });
            next.internalTests.whiteList = createValidation({
              message: message,
              name: "oneOf",
              skipAbsent: true,
              test: function test(value) {
                var valids = this.schema._whitelist;
                var resolved = valids.resolveAll(this.resolve);
                return resolved.includes(value) ? true : this.createError({
                  params: {
                    values: Array.from(valids).join(", "),
                    resolved: resolved
                  }
                });
              }
            });
            return next;
          }
        }, {
          key: "notOneOf",
          value: function notOneOf(enums) {
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : mixed.notOneOf;
            var next = this.clone();
            enums.forEach(function (val) {
              next._blacklist.add(val);
              next._whitelist["delete"](val);
            });
            next.internalTests.blacklist = createValidation({
              message: message,
              name: "notOneOf",
              test: function test(value) {
                var invalids = this.schema._blacklist;
                var resolved = invalids.resolveAll(this.resolve);
                if (resolved.includes(value)) return this.createError({
                  params: {
                    values: Array.from(invalids).join(", "),
                    resolved: resolved
                  }
                });
                return true;
              }
            });
            return next;
          }
        }, {
          key: "strip",
          value: function strip() {
            var _strip = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : true;
            var next = this.clone();
            next.spec.strip = _strip;
            return next;
          }
          /**
           * Return a serialized description of the schema including validations, flags, types etc.
           *
           * @param options Provide any needed context for resolving runtime schema alterations (lazy, when conditions, etc).
           */
        }, {
          key: "describe",
          value: function describe(options) {
            var next = (options ? this.resolve(options) : this).clone();
            var _next$spec = next.spec,
              label = _next$spec.label,
              meta = _next$spec.meta,
              optional = _next$spec.optional,
              nullable = _next$spec.nullable;
            var description = {
              meta: meta,
              label: label,
              optional: optional,
              nullable: nullable,
              "default": next.getDefault(options),
              type: next.type,
              oneOf: next._whitelist.describe(),
              notOneOf: next._blacklist.describe(),
              tests: next.tests.filter(function (n, idx, list) {
                return list.findIndex(function (c) {
                  return c.OPTIONS.name === n.OPTIONS.name;
                }) === idx;
              }).map(function (fn) {
                var params = fn.OPTIONS.params && options ? resolveParams(Object.assign({}, fn.OPTIONS.params), options) : fn.OPTIONS.params;
                return {
                  name: fn.OPTIONS.name,
                  params: params
                };
              })
            };
            return description;
          }
        }, {
          key: "~standard",
          get: function get() {
            var schema = this;
            var standard = {
              version: 1,
              vendor: "yup",
              validate: function validate(value) {
                return _asyncToGenerator(/*#__PURE__*/_regenerator().m(function _callee() {
                  var result, _t;
                  return _regenerator().w(function (_context) {
                    while (1) switch (_context.p = _context.n) {
                      case 0:
                        _context.p = 0;
                        _context.n = 1;
                        return schema.validate(value, {
                          abortEarly: false
                        });
                      case 1:
                        result = _context.v;
                        return _context.a(2, {
                          value: result
                        });
                      case 2:
                        _context.p = 2;
                        _t = _context.v;
                        if (!(_t instanceof ValidationError)) {
                          _context.n = 3;
                          break;
                        }
                        return _context.a(2, {
                          issues: issuesFromValidationError(_t)
                        });
                      case 3:
                        throw _t;
                      case 4:
                        return _context.a(2);
                    }
                  }, _callee, null, [[0, 2]]);
                }))();
              }
            };
            return standard;
          }
        }]);
      }();
      Schema.prototype.__isYupSchema__ = true;
      var _loop = function _loop() {
        var method = _arr[_i4];
        Schema.prototype["".concat(method, "At")] = function (path, value) {
          var options = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
          var _getIn = getIn(this, path, value, options.context),
            parent = _getIn.parent,
            parentPath = _getIn.parentPath,
            schema = _getIn.schema;
          return schema[method](parent && parent[parentPath], Object.assign({}, options, {
            parent: parent,
            path: path
          }));
        };
      };
      for (var _i4 = 0, _arr = ["validate", "validateSync"]; _i4 < _arr.length; _i4++) {
        _loop();
      }
      for (var _i5 = 0, _arr2 = ["equals", "is"]; _i5 < _arr2.length; _i5++) {
        var alias = _arr2[_i5];
        Schema.prototype[alias] = Schema.prototype.oneOf;
      }
      for (var _i6 = 0, _arr3 = ["not", "nope"]; _i6 < _arr3.length; _i6++) {
        var _alias = _arr3[_i6];
        Schema.prototype[_alias] = Schema.prototype.notOneOf;
      }
      var returnsTrue = function returnsTrue() {
        return true;
      };
      function create$8(spec) {
        return new MixedSchema(spec);
      }
      var MixedSchema = /*#__PURE__*/function (_Schema) {
        function MixedSchema(spec) {
          _classCallCheck(this, MixedSchema);
          return _callSuper(this, MixedSchema, [typeof spec === "function" ? {
            type: "mixed",
            check: spec
          } : Object.assign({
            type: "mixed",
            check: returnsTrue
          }, spec)]);
        }
        _inherits(MixedSchema, _Schema);
        return _createClass(MixedSchema);
      }(Schema);
      create$8.prototype = MixedSchema.prototype;
      function create$7() {
        return new BooleanSchema();
      }
      var BooleanSchema = /*#__PURE__*/function (_Schema2) {
        function BooleanSchema() {
          var _this7;
          _classCallCheck(this, BooleanSchema);
          _this7 = _callSuper(this, BooleanSchema, [{
            type: "boolean",
            check: function check(v) {
              if (v instanceof Boolean) v = v.valueOf();
              return typeof v === "boolean";
            }
          }]);
          _this7.withMutation(function () {
            _this7.transform(function (value, _raw) {
              if (_this7.spec.coerce && !_this7.isType(value)) {
                if (/^(true|1)$/i.test(String(value))) return true;
                if (/^(false|0)$/i.test(String(value))) return false;
              }
              return value;
            });
          });
          return _this7;
        }
        _inherits(BooleanSchema, _Schema2);
        return _createClass(BooleanSchema, [{
          key: "isTrue",
          value: function isTrue() {
            var message = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : _boolean.isValue;
            return this.test({
              message: message,
              name: "is-value",
              exclusive: true,
              params: {
                value: "true"
              },
              test: function test(value) {
                return isAbsent(value) || value === true;
              }
            });
          }
        }, {
          key: "isFalse",
          value: function isFalse() {
            var message = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : _boolean.isValue;
            return this.test({
              message: message,
              name: "is-value",
              exclusive: true,
              params: {
                value: "false"
              },
              test: function test(value) {
                return isAbsent(value) || value === false;
              }
            });
          }
        }, {
          key: "default",
          value: function _default(def) {
            return _superPropGet(BooleanSchema, "default", this, 3)([def]);
          }
        }, {
          key: "defined",
          value: function defined(msg) {
            return _superPropGet(BooleanSchema, "defined", this, 3)([msg]);
          }
        }, {
          key: "optional",
          value: function optional() {
            return _superPropGet(BooleanSchema, "optional", this, 3)([]);
          }
        }, {
          key: "required",
          value: function required(msg) {
            return _superPropGet(BooleanSchema, "required", this, 3)([msg]);
          }
        }, {
          key: "notRequired",
          value: function notRequired() {
            return _superPropGet(BooleanSchema, "notRequired", this, 3)([]);
          }
        }, {
          key: "nullable",
          value: function nullable() {
            return _superPropGet(BooleanSchema, "nullable", this, 3)([]);
          }
        }, {
          key: "nonNullable",
          value: function nonNullable(msg) {
            return _superPropGet(BooleanSchema, "nonNullable", this, 3)([msg]);
          }
        }, {
          key: "strip",
          value: function strip(v) {
            return _superPropGet(BooleanSchema, "strip", this, 3)([v]);
          }
        }]);
      }(Schema);
      create$7.prototype = BooleanSchema.prototype;
      var isoReg = /^(\d{4}|[+-]\d{6})(?:-?(\d{2})(?:-?(\d{2}))?)?(?:[ T]?(\d{2}):?(\d{2})(?::?(\d{2})(?:[,.](\d{1,}))?)?(?:(Z)|([+-])(\d{2})(?::?(\d{2}))?)?)?$/;
      function parseIsoDate(date2) {
        var struct = parseDateStruct(date2);
        if (!struct) return Date.parse ? Date.parse(date2) : Number.NaN;
        if (struct.z === void 0 && struct.plusMinus === void 0) {
          return new Date(struct.year, struct.month, struct.day, struct.hour, struct.minute, struct.second, struct.millisecond).valueOf();
        }
        var totalMinutesOffset = 0;
        if (struct.z !== "Z" && struct.plusMinus !== void 0) {
          totalMinutesOffset = struct.hourOffset * 60 + struct.minuteOffset;
          if (struct.plusMinus === "+") totalMinutesOffset = 0 - totalMinutesOffset;
        }
        return Date.UTC(struct.year, struct.month, struct.day, struct.hour, struct.minute + totalMinutesOffset, struct.second, struct.millisecond);
      }
      function parseDateStruct(date2) {
        var _regexResult$7$length, _regexResult$;
        var regexResult = isoReg.exec(date2);
        if (!regexResult) return null;
        return {
          year: toNumber(regexResult[1]),
          month: toNumber(regexResult[2], 1) - 1,
          day: toNumber(regexResult[3], 1),
          hour: toNumber(regexResult[4]),
          minute: toNumber(regexResult[5]),
          second: toNumber(regexResult[6]),
          millisecond: regexResult[7] ?
          // allow arbitrary sub-second precision beyond milliseconds
          toNumber(regexResult[7].substring(0, 3)) : 0,
          precision: (_regexResult$7$length = (_regexResult$ = regexResult[7]) == null ? void 0 : _regexResult$.length) != null ? _regexResult$7$length : void 0,
          z: regexResult[8] || void 0,
          plusMinus: regexResult[9] || void 0,
          hourOffset: toNumber(regexResult[10]),
          minuteOffset: toNumber(regexResult[11])
        };
      }
      function toNumber(str) {
        var defaultValue = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : 0;
        return Number(str) || defaultValue;
      }
      var rEmail =
      // eslint-disable-next-line
      /^[a-zA-Z0-9.!#$%&'*+\/=?^_`{|}~-]+@[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(?:\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$/;
      var rUrl =
      // eslint-disable-next-line
      /^((https?|ftp):)?\/\/(((([a-z]|\d|-|\.|_|~|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])|(%[\da-f]{2})|[!\$&'\(\)\*\+,;=]|:)*@)?(((\d|[1-9]\d|1\d\d|2[0-4]\d|25[0-5])\.(\d|[1-9]\d|1\d\d|2[0-4]\d|25[0-5])\.(\d|[1-9]\d|1\d\d|2[0-4]\d|25[0-5])\.(\d|[1-9]\d|1\d\d|2[0-4]\d|25[0-5]))|((([a-z]|\d|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])|(([a-z]|\d|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])([a-z]|\d|-|\.|_|~|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])*([a-z]|\d|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])))\.)+(([a-z]|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])|(([a-z]|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])([a-z]|\d|-|\.|_|~|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])*([a-z]|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])))\.?)(:\d*)?)(\/((([a-z]|\d|-|\.|_|~|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])|(%[\da-f]{2})|[!\$&'\(\)\*\+,;=]|:|@)+(\/(([a-z]|\d|-|\.|_|~|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])|(%[\da-f]{2})|[!\$&'\(\)\*\+,;=]|:|@)*)*)?)?(\?((([a-z]|\d|-|\.|_|~|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])|(%[\da-f]{2})|[!\$&'\(\)\*\+,;=]|:|@)|[\uE000-\uF8FF]|\/|\?)*)?(\#((([a-z]|\d|-|\.|_|~|[\u00A0-\uD7FF\uF900-\uFDCF\uFDF0-\uFFEF])|(%[\da-f]{2})|[!\$&'\(\)\*\+,;=]|:|@)|\/|\?)*)?$/i;
      var rUUID = /^(?:[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}|00000000-0000-0000-0000-000000000000)$/i;
      var yearMonthDay = "^\\d{4}-\\d{2}-\\d{2}";
      var hourMinuteSecond = "\\d{2}:\\d{2}:\\d{2}";
      var zOrOffset = "(([+-]\\d{2}(:?\\d{2})?)|Z)";
      var rIsoDateTime = new RegExp("".concat(yearMonthDay, "T").concat(hourMinuteSecond, "(\\.\\d+)?").concat(zOrOffset, "$"));
      var isTrimmed = function isTrimmed(value) {
        return isAbsent(value) || value === value.trim();
      };
      var objStringTag = {}.toString();
      function create$6() {
        return new StringSchema();
      }
      var StringSchema = /*#__PURE__*/function (_Schema3) {
        function StringSchema() {
          var _this8;
          _classCallCheck(this, StringSchema);
          _this8 = _callSuper(this, StringSchema, [{
            type: "string",
            check: function check(value) {
              if (value instanceof String) value = value.valueOf();
              return typeof value === "string";
            }
          }]);
          _this8.withMutation(function () {
            _this8.transform(function (value, _raw) {
              if (!_this8.spec.coerce || _this8.isType(value)) return value;
              if (Array.isArray(value)) return value;
              var strValue = value != null && value.toString ? value.toString() : value;
              if (strValue === objStringTag) return value;
              return strValue;
            });
          });
          return _this8;
        }
        _inherits(StringSchema, _Schema3);
        return _createClass(StringSchema, [{
          key: "required",
          value: function required(message) {
            return _superPropGet(StringSchema, "required", this, 3)([message]).withMutation(function (schema) {
              return schema.test({
                message: message || mixed.required,
                name: "required",
                skipAbsent: true,
                test: function test(value) {
                  return !!value.length;
                }
              });
            });
          }
        }, {
          key: "notRequired",
          value: function notRequired() {
            return _superPropGet(StringSchema, "notRequired", this, 3)([]).withMutation(function (schema) {
              schema.tests = schema.tests.filter(function (t) {
                return t.OPTIONS.name !== "required";
              });
              return schema;
            });
          }
        }, {
          key: "length",
          value: function length(_length) {
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : string.length;
            return this.test({
              message: message,
              name: "length",
              exclusive: true,
              params: {
                length: _length
              },
              skipAbsent: true,
              test: function test(value) {
                return value.length === this.resolve(_length);
              }
            });
          }
        }, {
          key: "min",
          value: function min(_min) {
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : string.min;
            return this.test({
              message: message,
              name: "min",
              exclusive: true,
              params: {
                min: _min
              },
              skipAbsent: true,
              test: function test(value) {
                return value.length >= this.resolve(_min);
              }
            });
          }
        }, {
          key: "max",
          value: function max(_max) {
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : string.max;
            return this.test({
              name: "max",
              exclusive: true,
              message: message,
              params: {
                max: _max
              },
              skipAbsent: true,
              test: function test(value) {
                return value.length <= this.resolve(_max);
              }
            });
          }
        }, {
          key: "matches",
          value: function matches(regex, options) {
            var excludeEmptyString = false;
            var message;
            var name;
            if (options) {
              if (_typeof(options) === "object") {
                var _options$excludeEmpty = options.excludeEmptyString;
                excludeEmptyString = _options$excludeEmpty === void 0 ? false : _options$excludeEmpty;
                message = options.message;
                name = options.name;
              } else {
                message = options;
              }
            }
            return this.test({
              name: name || "matches",
              message: message || string.matches,
              params: {
                regex: regex
              },
              skipAbsent: true,
              test: function test(value) {
                return value === "" && excludeEmptyString || value.search(regex) !== -1;
              }
            });
          }
        }, {
          key: "email",
          value: function email() {
            var message = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : string.email;
            return this.matches(rEmail, {
              name: "email",
              message: message,
              excludeEmptyString: true
            });
          }
        }, {
          key: "url",
          value: function url() {
            var message = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : string.url;
            return this.matches(rUrl, {
              name: "url",
              message: message,
              excludeEmptyString: true
            });
          }
        }, {
          key: "uuid",
          value: function uuid() {
            var message = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : string.uuid;
            return this.matches(rUUID, {
              name: "uuid",
              message: message,
              excludeEmptyString: false
            });
          }
        }, {
          key: "datetime",
          value: function datetime(options) {
            var message = "";
            var allowOffset;
            var precision;
            if (options) {
              if (_typeof(options) === "object") {
                var _options$message = options.message;
                message = _options$message === void 0 ? "" : _options$message;
                var _options$allowOffset = options.allowOffset;
                allowOffset = _options$allowOffset === void 0 ? false : _options$allowOffset;
                var _options$precision = options.precision;
                precision = _options$precision === void 0 ? void 0 : _options$precision;
              } else {
                message = options;
              }
            }
            return this.matches(rIsoDateTime, {
              name: "datetime",
              message: message || string.datetime,
              excludeEmptyString: true
            }).test({
              name: "datetime_offset",
              message: message || string.datetime_offset,
              params: {
                allowOffset: allowOffset
              },
              skipAbsent: true,
              test: function test(value) {
                if (!value || allowOffset) return true;
                var struct = parseDateStruct(value);
                if (!struct) return false;
                return !!struct.z;
              }
            }).test({
              name: "datetime_precision",
              message: message || string.datetime_precision,
              params: {
                precision: precision
              },
              skipAbsent: true,
              test: function test(value) {
                if (!value || precision == void 0) return true;
                var struct = parseDateStruct(value);
                if (!struct) return false;
                return struct.precision === precision;
              }
            });
          }
          //-- transforms --
        }, {
          key: "ensure",
          value: function ensure() {
            return this["default"]("").transform(function (val) {
              return val === null ? "" : val;
            });
          }
        }, {
          key: "trim",
          value: function trim() {
            var message = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : string.trim;
            return this.transform(function (val) {
              return val != null ? val.trim() : val;
            }).test({
              message: message,
              name: "trim",
              test: isTrimmed
            });
          }
        }, {
          key: "lowercase",
          value: function lowercase() {
            var message = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : string.lowercase;
            return this.transform(function (value) {
              return !isAbsent(value) ? value.toLowerCase() : value;
            }).test({
              message: message,
              name: "string_case",
              exclusive: true,
              skipAbsent: true,
              test: function test(value) {
                return isAbsent(value) || value === value.toLowerCase();
              }
            });
          }
        }, {
          key: "uppercase",
          value: function uppercase() {
            var message = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : string.uppercase;
            return this.transform(function (value) {
              return !isAbsent(value) ? value.toUpperCase() : value;
            }).test({
              message: message,
              name: "string_case",
              exclusive: true,
              skipAbsent: true,
              test: function test(value) {
                return isAbsent(value) || value === value.toUpperCase();
              }
            });
          }
        }]);
      }(Schema);
      create$6.prototype = StringSchema.prototype;
      var isNaN$1 = function isNaN$1(value) {
        return value != +value;
      };
      function create$5() {
        return new NumberSchema();
      }
      var NumberSchema = /*#__PURE__*/function (_Schema4) {
        function NumberSchema() {
          var _this9;
          _classCallCheck(this, NumberSchema);
          _this9 = _callSuper(this, NumberSchema, [{
            type: "number",
            check: function check(value) {
              if (value instanceof Number) value = value.valueOf();
              return typeof value === "number" && !isNaN$1(value);
            }
          }]);
          _this9.withMutation(function () {
            _this9.transform(function (value, _raw) {
              if (!_this9.spec.coerce) return value;
              var parsed = value;
              if (typeof parsed === "string") {
                parsed = parsed.replace(/\s/g, "");
                if (parsed === "") return NaN;
                parsed = +parsed;
              }
              if (_this9.isType(parsed) || parsed === null) return parsed;
              return parseFloat(parsed);
            });
          });
          return _this9;
        }
        _inherits(NumberSchema, _Schema4);
        return _createClass(NumberSchema, [{
          key: "min",
          value: function min(_min2) {
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : number.min;
            return this.test({
              message: message,
              name: "min",
              exclusive: true,
              params: {
                min: _min2
              },
              skipAbsent: true,
              test: function test(value) {
                return value >= this.resolve(_min2);
              }
            });
          }
        }, {
          key: "max",
          value: function max(_max2) {
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : number.max;
            return this.test({
              message: message,
              name: "max",
              exclusive: true,
              params: {
                max: _max2
              },
              skipAbsent: true,
              test: function test(value) {
                return value <= this.resolve(_max2);
              }
            });
          }
        }, {
          key: "lessThan",
          value: function lessThan(less) {
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : number.lessThan;
            return this.test({
              message: message,
              name: "max",
              exclusive: true,
              params: {
                less: less
              },
              skipAbsent: true,
              test: function test(value) {
                return value < this.resolve(less);
              }
            });
          }
        }, {
          key: "moreThan",
          value: function moreThan(more) {
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : number.moreThan;
            return this.test({
              message: message,
              name: "min",
              exclusive: true,
              params: {
                more: more
              },
              skipAbsent: true,
              test: function test(value) {
                return value > this.resolve(more);
              }
            });
          }
        }, {
          key: "positive",
          value: function positive() {
            var msg = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : number.positive;
            return this.moreThan(0, msg);
          }
        }, {
          key: "negative",
          value: function negative() {
            var msg = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : number.negative;
            return this.lessThan(0, msg);
          }
        }, {
          key: "integer",
          value: function integer() {
            var message = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : number.integer;
            return this.test({
              name: "integer",
              message: message,
              skipAbsent: true,
              test: function test(val) {
                return Number.isInteger(val);
              }
            });
          }
        }, {
          key: "truncate",
          value: function truncate() {
            return this.transform(function (value) {
              return !isAbsent(value) ? value | 0 : value;
            });
          }
        }, {
          key: "round",
          value: function round(method) {
            var _method;
            var avail = ["ceil", "floor", "round", "trunc"];
            method = ((_method = method) == null ? void 0 : _method.toLowerCase()) || "round";
            if (method === "trunc") return this.truncate();
            if (avail.indexOf(method.toLowerCase()) === -1) throw new TypeError("Only valid options for round() are: " + avail.join(", "));
            return this.transform(function (value) {
              return !isAbsent(value) ? Math[method](value) : value;
            });
          }
        }]);
      }(Schema);
      create$5.prototype = NumberSchema.prototype;
      var invalidDate = /* @__PURE__ */new Date("");
      var isDate = function isDate(obj) {
        return Object.prototype.toString.call(obj) === "[object Date]";
      };
      function create$4() {
        return new DateSchema();
      }
      var DateSchema = /*#__PURE__*/function (_Schema5) {
        function _DateSchema() {
          var _this0;
          _classCallCheck(this, _DateSchema);
          _this0 = _callSuper(this, _DateSchema, [{
            type: "date",
            check: function check(v) {
              return isDate(v) && !isNaN(v.getTime());
            }
          }]);
          _this0.withMutation(function () {
            _this0.transform(function (value, _raw) {
              if (!_this0.spec.coerce || _this0.isType(value) || value === null) return value;
              value = parseIsoDate(value);
              return !isNaN(value) ? new Date(value) : _DateSchema.INVALID_DATE;
            });
          });
          return _this0;
        }
        _inherits(_DateSchema, _Schema5);
        return _createClass(_DateSchema, [{
          key: "prepareParam",
          value: function prepareParam(ref, name) {
            var param;
            if (!Reference.isRef(ref)) {
              var cast = this.cast(ref);
              if (!this._typeCheck(cast)) throw new TypeError("`".concat(name, "` must be a Date or a value that can be `cast()` to a Date"));
              param = cast;
            } else {
              param = ref;
            }
            return param;
          }
        }, {
          key: "min",
          value: function min(_min3) {
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : date.min;
            var limit = this.prepareParam(_min3, "min");
            return this.test({
              message: message,
              name: "min",
              exclusive: true,
              params: {
                min: _min3
              },
              skipAbsent: true,
              test: function test(value) {
                return value >= this.resolve(limit);
              }
            });
          }
        }, {
          key: "max",
          value: function max(_max3) {
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : date.max;
            var limit = this.prepareParam(_max3, "max");
            return this.test({
              message: message,
              name: "max",
              exclusive: true,
              params: {
                max: _max3
              },
              skipAbsent: true,
              test: function test(value) {
                return value <= this.resolve(limit);
              }
            });
          }
        }]);
      }(Schema);
      DateSchema.INVALID_DATE = invalidDate;
      create$4.prototype = DateSchema.prototype;
      create$4.INVALID_DATE = invalidDate;
      function sortFields(fields) {
        var excludedEdges = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : [];
        var edges = [];
        var nodes = /* @__PURE__ */new Set();
        var excludes = new Set(excludedEdges.map(function (_ref4) {
          var _ref5 = _slicedToArray(_ref4, 2),
            a = _ref5[0],
            b = _ref5[1];
          return "".concat(a, "-").concat(b);
        }));
        function addNode(depPath, key) {
          var node = propertyExpr.split(depPath)[0];
          nodes.add(node);
          if (!excludes.has("".concat(key, "-").concat(node))) edges.push([key, node]);
        }
        var _loop2 = function _loop2() {
          var key = _Object$keys2[_i7];
          var value = fields[key];
          nodes.add(key);
          if (Reference.isRef(value) && value.isSibling) addNode(value.path, key);else if (isSchema(value) && "deps" in value) value.deps.forEach(function (path) {
            return addNode(path, key);
          });
        };
        for (var _i7 = 0, _Object$keys2 = Object.keys(fields); _i7 < _Object$keys2.length; _i7++) {
          _loop2();
        }
        return toposort__default["default"].array(Array.from(nodes), edges).reverse();
      }
      function findIndex(arr, err) {
        var idx = Infinity;
        arr.some(function (key, ii) {
          var _err$path;
          if ((_err$path = err.path) != null && _err$path.includes(key)) {
            idx = ii;
            return true;
          }
        });
        return idx;
      }
      function sortByKeyOrder(keys) {
        return function (a, b) {
          return findIndex(keys, a) - findIndex(keys, b);
        };
      }
      var parseJson = function parseJson(value, _, schema) {
        if (typeof value !== "string") {
          return value;
        }
        var parsed = value;
        try {
          parsed = JSON.parse(value);
        } catch (err) {}
        return schema.isType(parsed) ? parsed : value;
      };
      function _deepPartial(schema) {
        if ("fields" in schema) {
          var partial = {};
          for (var _i8 = 0, _Object$entries2 = Object.entries(schema.fields); _i8 < _Object$entries2.length; _i8++) {
            var _Object$entries2$_i = _slicedToArray(_Object$entries2[_i8], 2),
              key = _Object$entries2$_i[0],
              fieldSchema = _Object$entries2$_i[1];
            partial[key] = _deepPartial(fieldSchema);
          }
          return schema.setFields(partial);
        }
        if (schema.type === "array") {
          var nextArray = schema.optional();
          if (nextArray.innerType) nextArray.innerType = _deepPartial(nextArray.innerType);
          return nextArray;
        }
        if (schema.type === "tuple") {
          return schema.optional().clone({
            types: schema.spec.types.map(_deepPartial)
          });
        }
        if ("optional" in schema) {
          return schema.optional();
        }
        return schema;
      }
      var deepHas = function deepHas(obj, p) {
        var path = _toConsumableArray(propertyExpr.normalizePath(p));
        if (path.length === 1) return path[0] in obj;
        var last = path.pop();
        var parent = propertyExpr.getter(propertyExpr.join(path), true)(obj);
        return !!(parent && last in parent);
      };
      var isObject = function isObject(obj) {
        return Object.prototype.toString.call(obj) === "[object Object]";
      };
      function unknown(ctx, value) {
        var known = Object.keys(ctx.fields);
        return Object.keys(value).filter(function (key) {
          return known.indexOf(key) === -1;
        });
      }
      var defaultSort = sortByKeyOrder([]);
      function create$3(spec) {
        return new ObjectSchema(spec);
      }
      var ObjectSchema = /*#__PURE__*/function (_Schema6) {
        function ObjectSchema(spec) {
          var _this1;
          _classCallCheck(this, ObjectSchema);
          _this1 = _callSuper(this, ObjectSchema, [{
            type: "object",
            check: function check(value) {
              return isObject(value) || typeof value === "function";
            }
          }]);
          _this1.fields = /* @__PURE__ */Object.create(null);
          _this1._sortErrors = defaultSort;
          _this1._nodes = [];
          _this1._excludedEdges = [];
          _this1.withMutation(function () {
            if (spec) {
              _this1.shape(spec);
            }
          });
          return _this1;
        }
        _inherits(ObjectSchema, _Schema6);
        return _createClass(ObjectSchema, [{
          key: "_cast",
          value: function _cast(_value) {
            var _this10 = this;
            var options = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
            var _options$stripUnknown;
            var value = _superPropGet(ObjectSchema, "_cast", this, 3)([_value, options]);
            if (value === void 0) return this.getDefault(options);
            if (!this._typeCheck(value)) return value;
            var fields = this.fields;
            var strip = (_options$stripUnknown = options.stripUnknown) != null ? _options$stripUnknown : this.spec.noUnknown;
            var props = [].concat(this._nodes, Object.keys(value).filter(function (v) {
              return !_this10._nodes.includes(v);
            }));
            var intermediateValue = {};
            var innerOptions = Object.assign({}, options, {
              parent: intermediateValue,
              __validating: options.__validating || false
            });
            var isChanged = false;
            var _iterator5 = _createForOfIteratorHelper(props),
              _step5;
            try {
              for (_iterator5.s(); !(_step5 = _iterator5.n()).done;) {
                var prop = _step5.value;
                var field = fields[prop];
                var exists = prop in value;
                var inputValue = value[prop];
                if (field) {
                  var fieldValue = void 0;
                  innerOptions.path = (options.path ? "".concat(options.path, ".") : "") + prop;
                  field = field.resolve({
                    value: inputValue,
                    context: options.context,
                    parent: intermediateValue
                  });
                  var fieldSpec = field instanceof Schema ? field.spec : void 0;
                  var strict = fieldSpec == null ? void 0 : fieldSpec.strict;
                  if (fieldSpec != null && fieldSpec.strip) {
                    isChanged = isChanged || prop in value;
                    continue;
                  }
                  fieldValue = !options.__validating || !strict ? field.cast(inputValue, innerOptions) : inputValue;
                  if (fieldValue !== void 0) {
                    intermediateValue[prop] = fieldValue;
                  }
                } else if (exists && !strip) {
                  intermediateValue[prop] = inputValue;
                }
                if (exists !== prop in intermediateValue || intermediateValue[prop] !== inputValue) {
                  isChanged = true;
                }
              }
            } catch (err) {
              _iterator5.e(err);
            } finally {
              _iterator5.f();
            }
            return isChanged ? intermediateValue : value;
          }
        }, {
          key: "_validate",
          value: function _validate(_value) {
            var _this11 = this;
            var options = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
            var panic = arguments.length > 2 ? arguments[2] : undefined;
            var next = arguments.length > 3 ? arguments[3] : undefined;
            var _options$from = options.from,
              from = _options$from === void 0 ? [] : _options$from,
              _options$originalValu4 = options.originalValue,
              originalValue = _options$originalValu4 === void 0 ? _value : _options$originalValu4,
              _options$recursive2 = options.recursive,
              recursive = _options$recursive2 === void 0 ? this.spec.recursive : _options$recursive2;
            options.from = [{
              schema: this,
              value: originalValue
            }].concat(_toConsumableArray(from));
            options.__validating = true;
            options.originalValue = originalValue;
            _superPropGet(ObjectSchema, "_validate", this, 3)([_value, options, panic, function (objectErrors, value) {
              if (!recursive || !isObject(value)) {
                next(objectErrors, value);
                return;
              }
              originalValue = originalValue || value;
              var tests = [];
              var _iterator6 = _createForOfIteratorHelper(_this11._nodes),
                _step6;
              try {
                for (_iterator6.s(); !(_step6 = _iterator6.n()).done;) {
                  var key = _step6.value;
                  var field = _this11.fields[key];
                  if (!field || Reference.isRef(field)) {
                    continue;
                  }
                  tests.push(field.asNestedTest({
                    options: options,
                    key: key,
                    parent: value,
                    parentPath: options.path,
                    originalParent: originalValue
                  }));
                }
              } catch (err) {
                _iterator6.e(err);
              } finally {
                _iterator6.f();
              }
              _this11.runTests({
                tests: tests,
                value: value,
                originalValue: originalValue,
                options: options
              }, panic, function (fieldErrors) {
                next(fieldErrors.sort(_this11._sortErrors).concat(objectErrors), value);
              });
            }]);
          }
        }, {
          key: "clone",
          value: function clone(spec) {
            var next = _superPropGet(ObjectSchema, "clone", this, 3)([spec]);
            next.fields = Object.assign({}, this.fields);
            next._nodes = this._nodes;
            next._excludedEdges = this._excludedEdges;
            next._sortErrors = this._sortErrors;
            return next;
          }
        }, {
          key: "concat",
          value: function concat(schema) {
            var _this12 = this;
            var next = _superPropGet(ObjectSchema, "concat", this, 3)([schema]);
            var nextFields = next.fields;
            for (var _i9 = 0, _Object$entries3 = Object.entries(this.fields); _i9 < _Object$entries3.length; _i9++) {
              var _Object$entries3$_i = _slicedToArray(_Object$entries3[_i9], 2),
                field = _Object$entries3$_i[0],
                schemaOrRef = _Object$entries3$_i[1];
              var target = nextFields[field];
              nextFields[field] = target === void 0 ? schemaOrRef : target;
            }
            return next.withMutation(function (s) {
              return (
                // XXX: excludes here is wrong
                s.setFields(nextFields, [].concat(_toConsumableArray(_this12._excludedEdges), _toConsumableArray(schema._excludedEdges)))
              );
            });
          }
        }, {
          key: "_getDefault",
          value: function _getDefault(options) {
            var _this13 = this;
            if ("default" in this.spec) {
              return _superPropGet(ObjectSchema, "_getDefault", this, 3)([options]);
            }
            if (!this._nodes.length) {
              return void 0;
            }
            var dft = {};
            this._nodes.forEach(function (key) {
              var _innerOptions;
              var field = _this13.fields[key];
              var innerOptions = options;
              if ((_innerOptions = innerOptions) != null && _innerOptions.value) {
                innerOptions = Object.assign({}, innerOptions, {
                  parent: innerOptions.value,
                  value: innerOptions.value[key]
                });
              }
              dft[key] = field && "getDefault" in field ? field.getDefault(innerOptions) : void 0;
            });
            return dft;
          }
        }, {
          key: "setFields",
          value: function setFields(shape, excludedEdges) {
            var next = this.clone();
            next.fields = shape;
            next._nodes = sortFields(shape, excludedEdges);
            next._sortErrors = sortByKeyOrder(Object.keys(shape));
            if (excludedEdges) next._excludedEdges = excludedEdges;
            return next;
          }
        }, {
          key: "shape",
          value: function shape(additions) {
            var excludes = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : [];
            return this.clone().withMutation(function (next) {
              var edges = next._excludedEdges;
              if (excludes.length) {
                if (!Array.isArray(excludes[0])) excludes = [excludes];
                edges = [].concat(_toConsumableArray(next._excludedEdges), _toConsumableArray(excludes));
              }
              return next.setFields(Object.assign(next.fields, additions), edges);
            });
          }
        }, {
          key: "partial",
          value: function partial() {
            var partial = {};
            for (var _i0 = 0, _Object$entries4 = Object.entries(this.fields); _i0 < _Object$entries4.length; _i0++) {
              var _Object$entries4$_i = _slicedToArray(_Object$entries4[_i0], 2),
                key = _Object$entries4$_i[0],
                schema = _Object$entries4$_i[1];
              partial[key] = "optional" in schema && schema.optional instanceof Function ? schema.optional() : schema;
            }
            return this.setFields(partial);
          }
        }, {
          key: "deepPartial",
          value: function deepPartial() {
            var next = _deepPartial(this);
            return next;
          }
        }, {
          key: "pick",
          value: function pick(keys) {
            var picked = {};
            var _iterator7 = _createForOfIteratorHelper(keys),
              _step7;
            try {
              for (_iterator7.s(); !(_step7 = _iterator7.n()).done;) {
                var key = _step7.value;
                if (this.fields[key]) picked[key] = this.fields[key];
              }
            } catch (err) {
              _iterator7.e(err);
            } finally {
              _iterator7.f();
            }
            return this.setFields(picked, this._excludedEdges.filter(function (_ref6) {
              var _ref7 = _slicedToArray(_ref6, 2),
                a = _ref7[0],
                b = _ref7[1];
              return keys.includes(a) && keys.includes(b);
            }));
          }
        }, {
          key: "omit",
          value: function omit(keys) {
            var remaining = [];
            for (var _i1 = 0, _Object$keys3 = Object.keys(this.fields); _i1 < _Object$keys3.length; _i1++) {
              var key = _Object$keys3[_i1];
              if (keys.includes(key)) continue;
              remaining.push(key);
            }
            return this.pick(remaining);
          }
        }, {
          key: "from",
          value: function from(_from, to, alias) {
            var fromGetter = propertyExpr.getter(_from, true);
            return this.transform(function (obj) {
              if (!obj) return obj;
              var newObj = obj;
              if (deepHas(obj, _from)) {
                newObj = Object.assign({}, obj);
                if (!alias) delete newObj[_from];
                newObj[to] = fromGetter(obj);
              }
              return newObj;
            });
          }
          /** Parse an input JSON string to an object */
        }, {
          key: "json",
          value: function json() {
            return this.transform(parseJson);
          }
          /**
           * Similar to `noUnknown` but only validates that an object is the right shape without stripping the unknown keys
           */
        }, {
          key: "exact",
          value: function exact(message) {
            return this.test({
              name: "exact",
              exclusive: true,
              message: message || object.exact,
              test: function test(value) {
                if (value == null) return true;
                var unknownKeys = unknown(this.schema, value);
                return unknownKeys.length === 0 || this.createError({
                  params: {
                    properties: unknownKeys.join(", ")
                  }
                });
              }
            });
          }
        }, {
          key: "stripUnknown",
          value: function stripUnknown() {
            return this.clone({
              noUnknown: true
            });
          }
        }, {
          key: "noUnknown",
          value: function noUnknown() {
            var noAllow = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : true;
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : object.noUnknown;
            if (typeof noAllow !== "boolean") {
              message = noAllow;
              noAllow = true;
            }
            var next = this.test({
              name: "noUnknown",
              exclusive: true,
              message: message,
              test: function test(value) {
                if (value == null) return true;
                var unknownKeys = unknown(this.schema, value);
                return !noAllow || unknownKeys.length === 0 || this.createError({
                  params: {
                    unknown: unknownKeys.join(", ")
                  }
                });
              }
            });
            next.spec.noUnknown = noAllow;
            return next;
          }
        }, {
          key: "unknown",
          value: function unknown() {
            var allow = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : true;
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : object.noUnknown;
            return this.noUnknown(!allow, message);
          }
        }, {
          key: "transformKeys",
          value: function transformKeys(fn) {
            return this.transform(function (obj) {
              if (!obj) return obj;
              var result = {};
              for (var _i10 = 0, _Object$keys4 = Object.keys(obj); _i10 < _Object$keys4.length; _i10++) {
                var key = _Object$keys4[_i10];
                result[fn(key)] = obj[key];
              }
              return result;
            });
          }
        }, {
          key: "camelCase",
          value: function camelCase() {
            return this.transformKeys(tinyCase.camelCase);
          }
        }, {
          key: "snakeCase",
          value: function snakeCase() {
            return this.transformKeys(tinyCase.snakeCase);
          }
        }, {
          key: "constantCase",
          value: function constantCase() {
            return this.transformKeys(function (key) {
              return tinyCase.snakeCase(key).toUpperCase();
            });
          }
        }, {
          key: "describe",
          value: function describe(options) {
            var next = (options ? this.resolve(options) : this).clone();
            var base = _superPropGet(ObjectSchema, "describe", this, 3)([options]);
            base.fields = {};
            for (var _i11 = 0, _Object$entries5 = Object.entries(next.fields); _i11 < _Object$entries5.length; _i11++) {
              var _Object$entries5$_i = _slicedToArray(_Object$entries5[_i11], 2),
                key = _Object$entries5$_i[0],
                value = _Object$entries5$_i[1];
              var _innerOptions2;
              var innerOptions = options;
              if ((_innerOptions2 = innerOptions) != null && _innerOptions2.value) {
                innerOptions = Object.assign({}, innerOptions, {
                  parent: innerOptions.value,
                  value: innerOptions.value[key]
                });
              }
              base.fields[key] = value.describe(innerOptions);
            }
            return base;
          }
        }]);
      }(Schema);
      create$3.prototype = ObjectSchema.prototype;
      function create$2(type) {
        return new ArraySchema(type);
      }
      var ArraySchema = /*#__PURE__*/function (_Schema7) {
        function ArraySchema(type) {
          var _this14;
          _classCallCheck(this, ArraySchema);
          _this14 = _callSuper(this, ArraySchema, [{
            type: "array",
            spec: {
              types: type
            },
            check: function check(v) {
              return Array.isArray(v);
            }
          }]);
          _this14.innerType = void 0;
          _this14.innerType = type;
          return _this14;
        }
        _inherits(ArraySchema, _Schema7);
        return _createClass(ArraySchema, [{
          key: "_cast",
          value: function _cast(_value, _opts) {
            var _this15 = this;
            var value = _superPropGet(ArraySchema, "_cast", this, 3)([_value, _opts]);
            if (!this._typeCheck(value) || !this.innerType) {
              return value;
            }
            var isChanged = false;
            var castArray = value.map(function (v, idx) {
              var castElement = _this15.innerType.cast(v, Object.assign({}, _opts, {
                path: "".concat(_opts.path || "", "[").concat(idx, "]"),
                parent: value,
                originalValue: v,
                value: v,
                index: idx
              }));
              if (castElement !== v) {
                isChanged = true;
              }
              return castElement;
            });
            return isChanged ? castArray : value;
          }
        }, {
          key: "_validate",
          value: function _validate(_value) {
            var _this16 = this;
            var options = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
            var panic = arguments.length > 2 ? arguments[2] : undefined;
            var next = arguments.length > 3 ? arguments[3] : undefined;
            var _options$recursive;
            var innerType = this.innerType;
            var recursive = (_options$recursive = options.recursive) != null ? _options$recursive : this.spec.recursive;
            options.originalValue != null ? options.originalValue : _value;
            _superPropGet(ArraySchema, "_validate", this, 3)([_value, options, panic, function (arrayErrors, value) {
              var _options$originalValu2;
              if (!recursive || !innerType || !_this16._typeCheck(value)) {
                next(arrayErrors, value);
                return;
              }
              var tests = new Array(value.length);
              for (var index = 0; index < value.length; index++) {
                var _options$originalValu;
                tests[index] = innerType.asNestedTest({
                  options: options,
                  index: index,
                  parent: value,
                  parentPath: options.path,
                  originalParent: (_options$originalValu = options.originalValue) != null ? _options$originalValu : _value
                });
              }
              _this16.runTests({
                value: value,
                tests: tests,
                originalValue: (_options$originalValu2 = options.originalValue) != null ? _options$originalValu2 : _value,
                options: options
              }, panic, function (innerTypeErrors) {
                return next(innerTypeErrors.concat(arrayErrors), value);
              });
            }]);
          }
        }, {
          key: "clone",
          value: function clone(spec) {
            var next = _superPropGet(ArraySchema, "clone", this, 3)([spec]);
            next.innerType = this.innerType;
            return next;
          }
          /** Parse an input JSON string to an object */
        }, {
          key: "json",
          value: function json() {
            return this.transform(parseJson);
          }
        }, {
          key: "concat",
          value: function concat(schema) {
            var next = _superPropGet(ArraySchema, "concat", this, 3)([schema]);
            next.innerType = this.innerType;
            if (schema.innerType) next.innerType = next.innerType ?
            // @ts-expect-error Lazy doesn't have concat and will break
            next.innerType.concat(schema.innerType) : schema.innerType;
            return next;
          }
        }, {
          key: "of",
          value: function of(schema) {
            var next = this.clone();
            if (!isSchema(schema)) throw new TypeError("`array.of()` sub-schema must be a valid yup schema not: " + printValue(schema));
            next.innerType = schema;
            next.spec = Object.assign({}, next.spec, {
              types: schema
            });
            return next;
          }
        }, {
          key: "length",
          value: function length(_length2) {
            var message = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : array.length;
            return this.test({
              message: message,
              name: "length",
              exclusive: true,
              params: {
                length: _length2
              },
              skipAbsent: true,
              test: function test(value) {
                return value.length === this.resolve(_length2);
              }
            });
          }
        }, {
          key: "min",
          value: function min(_min4, message) {
            message = message || array.min;
            return this.test({
              message: message,
              name: "min",
              exclusive: true,
              params: {
                min: _min4
              },
              skipAbsent: true,
              // FIXME(ts): Array<typeof T>
              test: function test(value) {
                return value.length >= this.resolve(_min4);
              }
            });
          }
        }, {
          key: "max",
          value: function max(_max4, message) {
            message = message || array.max;
            return this.test({
              message: message,
              name: "max",
              exclusive: true,
              params: {
                max: _max4
              },
              skipAbsent: true,
              test: function test(value) {
                return value.length <= this.resolve(_max4);
              }
            });
          }
        }, {
          key: "ensure",
          value: function ensure() {
            var _this17 = this;
            return this["default"](function () {
              return [];
            }).transform(function (val, original) {
              if (_this17._typeCheck(val)) return val;
              return original == null ? [] : [].concat(original);
            });
          }
        }, {
          key: "compact",
          value: function compact(rejector) {
            var reject = !rejector ? function (v) {
              return !!v;
            } : function (v, i, a) {
              return !rejector(v, i, a);
            };
            return this.transform(function (values) {
              return values != null ? values.filter(reject) : values;
            });
          }
        }, {
          key: "describe",
          value: function describe(options) {
            var next = (options ? this.resolve(options) : this).clone();
            var base = _superPropGet(ArraySchema, "describe", this, 3)([options]);
            if (next.innerType) {
              var _innerOptions;
              var innerOptions = options;
              if ((_innerOptions = innerOptions) != null && _innerOptions.value) {
                innerOptions = Object.assign({}, innerOptions, {
                  parent: innerOptions.value,
                  value: innerOptions.value[0]
                });
              }
              base.innerType = next.innerType.describe(innerOptions);
            }
            return base;
          }
        }]);
      }(Schema);
      create$2.prototype = ArraySchema.prototype;
      function create$1(schemas) {
        return new TupleSchema(schemas);
      }
      var TupleSchema = /*#__PURE__*/function (_Schema8) {
        function TupleSchema(schemas) {
          var _this18;
          _classCallCheck(this, TupleSchema);
          _this18 = _callSuper(this, TupleSchema, [{
            type: "tuple",
            spec: {
              types: schemas
            },
            check: function check(v) {
              var types = this.spec.types;
              return Array.isArray(v) && v.length === types.length;
            }
          }]);
          _this18.withMutation(function () {
            _this18.typeError(tuple.notType);
          });
          return _this18;
        }
        _inherits(TupleSchema, _Schema8);
        return _createClass(TupleSchema, [{
          key: "_cast",
          value: function _cast(inputValue, options) {
            var types = this.spec.types;
            var value = _superPropGet(TupleSchema, "_cast", this, 3)([inputValue, options]);
            if (!this._typeCheck(value)) {
              return value;
            }
            var isChanged = false;
            var castArray = types.map(function (type, idx) {
              var castElement = type.cast(value[idx], Object.assign({}, options, {
                path: "".concat(options.path || "", "[").concat(idx, "]"),
                parent: value,
                originalValue: value[idx],
                value: value[idx],
                index: idx
              }));
              if (castElement !== value[idx]) isChanged = true;
              return castElement;
            });
            return isChanged ? castArray : value;
          }
        }, {
          key: "_validate",
          value: function _validate(_value) {
            var _this19 = this;
            var options = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
            var panic = arguments.length > 2 ? arguments[2] : undefined;
            var next = arguments.length > 3 ? arguments[3] : undefined;
            var itemTypes = this.spec.types;
            _superPropGet(TupleSchema, "_validate", this, 3)([_value, options, panic, function (tupleErrors, value) {
              var _options$originalValu2;
              if (!_this19._typeCheck(value)) {
                next(tupleErrors, value);
                return;
              }
              var tests = [];
              var _iterator8 = _createForOfIteratorHelper(itemTypes.entries()),
                _step8;
              try {
                for (_iterator8.s(); !(_step8 = _iterator8.n()).done;) {
                  var _step8$value = _slicedToArray(_step8.value, 2),
                    index = _step8$value[0],
                    itemSchema = _step8$value[1];
                  var _options$originalValu;
                  tests[index] = itemSchema.asNestedTest({
                    options: options,
                    index: index,
                    parent: value,
                    parentPath: options.path,
                    originalParent: (_options$originalValu = options.originalValue) != null ? _options$originalValu : _value
                  });
                }
              } catch (err) {
                _iterator8.e(err);
              } finally {
                _iterator8.f();
              }
              _this19.runTests({
                value: value,
                tests: tests,
                originalValue: (_options$originalValu2 = options.originalValue) != null ? _options$originalValu2 : _value,
                options: options
              }, panic, function (innerTypeErrors) {
                return next(innerTypeErrors.concat(tupleErrors), value);
              });
            }]);
          }
        }, {
          key: "describe",
          value: function describe(options) {
            var next = (options ? this.resolve(options) : this).clone();
            var base = _superPropGet(TupleSchema, "describe", this, 3)([options]);
            base.innerType = next.spec.types.map(function (schema, index) {
              var _innerOptions;
              var innerOptions = options;
              if ((_innerOptions = innerOptions) != null && _innerOptions.value) {
                innerOptions = Object.assign({}, innerOptions, {
                  parent: innerOptions.value,
                  value: innerOptions.value[index]
                });
              }
              return schema.describe(innerOptions);
            });
            return base;
          }
        }]);
      }(Schema);
      create$1.prototype = TupleSchema.prototype;
      function create(builder) {
        return new Lazy(builder);
      }
      function catchValidationError(fn) {
        try {
          return fn();
        } catch (err) {
          if (ValidationError.isError(err)) return Promise.reject(err);
          throw err;
        }
      }
      var Lazy = /*#__PURE__*/function () {
        function _Lazy(builder) {
          var _this20 = this;
          _classCallCheck(this, _Lazy);
          this.type = "lazy";
          this.__isYupSchema__ = true;
          this.spec = void 0;
          this._resolve = function (value) {
            var options = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
            var schema = _this20.builder(value, options);
            if (!isSchema(schema)) throw new TypeError("lazy() functions must return a valid schema");
            if (_this20.spec.optional) schema = schema.optional();
            return schema.resolve(options);
          };
          this.builder = builder;
          this.spec = {
            meta: void 0,
            optional: false
          };
        }
        return _createClass(_Lazy, [{
          key: "clone",
          value: function clone(spec) {
            var next = new _Lazy(this.builder);
            next.spec = Object.assign({}, this.spec, spec);
            return next;
          }
        }, {
          key: "optionality",
          value: function optionality(optional) {
            var next = this.clone({
              optional: optional
            });
            return next;
          }
        }, {
          key: "optional",
          value: function optional() {
            return this.optionality(true);
          }
        }, {
          key: "resolve",
          value: function resolve(options) {
            return this._resolve(options.value, options);
          }
        }, {
          key: "cast",
          value: function cast(value, options) {
            return this._resolve(value, options).cast(value, options);
          }
        }, {
          key: "asNestedTest",
          value: function asNestedTest(config) {
            var key = config.key,
              index = config.index,
              parent = config.parent,
              options = config.options;
            var value = parent[index != null ? index : key];
            return this._resolve(value, Object.assign({}, options, {
              value: value,
              parent: parent
            })).asNestedTest(config);
          }
        }, {
          key: "validate",
          value: function validate(value, options) {
            var _this21 = this;
            return catchValidationError(function () {
              return _this21._resolve(value, options).validate(value, options);
            });
          }
        }, {
          key: "validateSync",
          value: function validateSync(value, options) {
            return this._resolve(value, options).validateSync(value, options);
          }
        }, {
          key: "validateAt",
          value: function validateAt(path, value, options) {
            var _this22 = this;
            return catchValidationError(function () {
              return _this22._resolve(value, options).validateAt(path, value, options);
            });
          }
        }, {
          key: "validateSyncAt",
          value: function validateSyncAt(path, value, options) {
            return this._resolve(value, options).validateSyncAt(path, value, options);
          }
        }, {
          key: "isValid",
          value: function isValid(value, options) {
            try {
              return this._resolve(value, options).isValid(value, options);
            } catch (err) {
              if (ValidationError.isError(err)) {
                return Promise.resolve(false);
              }
              throw err;
            }
          }
        }, {
          key: "isValidSync",
          value: function isValidSync(value, options) {
            return this._resolve(value, options).isValidSync(value, options);
          }
        }, {
          key: "describe",
          value: function describe(options) {
            return options ? this.resolve(options).describe(options) : {
              type: "lazy",
              meta: this.spec.meta,
              label: void 0
            };
          }
        }, {
          key: "meta",
          value: function meta() {
            if (arguments.length === 0) return this.spec.meta;
            var next = this.clone();
            next.spec.meta = Object.assign(next.spec.meta || {}, arguments.length <= 0 ? undefined : arguments[0]);
            return next;
          }
        }, {
          key: "~standard",
          get: function get() {
            var schema = this;
            var standard = {
              version: 1,
              vendor: "yup",
              validate: function validate(value) {
                return _asyncToGenerator(/*#__PURE__*/_regenerator().m(function _callee2() {
                  var result, _t2;
                  return _regenerator().w(function (_context2) {
                    while (1) switch (_context2.p = _context2.n) {
                      case 0:
                        _context2.p = 0;
                        _context2.n = 1;
                        return schema.validate(value, {
                          abortEarly: false
                        });
                      case 1:
                        result = _context2.v;
                        return _context2.a(2, {
                          value: result
                        });
                      case 2:
                        _context2.p = 2;
                        _t2 = _context2.v;
                        if (!ValidationError.isError(_t2)) {
                          _context2.n = 3;
                          break;
                        }
                        return _context2.a(2, {
                          issues: issuesFromValidationError(_t2)
                        });
                      case 3:
                        throw _t2;
                      case 4:
                        return _context2.a(2);
                    }
                  }, _callee2, null, [[0, 2]]);
                }))();
              }
            };
            return standard;
          }
        }]);
      }();
      function setLocale(custom) {
        Object.keys(custom).forEach(function (type) {
          Object.keys(custom[type]).forEach(function (method) {
            locale[type][method] = custom[type][method];
          });
        });
      }
      function addMethod(schemaType, name, fn) {
        if (!schemaType || !isSchema(schemaType.prototype)) throw new TypeError("You must provide a yup schema constructor function");
        if (typeof name !== "string") throw new TypeError("A Method name must be provided");
        if (typeof fn !== "function") throw new TypeError("Method function must be provided");
        schemaType.prototype[name] = fn;
      }
      exports.ArraySchema = ArraySchema;
      exports.BooleanSchema = BooleanSchema;
      exports.DateSchema = DateSchema;
      exports.LazySchema = Lazy;
      exports.MixedSchema = MixedSchema;
      exports.NumberSchema = NumberSchema;
      exports.ObjectSchema = ObjectSchema;
      exports.Schema = Schema;
      exports.StringSchema = StringSchema;
      exports.TupleSchema = TupleSchema;
      exports.ValidationError = ValidationError;
      exports.addMethod = addMethod;
      exports.array = create$2;
      exports.bool = create$7;
      exports["boolean"] = create$7;
      exports.date = create$4;
      exports.defaultLocale = locale;
      exports.getIn = getIn;
      exports.isSchema = isSchema;
      exports.lazy = create;
      exports.mixed = create$8;
      exports.number = create$5;
      exports.object = create$3;
      exports.printValue = printValue;
      exports.reach = reach;
      exports.ref = create$9;
      exports.setLocale = setLocale;
      exports.string = create$6;
      exports.tuple = create$1;
    }
  });

  // entry.js
  var require_entry = __commonJS({
    "entry.js": function entryJs(exports, module) {
      var yup = require_package();
      module.exports = yup;
    }
  });
  return require_entry();
}();
// === Yup Test Suite ===

// T1: string schema - basic validation
try {
  var r = yup.string().validateSync("hello");
  console.log("T1 string basic: " + r);
} catch(e) { console.log("T1 ERROR: " + e.message); }

// T2: number schema - basic validation
try {
  var r = yup.number().validateSync(42);
  console.log("T2 number basic: " + r);
} catch(e) { console.log("T2 ERROR: " + e.message); }

// T3: boolean schema
try {
  var r = yup.boolean().validateSync(true);
  console.log("T3 boolean: " + r);
} catch(e) { console.log("T3 ERROR: " + e.message); }

// T4: string.required - valid
try {
  var r = yup.string().required().validateSync("world");
  console.log("T4 required valid: " + r);
} catch(e) { console.log("T4 ERROR: " + e.message); }

// T5: string.required - invalid (empty string)
try {
  yup.string().required().validateSync("");
  console.log("T5 should have thrown");
} catch(e) { console.log("T5 required empty: " + e.name); }

// T6: string.required - invalid (undefined)
try {
  yup.string().required().validateSync(undefined);
  console.log("T6 should have thrown");
} catch(e) { console.log("T6 required undef: " + e.name); }

// T7: number.required
try {
  yup.number().required().validateSync(undefined);
  console.log("T7 should have thrown");
} catch(e) { console.log("T7 num required: " + e.name); }

// T8: string.min
try {
  yup.string().min(5).validateSync("hi");
  console.log("T8 should have thrown");
} catch(e) { console.log("T8 min: " + e.name); }

// T9: string.max
try {
  yup.string().max(3).validateSync("hello");
  console.log("T9 should have thrown");
} catch(e) { console.log("T9 max: " + e.name); }

// T10: string.min valid
try {
  var r = yup.string().min(2).validateSync("hello");
  console.log("T10 min valid: " + r);
} catch(e) { console.log("T10 ERROR: " + e.message); }

// T11: number.min
try {
  yup.number().min(10).validateSync(5);
  console.log("T11 should have thrown");
} catch(e) { console.log("T11 num min: " + e.name); }

// T12: number.max
try {
  yup.number().max(10).validateSync(15);
  console.log("T12 should have thrown");
} catch(e) { console.log("T12 num max: " + e.name); }

// T13: number.positive
try {
  yup.number().positive().validateSync(-5);
  console.log("T13 should have thrown");
} catch(e) { console.log("T13 positive: " + e.name); }

// T14: number.negative
try {
  yup.number().negative().validateSync(5);
  console.log("T14 should have thrown");
} catch(e) { console.log("T14 negative: " + e.name); }

// T15: number.integer
try {
  yup.number().integer().validateSync(3.5);
  console.log("T15 should have thrown");
} catch(e) { console.log("T15 integer: " + e.name); }

// T16: string.email
try {
  yup.string().email().validateSync("not-email");
  console.log("T16 should have thrown");
} catch(e) { console.log("T16 email: " + e.name); }

// T17: string.email valid
try {
  var r = yup.string().email().validateSync("test@example.com");
  console.log("T17 email valid: " + r);
} catch(e) { console.log("T17 ERROR: " + e.message); }

// T18: string.url
try {
  yup.string().url().validateSync("not-url");
  console.log("T18 should have thrown");
} catch(e) { console.log("T18 url: " + e.name); }

// T19: string.matches (regex)
try {
  var r = yup.string().matches(/^[a-z]+$/).validateSync("hello");
  console.log("T19 matches: " + r);
} catch(e) { console.log("T19 ERROR: " + e.message); }

// T20: string.matches fail
try {
  yup.string().matches(/^[a-z]+$/).validateSync("Hello123");
  console.log("T20 should have thrown");
} catch(e) { console.log("T20 matches fail: " + e.name); }

// T21: string.oneOf
try {
  var r = yup.string().oneOf(["red", "green", "blue"]).validateSync("red");
  console.log("T21 oneOf valid: " + r);
} catch(e) { console.log("T21 ERROR: " + e.message); }

// T22: string.oneOf fail
try {
  yup.string().oneOf(["red", "green", "blue"]).validateSync("yellow");
  console.log("T22 should have thrown");
} catch(e) { console.log("T22 oneOf fail: " + e.name); }

// T23: string.notOneOf
try {
  yup.string().notOneOf(["bad", "evil"]).validateSync("bad");
  console.log("T23 should have thrown");
} catch(e) { console.log("T23 notOneOf: " + e.name); }

// T24: number.truncate + round
try {
  var r = yup.number().truncate().validateSync(3.7);
  console.log("T24 truncate: " + r);
} catch(e) { console.log("T24 ERROR: " + e.message); }

// T25: number.round
try {
  var r = yup.number().round("floor").validateSync(3.7);
  console.log("T25 round floor: " + r);
} catch(e) { console.log("T25 round floor: " + e.name); }

// T26: object schema
try {
  var schema = yup.object({
    name: yup.string().required(),
    age: yup.number().positive().integer()
  });
  var r = schema.validateSync({ name: "Alice", age: 25 });
  console.log("T26 object valid: " + r.name + " " + r.age);
} catch(e) { console.log("T26 ERROR: " + e.message); }

// T27: object schema fail
try {
  var schema = yup.object({
    name: yup.string().required(),
    age: yup.number().positive()
  });
  schema.validateSync({ name: "", age: 25 });
  console.log("T27 should have thrown");
} catch(e) { console.log("T27 object fail: " + e.name); }

// T28: array schema
try {
  var r = yup.array().of(yup.number()).validateSync([1, 2, 3]);
  console.log("T28 array: " + r.join(","));
} catch(e) { console.log("T28 ERROR: " + e.message); }

// T29: array.min
try {
  yup.array().min(3).validateSync([1, 2]);
  console.log("T29 should have thrown");
} catch(e) { console.log("T29 array min: " + e.name); }

// T30: isValidSync
try {
  var s = yup.string().required();
  var r1 = s.isValidSync("hello");
  var r2 = s.isValidSync("");
  var r3 = s.isValidSync(undefined);
  console.log("T30 isValidSync: " + r1 + " " + r2 + " " + r3);
} catch(e) { console.log("T30 ERROR: " + e.message); }

// T31: number cast from string
try {
  var r = yup.number().validateSync("42");
  console.log("T31 cast: " + r + " " + typeof r);
} catch(e) { console.log("T31 ERROR: " + e.message); }

// T32: string.trim transform
try {
  var r = yup.string().trim().validateSync("  hello  ");
  console.log("T32 trim: [" + r + "]");
} catch(e) { console.log("T32 ERROR: " + e.message); }

// T33: string.lowercase transform
try {
  var r = yup.string().lowercase().validateSync("HELLO");
  console.log("T33 lowercase: " + r);
} catch(e) { console.log("T33 ERROR: " + e.message); }

// T34: string.uppercase transform
try {
  var r = yup.string().uppercase().validateSync("hello");
  console.log("T34 uppercase: " + r);
} catch(e) { console.log("T34 ERROR: " + e.message); }

// T35: nullable
try {
  var r = yup.string().nullable().validateSync(null);
  console.log("T35 nullable: " + r);
} catch(e) { console.log("T35 ERROR: " + e.message); }

// T36: not nullable
try {
  yup.string().nonNullable().validateSync(null);
  console.log("T36 should have thrown");
} catch(e) { console.log("T36 nonNullable: " + e.name); }

// T37: default value
try {
  var s = yup.string().default("fallback");
  var r = s.cast(undefined);
  console.log("T37 default: " + r);
} catch(e) { console.log("T37 ERROR: " + e.message); }

// T38: string.length exact
try {
  yup.string().length(5).validateSync("hi");
  console.log("T38 should have thrown");
} catch(e) { console.log("T38 length: " + e.name); }

// T39: string.length exact valid
try {
  var r = yup.string().length(5).validateSync("hello");
  console.log("T39 length valid: " + r);
} catch(e) { console.log("T39 ERROR: " + e.message); }

// T40: ValidationError properties
try {
  yup.string().required().validateSync("");
} catch(e) {
  console.log("T40 VE props: " + e.name + " path=" + e.path + " type=" + e.type + " errors=" + e.errors.length);
}

// T41: multiple errors with abortEarly=false
try {
  var schema = yup.object({
    name: yup.string().required(),
    age: yup.number().positive().required()
  });
  schema.validateSync({}, { abortEarly: false });
  console.log("T41 should have thrown");
} catch(e) {
  console.log("T41 multi errors: " + e.inner.length);
}

// T42: string.defined
try {
  yup.string().defined().validateSync(undefined);
  console.log("T42 should have thrown");
} catch(e) { console.log("T42 defined: " + e.name); }

// T43: mixed.oneOf with ref
try {
  var r = yup.number().oneOf([1, 2, 3]).isValidSync(2);
  console.log("T43 number oneOf: " + r);
} catch(e) { console.log("T43 ERROR: " + e.message); }

// T44: number.lessThan
try {
  yup.number().lessThan(5).validateSync(10);
  console.log("T44 should have thrown");
} catch(e) { console.log("T44 lessThan: " + e.name); }

// T45: number.moreThan
try {
  yup.number().moreThan(5).validateSync(3);
  console.log("T45 should have thrown");
} catch(e) { console.log("T45 moreThan: " + e.name); }

// T46: boolean.isTrue
try {
  yup.boolean().isTrue().validateSync(false);
  console.log("T46 should have thrown");
} catch(e) { console.log("T46 isTrue: " + e.name); }

// T47: boolean.isFalse
try {
  yup.boolean().isFalse().validateSync(true);
  console.log("T47 should have thrown");
} catch(e) { console.log("T47 isFalse: " + e.name); }

// T48: schema.describe
try {
  var d = yup.string().required().min(3).max(10).describe();
  console.log("T48 describe: type=" + d.type + " optional=" + d.optional + " tests=" + d.tests.length);
} catch(e) { console.log("T48 ERROR: " + e.message); }

// T49: schema.isType
try {
  var s = yup.string();
  console.log("T49 isType: " + s.isType("hello") + " " + s.isType(42) + " " + s.isType(null));
} catch(e) { console.log("T49 ERROR: " + e.message); }

// T50: object.shape
try {
  var schema = yup.object().shape({
    email: yup.string().email(),
    age: yup.number().min(0)
  });
  var r = schema.isValidSync({ email: "a@b.com", age: 25 });
  console.log("T50 shape: " + r);
} catch(e) { console.log("T50 ERROR: " + e.message); }

// T51: array.of with invalid element
try {
  yup.array().of(yup.number()).validateSync([1, "abc", 3]);
  console.log("T51 should have thrown");
} catch(e) { console.log("T51 array of fail: " + e.name); }

// T52: string.ensure (transforms undefined to "")
try {
  var r = yup.string().ensure().validateSync(undefined);
  console.log("T52 ensure: [" + r + "]");
} catch(e) { console.log("T52 ERROR: " + e.message); }

// T53: object.noUnknown
try {
  var r = yup.object({ name: yup.string() }).noUnknown().validateSync({ name: "Alice", extra: true });
  console.log("T53 noUnknown: " + (r !== undefined ? "passed" : "undefined"));
} catch(e) { console.log("T53 noUnknown: " + e.name); }

// T54: mixed.strip
try {
  var schema = yup.object({
    name: yup.string(),
    secret: yup.string().strip()
  });
  var r = schema.validateSync({ name: "Alice", secret: "hidden" });
  console.log("T54 strip: " + JSON.stringify(r));
} catch(e) { console.log("T54 ERROR: " + e.message); }

// T55: array.compact
try {
  var r = yup.array().compact().validateSync([1, null, 2, undefined, 3, 0, false, ""]);
  console.log("T55 compact: " + JSON.stringify(r));
} catch(e) { console.log("T55 ERROR: " + e.message); }
