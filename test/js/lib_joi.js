var module = { exports: {} };
var exports = module.exports;
globalThis.global = globalThis;
globalThis.self = globalThis;
if (!Error.captureStackTrace) { Error.captureStackTrace = function(obj, cons) {}; }
function URL(s) { this.href = s; this.hostname = ""; }
function TextEncoder() {}
TextEncoder.prototype.encode = function(s) {
  var arr = [];
  for (var i = 0; i < s.length; i++) { var c = s.charCodeAt(i); if (c < 128) arr.push(c); }
  return arr;
};
"use strict";

var _excluded = ["functions"];
function _regeneratorValues(e) { if (null != e) { var t = e["function" == typeof Symbol && Symbol.iterator || "@@iterator"], r = 0; if (t) return t.call(e); if ("function" == typeof e.next) return e; if (!isNaN(e.length)) return { next: function next() { return e && r >= e.length && (e = void 0), { value: e && e[r++], done: !e }; } }; } throw new TypeError(_typeof(e) + " is not iterable"); }
function _regenerator() { /*! regenerator-runtime -- Copyright (c) 2014-present, Facebook, Inc. -- license (MIT): https://github.com/babel/babel/blob/main/packages/babel-helpers/LICENSE */ var e, t, r = "function" == typeof Symbol ? Symbol : {}, n = r.iterator || "@@iterator", o = r.toStringTag || "@@toStringTag"; function i(r, n, o, i) { var c = n && n.prototype instanceof Generator ? n : Generator, u = Object.create(c.prototype); return _regeneratorDefine2(u, "_invoke", function (r, n, o) { var i, c, u, f = 0, p = o || [], y = !1, G = { p: 0, n: 0, v: e, a: d, f: d.bind(e, 4), d: function d(t, r) { return i = t, c = 0, u = e, G.n = r, a; } }; function d(r, n) { for (c = r, u = n, t = 0; !y && f && !o && t < p.length; t++) { var o, i = p[t], d = G.p, l = i[2]; r > 3 ? (o = l === n) && (u = i[(c = i[4]) ? 5 : (c = 3, 3)], i[4] = i[5] = e) : i[0] <= d && ((o = r < 2 && d < i[1]) ? (c = 0, G.v = n, G.n = i[1]) : d < l && (o = r < 3 || i[0] > n || n > l) && (i[4] = r, i[5] = n, G.n = l, c = 0)); } if (o || r > 1) return a; throw y = !0, n; } return function (o, p, l) { if (f > 1) throw TypeError("Generator is already running"); for (y && 1 === p && d(p, l), c = p, u = l; (t = c < 2 ? e : u) || !y;) { i || (c ? c < 3 ? (c > 1 && (G.n = -1), d(c, u)) : G.n = u : G.v = u); try { if (f = 2, i) { if (c || (o = "next"), t = i[o]) { if (!(t = t.call(i, u))) throw TypeError("iterator result is not an object"); if (!t.done) return t; u = t.value, c < 2 && (c = 0); } else 1 === c && (t = i["return"]) && t.call(i), c < 2 && (u = TypeError("The iterator does not provide a '" + o + "' method"), c = 1); i = e; } else if ((t = (y = G.n < 0) ? u : r.call(n, G)) !== a) break; } catch (t) { i = e, c = 1, u = t; } finally { f = 1; } } return { value: t, done: y }; }; }(r, o, i), !0), u; } var a = {}; function Generator() {} function GeneratorFunction() {} function GeneratorFunctionPrototype() {} t = Object.getPrototypeOf; var c = [][n] ? t(t([][n]())) : (_regeneratorDefine2(t = {}, n, function () { return this; }), t), u = GeneratorFunctionPrototype.prototype = Generator.prototype = Object.create(c); function f(e) { return Object.setPrototypeOf ? Object.setPrototypeOf(e, GeneratorFunctionPrototype) : (e.__proto__ = GeneratorFunctionPrototype, _regeneratorDefine2(e, o, "GeneratorFunction")), e.prototype = Object.create(u), e; } return GeneratorFunction.prototype = GeneratorFunctionPrototype, _regeneratorDefine2(u, "constructor", GeneratorFunctionPrototype), _regeneratorDefine2(GeneratorFunctionPrototype, "constructor", GeneratorFunction), GeneratorFunction.displayName = "GeneratorFunction", _regeneratorDefine2(GeneratorFunctionPrototype, o, "GeneratorFunction"), _regeneratorDefine2(u), _regeneratorDefine2(u, o, "Generator"), _regeneratorDefine2(u, n, function () { return this; }), _regeneratorDefine2(u, "toString", function () { return "[object Generator]"; }), (_regenerator = function _regenerator() { return { w: i, m: f }; })(); }
function _regeneratorDefine2(e, r, n, t) { var i = Object.defineProperty; try { i({}, "", {}); } catch (e) { i = 0; } _regeneratorDefine2 = function _regeneratorDefine(e, r, n, t) { function o(r, n) { _regeneratorDefine2(e, r, function (e) { return this._invoke(r, n, e); }); } r ? i ? i(e, r, { value: n, enumerable: !t, configurable: !t, writable: !t }) : e[r] = n : (o("next", 0), o("throw", 1), o("return", 2)); }, _regeneratorDefine2(e, r, n, t); }
function asyncGeneratorStep(n, t, e, r, o, a, c) { try { var i = n[a](c), u = i.value; } catch (n) { return void e(n); } i.done ? t(u) : Promise.resolve(u).then(r, o); }
function _asyncToGenerator(n) { return function () { var t = this, e = arguments; return new Promise(function (r, o) { var a = n.apply(t, e); function _next(n) { asyncGeneratorStep(a, r, o, _next, _throw, "next", n); } function _throw(n) { asyncGeneratorStep(a, r, o, _next, _throw, "throw", n); } _next(void 0); }); }; }
function _objectWithoutProperties(e, t) { if (null == e) return {}; var o, r, i = _objectWithoutPropertiesLoose(e, t); if (Object.getOwnPropertySymbols) { var n = Object.getOwnPropertySymbols(e); for (r = 0; r < n.length; r++) o = n[r], -1 === t.indexOf(o) && {}.propertyIsEnumerable.call(e, o) && (i[o] = e[o]); } return i; }
function _objectWithoutPropertiesLoose(r, e) { if (null == r) return {}; var t = {}; for (var n in r) if ({}.hasOwnProperty.call(r, n)) { if (-1 !== e.indexOf(n)) continue; t[n] = r[n]; } return t; }
function ownKeys(e, r) { var t = Object.keys(e); if (Object.getOwnPropertySymbols) { var o = Object.getOwnPropertySymbols(e); r && (o = o.filter(function (r) { return Object.getOwnPropertyDescriptor(e, r).enumerable; })), t.push.apply(t, o); } return t; }
function _objectSpread(e) { for (var r = 1; r < arguments.length; r++) { var t = null != arguments[r] ? arguments[r] : {}; r % 2 ? ownKeys(Object(t), !0).forEach(function (r) { _defineProperty(e, r, t[r]); }) : Object.getOwnPropertyDescriptors ? Object.defineProperties(e, Object.getOwnPropertyDescriptors(t)) : ownKeys(Object(t)).forEach(function (r) { Object.defineProperty(e, r, Object.getOwnPropertyDescriptor(t, r)); }); } return e; }
function _slicedToArray(r, e) { return _arrayWithHoles(r) || _iterableToArrayLimit(r, e) || _unsupportedIterableToArray(r, e) || _nonIterableRest(); }
function _nonIterableRest() { throw new TypeError("Invalid attempt to destructure non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method."); }
function _iterableToArrayLimit(r, l) { var t = null == r ? null : "undefined" != typeof Symbol && r[Symbol.iterator] || r["@@iterator"]; if (null != t) { var e, n, i, u, a = [], f = !0, o = !1; try { if (i = (t = t.call(r)).next, 0 === l) { if (Object(t) !== t) return; f = !1; } else for (; !(f = (e = i.call(t)).done) && (a.push(e.value), a.length !== l); f = !0); } catch (r) { o = !0, n = r; } finally { try { if (!f && null != t["return"] && (u = t["return"](), Object(u) !== u)) return; } finally { if (o) throw n; } } return a; } }
function _arrayWithHoles(r) { if (Array.isArray(r)) return r; }
function _callSuper(t, o, e) { return o = _getPrototypeOf(o), _possibleConstructorReturn(t, _isNativeReflectConstruct() ? Reflect.construct(o, e || [], _getPrototypeOf(t).constructor) : o.apply(t, e)); }
function _possibleConstructorReturn(t, e) { if (e && ("object" == _typeof(e) || "function" == typeof e)) return e; if (void 0 !== e) throw new TypeError("Derived constructors may only return object or undefined"); return _assertThisInitialized(t); }
function _assertThisInitialized(e) { if (void 0 === e) throw new ReferenceError("this hasn't been initialised - super() hasn't been called"); return e; }
function _inherits(t, e) { if ("function" != typeof e && null !== e) throw new TypeError("Super expression must either be null or a function"); t.prototype = Object.create(e && e.prototype, { constructor: { value: t, writable: !0, configurable: !0 } }), Object.defineProperty(t, "prototype", { writable: !1 }), e && _setPrototypeOf(t, e); }
function _wrapNativeSuper(t) { var r = "function" == typeof Map ? new Map() : void 0; return _wrapNativeSuper = function _wrapNativeSuper(t) { if (null === t || !_isNativeFunction(t)) return t; if ("function" != typeof t) throw new TypeError("Super expression must either be null or a function"); if (void 0 !== r) { if (r.has(t)) return r.get(t); r.set(t, Wrapper); } function Wrapper() { return _construct(t, arguments, _getPrototypeOf(this).constructor); } return Wrapper.prototype = Object.create(t.prototype, { constructor: { value: Wrapper, enumerable: !1, writable: !0, configurable: !0 } }), _setPrototypeOf(Wrapper, t); }, _wrapNativeSuper(t); }
function _construct(t, e, r) { if (_isNativeReflectConstruct()) return Reflect.construct.apply(null, arguments); var o = [null]; o.push.apply(o, e); var p = new (t.bind.apply(t, o))(); return r && _setPrototypeOf(p, r.prototype), p; }
function _isNativeReflectConstruct() { try { var t = !Boolean.prototype.valueOf.call(Reflect.construct(Boolean, [], function () {})); } catch (t) {} return (_isNativeReflectConstruct = function _isNativeReflectConstruct() { return !!t; })(); }
function _isNativeFunction(t) { try { return -1 !== Function.toString.call(t).indexOf("[native code]"); } catch (n) { return "function" == typeof t; } }
function _setPrototypeOf(t, e) { return _setPrototypeOf = Object.setPrototypeOf ? Object.setPrototypeOf.bind() : function (t, e) { return t.__proto__ = e, t; }, _setPrototypeOf(t, e); }
function _getPrototypeOf(t) { return _getPrototypeOf = Object.setPrototypeOf ? Object.getPrototypeOf.bind() : function (t) { return t.__proto__ || Object.getPrototypeOf(t); }, _getPrototypeOf(t); }
function _toConsumableArray(r) { return _arrayWithoutHoles(r) || _iterableToArray(r) || _unsupportedIterableToArray(r) || _nonIterableSpread(); }
function _nonIterableSpread() { throw new TypeError("Invalid attempt to spread non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method."); }
function _iterableToArray(r) { if ("undefined" != typeof Symbol && null != r[Symbol.iterator] || null != r["@@iterator"]) return Array.from(r); }
function _arrayWithoutHoles(r) { if (Array.isArray(r)) return _arrayLikeToArray(r); }
function _defineProperty(e, r, t) { return (r = _toPropertyKey(r)) in e ? Object.defineProperty(e, r, { value: t, enumerable: !0, configurable: !0, writable: !0 }) : e[r] = t, e; }
function _createForOfIteratorHelper(r, e) { var t = "undefined" != typeof Symbol && r[Symbol.iterator] || r["@@iterator"]; if (!t) { if (Array.isArray(r) || (t = _unsupportedIterableToArray(r)) || e && r && "number" == typeof r.length) { t && (r = t); var n = 0, F = function F() {}; return { s: F, n: function (_n47) { function n() { return _n47.apply(this, arguments); } n.toString = function () { return _n47.toString(); }; return n; }(function () { return n >= r.length ? { done: !0 } : { done: !1, value: r[n++] }; }), e: function e(r) { throw r; }, f: F }; } throw new TypeError("Invalid attempt to iterate non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method."); } var o, a = !0, u = !1; return { s: function s() { t = t.call(r); }, n: function n() { var r = t.next(); return a = r.done, r; }, e: function e(r) { u = !0, o = r; }, f: function f() { try { a || null == t["return"] || t["return"](); } finally { if (u) throw o; } } }; }
function _unsupportedIterableToArray(r, a) { if (r) { if ("string" == typeof r) return _arrayLikeToArray(r, a); var t = {}.toString.call(r).slice(8, -1); return "Object" === t && r.constructor && (t = r.constructor.name), "Map" === t || "Set" === t ? Array.from(r) : "Arguments" === t || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(t) ? _arrayLikeToArray(r, a) : void 0; } }
function _arrayLikeToArray(r, a) { (null == a || a > r.length) && (a = r.length); for (var e = 0, n = Array(a); e < a; e++) n[e] = r[e]; return n; }
function _classCallCheck(a, n) { if (!(a instanceof n)) throw new TypeError("Cannot call a class as a function"); }
function _defineProperties(e, r) { for (var t = 0; t < r.length; t++) { var o = r[t]; o.enumerable = o.enumerable || !1, o.configurable = !0, "value" in o && (o.writable = !0), Object.defineProperty(e, _toPropertyKey(o.key), o); } }
function _createClass(e, r, t) { return r && _defineProperties(e.prototype, r), t && _defineProperties(e, t), Object.defineProperty(e, "prototype", { writable: !1 }), e; }
function _toPropertyKey(t) { var i = _toPrimitive(t, "string"); return "symbol" == _typeof(i) ? i : i + ""; }
function _toPrimitive(t, r) { if ("object" != _typeof(t) || !t) return t; var e = t[Symbol.toPrimitive]; if (void 0 !== e) { var i = e.call(t, r || "default"); if ("object" != _typeof(i)) return i; throw new TypeError("@@toPrimitive must return a primitive value."); } return ("string" === r ? String : Number)(t); }
function _typeof(o) { "@babel/helpers - typeof"; return _typeof = "function" == typeof Symbol && "symbol" == typeof Symbol.iterator ? function (o) { return typeof o; } : function (o) { return o && "function" == typeof Symbol && o.constructor === Symbol && o !== Symbol.prototype ? "symbol" : typeof o; }, _typeof(o); }
!function (e, t) {
  "object" == (typeof exports === "undefined" ? "undefined" : _typeof(exports)) && "object" == (typeof module === "undefined" ? "undefined" : _typeof(module)) ? module.exports = t() : "function" == typeof define && define.amd ? define([], t) : "object" == (typeof exports === "undefined" ? "undefined" : _typeof(exports)) ? exports.joi = t() : e.joi = t();
}(self, function () {
  return e = {
    7629: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8571),
        a = r(9474),
        i = r(1687),
        o = r(8652),
        l = r(8160),
        c = r(3292),
        u = r(6354),
        f = r(8901),
        m = r(9708),
        h = r(6914),
        d = r(2294),
        p = r(6133),
        g = r(1152),
        y = r(8863),
        b = r(2036),
        v = {
          Base: /*#__PURE__*/function () {
            function Base(e) {
              _classCallCheck(this, Base);
              this.type = e, this.$_root = null, this._definition = {}, this._reset();
            }
            return _createClass(Base, [{
              key: "_reset",
              value: function _reset() {
                this._ids = new d.Ids(), this._preferences = null, this._refs = new p.Manager(), this._cache = null, this._valids = null, this._invalids = null, this._flags = {}, this._rules = [], this._singleRules = new Map(), this.$_terms = {}, this.$_temp = {
                  ruleset: null,
                  whens: {}
                };
              }
            }, {
              key: "describe",
              value: function describe() {
                return s("function" == typeof m.describe, "Manifest functionality disabled"), m.describe(this);
              }
            }, {
              key: "allow",
              value: function allow() {
                for (var _len = arguments.length, e = new Array(_len), _key = 0; _key < _len; _key++) {
                  e[_key] = arguments[_key];
                }
                return l.verifyFlat(e, "allow"), this._values(e, "_valids");
              }
            }, {
              key: "alter",
              value: function alter(e) {
                s(e && "object" == _typeof(e) && !Array.isArray(e), "Invalid targets argument"), s(!this._inRuleset(), "Cannot set alterations inside a ruleset");
                var t = this.clone();
                t.$_terms.alterations = t.$_terms.alterations || [];
                for (var _r in e) {
                  var _n = e[_r];
                  s("function" == typeof _n, "Alteration adjuster for", _r, "must be a function"), t.$_terms.alterations.push({
                    target: _r,
                    adjuster: _n
                  });
                }
                return t.$_temp.ruleset = !1, t;
              }
            }, {
              key: "artifact",
              value: function artifact(e) {
                return s(void 0 !== e, "Artifact cannot be undefined"), s(!this._cache, "Cannot set an artifact with a rule cache"), this.$_setFlag("artifact", e);
              }
            }, {
              key: "cast",
              value: function cast(e) {
                return s(!1 === e || "string" == typeof e, "Invalid to value"), s(!1 === e || this._definition.cast[e], "Type", this.type, "does not support casting to", e), this.$_setFlag("cast", !1 === e ? void 0 : e);
              }
            }, {
              key: "default",
              value: function _default(e, t) {
                return this._default("default", e, t);
              }
            }, {
              key: "description",
              value: function description(e) {
                return s(e && "string" == typeof e, "Description must be a non-empty string"), this.$_setFlag("description", e);
              }
            }, {
              key: "empty",
              value: function empty(e) {
                var t = this.clone();
                return void 0 !== e && (e = t.$_compile(e, {
                  override: !1
                })), t.$_setFlag("empty", e, {
                  clone: !1
                });
              }
            }, {
              key: "error",
              value: function error(e) {
                return s(e, "Missing error"), s(e instanceof Error || "function" == typeof e, "Must provide a valid Error object or a function"), this.$_setFlag("error", e);
              }
            }, {
              key: "example",
              value: function example(e) {
                var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
                return s(void 0 !== e, "Missing example"), l.assertOptions(t, ["override"]), this._inner("examples", e, {
                  single: !0,
                  override: t.override
                });
              }
            }, {
              key: "external",
              value: function external(e, t) {
                return "object" == _typeof(e) && (s(!t, "Cannot combine options with description"), t = e.description, e = e.method), s("function" == typeof e, "Method must be a function"), s(void 0 === t || t && "string" == typeof t, "Description must be a non-empty string"), this._inner("externals", {
                  method: e,
                  description: t
                }, {
                  single: !0
                });
              }
            }, {
              key: "failover",
              value: function failover(e, t) {
                return this._default("failover", e, t);
              }
            }, {
              key: "forbidden",
              value: function forbidden() {
                return this.presence("forbidden");
              }
            }, {
              key: "id",
              value: function id(e) {
                return e ? (s("string" == typeof e, "id must be a non-empty string"), s(/^[^\.]+$/.test(e), "id cannot contain period character"), this.$_setFlag("id", e)) : this.$_setFlag("id", void 0);
              }
            }, {
              key: "invalid",
              value: function invalid() {
                for (var _len2 = arguments.length, e = new Array(_len2), _key2 = 0; _key2 < _len2; _key2++) {
                  e[_key2] = arguments[_key2];
                }
                return this._values(e, "_invalids");
              }
            }, {
              key: "label",
              value: function label(e) {
                return s(e && "string" == typeof e, "Label name must be a non-empty string"), this.$_setFlag("label", e);
              }
            }, {
              key: "meta",
              value: function meta(e) {
                return s(void 0 !== e, "Meta cannot be undefined"), this._inner("metas", e, {
                  single: !0
                });
              }
            }, {
              key: "note",
              value: function note() {
                for (var _len3 = arguments.length, e = new Array(_len3), _key3 = 0; _key3 < _len3; _key3++) {
                  e[_key3] = arguments[_key3];
                }
                s(e.length, "Missing notes");
                for (var _i = 0, _e = e; _i < _e.length; _i++) {
                  var _t = _e[_i];
                  s(_t && "string" == typeof _t, "Notes must be non-empty strings");
                }
                return this._inner("notes", e);
              }
            }, {
              key: "only",
              value: function only() {
                var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : !0;
                return s("boolean" == typeof e, "Invalid mode:", e), this.$_setFlag("only", e);
              }
            }, {
              key: "optional",
              value: function optional() {
                return this.presence("optional");
              }
            }, {
              key: "prefs",
              value: function prefs(e) {
                s(e, "Missing preferences"), s(void 0 === e.context, "Cannot override context"), s(void 0 === e.externals, "Cannot override externals"), s(void 0 === e.warnings, "Cannot override warnings"), s(void 0 === e.debug, "Cannot override debug"), l.checkPreferences(e);
                var t = this.clone();
                return t._preferences = l.preferences(t._preferences, e), t;
              }
            }, {
              key: "presence",
              value: function presence(e) {
                return s(["optional", "required", "forbidden"].includes(e), "Unknown presence mode", e), this.$_setFlag("presence", e);
              }
            }, {
              key: "raw",
              value: function raw() {
                var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : !0;
                return this.$_setFlag("result", e ? "raw" : void 0);
              }
            }, {
              key: "result",
              value: function result(e) {
                return s(["raw", "strip"].includes(e), "Unknown result mode", e), this.$_setFlag("result", e);
              }
            }, {
              key: "required",
              value: function required() {
                return this.presence("required");
              }
            }, {
              key: "strict",
              value: function strict(e) {
                var t = this.clone(),
                  r = void 0 !== e && !e;
                return t._preferences = l.preferences(t._preferences, {
                  convert: r
                }), t;
              }
            }, {
              key: "strip",
              value: function strip() {
                var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : !0;
                return this.$_setFlag("result", e ? "strip" : void 0);
              }
            }, {
              key: "tag",
              value: function tag() {
                for (var _len4 = arguments.length, e = new Array(_len4), _key4 = 0; _key4 < _len4; _key4++) {
                  e[_key4] = arguments[_key4];
                }
                s(e.length, "Missing tags");
                for (var _i2 = 0, _e2 = e; _i2 < _e2.length; _i2++) {
                  var _t2 = _e2[_i2];
                  s(_t2 && "string" == typeof _t2, "Tags must be non-empty strings");
                }
                return this._inner("tags", e);
              }
            }, {
              key: "unit",
              value: function unit(e) {
                return s(e && "string" == typeof e, "Unit name must be a non-empty string"), this.$_setFlag("unit", e);
              }
            }, {
              key: "valid",
              value: function valid() {
                for (var _len5 = arguments.length, e = new Array(_len5), _key5 = 0; _key5 < _len5; _key5++) {
                  e[_key5] = arguments[_key5];
                }
                l.verifyFlat(e, "valid");
                var t = this.allow.apply(this, e);
                return t.$_setFlag("only", !!t._valids, {
                  clone: !1
                }), t;
              }
            }, {
              key: "when",
              value: function when(e, t) {
                var r = this.clone();
                r.$_terms.whens || (r.$_terms.whens = []);
                var n = c.when(r, e, t);
                if (!["any", "link"].includes(r.type)) {
                  var _e3 = n.is ? [n] : n["switch"];
                  var _iterator = _createForOfIteratorHelper(_e3),
                    _step;
                  try {
                    for (_iterator.s(); !(_step = _iterator.n()).done;) {
                      var _t3 = _step.value;
                      s(!_t3.then || "any" === _t3.then.type || _t3.then.type === r.type, "Cannot combine", r.type, "with", _t3.then && _t3.then.type), s(!_t3.otherwise || "any" === _t3.otherwise.type || _t3.otherwise.type === r.type, "Cannot combine", r.type, "with", _t3.otherwise && _t3.otherwise.type);
                    }
                  } catch (err) {
                    _iterator.e(err);
                  } finally {
                    _iterator.f();
                  }
                }
                return r.$_terms.whens.push(n), r.$_mutateRebuild();
              }
            }, {
              key: "cache",
              value: function cache(e) {
                s(!this._inRuleset(), "Cannot set caching inside a ruleset"), s(!this._cache, "Cannot override schema cache"), s(void 0 === this._flags.artifact, "Cannot cache a rule with an artifact");
                var t = this.clone();
                return t._cache = e || o.provider.provision(), t.$_temp.ruleset = !1, t;
              }
            }, {
              key: "clone",
              value: function clone() {
                var e = Object.create(Object.getPrototypeOf(this));
                return this._assign(e);
              }
            }, {
              key: "concat",
              value: function concat(e) {
                s(l.isSchema(e), "Invalid schema object"), s("any" === this.type || "any" === e.type || e.type === this.type, "Cannot merge type", this.type, "with another type:", e.type), s(!this._inRuleset(), "Cannot concatenate onto a schema with open ruleset"), s(!e._inRuleset(), "Cannot concatenate a schema with open ruleset");
                var t = this.clone();
                if ("any" === this.type && "any" !== e.type) {
                  var _r2 = e.clone();
                  for (var _i3 = 0, _Object$keys = Object.keys(t); _i3 < _Object$keys.length; _i3++) {
                    var _e4 = _Object$keys[_i3];
                    "type" !== _e4 && (_r2[_e4] = t[_e4]);
                  }
                  t = _r2;
                }
                t._ids.concat(e._ids), t._refs.register(e, p.toSibling), t._preferences = t._preferences ? l.preferences(t._preferences, e._preferences) : e._preferences, t._valids = b.merge(t._valids, e._valids, e._invalids), t._invalids = b.merge(t._invalids, e._invalids, e._valids);
                var _iterator2 = _createForOfIteratorHelper(e._singleRules.keys()),
                  _step2;
                try {
                  var _loop = function _loop() {
                    var r = _step2.value;
                    t._singleRules.has(r) && (t._rules = t._rules.filter(function (e) {
                      return e.keep || e.name !== r;
                    }), t._singleRules["delete"](r));
                  };
                  for (_iterator2.s(); !(_step2 = _iterator2.n()).done;) {
                    _loop();
                  }
                } catch (err) {
                  _iterator2.e(err);
                } finally {
                  _iterator2.f();
                }
                var _iterator3 = _createForOfIteratorHelper(e._rules),
                  _step3;
                try {
                  for (_iterator3.s(); !(_step3 = _iterator3.n()).done;) {
                    var _r6 = _step3.value;
                    e._definition.rules[_r6.method].multi || t._singleRules.set(_r6.name, _r6), t._rules.push(_r6);
                  }
                } catch (err) {
                  _iterator3.e(err);
                } finally {
                  _iterator3.f();
                }
                if (t._flags.empty && e._flags.empty) {
                  t._flags.empty = t._flags.empty.concat(e._flags.empty);
                  var _r3 = Object.assign({}, e._flags);
                  delete _r3.empty, i(t._flags, _r3);
                } else if (e._flags.empty) {
                  t._flags.empty = e._flags.empty;
                  var _r4 = Object.assign({}, e._flags);
                  delete _r4.empty, i(t._flags, _r4);
                } else i(t._flags, e._flags);
                for (var _r5 in e.$_terms) {
                  var _s = e.$_terms[_r5];
                  _s ? t.$_terms[_r5] ? t.$_terms[_r5] = t.$_terms[_r5].concat(_s) : t.$_terms[_r5] = _s.slice() : t.$_terms[_r5] || (t.$_terms[_r5] = _s);
                }
                return this.$_root._tracer && this.$_root._tracer._combine(t, [this, e]), t.$_mutateRebuild();
              }
            }, {
              key: "extend",
              value: function extend(e) {
                return s(!e.base, "Cannot extend type with another base"), f.type(this, e);
              }
            }, {
              key: "extract",
              value: function extract(e) {
                return e = Array.isArray(e) ? e : e.split("."), this._ids.reach(e);
              }
            }, {
              key: "fork",
              value: function fork(e, t) {
                s(!this._inRuleset(), "Cannot fork inside a ruleset");
                var r = this;
                var _iterator4 = _createForOfIteratorHelper([].concat(e)),
                  _step4;
                try {
                  for (_iterator4.s(); !(_step4 = _iterator4.n()).done;) {
                    var _s2 = _step4.value;
                    _s2 = Array.isArray(_s2) ? _s2 : _s2.split("."), r = r._ids.fork(_s2, t, r);
                  }
                } catch (err) {
                  _iterator4.e(err);
                } finally {
                  _iterator4.f();
                }
                return r.$_temp.ruleset = !1, r;
              }
            }, {
              key: "rule",
              value: function rule(e) {
                var t = this._definition;
                l.assertOptions(e, Object.keys(t.modifiers)), s(!1 !== this.$_temp.ruleset, "Cannot apply rules to empty ruleset or the last rule added does not support rule properties");
                var r = null === this.$_temp.ruleset ? this._rules.length - 1 : this.$_temp.ruleset;
                s(r >= 0 && r < this._rules.length, "Cannot apply rules to empty ruleset");
                var a = this.clone();
                for (var _i4 = r; _i4 < a._rules.length; ++_i4) {
                  var _r7 = a._rules[_i4],
                    _o = n(_r7);
                  for (var _n2 in e) t.modifiers[_n2](_o, e[_n2]), s(_o.name === _r7.name, "Cannot change rule name");
                  a._rules[_i4] = _o, a._singleRules.get(_o.name) === _r7 && a._singleRules.set(_o.name, _o);
                }
                return a.$_temp.ruleset = !1, a.$_mutateRebuild();
              }
            }, {
              key: "ruleset",
              get: function get() {
                s(!this._inRuleset(), "Cannot start a new ruleset without closing the previous one");
                var e = this.clone();
                return e.$_temp.ruleset = e._rules.length, e;
              }
            }, {
              key: "$",
              get: function get() {
                return this.ruleset;
              }
            }, {
              key: "tailor",
              value: function tailor(e) {
                e = [].concat(e), s(!this._inRuleset(), "Cannot tailor inside a ruleset");
                var t = this;
                if (this.$_terms.alterations) {
                  var _iterator5 = _createForOfIteratorHelper(this.$_terms.alterations),
                    _step5;
                  try {
                    for (_iterator5.s(); !(_step5 = _iterator5.n()).done;) {
                      var _step5$value = _step5.value,
                        _r8 = _step5$value.target,
                        _n3 = _step5$value.adjuster;
                      e.includes(_r8) && (t = _n3(t), s(l.isSchema(t), "Alteration adjuster for", _r8, "failed to return a schema object"));
                    }
                  } catch (err) {
                    _iterator5.e(err);
                  } finally {
                    _iterator5.f();
                  }
                }
                return t = t.$_modify({
                  each: function each(t) {
                    return t.tailor(e);
                  },
                  ref: !1
                }), t.$_temp.ruleset = !1, t.$_mutateRebuild();
              }
            }, {
              key: "tracer",
              value: function tracer() {
                return g.location ? g.location(this) : this;
              }
            }, {
              key: "validate",
              value: function validate(e, t) {
                return y.entry(e, this, t);
              }
            }, {
              key: "validateAsync",
              value: function validateAsync(e, t) {
                return y.entryAsync(e, this, t);
              }
            }, {
              key: "$_addRule",
              value: function $_addRule(e) {
                "string" == typeof e && (e = {
                  name: e
                }), s(e && "object" == _typeof(e), "Invalid options"), s(e.name && "string" == typeof e.name, "Invalid rule name");
                for (var _t4 in e) s("_" !== _t4[0], "Cannot set private rule properties");
                var t = Object.assign({}, e);
                t._resolve = [], t.method = t.method || t.name;
                var r = this._definition.rules[t.method],
                  n = t.args;
                s(r, "Unknown rule", t.method);
                var a = this.clone();
                if (n) {
                  s(1 === Object.keys(n).length || Object.keys(n).length === this._definition.rules[t.name].args.length, "Invalid rule definition for", this.type, t.name);
                  for (var _e5 in n) {
                    var _i5 = n[_e5];
                    if (r.argsByName) {
                      var _o2 = r.argsByName.get(_e5);
                      if (_o2.ref && l.isResolvable(_i5)) t._resolve.push(_e5), a.$_mutateRegister(_i5);else if (_o2.normalize && (_i5 = _o2.normalize(_i5), n[_e5] = _i5), _o2.assert) {
                        var _t5 = l.validateArg(_i5, _e5, _o2);
                        s(!_t5, _t5, "or reference");
                      }
                    }
                    void 0 !== _i5 ? n[_e5] = _i5 : delete n[_e5];
                  }
                }
                return r.multi || (a._ruleRemove(t.name, {
                  clone: !1
                }), a._singleRules.set(t.name, t)), !1 === a.$_temp.ruleset && (a.$_temp.ruleset = null), r.priority ? a._rules.unshift(t) : a._rules.push(t), a;
              }
            }, {
              key: "$_compile",
              value: function $_compile(e, t) {
                return c.schema(this.$_root, e, t);
              }
            }, {
              key: "$_createError",
              value: function $_createError(e, t, r, s, n) {
                var a = arguments.length > 5 && arguments[5] !== undefined ? arguments[5] : {};
                var i = !1 !== a.flags ? this._flags : {},
                  o = a.messages ? h.merge(this._definition.messages, a.messages) : this._definition.messages;
                return new u.Report(e, t, r, i, o, s, n);
              }
            }, {
              key: "$_getFlag",
              value: function $_getFlag(e) {
                return this._flags[e];
              }
            }, {
              key: "$_getRule",
              value: function $_getRule(e) {
                return this._singleRules.get(e);
              }
            }, {
              key: "$_mapLabels",
              value: function $_mapLabels(e) {
                return e = Array.isArray(e) ? e : e.split("."), this._ids.labels(e);
              }
            }, {
              key: "$_match",
              value: function $_match(e, t, r, s) {
                (r = Object.assign({}, r)).abortEarly = !0, r._externals = !1, t.snapshot();
                var n = !y.validate(e, this, t, r, s).errors;
                return t.restore(), n;
              }
            }, {
              key: "$_modify",
              value: function $_modify(e) {
                return l.assertOptions(e, ["each", "once", "ref", "schema"]), d.schema(this, e) || this;
              }
            }, {
              key: "$_mutateRebuild",
              value: function $_mutateRebuild() {
                var _this = this;
                return s(!this._inRuleset(), "Cannot add this rule inside a ruleset"), this._refs.reset(), this._ids.reset(), this.$_modify({
                  each: function each(e, _ref) {
                    var t = _ref.source,
                      r = _ref.name,
                      s = _ref.path,
                      n = _ref.key;
                    var a = _this._definition[t][r] && _this._definition[t][r].register;
                    !1 !== a && _this.$_mutateRegister(e, {
                      family: a,
                      key: n
                    });
                  }
                }), this._definition.rebuild && this._definition.rebuild(this), this.$_temp.ruleset = !1, this;
              }
            }, {
              key: "$_mutateRegister",
              value: function $_mutateRegister(e) {
                var _ref2 = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {},
                  t = _ref2.family,
                  r = _ref2.key;
                this._refs.register(e, t), this._ids.register(e, {
                  key: r
                });
              }
            }, {
              key: "$_property",
              value: function $_property(e) {
                return this._definition.properties[e];
              }
            }, {
              key: "$_reach",
              value: function $_reach(e) {
                return this._ids.reach(e);
              }
            }, {
              key: "$_rootReferences",
              value: function $_rootReferences() {
                return this._refs.roots();
              }
            }, {
              key: "$_setFlag",
              value: function $_setFlag(e, t) {
                var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
                s("_" === e[0] || !this._inRuleset(), "Cannot set flag inside a ruleset");
                var n = this._definition.flags[e] || {};
                if (a(t, n["default"]) && (t = void 0), a(t, this._flags[e])) return this;
                var i = !1 !== r.clone ? this.clone() : this;
                return void 0 !== t ? (i._flags[e] = t, i.$_mutateRegister(t)) : delete i._flags[e], "_" !== e[0] && (i.$_temp.ruleset = !1), i;
              }
            }, {
              key: "$_parent",
              value: function $_parent(e) {
                var _this$e$l$symbols$par;
                for (var _len6 = arguments.length, t = new Array(_len6 > 1 ? _len6 - 1 : 0), _key6 = 1; _key6 < _len6; _key6++) {
                  t[_key6 - 1] = arguments[_key6];
                }
                return (_this$e$l$symbols$par = this[e][l.symbols.parent]).call.apply(_this$e$l$symbols$par, [this].concat(t));
              }
            }, {
              key: "$_validate",
              value: function $_validate(e, t, r) {
                return y.validate(e, this, t, r);
              }
            }, {
              key: "_assign",
              value: function _assign(e) {
                e.type = this.type, e.$_root = this.$_root, e.$_temp = Object.assign({}, this.$_temp), e.$_temp.whens = {}, e._ids = this._ids.clone(), e._preferences = this._preferences, e._valids = this._valids && this._valids.clone(), e._invalids = this._invalids && this._invalids.clone(), e._rules = this._rules.slice(), e._singleRules = n(this._singleRules, {
                  shallow: !0
                }), e._refs = this._refs.clone(), e._flags = Object.assign({}, this._flags), e._cache = null, e.$_terms = {};
                for (var _t6 in this.$_terms) e.$_terms[_t6] = this.$_terms[_t6] ? this.$_terms[_t6].slice() : null;
                e.$_super = {};
                for (var _t7 in this.$_super) e.$_super[_t7] = this._super[_t7].bind(e);
                return e;
              }
            }, {
              key: "_bare",
              value: function _bare() {
                var e = this.clone();
                e._reset();
                var t = e._definition.terms;
                for (var _r9 in t) {
                  var _s3 = t[_r9];
                  e.$_terms[_r9] = _s3.init;
                }
                return e.$_mutateRebuild();
              }
            }, {
              key: "_default",
              value: function _default(e, t) {
                var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
                return l.assertOptions(r, "literal"), s(void 0 !== t, "Missing", e, "value"), s("function" == typeof t || !r.literal, "Only function value supports literal option"), "function" == typeof t && r.literal && (t = _defineProperty(_defineProperty({}, l.symbols.literal, !0), "literal", t)), this.$_setFlag(e, t);
              }
            }, {
              key: "_generate",
              value: function _generate(e, t, r) {
                if (!this.$_terms.whens) return {
                  schema: this
                };
                var s = [],
                  n = [];
                for (var _a = 0; _a < this.$_terms.whens.length; ++_a) {
                  var _i6 = this.$_terms.whens[_a];
                  if (_i6.concat) {
                    s.push(_i6.concat), n.push("".concat(_a, ".concat"));
                    continue;
                  }
                  var _o3 = _i6.ref ? _i6.ref.resolve(e, t, r) : e,
                    _l = _i6.is ? [_i6] : _i6["switch"],
                    _c = n.length;
                  for (var _c2 = 0; _c2 < _l.length; ++_c2) {
                    var _l$_c = _l[_c2],
                      _u = _l$_c.is,
                      _f = _l$_c.then,
                      _m = _l$_c.otherwise,
                      _h = "".concat(_a).concat(_i6["switch"] ? "." + _c2 : "");
                    if (_u.$_match(_o3, t.nest(_u, "".concat(_h, ".is")), r)) {
                      if (_f) {
                        var _a2 = t.localize([].concat(_toConsumableArray(t.path), ["".concat(_h, ".then")]), t.ancestors, t.schemas),
                          _f$_generate = _f._generate(e, _a2, r),
                          _i7 = _f$_generate.schema,
                          _o4 = _f$_generate.id;
                        s.push(_i7), n.push("".concat(_h, ".then").concat(_o4 ? "(".concat(_o4, ")") : ""));
                        break;
                      }
                    } else if (_m) {
                      var _a3 = t.localize([].concat(_toConsumableArray(t.path), ["".concat(_h, ".otherwise")]), t.ancestors, t.schemas),
                        _m$_generate = _m._generate(e, _a3, r),
                        _i8 = _m$_generate.schema,
                        _o5 = _m$_generate.id;
                      s.push(_i8), n.push("".concat(_h, ".otherwise").concat(_o5 ? "(".concat(_o5, ")") : ""));
                      break;
                    }
                  }
                  if (_i6["break"] && n.length > _c) break;
                }
                var a = n.join(", ");
                if (t.mainstay.tracer.debug(t, "rule", "when", a), !a) return {
                  schema: this
                };
                if (!t.mainstay.tracer.active && this.$_temp.whens[a]) return {
                  schema: this.$_temp.whens[a],
                  id: a
                };
                var i = this;
                this._definition.generate && (i = this._definition.generate(this, e, t, r));
                for (var _i9 = 0, _s4 = s; _i9 < _s4.length; _i9++) {
                  var _e6 = _s4[_i9];
                  i = i.concat(_e6);
                }
                return this.$_root._tracer && this.$_root._tracer._combine(i, [this].concat(s)), this.$_temp.whens[a] = i, {
                  schema: i,
                  id: a
                };
              }
            }, {
              key: "_inner",
              value: function _inner(e, t) {
                var _n$$_terms$e;
                var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
                s(!this._inRuleset(), "Cannot set ".concat(e, " inside a ruleset"));
                var n = this.clone();
                return n.$_terms[e] && !r.override || (n.$_terms[e] = []), r.single ? n.$_terms[e].push(t) : (_n$$_terms$e = n.$_terms[e]).push.apply(_n$$_terms$e, _toConsumableArray(t)), n.$_temp.ruleset = !1, n;
              }
            }, {
              key: "_inRuleset",
              value: function _inRuleset() {
                return null !== this.$_temp.ruleset && !1 !== this.$_temp.ruleset;
              }
            }, {
              key: "_ruleRemove",
              value: function _ruleRemove(e) {
                var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
                if (!this._singleRules.has(e)) return this;
                var r = !1 !== t.clone ? this.clone() : this;
                r._singleRules["delete"](e);
                var s = [];
                for (var _t9 = 0; _t9 < r._rules.length; ++_t9) {
                  var _n4 = r._rules[_t9];
                  _n4.name !== e || _n4.keep ? s.push(_n4) : r._inRuleset() && _t9 < r.$_temp.ruleset && --r.$_temp.ruleset;
                }
                return r._rules = s, r;
              }
            }, {
              key: "_values",
              value: function _values(e, t) {
                l.verifyFlat(e, t.slice(1, -1));
                var r = this.clone(),
                  n = e[0] === l.symbols.override;
                if (n && (e = e.slice(1)), !r[t] && e.length ? r[t] = new b() : n && (r[t] = e.length ? new b() : null, r.$_mutateRebuild()), !r[t]) return r;
                n && r[t].override();
                var _iterator6 = _createForOfIteratorHelper(e),
                  _step6;
                try {
                  for (_iterator6.s(); !(_step6 = _iterator6.n()).done;) {
                    var _n5 = _step6.value;
                    s(void 0 !== _n5, "Cannot call allow/valid/invalid with undefined"), s(_n5 !== l.symbols.override, "Override must be the first value");
                    var _e7 = "_invalids" === t ? "_valids" : "_invalids";
                    r[_e7] && (r[_e7].remove(_n5), r[_e7].length || (s("_valids" === t || !r._flags.only, "Setting invalid value", _n5, "leaves schema rejecting all values due to previous valid rule"), r[_e7] = null)), r[t].add(_n5, r._refs);
                  }
                } catch (err) {
                  _iterator6.e(err);
                } finally {
                  _iterator6.f();
                }
                return r;
              }
            }]);
          }()
        };
      v.Base.prototype[l.symbols.any] = {
        version: l.version,
        compile: c.compile,
        root: "$_root"
      }, v.Base.prototype.isImmutable = !0, v.Base.prototype.deny = v.Base.prototype.invalid, v.Base.prototype.disallow = v.Base.prototype.invalid, v.Base.prototype.equal = v.Base.prototype.valid, v.Base.prototype.exist = v.Base.prototype.required, v.Base.prototype.not = v.Base.prototype.invalid, v.Base.prototype.options = v.Base.prototype.prefs, v.Base.prototype.preferences = v.Base.prototype.prefs, e.exports = new v.Base();
    },
    8652: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8571),
        a = r(8160),
        i = {
          max: 1e3,
          supported: new Set(["undefined", "boolean", "number", "string"])
        };
      t.provider = {
        provision: function provision(e) {
          return new i.Cache(e);
        }
      }, i.Cache = /*#__PURE__*/function () {
        function _class() {
          var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
          _classCallCheck(this, _class);
          a.assertOptions(e, ["max"]), s(void 0 === e.max || e.max && e.max > 0 && isFinite(e.max), "Invalid max cache size"), this._max = e.max || i.max, this._map = new Map(), this._list = new i.List();
        }
        return _createClass(_class, [{
          key: "length",
          get: function get() {
            return this._map.size;
          }
        }, {
          key: "set",
          value: function set(e, t) {
            if (null !== e && !i.supported.has(_typeof(e))) return;
            var r = this._map.get(e);
            if (r) return r.value = t, void this._list.first(r);
            r = this._list.unshift({
              key: e,
              value: t
            }), this._map.set(e, r), this._compact();
          }
        }, {
          key: "get",
          value: function get(e) {
            var t = this._map.get(e);
            if (t) return this._list.first(t), n(t.value);
          }
        }, {
          key: "_compact",
          value: function _compact() {
            if (this._map.size > this._max) {
              var _e8 = this._list.pop();
              this._map["delete"](_e8.key);
            }
          }
        }]);
      }(), i.List = /*#__PURE__*/function () {
        function _class2() {
          _classCallCheck(this, _class2);
          this.tail = null, this.head = null;
        }
        return _createClass(_class2, [{
          key: "unshift",
          value: function unshift(e) {
            return e.next = null, e.prev = this.head, this.head && (this.head.next = e), this.head = e, this.tail || (this.tail = e), e;
          }
        }, {
          key: "first",
          value: function first(e) {
            e !== this.head && (this._remove(e), this.unshift(e));
          }
        }, {
          key: "pop",
          value: function pop() {
            return this._remove(this.tail);
          }
        }, {
          key: "_remove",
          value: function _remove(e) {
            var t = e.next,
              r = e.prev;
            return t.prev = r, r && (r.next = t), e === this.tail && (this.tail = t), e.prev = null, e.next = null, e;
          }
        }]);
      }();
    },
    8160: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(7916),
        a = r(5934);
      var i, o;
      var l = {
        isoDate: /^(?:[-+]\d{2})?(?:\d{4}(?!\d{2}\b))(?:(-?)(?:(?:0[1-9]|1[0-2])(?:\1(?:[12]\d|0[1-9]|3[01]))?|W(?:[0-4]\d|5[0-2])(?:-?[1-7])?|(?:00[1-9]|0[1-9]\d|[12]\d{2}|3(?:[0-5]\d|6[1-6])))(?![T]$|[T][\d]+Z$)(?:[T\s](?:(?:(?:[01]\d|2[0-3])(?:(:?)[0-5]\d)?|24\:?00)(?:[.,]\d+(?!:))?)(?:\2[0-5]\d(?:[.,]\d+)?)?(?:[Z]|(?:[+-])(?:[01]\d|2[0-3])(?::?[0-5]\d)?)?)?)?$/
      };
      t.version = a.version, t.defaults = {
        abortEarly: !0,
        allowUnknown: !1,
        artifacts: !1,
        cache: !0,
        context: null,
        convert: !0,
        dateFormat: "iso",
        errors: {
          escapeHtml: !1,
          label: "path",
          language: null,
          render: !0,
          stack: !1,
          wrap: {
            label: '"',
            array: "[]"
          }
        },
        externals: !0,
        messages: {},
        nonEnumerables: !1,
        noDefaults: !1,
        presence: "optional",
        skipFunctions: !1,
        stripUnknown: !1,
        warnings: !1
      }, t.symbols = {
        any: Symbol["for"]("@hapi/joi/schema"),
        arraySingle: Symbol("arraySingle"),
        deepDefault: Symbol("deepDefault"),
        errors: Symbol("errors"),
        literal: Symbol("literal"),
        override: Symbol("override"),
        parent: Symbol("parent"),
        prefs: Symbol("prefs"),
        ref: Symbol("ref"),
        template: Symbol("template"),
        values: Symbol("values")
      }, t.assertOptions = function (e, t) {
        var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : "Options";
        s(e && "object" == _typeof(e) && !Array.isArray(e), "Options must be of type object");
        var n = Object.keys(e).filter(function (e) {
          return !t.includes(e);
        });
        s(0 === n.length, "".concat(r, " contain unknown keys: ").concat(n));
      }, t.checkPreferences = function (e) {
        o = o || r(3378);
        var t = o.preferences.validate(e);
        if (t.error) throw new n([t.error.details[0].message]);
      }, t.compare = function (e, t, r) {
        switch (r) {
          case "=":
            return e === t;
          case ">":
            return e > t;
          case "<":
            return e < t;
          case ">=":
            return e >= t;
          case "<=":
            return e <= t;
        }
      }, t["default"] = function (e, t) {
        return void 0 === e ? t : e;
      }, t.isIsoDate = function (e) {
        return l.isoDate.test(e);
      }, t.isNumber = function (e) {
        return "number" == typeof e && !isNaN(e);
      }, t.isResolvable = function (e) {
        return !!e && (e[t.symbols.ref] || e[t.symbols.template]);
      }, t.isSchema = function (e) {
        var r = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
        var n = e && e[t.symbols.any];
        return !!n && (s(r.legacy || n.version === t.version, "Cannot mix different versions of joi schemas"), !0);
      }, t.isValues = function (e) {
        return e[t.symbols.values];
      }, t.limit = function (e) {
        return Number.isSafeInteger(e) && e >= 0;
      }, t.preferences = function (e, s) {
        i = i || r(6914), e = e || {}, s = s || {};
        var n = Object.assign({}, e, s);
        return s.errors && e.errors && (n.errors = Object.assign({}, e.errors, s.errors), n.errors.wrap = Object.assign({}, e.errors.wrap, s.errors.wrap)), s.messages && (n.messages = i.compile(s.messages, e.messages)), delete n[t.symbols.prefs], n;
      }, t.tryWithPath = function (e, t) {
        var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
        try {
          return e();
        } catch (e) {
          throw void 0 !== e.path ? e.path = t + "." + e.path : e.path = t, r.append && (e.message = "".concat(e.message, " (").concat(e.path, ")")), e;
        }
      }, t.validateArg = function (e, r, _ref3) {
        var s = _ref3.assert,
          n = _ref3.message;
        if (t.isSchema(s)) {
          var _t0 = s.validate(e);
          if (!_t0.error) return;
          return _t0.error.message;
        }
        if (!s(e)) return r ? "".concat(r, " ").concat(n) : n;
      }, t.verifyFlat = function (e, t) {
        var _iterator7 = _createForOfIteratorHelper(e),
          _step7;
        try {
          for (_iterator7.s(); !(_step7 = _iterator7.n()).done;) {
            var _r0 = _step7.value;
            s(!Array.isArray(_r0), "Method no longer accepts array arguments:", t);
          }
        } catch (err) {
          _iterator7.e(err);
        } finally {
          _iterator7.f();
        }
      };
    },
    3292: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8160),
        a = r(6133),
        i = {};
      t.schema = function (e, t) {
        var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
        n.assertOptions(r, ["appendPath", "override"]);
        try {
          return i.schema(e, t, r);
        } catch (e) {
          throw r.appendPath && void 0 !== e.path && (e.message = "".concat(e.message, " (").concat(e.path, ")")), e;
        }
      }, i.schema = function (e, t, r) {
        s(void 0 !== t, "Invalid undefined schema"), Array.isArray(t) && (s(t.length, "Invalid empty array schema"), 1 === t.length && (t = t[0]));
        var a = function a(t) {
          for (var _len7 = arguments.length, s = new Array(_len7 > 1 ? _len7 - 1 : 0), _key7 = 1; _key7 < _len7; _key7++) {
            s[_key7 - 1] = arguments[_key7];
          }
          return !1 !== r.override ? t.valid.apply(t, [e.override].concat(s)) : t.valid.apply(t, s);
        };
        if (i.simple(t)) return a(e, t);
        if ("function" == typeof t) return e.custom(t);
        if (s("object" == _typeof(t), "Invalid schema content:", _typeof(t)), n.isResolvable(t)) return a(e, t);
        if (n.isSchema(t)) return t;
        if (Array.isArray(t)) {
          var _iterator8 = _createForOfIteratorHelper(t),
            _step8;
          try {
            for (_iterator8.s(); !(_step8 = _iterator8.n()).done;) {
              var _e$alternatives;
              var _r1 = _step8.value;
              if (!i.simple(_r1)) return (_e$alternatives = e.alternatives())["try"].apply(_e$alternatives, _toConsumableArray(t));
            }
          } catch (err) {
            _iterator8.e(err);
          } finally {
            _iterator8.f();
          }
          return a.apply(void 0, [e].concat(_toConsumableArray(t)));
        }
        return t instanceof RegExp ? e.string().regex(t) : t instanceof Date ? a(e.date(), t) : (s(Object.getPrototypeOf(t) === Object.getPrototypeOf({}), "Schema can only contain plain objects"), e.object().keys(t));
      }, t.ref = function (e, t) {
        return a.isRef(e) ? e : a.create(e, t);
      }, t.compile = function (e, r) {
        var a = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
        n.assertOptions(a, ["legacy"]);
        var o = r && r[n.symbols.any];
        if (o) return s(a.legacy || o.version === n.version, "Cannot mix different versions of joi schemas:", o.version, n.version), r;
        if ("object" != _typeof(r) || !a.legacy) return t.schema(e, r, {
          appendPath: !0
        });
        var l = i.walk(r);
        return l ? l.compile(l.root, r) : t.schema(e, r, {
          appendPath: !0
        });
      }, i.walk = function (e) {
        if ("object" != _typeof(e)) return null;
        if (Array.isArray(e)) {
          var _iterator9 = _createForOfIteratorHelper(e),
            _step9;
          try {
            for (_iterator9.s(); !(_step9 = _iterator9.n()).done;) {
              var _t1 = _step9.value;
              var _e9 = i.walk(_t1);
              if (_e9) return _e9;
            }
          } catch (err) {
            _iterator9.e(err);
          } finally {
            _iterator9.f();
          }
          return null;
        }
        var t = e[n.symbols.any];
        if (t) return {
          root: e[t.root],
          compile: t.compile
        };
        s(Object.getPrototypeOf(e) === Object.getPrototypeOf({}), "Schema can only contain plain objects");
        for (var _t10 in e) {
          var _r10 = i.walk(e[_t10]);
          if (_r10) return _r10;
        }
        return null;
      }, i.simple = function (e) {
        return null === e || ["boolean", "string", "number"].includes(_typeof(e));
      }, t.when = function (e, r, o) {
        if (void 0 === o && (s(r && "object" == _typeof(r), "Missing options"), o = r, r = a.create(".")), Array.isArray(o) && (o = {
          "switch": o
        }), n.assertOptions(o, ["is", "not", "then", "otherwise", "switch", "break"]), n.isSchema(r)) return s(void 0 === o.is, '"is" can not be used with a schema condition'), s(void 0 === o.not, '"not" can not be used with a schema condition'), s(void 0 === o["switch"], '"switch" can not be used with a schema condition'), i.condition(e, {
          is: r,
          then: o.then,
          otherwise: o.otherwise,
          "break": o["break"]
        });
        if (s(a.isRef(r) || "string" == typeof r, "Invalid condition:", r), s(void 0 === o.not || void 0 === o.is, 'Cannot combine "is" with "not"'), void 0 === o["switch"]) {
          var _l2 = o;
          void 0 !== o.not && (_l2 = {
            is: o.not,
            then: o.otherwise,
            otherwise: o.then,
            "break": o["break"]
          });
          var c = void 0 !== _l2.is ? e.$_compile(_l2.is) : e.$_root.invalid(null, !1, 0, "").required();
          return s(void 0 !== _l2.then || void 0 !== _l2.otherwise, 'options must have at least one of "then", "otherwise", or "switch"'), s(void 0 === _l2["break"] || void 0 === _l2.then || void 0 === _l2.otherwise, "Cannot specify then, otherwise, and break all together"), void 0 === o.is || a.isRef(o.is) || n.isSchema(o.is) || (c = c.required()), i.condition(e, {
            ref: t.ref(r),
            is: c,
            then: _l2.then,
            otherwise: _l2.otherwise,
            "break": _l2["break"]
          });
        }
        s(Array.isArray(o["switch"]), '"switch" must be an array'), s(void 0 === o.is, 'Cannot combine "switch" with "is"'), s(void 0 === o.not, 'Cannot combine "switch" with "not"'), s(void 0 === o.then, 'Cannot combine "switch" with "then"');
        var l = {
          ref: t.ref(r),
          "switch": [],
          "break": o["break"]
        };
        for (var _t11 = 0; _t11 < o["switch"].length; ++_t11) {
          var _r11 = o["switch"][_t11],
            _i0 = _t11 === o["switch"].length - 1;
          n.assertOptions(_r11, _i0 ? ["is", "then", "otherwise"] : ["is", "then"]), s(void 0 !== _r11.is, 'Switch statement missing "is"'), s(void 0 !== _r11.then, 'Switch statement missing "then"');
          var _c3 = {
            is: e.$_compile(_r11.is),
            then: e.$_compile(_r11.then)
          };
          if (a.isRef(_r11.is) || n.isSchema(_r11.is) || (_c3.is = _c3.is.required()), _i0) {
            s(void 0 === o.otherwise || void 0 === _r11.otherwise, 'Cannot specify "otherwise" inside and outside a "switch"');
            var _t12 = void 0 !== o.otherwise ? o.otherwise : _r11.otherwise;
            void 0 !== _t12 && (s(void 0 === l["break"], "Cannot specify both otherwise and break"), _c3.otherwise = e.$_compile(_t12));
          }
          l["switch"].push(_c3);
        }
        return l;
      }, i.condition = function (e, t) {
        for (var _i1 = 0, _arr = ["then", "otherwise"]; _i1 < _arr.length; _i1++) {
          var _r12 = _arr[_i1];
          void 0 === t[_r12] ? delete t[_r12] : t[_r12] = e.$_compile(t[_r12]);
        }
        return t;
      };
    },
    6354: function _(e, t, r) {
      "use strict";

      var s = r(5688),
        n = r(8160),
        a = r(3328);
      t.Report = /*#__PURE__*/function () {
        function _class3(e, r, s, n, a, i, o) {
          _classCallCheck(this, _class3);
          if (this.code = e, this.flags = n, this.messages = a, this.path = i.path, this.prefs = o, this.state = i, this.value = r, this.message = null, this.template = null, this.local = s || {}, this.local.label = t.label(this.flags, this.state, this.prefs, this.messages), void 0 === this.value || this.local.hasOwnProperty("value") || (this.local.value = this.value), this.path.length) {
            var _e0 = this.path[this.path.length - 1];
            "object" != _typeof(_e0) && (this.local.key = _e0);
          }
        }
        return _createClass(_class3, [{
          key: "_setTemplate",
          value: function _setTemplate(e) {
            if (this.template = e, !this.flags.label && 0 === this.path.length) {
              var _e1 = this._template(this.template, "root");
              _e1 && (this.local.label = _e1);
            }
          }
        }, {
          key: "toString",
          value: function toString() {
            if (this.message) return this.message;
            var e = this.code;
            if (!this.prefs.errors.render) return this.code;
            var t = this._template(this.template) || this._template(this.prefs.messages) || this._template(this.messages);
            return void 0 === t ? "Error code \"".concat(e, "\" is not defined, your custom type is missing the correct messages definition") : (this.message = t.render(this.value, this.state, this.prefs, this.local, {
              errors: this.prefs.errors,
              messages: [this.prefs.messages, this.messages]
            }), this.prefs.errors.label || (this.message = this.message.replace(/^"" /, "").trim()), this.message);
          }
        }, {
          key: "_template",
          value: function _template(e, r) {
            return t.template(this.value, e, r || this.code, this.state, this.prefs);
          }
        }]);
      }(), t.path = function (e) {
        var t = "";
        var _iterator0 = _createForOfIteratorHelper(e),
          _step0;
        try {
          for (_iterator0.s(); !(_step0 = _iterator0.n()).done;) {
            var _r13 = _step0.value;
            "object" != _typeof(_r13) && ("string" == typeof _r13 ? (t && (t += "."), t += _r13) : t += "[".concat(_r13, "]"));
          }
        } catch (err) {
          _iterator0.e(err);
        } finally {
          _iterator0.f();
        }
        return t;
      }, t.template = function (e, t, r, s, i) {
        if (!t) return;
        if (a.isTemplate(t)) return "root" !== r ? t : null;
        var o = i.errors.language;
        if (n.isResolvable(o) && (o = o.resolve(e, s, i)), o && t[o]) {
          if (void 0 !== t[o][r]) return t[o][r];
          if (void 0 !== t[o]["*"]) return t[o]["*"];
        }
        return t[r] ? t[r] : t["*"];
      }, t.label = function (e, r, s, n) {
        if (!s.errors.label) return "";
        if (e.label) return e.label;
        var a = r.path;
        "key" === s.errors.label && r.path.length > 1 && (a = r.path.slice(-1));
        return t.path(a) || t.template(null, s.messages, "root", r, s) || n && t.template(null, n, "root", r, s) || "value";
      }, t.process = function (e, r, s) {
        if (!e) return null;
        var _t$details = t.details(e),
          n = _t$details.override,
          a = _t$details.message,
          i = _t$details.details;
        if (n) return n;
        if (s.errors.stack) return new t.ValidationError(a, i, r);
        var o = Error.stackTraceLimit;
        Error.stackTraceLimit = 0;
        var l = new t.ValidationError(a, i, r);
        return Error.stackTraceLimit = o, l;
      }, t.details = function (e) {
        var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
        var r = [];
        var s = [];
        var _iterator1 = _createForOfIteratorHelper(e),
          _step1;
        try {
          for (_iterator1.s(); !(_step1 = _iterator1.n()).done;) {
            var _n6 = _step1.value;
            if (_n6 instanceof Error) {
              if (!1 !== t.override) return {
                override: _n6
              };
              var _e10 = _n6.toString();
              r.push(_e10), s.push({
                message: _e10,
                type: "override",
                context: {
                  error: _n6
                }
              });
              continue;
            }
            var _e11 = _n6.toString();
            r.push(_e11), s.push({
              message: _e11,
              path: _n6.path.filter(function (e) {
                return "object" != _typeof(e);
              }),
              type: _n6.code,
              context: _n6.local
            });
          }
        } catch (err) {
          _iterator1.e(err);
        } finally {
          _iterator1.f();
        }
        return r.length > 1 && (r = _toConsumableArray(new Set(r))), {
          message: r.join(". "),
          details: s
        };
      }, t.ValidationError = /*#__PURE__*/function (_Error) {
        function _class4(e, t, r) {
          var _this2;
          _classCallCheck(this, _class4);
          _this2 = _callSuper(this, _class4, [e]), _this2._original = r, _this2.details = t;
          return _this2;
        }
        _inherits(_class4, _Error);
        return _createClass(_class4, null, [{
          key: "isError",
          value: function isError(e) {
            return e instanceof t.ValidationError;
          }
        }]);
      }(/*#__PURE__*/_wrapNativeSuper(Error)), t.ValidationError.prototype.isJoi = !0, t.ValidationError.prototype.name = "ValidationError", t.ValidationError.prototype.annotate = s.error;
    },
    8901: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8571),
        a = r(8160),
        i = r(6914),
        o = {};
      t.type = function (e, t) {
        var r = Object.getPrototypeOf(e),
          l = n(r),
          c = e._assign(Object.create(l)),
          u = Object.assign({}, t);
        delete u.base, l._definition = u;
        var f = r._definition || {};
        u.messages = i.merge(f.messages, u.messages), u.properties = Object.assign({}, f.properties, u.properties), c.type = u.type, u.flags = Object.assign({}, f.flags, u.flags);
        var m = Object.assign({}, f.terms);
        if (u.terms) for (var _e12 in u.terms) {
          var _t13 = u.terms[_e12];
          s(void 0 === c.$_terms[_e12], "Invalid term override for", u.type, _e12), c.$_terms[_e12] = _t13.init, m[_e12] = _t13;
        }
        u.terms = m, u.args || (u.args = f.args), u.prepare = o.prepare(u.prepare, f.prepare), u.coerce && ("function" == typeof u.coerce && (u.coerce = {
          method: u.coerce
        }), u.coerce.from && !Array.isArray(u.coerce.from) && (u.coerce = {
          method: u.coerce.method,
          from: [].concat(u.coerce.from)
        })), u.coerce = o.coerce(u.coerce, f.coerce), u.validate = o.validate(u.validate, f.validate);
        var h = Object.assign({}, f.rules);
        if (u.rules) {
          var _loop2 = function _loop2(_e13) {
            var t = u.rules[_e13];
            s("object" == _typeof(t), "Invalid rule definition for", u.type, _e13);
            var r = t.method;
            if (void 0 === r && (r = function r() {
              return this.$_addRule(_e13);
            }), r && (s(!l[_e13], "Rule conflict in", u.type, _e13), l[_e13] = r), s(!h[_e13], "Rule conflict in", u.type, _e13), h[_e13] = t, t.alias) {
              var _e14 = [].concat(t.alias);
              var _iterator10 = _createForOfIteratorHelper(_e14),
                _step10;
              try {
                for (_iterator10.s(); !(_step10 = _iterator10.n()).done;) {
                  var _r14 = _step10.value;
                  l[_r14] = t.method;
                }
              } catch (err) {
                _iterator10.e(err);
              } finally {
                _iterator10.f();
              }
            }
            t.args && (t.argsByName = new Map(), t.args = t.args.map(function (e) {
              return "string" == typeof e && (e = {
                name: e
              }), s(!t.argsByName.has(e.name), "Duplicated argument name", e.name), a.isSchema(e.assert) && (e.assert = e.assert.strict().label(e.name)), t.argsByName.set(e.name, e), e;
            }));
          };
          for (var _e13 in u.rules) {
            _loop2(_e13);
          }
        }
        u.rules = h;
        var d = Object.assign({}, f.modifiers);
        if (u.modifiers) {
          var _loop3 = function _loop3(_e15) {
            s(!l[_e15], "Rule conflict in", u.type, _e15);
            var t = u.modifiers[_e15];
            s("function" == typeof t, "Invalid modifier definition for", u.type, _e15);
            var r = function r(t) {
              return this.rule(_defineProperty({}, _e15, t));
            };
            l[_e15] = r, d[_e15] = t;
          };
          for (var _e15 in u.modifiers) {
            _loop3(_e15);
          }
        }
        if (u.modifiers = d, u.overrides) {
          l._super = r, c.$_super = {};
          for (var _e16 in u.overrides) s(r[_e16], "Cannot override missing", _e16), u.overrides[_e16][a.symbols.parent] = r[_e16], c.$_super[_e16] = r[_e16].bind(c);
          Object.assign(l, u.overrides);
        }
        u.cast = Object.assign({}, f.cast, u.cast);
        var p = Object.assign({}, f.manifest, u.manifest);
        return p.build = o.build(u.manifest && u.manifest.build, f.manifest && f.manifest.build), u.manifest = p, u.rebuild = o.rebuild(u.rebuild, f.rebuild), c;
      }, o.build = function (e, t) {
        return e && t ? function (r, s) {
          return t(e(r, s), s);
        } : e || t;
      }, o.coerce = function (e, t) {
        return e && t ? {
          from: e.from && t.from ? _toConsumableArray(new Set([].concat(_toConsumableArray(e.from), _toConsumableArray(t.from)))) : null,
          method: function method(r, s) {
            var n;
            if ((!t.from || t.from.includes(_typeof(r))) && (n = t.method(r, s), n)) {
              if (n.errors || void 0 === n.value) return n;
              r = n.value;
            }
            if (!e.from || e.from.includes(_typeof(r))) {
              var _t14 = e.method(r, s);
              if (_t14) return _t14;
            }
            return n;
          }
        } : e || t;
      }, o.prepare = function (e, t) {
        return e && t ? function (r, s) {
          var n = e(r, s);
          if (n) {
            if (n.errors || void 0 === n.value) return n;
            r = n.value;
          }
          return t(r, s) || n;
        } : e || t;
      }, o.rebuild = function (e, t) {
        return e && t ? function (r) {
          t(r), e(r);
        } : e || t;
      }, o.validate = function (e, t) {
        return e && t ? function (r, s) {
          var n = t(r, s);
          if (n) {
            if (n.errors && (!Array.isArray(n.errors) || n.errors.length)) return n;
            r = n.value;
          }
          return e(r, s) || n;
        } : e || t;
      };
    },
    5107: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8571),
        a = r(8652),
        i = r(8160),
        o = r(3292),
        l = r(6354),
        c = r(8901),
        u = r(9708),
        f = r(6133),
        m = r(3328),
        h = r(1152);
      var d;
      var p = {
        types: {
          alternatives: r(4946),
          any: r(8068),
          array: r(546),
          "boolean": r(4937),
          date: r(7500),
          "function": r(390),
          link: r(8785),
          number: r(3832),
          object: r(8966),
          string: r(7417),
          symbol: r(8826)
        },
        aliases: {
          alt: "alternatives",
          bool: "boolean",
          func: "function"
        },
        root: function root() {
          var e = {
            _types: new Set(Object.keys(p.types))
          };
          var _iterator11 = _createForOfIteratorHelper(e._types),
            _step11;
          try {
            var _loop5 = function _loop5() {
              var t = _step11.value;
              e[t] = function () {
                for (var _len8 = arguments.length, e = new Array(_len8), _key8 = 0; _key8 < _len8; _key8++) {
                  e[_key8] = arguments[_key8];
                }
                return s(!e.length || ["alternatives", "link", "object"].includes(t), "The", t, "type does not allow arguments"), p.generate(this, p.types[t], e);
              };
            };
            for (_iterator11.s(); !(_step11 = _iterator11.n()).done;) {
              _loop5();
            }
          } catch (err) {
            _iterator11.e(err);
          } finally {
            _iterator11.f();
          }
          var _loop4 = function _loop4() {
            var t = _arr2[_i10];
            e[t] = function () {
              var _this$any;
              return (_this$any = this.any())[t].apply(_this$any, arguments);
            };
          };
          for (var _i10 = 0, _arr2 = ["allow", "custom", "disallow", "equal", "exist", "forbidden", "invalid", "not", "only", "optional", "options", "prefs", "preferences", "required", "strip", "valid", "when"]; _i10 < _arr2.length; _i10++) {
            _loop4();
          }
          Object.assign(e, p.methods);
          for (var _t15 in p.aliases) {
            var _r15 = p.aliases[_t15];
            e[_t15] = e[_r15];
          }
          return e.x = e.expression, h.setup && h.setup(e), e;
        }
      };
      p.methods = {
        ValidationError: l.ValidationError,
        version: i.version,
        cache: a.provider,
        assert: function assert(e, t) {
          for (var _len9 = arguments.length, r = new Array(_len9 > 2 ? _len9 - 2 : 0), _key9 = 2; _key9 < _len9; _key9++) {
            r[_key9 - 2] = arguments[_key9];
          }
          p.assert(e, t, !0, r);
        },
        attempt: function attempt(e, t) {
          for (var _len0 = arguments.length, r = new Array(_len0 > 2 ? _len0 - 2 : 0), _key0 = 2; _key0 < _len0; _key0++) {
            r[_key0 - 2] = arguments[_key0];
          }
          return p.assert(e, t, !1, r);
        },
        build: function build(e) {
          return s("function" == typeof u.build, "Manifest functionality disabled"), u.build(this, e);
        },
        checkPreferences: function checkPreferences(e) {
          i.checkPreferences(e);
        },
        compile: function compile(e, t) {
          return o.compile(this, e, t);
        },
        defaults: function defaults(e) {
          s("function" == typeof e, "modifier must be a function");
          var t = Object.assign({}, this);
          var _iterator12 = _createForOfIteratorHelper(t._types),
            _step12;
          try {
            var _loop6 = function _loop6() {
              var r = _step12.value;
              var n = e(t[r]());
              s(i.isSchema(n), "modifier must return a valid schema object"), t[r] = function () {
                for (var _len1 = arguments.length, e = new Array(_len1), _key1 = 0; _key1 < _len1; _key1++) {
                  e[_key1] = arguments[_key1];
                }
                return p.generate(this, n, e);
              };
            };
            for (_iterator12.s(); !(_step12 = _iterator12.n()).done;) {
              _loop6();
            }
          } catch (err) {
            _iterator12.e(err);
          } finally {
            _iterator12.f();
          }
          return t;
        },
        expression: function expression() {
          for (var _len10 = arguments.length, e = new Array(_len10), _key10 = 0; _key10 < _len10; _key10++) {
            e[_key10] = arguments[_key10];
          }
          return _construct(m, e);
        },
        extend: function extend() {
          var _this3 = this;
          for (var _len11 = arguments.length, e = new Array(_len11), _key11 = 0; _key11 < _len11; _key11++) {
            e[_key11] = arguments[_key11];
          }
          i.verifyFlat(e, "extend"), d = d || r(3378), s(e.length, "You need to provide at least one extension"), this.assert(e, d.extensions);
          var t = Object.assign({}, this);
          t._types = new Set(t._types);
          for (var _i11 = 0, _e17 = e; _i11 < _e17.length; _i11++) {
            var _r16 = _e17[_i11];
            "function" == typeof _r16 && (_r16 = _r16(t)), this.assert(_r16, d.extension);
            var _e18 = p.expandExtension(_r16, t);
            var _iterator13 = _createForOfIteratorHelper(_e18),
              _step13;
            try {
              var _loop7 = function _loop7() {
                var r = _step13.value;
                s(void 0 === t[r.type] || t._types.has(r.type), "Cannot override name", r.type);
                var e = r.base || _this3.any(),
                  n = c.type(e, r);
                t._types.add(r.type), t[r.type] = function () {
                  for (var _len12 = arguments.length, e = new Array(_len12), _key12 = 0; _key12 < _len12; _key12++) {
                    e[_key12] = arguments[_key12];
                  }
                  return p.generate(this, n, e);
                };
              };
              for (_iterator13.s(); !(_step13 = _iterator13.n()).done;) {
                _loop7();
              }
            } catch (err) {
              _iterator13.e(err);
            } finally {
              _iterator13.f();
            }
          }
          return t;
        },
        isError: l.ValidationError.isError,
        isExpression: m.isTemplate,
        isRef: f.isRef,
        isSchema: i.isSchema,
        "in": function _in() {
          return f["in"].apply(f, arguments);
        },
        override: i.symbols.override,
        ref: function ref() {
          return f.create.apply(f, arguments);
        },
        types: function types() {
          var e = {};
          var _iterator14 = _createForOfIteratorHelper(this._types),
            _step14;
          try {
            for (_iterator14.s(); !(_step14 = _iterator14.n()).done;) {
              var _t17 = _step14.value;
              e[_t17] = this[_t17]();
            }
          } catch (err) {
            _iterator14.e(err);
          } finally {
            _iterator14.f();
          }
          for (var _t16 in p.aliases) e[_t16] = this[_t16]();
          return e;
        }
      }, p.assert = function (e, t, r, s) {
        var a = s[0] instanceof Error || "string" == typeof s[0] ? s[0] : null,
          o = null !== a ? s[1] : s[0],
          c = t.validate(e, i.preferences({
            errors: {
              stack: !0
            }
          }, o || {}));
        var u = c.error;
        if (!u) return c.value;
        if (a instanceof Error) throw a;
        var f = r && "function" == typeof u.annotate ? u.annotate() : u.message;
        throw u instanceof l.ValidationError == 0 && (u = n(u)), u.message = a ? "".concat(a, " ").concat(f) : f, u;
      }, p.generate = function (e, t, r) {
        var _t$_definition;
        return s(e, "Must be invoked on a Joi instance."), t.$_root = e, t._definition.args && r.length ? (_t$_definition = t._definition).args.apply(_t$_definition, [t].concat(_toConsumableArray(r))) : t;
      }, p.expandExtension = function (e, t) {
        if ("string" == typeof e.type) return [e];
        var r = [];
        var _iterator15 = _createForOfIteratorHelper(t._types),
          _step15;
        try {
          for (_iterator15.s(); !(_step15 = _iterator15.n()).done;) {
            var _s5 = _step15.value;
            if (e.type.test(_s5)) {
              var _n7 = Object.assign({}, e);
              _n7.type = _s5, _n7.base = t[_s5](), r.push(_n7);
            }
          }
        } catch (err) {
          _iterator15.e(err);
        } finally {
          _iterator15.f();
        }
        return r;
      }, e.exports = p.root();
    },
    6914: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8571),
        a = r(3328);
      t.compile = function (e, t) {
        if ("string" == typeof e) return s(!t, "Cannot set single message string"), new a(e);
        if (a.isTemplate(e)) return s(!t, "Cannot set single message template"), e;
        s("object" == _typeof(e) && !Array.isArray(e), "Invalid message options"), t = t ? n(t) : {};
        for (var _r17 in e) {
          var _n8 = e[_r17];
          if ("root" === _r17 || a.isTemplate(_n8)) {
            t[_r17] = _n8;
            continue;
          }
          if ("string" == typeof _n8) {
            t[_r17] = new a(_n8);
            continue;
          }
          s("object" == _typeof(_n8) && !Array.isArray(_n8), "Invalid message for", _r17);
          var i = _r17;
          for (_r17 in t[i] = t[i] || {}, _n8) {
            var _e19 = _n8[_r17];
            "root" === _r17 || a.isTemplate(_e19) ? t[i][_r17] = _e19 : (s("string" == typeof _e19, "Invalid message for", _r17, "in", i), t[i][_r17] = new a(_e19));
          }
        }
        return t;
      }, t.decompile = function (e) {
        var t = {};
        for (var _r18 in e) {
          var _s6 = e[_r18];
          if ("root" === _r18) {
            t.root = _s6;
            continue;
          }
          if (a.isTemplate(_s6)) {
            t[_r18] = _s6.describe({
              compact: !0
            });
            continue;
          }
          var _n9 = _r18;
          for (_r18 in t[_n9] = {}, _s6) {
            var _e20 = _s6[_r18];
            "root" !== _r18 ? t[_n9][_r18] = _e20.describe({
              compact: !0
            }) : t[_n9].root = _e20;
          }
        }
        return t;
      }, t.merge = function (e, r) {
        if (!e) return t.compile(r);
        if (!r) return e;
        if ("string" == typeof r) return new a(r);
        if (a.isTemplate(r)) return r;
        var i = n(e);
        for (var _e21 in r) {
          var _t18 = r[_e21];
          if ("root" === _e21 || a.isTemplate(_t18)) {
            i[_e21] = _t18;
            continue;
          }
          if ("string" == typeof _t18) {
            i[_e21] = new a(_t18);
            continue;
          }
          s("object" == _typeof(_t18) && !Array.isArray(_t18), "Invalid message for", _e21);
          var _n0 = _e21;
          for (_e21 in i[_n0] = i[_n0] || {}, _t18) {
            var _r19 = _t18[_e21];
            "root" === _e21 || a.isTemplate(_r19) ? i[_n0][_e21] = _r19 : (s("string" == typeof _r19, "Invalid message for", _e21, "in", _n0), i[_n0][_e21] = new a(_r19));
          }
        }
        return i;
      };
    },
    2294: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8160),
        a = r(6133),
        i = {};
      t.Ids = i.Ids = /*#__PURE__*/function () {
        function _class5() {
          _classCallCheck(this, _class5);
          this._byId = new Map(), this._byKey = new Map(), this._schemaChain = !1;
        }
        return _createClass(_class5, [{
          key: "clone",
          value: function clone() {
            var e = new i.Ids();
            return e._byId = new Map(this._byId), e._byKey = new Map(this._byKey), e._schemaChain = this._schemaChain, e;
          }
        }, {
          key: "concat",
          value: function concat(e) {
            e._schemaChain && (this._schemaChain = !0);
            var _iterator16 = _createForOfIteratorHelper(e._byId.entries()),
              _step16;
            try {
              for (_iterator16.s(); !(_step16 = _iterator16.n()).done;) {
                var _step16$value = _slicedToArray(_step16.value, 2),
                  _t19 = _step16$value[0],
                  _r20 = _step16$value[1];
                s(!this._byKey.has(_t19), "Schema id conflicts with existing key:", _t19), this._byId.set(_t19, _r20);
              }
            } catch (err) {
              _iterator16.e(err);
            } finally {
              _iterator16.f();
            }
            var _iterator17 = _createForOfIteratorHelper(e._byKey.entries()),
              _step17;
            try {
              for (_iterator17.s(); !(_step17 = _iterator17.n()).done;) {
                var _step17$value = _slicedToArray(_step17.value, 2),
                  _t20 = _step17$value[0],
                  _r21 = _step17$value[1];
                s(!this._byId.has(_t20), "Schema key conflicts with existing id:", _t20), this._byKey.set(_t20, _r21);
              }
            } catch (err) {
              _iterator17.e(err);
            } finally {
              _iterator17.f();
            }
          }
        }, {
          key: "fork",
          value: function fork(e, t, r) {
            var a = this._collect(e);
            a.push({
              schema: r
            });
            var o = a.shift();
            var l = {
              id: o.id,
              schema: t(o.schema)
            };
            s(n.isSchema(l.schema), "adjuster function failed to return a joi schema type");
            var _iterator18 = _createForOfIteratorHelper(a),
              _step18;
            try {
              for (_iterator18.s(); !(_step18 = _iterator18.n()).done;) {
                var _e22 = _step18.value;
                l = {
                  id: _e22.id,
                  schema: i.fork(_e22.schema, l.id, l.schema)
                };
              }
            } catch (err) {
              _iterator18.e(err);
            } finally {
              _iterator18.f();
            }
            return l.schema;
          }
        }, {
          key: "labels",
          value: function labels(e) {
            var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : [];
            var r = e[0],
              s = this._get(r);
            if (!s) return [].concat(_toConsumableArray(t), _toConsumableArray(e)).join(".");
            var n = e.slice(1);
            return t = [].concat(_toConsumableArray(t), [s.schema._flags.label || r]), n.length ? s.schema._ids.labels(n, t) : t.join(".");
          }
        }, {
          key: "reach",
          value: function reach(e) {
            var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : [];
            var r = e[0],
              n = this._get(r);
            s(n, "Schema does not contain path", [].concat(_toConsumableArray(t), _toConsumableArray(e)).join("."));
            var a = e.slice(1);
            return a.length ? n.schema._ids.reach(a, [].concat(_toConsumableArray(t), [r])) : n.schema;
          }
        }, {
          key: "register",
          value: function register(e) {
            var _ref4 = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {},
              t = _ref4.key;
            if (!e || !n.isSchema(e)) return;
            (e.$_property("schemaChain") || e._ids._schemaChain) && (this._schemaChain = !0);
            var r = e._flags.id;
            if (r) {
              var _t21 = this._byId.get(r);
              s(!_t21 || _t21.schema === e, "Cannot add different schemas with the same id:", r), s(!this._byKey.has(r), "Schema id conflicts with existing key:", r), this._byId.set(r, {
                schema: e,
                id: r
              });
            }
            t && (s(!this._byKey.has(t), "Schema already contains key:", t), s(!this._byId.has(t), "Schema key conflicts with existing id:", t), this._byKey.set(t, {
              schema: e,
              id: t
            }));
          }
        }, {
          key: "reset",
          value: function reset() {
            this._byId = new Map(), this._byKey = new Map(), this._schemaChain = !1;
          }
        }, {
          key: "_collect",
          value: function _collect(e) {
            var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : [];
            var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : [];
            var n = e[0],
              a = this._get(n);
            s(a, "Schema does not contain path", [].concat(_toConsumableArray(t), _toConsumableArray(e)).join(".")), r = [a].concat(_toConsumableArray(r));
            var i = e.slice(1);
            return i.length ? a.schema._ids._collect(i, [].concat(_toConsumableArray(t), [n]), r) : r;
          }
        }, {
          key: "_get",
          value: function _get(e) {
            return this._byId.get(e) || this._byKey.get(e);
          }
        }]);
      }(), i.fork = function (e, r, s) {
        var n = t.schema(e, {
          each: function each(e, _ref5) {
            var t = _ref5.key;
            if (r === (e._flags.id || t)) return s;
          },
          ref: !1
        });
        return n ? n.$_mutateRebuild() : e;
      }, t.schema = function (e, t) {
        var r;
        for (var _s7 in e._flags) {
          if ("_" === _s7[0]) continue;
          var _n1 = i.scan(e._flags[_s7], {
            source: "flags",
            name: _s7
          }, t);
          void 0 !== _n1 && (r = r || e.clone(), r._flags[_s7] = _n1);
        }
        for (var _s8 = 0; _s8 < e._rules.length; ++_s8) {
          var _n10 = e._rules[_s8],
            _a4 = i.scan(_n10.args, {
              source: "rules",
              name: _n10.name
            }, t);
          if (void 0 !== _a4) {
            r = r || e.clone();
            var _t22 = Object.assign({}, _n10);
            _t22.args = _a4, r._rules[_s8] = _t22, r._singleRules.get(_n10.name) === _n10 && r._singleRules.set(_n10.name, _t22);
          }
        }
        for (var _s9 in e.$_terms) {
          if ("_" === _s9[0]) continue;
          var _n11 = i.scan(e.$_terms[_s9], {
            source: "terms",
            name: _s9
          }, t);
          void 0 !== _n11 && (r = r || e.clone(), r.$_terms[_s9] = _n11);
        }
        return r;
      }, i.scan = function (e, t, r, s, o) {
        var l = s || [];
        if (null === e || "object" != _typeof(e)) return;
        var c;
        if (Array.isArray(e)) {
          for (var _s0 = 0; _s0 < e.length; ++_s0) {
            var _n12 = "terms" === t.source && "keys" === t.name && e[_s0].key,
              _a5 = i.scan(e[_s0], t, r, [_s0].concat(_toConsumableArray(l)), _n12);
            void 0 !== _a5 && (c = c || e.slice(), c[_s0] = _a5);
          }
          return c;
        }
        if (!1 !== r.schema && n.isSchema(e) || !1 !== r.ref && a.isRef(e)) {
          var _s1 = r.each(e, _objectSpread(_objectSpread({}, t), {}, {
            path: l,
            key: o
          }));
          if (_s1 === e) return;
          return _s1;
        }
        for (var _s10 in e) {
          if ("_" === _s10[0]) continue;
          var _n13 = i.scan(e[_s10], t, r, [_s10].concat(_toConsumableArray(l)), o);
          void 0 !== _n13 && (c = c || Object.assign({}, e), c[_s10] = _n13);
        }
        return c;
      };
    },
    6133: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8571),
        a = r(9621),
        i = r(8160);
      var o;
      var l = {
        symbol: Symbol("ref"),
        defaults: {
          adjust: null,
          "in": !1,
          iterables: null,
          map: null,
          separator: ".",
          type: "value"
        }
      };
      t.create = function (e) {
        var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
        s("string" == typeof e, "Invalid reference key:", e), i.assertOptions(t, ["adjust", "ancestor", "in", "iterables", "map", "prefix", "render", "separator"]), s(!t.prefix || "object" == _typeof(t.prefix), "options.prefix must be of type object");
        var r = Object.assign({}, l.defaults, t);
        delete r.prefix;
        var n = r.separator,
          a = l.context(e, n, t.prefix);
        if (r.type = a.type, e = a.key, "value" === r.type) if (a.root && (s(!n || e[0] !== n, "Cannot specify relative path with root prefix"), r.ancestor = "root", e || (e = null)), n && n === e) e = null, r.ancestor = 0;else if (void 0 !== r.ancestor) s(!n || !e || e[0] !== n, "Cannot combine prefix with ancestor option");else {
          var _l$ancestor = l.ancestor(e, n),
            _l$ancestor2 = _slicedToArray(_l$ancestor, 2),
            _t23 = _l$ancestor2[0],
            _s11 = _l$ancestor2[1];
          _s11 && "" === (e = e.slice(_s11)) && (e = null), r.ancestor = _t23;
        }
        return r.path = n ? null === e ? [] : e.split(n) : [e], new l.Ref(r);
      }, t["in"] = function (e) {
        var r = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
        return t.create(e, _objectSpread(_objectSpread({}, r), {}, {
          "in": !0
        }));
      }, t.isRef = function (e) {
        return !!e && !!e[i.symbols.ref];
      }, l.Ref = /*#__PURE__*/function () {
        function _class6(e) {
          _classCallCheck(this, _class6);
          s("object" == _typeof(e), "Invalid reference construction"), i.assertOptions(e, ["adjust", "ancestor", "in", "iterables", "map", "path", "render", "separator", "type", "depth", "key", "root", "display"]), s([!1, void 0].includes(e.separator) || "string" == typeof e.separator && 1 === e.separator.length, "Invalid separator"), s(!e.adjust || "function" == typeof e.adjust, "options.adjust must be a function"), s(!e.map || Array.isArray(e.map), "options.map must be an array"), s(!e.map || !e.adjust, "Cannot set both map and adjust options"), Object.assign(this, l.defaults, e), s("value" === this.type || void 0 === this.ancestor, "Non-value references cannot reference ancestors"), Array.isArray(this.map) && (this.map = new Map(this.map)), this.depth = this.path.length, this.key = this.path.length ? this.path.join(this.separator) : null, this.root = this.path[0], this.updateDisplay();
        }
        return _createClass(_class6, [{
          key: "resolve",
          value: function resolve(e, t, r, n) {
            var a = arguments.length > 4 && arguments[4] !== undefined ? arguments[4] : {};
            return s(!this["in"] || a["in"], "Invalid in() reference usage"), "global" === this.type ? this._resolve(r.context, t, a) : "local" === this.type ? this._resolve(n, t, a) : this.ancestor ? "root" === this.ancestor ? this._resolve(t.ancestors[t.ancestors.length - 1], t, a) : (s(this.ancestor <= t.ancestors.length, "Invalid reference exceeds the schema root:", this.display), this._resolve(t.ancestors[this.ancestor - 1], t, a)) : this._resolve(e, t, a);
          }
        }, {
          key: "_resolve",
          value: function _resolve(e, t, r) {
            var s;
            if ("value" === this.type && t.mainstay.shadow && !1 !== r.shadow && (s = t.mainstay.shadow.get(this.absolute(t))), void 0 === s && (s = a(e, this.path, {
              iterables: this.iterables,
              functions: !0
            })), this.adjust && (s = this.adjust(s)), this.map) {
              var _e23 = this.map.get(s);
              void 0 !== _e23 && (s = _e23);
            }
            return t.mainstay && t.mainstay.tracer.resolve(t, this, s), s;
          }
        }, {
          key: "toString",
          value: function toString() {
            return this.display;
          }
        }, {
          key: "absolute",
          value: function absolute(e) {
            return [].concat(_toConsumableArray(e.path.slice(0, -this.ancestor)), _toConsumableArray(this.path));
          }
        }, {
          key: "clone",
          value: function clone() {
            return new l.Ref(this);
          }
        }, {
          key: "describe",
          value: function describe() {
            var e = {
              path: this.path
            };
            "value" !== this.type && (e.type = this.type), "." !== this.separator && (e.separator = this.separator), "value" === this.type && 1 !== this.ancestor && (e.ancestor = this.ancestor), this.map && (e.map = _toConsumableArray(this.map));
            for (var _i12 = 0, _arr3 = ["adjust", "iterables", "render"]; _i12 < _arr3.length; _i12++) {
              var _t24 = _arr3[_i12];
              null !== this[_t24] && void 0 !== this[_t24] && (e[_t24] = this[_t24]);
            }
            return !1 !== this["in"] && (e["in"] = !0), {
              ref: e
            };
          }
        }, {
          key: "updateDisplay",
          value: function updateDisplay() {
            var e = null !== this.key ? this.key : "";
            if ("value" !== this.type) return void (this.display = "ref:".concat(this.type, ":").concat(e));
            if (!this.separator) return void (this.display = "ref:".concat(e));
            if (!this.ancestor) return void (this.display = "ref:".concat(this.separator).concat(e));
            if ("root" === this.ancestor) return void (this.display = "ref:root:".concat(e));
            if (1 === this.ancestor) return void (this.display = "ref:".concat(e || ".."));
            var t = new Array(this.ancestor + 1).fill(this.separator).join("");
            this.display = "ref:".concat(t).concat(e || "");
          }
        }]);
      }(), l.Ref.prototype[i.symbols.ref] = !0, t.build = function (e) {
        return "value" === (e = Object.assign({}, l.defaults, e)).type && void 0 === e.ancestor && (e.ancestor = 1), new l.Ref(e);
      }, l.context = function (e, t) {
        var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
        if (e = e.trim(), r) {
          var _s12 = void 0 === r.global ? "$" : r.global;
          if (_s12 !== t && e.startsWith(_s12)) return {
            key: e.slice(_s12.length),
            type: "global"
          };
          var _n14 = void 0 === r.local ? "#" : r.local;
          if (_n14 !== t && e.startsWith(_n14)) return {
            key: e.slice(_n14.length),
            type: "local"
          };
          var _a6 = void 0 === r.root ? "/" : r.root;
          if (_a6 !== t && e.startsWith(_a6)) return {
            key: e.slice(_a6.length),
            type: "value",
            root: !0
          };
        }
        return {
          key: e,
          type: "value"
        };
      }, l.ancestor = function (e, t) {
        if (!t) return [1, 0];
        if (e[0] !== t) return [1, 0];
        if (e[1] !== t) return [0, 1];
        var r = 2;
        for (; e[r] === t;) ++r;
        return [r - 1, r];
      }, t.toSibling = 0, t.toParent = 1, t.Manager = /*#__PURE__*/function () {
        function _class7() {
          _classCallCheck(this, _class7);
          this.refs = [];
        }
        return _createClass(_class7, [{
          key: "register",
          value: function register(e, s) {
            if (e) if (s = void 0 === s ? t.toParent : s, Array.isArray(e)) {
              var _iterator19 = _createForOfIteratorHelper(e),
                _step19;
              try {
                for (_iterator19.s(); !(_step19 = _iterator19.n()).done;) {
                  var _t25 = _step19.value;
                  this.register(_t25, s);
                }
              } catch (err) {
                _iterator19.e(err);
              } finally {
                _iterator19.f();
              }
            } else if (i.isSchema(e)) {
              var _iterator20 = _createForOfIteratorHelper(e._refs.refs),
                _step20;
              try {
                for (_iterator20.s(); !(_step20 = _iterator20.n()).done;) {
                  var _t26 = _step20.value;
                  _t26.ancestor - s >= 0 && this.refs.push({
                    ancestor: _t26.ancestor - s,
                    root: _t26.root
                  });
                }
              } catch (err) {
                _iterator20.e(err);
              } finally {
                _iterator20.f();
              }
            } else t.isRef(e) && "value" === e.type && e.ancestor - s >= 0 && this.refs.push({
              ancestor: e.ancestor - s,
              root: e.root
            }), o = o || r(3328), o.isTemplate(e) && this.register(e.refs(), s);
          }
        }, {
          key: "length",
          get: function get() {
            return this.refs.length;
          }
        }, {
          key: "clone",
          value: function clone() {
            var e = new t.Manager();
            return e.refs = n(this.refs), e;
          }
        }, {
          key: "reset",
          value: function reset() {
            this.refs = [];
          }
        }, {
          key: "roots",
          value: function roots() {
            return this.refs.filter(function (e) {
              return !e.ancestor;
            }).map(function (e) {
              return e.root;
            });
          }
        }]);
      }();
    },
    3378: function _(e, t, r) {
      "use strict";

      var s = r(5107),
        n = {};
      n.wrap = s.string().min(1).max(2).allow(!1), t.preferences = s.object({
        allowUnknown: s["boolean"](),
        abortEarly: s["boolean"](),
        artifacts: s["boolean"](),
        cache: s["boolean"](),
        context: s.object(),
        convert: s["boolean"](),
        dateFormat: s.valid("date", "iso", "string", "time", "utc"),
        debug: s["boolean"](),
        errors: {
          escapeHtml: s["boolean"](),
          label: s.valid("path", "key", !1),
          language: [s.string(), s.object().ref()],
          render: s["boolean"](),
          stack: s["boolean"](),
          wrap: {
            label: n.wrap,
            array: n.wrap,
            string: n.wrap
          }
        },
        externals: s["boolean"](),
        messages: s.object(),
        noDefaults: s["boolean"](),
        nonEnumerables: s["boolean"](),
        presence: s.valid("required", "optional", "forbidden"),
        skipFunctions: s["boolean"](),
        stripUnknown: s.object({
          arrays: s["boolean"](),
          objects: s["boolean"]()
        }).or("arrays", "objects").allow(!0, !1),
        warnings: s["boolean"]()
      }).strict(), n.nameRx = /^[a-zA-Z0-9]\w*$/, n.rule = s.object({
        alias: s.array().items(s.string().pattern(n.nameRx)).single(),
        args: s.array().items(s.string(), s.object({
          name: s.string().pattern(n.nameRx).required(),
          ref: s["boolean"](),
          assert: s.alternatives([s["function"](), s.object().schema()]).conditional("ref", {
            is: !0,
            then: s.required()
          }),
          normalize: s["function"](),
          message: s.string().when("assert", {
            is: s["function"](),
            then: s.required()
          })
        })),
        convert: s["boolean"](),
        manifest: s["boolean"](),
        method: s["function"]().allow(!1),
        multi: s["boolean"](),
        validate: s["function"]()
      }), t.extension = s.object({
        type: s.alternatives([s.string(), s.object().regex()]).required(),
        args: s["function"](),
        cast: s.object().pattern(n.nameRx, s.object({
          from: s["function"]().maxArity(1).required(),
          to: s["function"]().minArity(1).maxArity(2).required()
        })),
        base: s.object().schema().when("type", {
          is: s.object().regex(),
          then: s.forbidden()
        }),
        coerce: [s["function"]().maxArity(3), s.object({
          method: s["function"]().maxArity(3).required(),
          from: s.array().items(s.string()).single()
        })],
        flags: s.object().pattern(n.nameRx, s.object({
          setter: s.string(),
          "default": s.any()
        })),
        manifest: {
          build: s["function"]().arity(2)
        },
        messages: [s.object(), s.string()],
        modifiers: s.object().pattern(n.nameRx, s["function"]().minArity(1).maxArity(2)),
        overrides: s.object().pattern(n.nameRx, s["function"]()),
        prepare: s["function"]().maxArity(3),
        rebuild: s["function"]().arity(1),
        rules: s.object().pattern(n.nameRx, n.rule),
        terms: s.object().pattern(n.nameRx, s.object({
          init: s.array().allow(null).required(),
          manifest: s.object().pattern(/.+/, [s.valid("schema", "single"), s.object({
            mapped: s.object({
              from: s.string().required(),
              to: s.string().required()
            }).required()
          })])
        })),
        validate: s["function"]().maxArity(3)
      }).strict(), t.extensions = s.array().items(s.object(), s["function"]().arity(1)).strict(), n.desc = {
        buffer: s.object({
          buffer: s.string()
        }),
        func: s.object({
          "function": s["function"]().required(),
          options: {
            literal: !0
          }
        }),
        override: s.object({
          override: !0
        }),
        ref: s.object({
          ref: s.object({
            type: s.valid("value", "global", "local"),
            path: s.array().required(),
            separator: s.string().length(1).allow(!1),
            ancestor: s.number().min(0).integer().allow("root"),
            map: s.array().items(s.array().length(2)).min(1),
            adjust: s["function"](),
            iterables: s["boolean"](),
            "in": s["boolean"](),
            render: s["boolean"]()
          }).required()
        }),
        regex: s.object({
          regex: s.string().min(3)
        }),
        special: s.object({
          special: s.valid("deep").required()
        }),
        template: s.object({
          template: s.string().required(),
          options: s.object()
        }),
        value: s.object({
          value: s.alternatives([s.object(), s.array()]).required()
        })
      }, n.desc.entity = s.alternatives([s.array().items(s.link("...")), s["boolean"](), s["function"](), s.number(), s.string(), n.desc.buffer, n.desc.func, n.desc.ref, n.desc.regex, n.desc.special, n.desc.template, n.desc.value, s.link("/")]), n.desc.values = s.array().items(null, s["boolean"](), s["function"](), s.number().allow(1 / 0, -1 / 0), s.string().allow(""), s.symbol(), n.desc.buffer, n.desc.func, n.desc.override, n.desc.ref, n.desc.regex, n.desc.template, n.desc.value), n.desc.messages = s.object().pattern(/.+/, [s.string(), n.desc.template, s.object().pattern(/.+/, [s.string(), n.desc.template])]), t.description = s.object({
        type: s.string().required(),
        flags: s.object({
          cast: s.string(),
          "default": s.any(),
          description: s.string(),
          empty: s.link("/"),
          failover: n.desc.entity,
          id: s.string(),
          label: s.string(),
          only: !0,
          presence: ["optional", "required", "forbidden"],
          result: ["raw", "strip"],
          strip: s["boolean"](),
          unit: s.string()
        }).unknown(),
        preferences: {
          allowUnknown: s["boolean"](),
          abortEarly: s["boolean"](),
          artifacts: s["boolean"](),
          cache: s["boolean"](),
          convert: s["boolean"](),
          dateFormat: ["date", "iso", "string", "time", "utc"],
          errors: {
            escapeHtml: s["boolean"](),
            label: ["path", "key"],
            language: [s.string(), n.desc.ref],
            wrap: {
              label: n.wrap,
              array: n.wrap
            }
          },
          externals: s["boolean"](),
          messages: n.desc.messages,
          noDefaults: s["boolean"](),
          nonEnumerables: s["boolean"](),
          presence: ["required", "optional", "forbidden"],
          skipFunctions: s["boolean"](),
          stripUnknown: s.object({
            arrays: s["boolean"](),
            objects: s["boolean"]()
          }).or("arrays", "objects").allow(!0, !1),
          warnings: s["boolean"]()
        },
        allow: n.desc.values,
        invalid: n.desc.values,
        rules: s.array().min(1).items({
          name: s.string().required(),
          args: s.object().min(1),
          keep: s["boolean"](),
          message: [s.string(), n.desc.messages],
          warn: s["boolean"]()
        }),
        keys: s.object().pattern(/.*/, s.link("/")),
        link: n.desc.ref
      }).pattern(/^[a-z]\w*$/, s.any());
    },
    493: function _(e, t, r) {
      "use strict";

      var s = r(8571),
        n = r(9621),
        a = r(8160),
        i = {
          value: Symbol("value")
        };
      e.exports = i.State = /*#__PURE__*/function () {
        function _class8(e, t, r) {
          _classCallCheck(this, _class8);
          this.path = e, this.ancestors = t, this.mainstay = r.mainstay, this.schemas = r.schemas, this.debug = null;
        }
        return _createClass(_class8, [{
          key: "localize",
          value: function localize(e) {
            var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : null;
            var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : null;
            var s = new i.State(e, t, this);
            return r && s.schemas && (s.schemas = [i.schemas(r)].concat(_toConsumableArray(s.schemas))), s;
          }
        }, {
          key: "nest",
          value: function nest(e, t) {
            var r = new i.State(this.path, this.ancestors, this);
            return r.schemas = r.schemas && [i.schemas(e)].concat(_toConsumableArray(r.schemas)), r.debug = t, r;
          }
        }, {
          key: "shadow",
          value: function shadow(e, t) {
            this.mainstay.shadow = this.mainstay.shadow || new i.Shadow(), this.mainstay.shadow.set(this.path, e, t);
          }
        }, {
          key: "snapshot",
          value: function snapshot() {
            this.mainstay.shadow && (this._snapshot = s(this.mainstay.shadow.node(this.path))), this.mainstay.snapshot();
          }
        }, {
          key: "restore",
          value: function restore() {
            this.mainstay.shadow && (this.mainstay.shadow.override(this.path, this._snapshot), this._snapshot = void 0), this.mainstay.restore();
          }
        }, {
          key: "commit",
          value: function commit() {
            this.mainstay.shadow && (this.mainstay.shadow.override(this.path, this._snapshot), this._snapshot = void 0), this.mainstay.commit();
          }
        }]);
      }(), i.schemas = function (e) {
        return a.isSchema(e) ? {
          schema: e
        } : e;
      }, i.Shadow = /*#__PURE__*/function () {
        function _class9() {
          _classCallCheck(this, _class9);
          this._values = null;
        }
        return _createClass(_class9, [{
          key: "set",
          value: function set(e, t, r) {
            if (!e.length) return;
            if ("strip" === r && "number" == typeof e[e.length - 1]) return;
            this._values = this._values || new Map();
            var s = this._values;
            for (var _t27 = 0; _t27 < e.length; ++_t27) {
              var _r22 = e[_t27];
              var _n15 = s.get(_r22);
              _n15 || (_n15 = new Map(), s.set(_r22, _n15)), s = _n15;
            }
            s[i.value] = t;
          }
        }, {
          key: "get",
          value: function get(e) {
            var t = this.node(e);
            if (t) return t[i.value];
          }
        }, {
          key: "node",
          value: function node(e) {
            if (this._values) return n(this._values, e, {
              iterables: !0
            });
          }
        }, {
          key: "override",
          value: function override(e, t) {
            if (!this._values) return;
            var r = e.slice(0, -1),
              s = e[e.length - 1],
              a = n(this._values, r, {
                iterables: !0
              });
            t ? a.set(s, t) : a && a["delete"](s);
          }
        }]);
      }();
    },
    3328: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8571),
        a = r(5277),
        i = r(1447),
        o = r(8160),
        l = r(6354),
        c = r(6133),
        u = {
          symbol: Symbol("template"),
          opens: new Array(1e3).join("\0"),
          closes: new Array(1e3).join(""),
          dateFormat: {
            date: Date.prototype.toDateString,
            iso: Date.prototype.toISOString,
            string: Date.prototype.toString,
            time: Date.prototype.toTimeString,
            utc: Date.prototype.toUTCString
          }
        };
      e.exports = u.Template = /*#__PURE__*/function () {
        function _class0(e, t) {
          _classCallCheck(this, _class0);
          if (s("string" == typeof e, "Template source must be a string"), s(!e.includes("\0") && !e.includes(""), "Template source cannot contain reserved control characters"), this.source = e, this.rendered = e, this._template = null, t) {
            var _e24 = t.functions,
              _r23 = _objectWithoutProperties(t, _excluded);
            this._settings = Object.keys(_r23).length ? n(_r23) : void 0, this._functions = _e24, this._functions && (s(Object.keys(this._functions).every(function (e) {
              return "string" == typeof e;
            }), "Functions keys must be strings"), s(Object.values(this._functions).every(function (e) {
              return "function" == typeof e;
            }), "Functions values must be functions"));
          } else this._settings = void 0, this._functions = void 0;
          this._parse();
        }
        return _createClass(_class0, [{
          key: "_parse",
          value: function _parse() {
            if (!this.source.includes("{")) return;
            var e = u.encode(this.source),
              t = u.split(e);
            var r = !1;
            var s = [],
              n = t.shift();
            n && s.push(n);
            var _iterator21 = _createForOfIteratorHelper(t),
              _step21;
            try {
              for (_iterator21.s(); !(_step21 = _iterator21.n()).done;) {
                var _e25 = _step21.value;
                var _t28 = "{" !== _e25[0],
                  _n16 = _t28 ? "}" : "}}",
                  _a7 = _e25.indexOf(_n16);
                if (-1 === _a7 || "{" === _e25[1]) {
                  s.push("{".concat(u.decode(_e25)));
                  continue;
                }
                var _i13 = _e25.slice(_t28 ? 0 : 1, _a7);
                var _o6 = ":" === _i13[0];
                _o6 && (_i13 = _i13.slice(1));
                var _l3 = this._ref(u.decode(_i13), {
                  raw: _t28,
                  wrapped: _o6
                });
                s.push(_l3), "string" != typeof _l3 && (r = !0);
                var _c4 = _e25.slice(_a7 + _n16.length);
                _c4 && s.push(u.decode(_c4));
              }
            } catch (err) {
              _iterator21.e(err);
            } finally {
              _iterator21.f();
            }
            r ? this._template = s : this.rendered = s.join("");
          }
        }, {
          key: "describe",
          value: function describe() {
            var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
            if (!this._settings && e.compact) return this.source;
            var t = {
              template: this.source
            };
            return this._settings && (t.options = this._settings), this._functions && (t.functions = this._functions), t;
          }
        }, {
          key: "isDynamic",
          value: function isDynamic() {
            return !!this._template;
          }
        }, {
          key: "refs",
          value: function refs() {
            if (!this._template) return;
            var e = [];
            var _iterator22 = _createForOfIteratorHelper(this._template),
              _step22;
            try {
              for (_iterator22.s(); !(_step22 = _iterator22.n()).done;) {
                var _t29 = _step22.value;
                "string" != typeof _t29 && e.push.apply(e, _toConsumableArray(_t29.refs));
              }
            } catch (err) {
              _iterator22.e(err);
            } finally {
              _iterator22.f();
            }
            return e;
          }
        }, {
          key: "resolve",
          value: function resolve(e, t, r, s) {
            return this._template && 1 === this._template.length ? this._part(this._template[0], e, t, r, s, {}) : this.render(e, t, r, s);
          }
        }, {
          key: "_part",
          value: function _part(e) {
            var _e$ref;
            for (var _len13 = arguments.length, t = new Array(_len13 > 1 ? _len13 - 1 : 0), _key13 = 1; _key13 < _len13; _key13++) {
              t[_key13 - 1] = arguments[_key13];
            }
            return e.ref ? (_e$ref = e.ref).resolve.apply(_e$ref, t) : e.formula.evaluate(t);
          }
        }, {
          key: "render",
          value: function render(e, t, r, s) {
            var n = arguments.length > 4 && arguments[4] !== undefined ? arguments[4] : {};
            if (!this.isDynamic()) return this.rendered;
            var i = [];
            var _iterator23 = _createForOfIteratorHelper(this._template),
              _step23;
            try {
              for (_iterator23.s(); !(_step23 = _iterator23.n()).done;) {
                var _o7 = _step23.value;
                if ("string" == typeof _o7) i.push(_o7);else {
                  var _l4 = this._part(_o7, e, t, r, s, n),
                    _c5 = u.stringify(_l4, e, t, r, s, n);
                  if (void 0 !== _c5) {
                    var _e26 = _o7.raw || !1 === (n.errors && n.errors.escapeHtml) ? _c5 : a(_c5);
                    i.push(u.wrap(_e26, _o7.wrapped && r.errors.wrap.label));
                  }
                }
              }
            } catch (err) {
              _iterator23.e(err);
            } finally {
              _iterator23.f();
            }
            return i.join("");
          }
        }, {
          key: "_ref",
          value: function _ref(e, _ref6) {
            var _this4 = this;
            var t = _ref6.raw,
              r = _ref6.wrapped;
            var s = [],
              n = function n(e) {
                var t = c.create(e, _this4._settings);
                return s.push(t), function (e) {
                  var r = t.resolve.apply(t, _toConsumableArray(e));
                  return void 0 !== r ? r : null;
                };
              };
            try {
              var _t30 = this._functions ? _objectSpread(_objectSpread({}, u.functions), this._functions) : u.functions;
              var a = new i.Parser(e, {
                reference: n,
                functions: _t30,
                constants: u.constants
              });
            } catch (t) {
              throw t.message = "Invalid template variable \"".concat(e, "\" fails due to: ").concat(t.message), t;
            }
            if (a.single) {
              if ("reference" === a.single.type) {
                var _e27 = s[0];
                return {
                  ref: _e27,
                  raw: t,
                  refs: s,
                  wrapped: r || "local" === _e27.type && "label" === _e27.key
                };
              }
              return u.stringify(a.single.value);
            }
            return {
              formula: a,
              raw: t,
              refs: s
            };
          }
        }, {
          key: "toString",
          value: function toString() {
            return this.source;
          }
        }], [{
          key: "date",
          value: function date(e, t) {
            return u.dateFormat[t.dateFormat].call(e);
          }
        }, {
          key: "build",
          value: function build(e) {
            return new u.Template(e.template, e.options || e.functions ? _objectSpread(_objectSpread({}, e.options), {}, {
              functions: e.functions
            }) : void 0);
          }
        }, {
          key: "isTemplate",
          value: function isTemplate(e) {
            return !!e && !!e[o.symbols.template];
          }
        }]);
      }(), u.Template.prototype[o.symbols.template] = !0, u.Template.prototype.isImmutable = !0, u.encode = function (e) {
        return e.replace(/\\(\{+)/g, function (e, t) {
          return u.opens.slice(0, t.length);
        }).replace(/\\(\}+)/g, function (e, t) {
          return u.closes.slice(0, t.length);
        });
      }, u.decode = function (e) {
        return e.replace(/\u0000/g, "{").replace(/\u0001/g, "}");
      }, u.split = function (e) {
        var t = [];
        var r = "";
        for (var _s13 = 0; _s13 < e.length; ++_s13) {
          var _n17 = e[_s13];
          if ("{" === _n17) {
            var _n18 = "";
            for (; _s13 + 1 < e.length && "{" === e[_s13 + 1];) _n18 += "{", ++_s13;
            t.push(r), r = _n18;
          } else r += _n17;
        }
        return t.push(r), t;
      }, u.wrap = function (e, t) {
        return t ? 1 === t.length ? "".concat(t).concat(e).concat(t) : "".concat(t[0]).concat(e).concat(t[1]) : e;
      }, u.stringify = function (e, t, r, s, n) {
        var a = arguments.length > 5 && arguments[5] !== undefined ? arguments[5] : {};
        var i = _typeof(e),
          o = s && s.errors && s.errors.wrap || {};
        var l = !1;
        if (c.isRef(e) && e.render && (l = e["in"], e = e.resolve(t, r, s, n, _objectSpread({
          "in": e["in"]
        }, a))), null === e) return "null";
        if ("string" === i) return u.wrap(e, a.arrayItems && o.string);
        if ("number" === i || "function" === i || "symbol" === i) return e.toString();
        if ("object" !== i) return JSON.stringify(e);
        if (e instanceof Date) return u.Template.date(e, s);
        if (e instanceof Map) {
          var _t31 = [];
          var _iterator24 = _createForOfIteratorHelper(e.entries()),
            _step24;
          try {
            for (_iterator24.s(); !(_step24 = _iterator24.n()).done;) {
              var _step24$value = _slicedToArray(_step24.value, 2),
                _r24 = _step24$value[0],
                _s14 = _step24$value[1];
              _t31.push("".concat(_r24.toString(), " -> ").concat(_s14.toString()));
            }
          } catch (err) {
            _iterator24.e(err);
          } finally {
            _iterator24.f();
          }
          e = _t31;
        }
        if (!Array.isArray(e)) return e.toString();
        var f = [];
        var _iterator25 = _createForOfIteratorHelper(e),
          _step25;
        try {
          for (_iterator25.s(); !(_step25 = _iterator25.n()).done;) {
            var _i14 = _step25.value;
            f.push(u.stringify(_i14, t, r, s, n, _objectSpread({
              arrayItems: !0
            }, a)));
          }
        } catch (err) {
          _iterator25.e(err);
        } finally {
          _iterator25.f();
        }
        return u.wrap(f.join(", "), !l && o.array);
      }, u.constants = {
        "true": !0,
        "false": !1,
        "null": null,
        second: 1e3,
        minute: 6e4,
        hour: 36e5,
        day: 864e5
      }, u.functions = {
        "if": function _if(e, t, r) {
          return e ? t : r;
        },
        length: function length(e) {
          return "string" == typeof e ? e.length : e && "object" == _typeof(e) ? Array.isArray(e) ? e.length : Object.keys(e).length : null;
        },
        msg: function msg(e) {
          var _this5 = _slicedToArray(this, 5),
            t = _this5[0],
            r = _this5[1],
            s = _this5[2],
            n = _this5[3],
            a = _this5[4],
            i = a.messages;
          if (!i) return "";
          var o = l.template(t, i[0], e, r, s) || l.template(t, i[1], e, r, s);
          return o ? o.render(t, r, s, n, a) : "";
        },
        number: function number(e) {
          return "number" == typeof e ? e : "string" == typeof e ? parseFloat(e) : "boolean" == typeof e ? e ? 1 : 0 : e instanceof Date ? e.getTime() : null;
        }
      };
    },
    4946: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(1687),
        a = r(8068),
        i = r(8160),
        o = r(3292),
        l = r(6354),
        c = r(6133),
        u = {};
      e.exports = a.extend({
        type: "alternatives",
        flags: {
          match: {
            "default": "any"
          }
        },
        terms: {
          matches: {
            init: [],
            register: c.toSibling
          }
        },
        args: function args(e) {
          for (var _len14 = arguments.length, t = new Array(_len14 > 1 ? _len14 - 1 : 0), _key14 = 1; _key14 < _len14; _key14++) {
            t[_key14 - 1] = arguments[_key14];
          }
          return 1 === t.length && Array.isArray(t[0]) ? e["try"].apply(e, _toConsumableArray(t[0])) : e["try"].apply(e, t);
        },
        validate: function validate(e, t) {
          var r = t.schema,
            s = t.error,
            a = t.state,
            i = t.prefs;
          if (r._flags.match) {
            var _t32 = [],
              _o8 = [];
            for (var _s15 = 0; _s15 < r.$_terms.matches.length; ++_s15) {
              var _n19 = r.$_terms.matches[_s15],
                _l5 = a.nest(_n19.schema, "match.".concat(_s15));
              _l5.snapshot();
              var _c6 = _n19.schema.$_validate(e, _l5, i);
              _c6.errors ? (_o8.push(_c6.errors), _l5.restore()) : (_t32.push(_c6.value), _l5.commit());
            }
            if (0 === _t32.length) return {
              errors: s("alternatives.any", {
                details: _o8.map(function (e) {
                  return l.details(e, {
                    override: !1
                  });
                })
              })
            };
            if ("one" === r._flags.match) return 1 === _t32.length ? {
              value: _t32[0]
            } : {
              errors: s("alternatives.one")
            };
            if (_t32.length !== r.$_terms.matches.length) return {
              errors: s("alternatives.all", {
                details: _o8.map(function (e) {
                  return l.details(e, {
                    override: !1
                  });
                })
              })
            };
            var _c8 = function _c7(e) {
              return e.$_terms.matches.some(function (e) {
                return "object" === e.schema.type || "alternatives" === e.schema.type && _c8(e.schema);
              });
            };
            return _c8(r) ? {
              value: _t32.reduce(function (e, t) {
                return n(e, t, {
                  mergeArrays: !1
                });
              })
            } : {
              value: _t32[_t32.length - 1]
            };
          }
          var o = [];
          for (var _t33 = 0; _t33 < r.$_terms.matches.length; ++_t33) {
            var _s16 = r.$_terms.matches[_t33];
            if (_s16.schema) {
              var _r25 = a.nest(_s16.schema, "match.".concat(_t33));
              _r25.snapshot();
              var _n20 = _s16.schema.$_validate(e, _r25, i);
              if (!_n20.errors) return _r25.commit(), _n20;
              _r25.restore(), o.push({
                schema: _s16.schema,
                reports: _n20.errors
              });
              continue;
            }
            var _n21 = _s16.ref ? _s16.ref.resolve(e, a, i) : e,
              _l6 = _s16.is ? [_s16] : _s16["switch"];
            for (var _r26 = 0; _r26 < _l6.length; ++_r26) {
              var _o9 = _l6[_r26],
                _c9 = _o9.is,
                _u2 = _o9.then,
                f = _o9.otherwise,
                m = "match.".concat(_t33).concat(_s16["switch"] ? "." + _r26 : "");
              if (_c9.$_match(_n21, a.nest(_c9, "".concat(m, ".is")), i)) {
                if (_u2) return _u2.$_validate(e, a.nest(_u2, "".concat(m, ".then")), i);
              } else if (f) return f.$_validate(e, a.nest(f, "".concat(m, ".otherwise")), i);
            }
          }
          return u.errors(o, t);
        },
        rules: {
          conditional: {
            method: function method(e, t) {
              s(!this._flags._endedSwitch, "Unreachable condition"), s(!this._flags.match, "Cannot combine match mode", this._flags.match, "with conditional rule"), s(void 0 === t["break"], "Cannot use break option with alternatives conditional");
              var r = this.clone(),
                n = o.when(r, e, t),
                a = n.is ? [n] : n["switch"];
              var _iterator26 = _createForOfIteratorHelper(a),
                _step26;
              try {
                for (_iterator26.s(); !(_step26 = _iterator26.n()).done;) {
                  var _e28 = _step26.value;
                  if (_e28.then && _e28.otherwise) {
                    r.$_setFlag("_endedSwitch", !0, {
                      clone: !1
                    });
                    break;
                  }
                }
              } catch (err) {
                _iterator26.e(err);
              } finally {
                _iterator26.f();
              }
              return r.$_terms.matches.push(n), r.$_mutateRebuild();
            }
          },
          match: {
            method: function method(e) {
              if (s(["any", "one", "all"].includes(e), "Invalid alternatives match mode", e), "any" !== e) {
                var _iterator27 = _createForOfIteratorHelper(this.$_terms.matches),
                  _step27;
                try {
                  for (_iterator27.s(); !(_step27 = _iterator27.n()).done;) {
                    var _t34 = _step27.value;
                    s(_t34.schema, "Cannot combine match mode", e, "with conditional rules");
                  }
                } catch (err) {
                  _iterator27.e(err);
                } finally {
                  _iterator27.f();
                }
              }
              return this.$_setFlag("match", e);
            }
          },
          "try": {
            method: function method() {
              for (var _len15 = arguments.length, e = new Array(_len15), _key15 = 0; _key15 < _len15; _key15++) {
                e[_key15] = arguments[_key15];
              }
              s(e.length, "Missing alternative schemas"), i.verifyFlat(e, "try"), s(!this._flags._endedSwitch, "Unreachable condition");
              var t = this.clone();
              for (var _i15 = 0, _e29 = e; _i15 < _e29.length; _i15++) {
                var _r27 = _e29[_i15];
                t.$_terms.matches.push({
                  schema: t.$_compile(_r27)
                });
              }
              return t.$_mutateRebuild();
            }
          }
        },
        overrides: {
          label: function label(e) {
            return this.$_parent("label", e).$_modify({
              each: function each(t, r) {
                return "is" !== r.path[0] && "string" != typeof t._flags.label ? t.label(e) : void 0;
              },
              ref: !1
            });
          }
        },
        rebuild: function rebuild(e) {
          e.$_modify({
            each: function each(t) {
              i.isSchema(t) && "array" === t.type && e.$_setFlag("_arrayItems", !0, {
                clone: !1
              });
            }
          });
        },
        manifest: {
          build: function build(e, t) {
            if (t.matches) {
              var _iterator28 = _createForOfIteratorHelper(t.matches),
                _step28;
              try {
                for (_iterator28.s(); !(_step28 = _iterator28.n()).done;) {
                  var _r28 = _step28.value;
                  var _t35 = _r28.schema,
                    _s17 = _r28.ref,
                    _n22 = _r28.is,
                    _a8 = _r28.not,
                    _i16 = _r28.then,
                    _o0 = _r28.otherwise;
                  e = _t35 ? e["try"](_t35) : _s17 ? e.conditional(_s17, {
                    is: _n22,
                    then: _i16,
                    not: _a8,
                    otherwise: _o0,
                    "switch": _r28["switch"]
                  }) : e.conditional(_n22, {
                    then: _i16,
                    otherwise: _o0
                  });
                }
              } catch (err) {
                _iterator28.e(err);
              } finally {
                _iterator28.f();
              }
            }
            return e;
          }
        },
        messages: {
          "alternatives.all": "{{#label}} does not match all of the required types",
          "alternatives.any": "{{#label}} does not match any of the allowed types",
          "alternatives.match": "{{#label}} does not match any of the allowed types",
          "alternatives.one": "{{#label}} matches more than one allowed type",
          "alternatives.types": "{{#label}} must be one of {{#types}}"
        }
      }), u.errors = function (e, _ref7) {
        var t = _ref7.error,
          r = _ref7.state;
        if (!e.length) return {
          errors: t("alternatives.any")
        };
        if (1 === e.length) return {
          errors: e[0].reports
        };
        var s = new Set(),
          n = [];
        var _iterator29 = _createForOfIteratorHelper(e),
          _step29;
        try {
          for (_iterator29.s(); !(_step29 = _iterator29.n()).done;) {
            var _step29$value = _step29.value,
              _a9 = _step29$value.reports,
              _i17 = _step29$value.schema;
            if (_a9.length > 1) return u.unmatched(e, t);
            var _o1 = _a9[0];
            if (_o1 instanceof l.Report == 0) return u.unmatched(e, t);
            if (_o1.state.path.length !== r.path.length) {
              n.push({
                type: _i17.type,
                report: _o1
              });
              continue;
            }
            if ("any.only" === _o1.code) {
              var _iterator30 = _createForOfIteratorHelper(_o1.local.valids),
                _step30;
              try {
                for (_iterator30.s(); !(_step30 = _iterator30.n()).done;) {
                  var _e30 = _step30.value;
                  s.add(_e30);
                }
              } catch (err) {
                _iterator30.e(err);
              } finally {
                _iterator30.f();
              }
              continue;
            }
            var _o1$code$split = _o1.code.split("."),
              _o1$code$split2 = _slicedToArray(_o1$code$split, 2),
              _c0 = _o1$code$split2[0],
              f = _o1$code$split2[1];
            "base" !== f ? n.push({
              type: _i17.type,
              report: _o1
            }) : "object.base" === _o1.code ? s.add(_o1.local.type) : s.add(_c0);
          }
        } catch (err) {
          _iterator29.e(err);
        } finally {
          _iterator29.f();
        }
        return n.length ? 1 === n.length ? {
          errors: n[0].report
        } : u.unmatched(e, t) : {
          errors: t("alternatives.types", {
            types: _toConsumableArray(s)
          })
        };
      }, u.unmatched = function (e, t) {
        var r = [];
        var _iterator31 = _createForOfIteratorHelper(e),
          _step31;
        try {
          for (_iterator31.s(); !(_step31 = _iterator31.n()).done;) {
            var _t36 = _step31.value;
            r.push.apply(r, _toConsumableArray(_t36.reports));
          }
        } catch (err) {
          _iterator31.e(err);
        } finally {
          _iterator31.f();
        }
        return {
          errors: t("alternatives.match", l.details(r, {
            override: !1
          }))
        };
      };
    },
    8068: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(7629),
        a = r(8160),
        i = r(6914);
      e.exports = n.extend({
        type: "any",
        flags: {
          only: {
            "default": !1
          }
        },
        terms: {
          alterations: {
            init: null
          },
          examples: {
            init: null
          },
          externals: {
            init: null
          },
          metas: {
            init: []
          },
          notes: {
            init: []
          },
          shared: {
            init: null
          },
          tags: {
            init: []
          },
          whens: {
            init: null
          }
        },
        rules: {
          custom: {
            method: function method(e, t) {
              return s("function" == typeof e, "Method must be a function"), s(void 0 === t || t && "string" == typeof t, "Description must be a non-empty string"), this.$_addRule({
                name: "custom",
                args: {
                  method: e,
                  description: t
                }
              });
            },
            validate: function validate(e, t, _ref8) {
              var r = _ref8.method;
              try {
                return r(e, t);
              } catch (e) {
                return t.error("any.custom", {
                  error: e
                });
              }
            },
            args: ["method", "description"],
            multi: !0
          },
          messages: {
            method: function method(e) {
              return this.prefs({
                messages: e
              });
            }
          },
          shared: {
            method: function method(e) {
              s(a.isSchema(e) && e._flags.id, "Schema must be a schema with an id");
              var t = this.clone();
              return t.$_terms.shared = t.$_terms.shared || [], t.$_terms.shared.push(e), t.$_mutateRegister(e), t;
            }
          },
          warning: {
            method: function method(e, t) {
              return s(e && "string" == typeof e, "Invalid warning code"), this.$_addRule({
                name: "warning",
                args: {
                  code: e,
                  local: t
                },
                warn: !0
              });
            },
            validate: function validate(e, t, _ref9) {
              var r = _ref9.code,
                s = _ref9.local;
              return t.error(r, s);
            },
            args: ["code", "local"],
            multi: !0
          }
        },
        modifiers: {
          keep: function keep(e) {
            var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : !0;
            e.keep = t;
          },
          message: function message(e, t) {
            e.message = i.compile(t);
          },
          warn: function warn(e) {
            var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : !0;
            e.warn = t;
          }
        },
        manifest: {
          build: function build(e, t) {
            for (var _r29 in t) {
              var _s18 = t[_r29];
              if (["examples", "externals", "metas", "notes", "tags"].includes(_r29)) {
                var _iterator32 = _createForOfIteratorHelper(_s18),
                  _step32;
                try {
                  for (_iterator32.s(); !(_step32 = _iterator32.n()).done;) {
                    var _t37 = _step32.value;
                    e = e[_r29.slice(0, -1)](_t37);
                  }
                } catch (err) {
                  _iterator32.e(err);
                } finally {
                  _iterator32.f();
                }
              } else if ("alterations" !== _r29) {
                if ("whens" !== _r29) {
                  if ("shared" === _r29) {
                    var _iterator33 = _createForOfIteratorHelper(_s18),
                      _step33;
                    try {
                      for (_iterator33.s(); !(_step33 = _iterator33.n()).done;) {
                        var _t38 = _step33.value;
                        e = e.shared(_t38);
                      }
                    } catch (err) {
                      _iterator33.e(err);
                    } finally {
                      _iterator33.f();
                    }
                  }
                } else {
                  var _iterator34 = _createForOfIteratorHelper(_s18),
                    _step34;
                  try {
                    for (_iterator34.s(); !(_step34 = _iterator34.n()).done;) {
                      var _t39 = _step34.value;
                      var _r30 = _t39.ref,
                        _s19 = _t39.is,
                        _n23 = _t39.not,
                        _a0 = _t39.then,
                        _i18 = _t39.otherwise,
                        o = _t39.concat;
                      e = o ? e.concat(o) : _r30 ? e.when(_r30, {
                        is: _s19,
                        not: _n23,
                        then: _a0,
                        otherwise: _i18,
                        "switch": _t39["switch"],
                        "break": _t39["break"]
                      }) : e.when(_s19, {
                        then: _a0,
                        otherwise: _i18,
                        "break": _t39["break"]
                      });
                    }
                  } catch (err) {
                    _iterator34.e(err);
                  } finally {
                    _iterator34.f();
                  }
                }
              } else {
                var _t40 = {};
                var _iterator35 = _createForOfIteratorHelper(_s18),
                  _step35;
                try {
                  for (_iterator35.s(); !(_step35 = _iterator35.n()).done;) {
                    var _step35$value = _step35.value,
                      _e31 = _step35$value.target,
                      _r31 = _step35$value.adjuster;
                    _t40[_e31] = _r31;
                  }
                } catch (err) {
                  _iterator35.e(err);
                } finally {
                  _iterator35.f();
                }
                e = e.alter(_t40);
              }
            }
            return e;
          }
        },
        messages: {
          "any.custom": "{{#label}} failed custom validation because {{#error.message}}",
          "any.default": "{{#label}} threw an error when running default method",
          "any.failover": "{{#label}} threw an error when running failover method",
          "any.invalid": "{{#label}} contains an invalid value",
          "any.only": '{{#label}} must be {if(#valids.length == 1, "", "one of ")}{{#valids}}',
          "any.ref": "{{#label}} {{#arg}} references {{:#ref}} which {{#reason}}",
          "any.required": "{{#label}} is required",
          "any.unknown": "{{#label}} is not allowed"
        }
      });
    },
    546: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(9474),
        a = r(9621),
        i = r(8068),
        o = r(8160),
        l = r(3292),
        c = {};
      e.exports = i.extend({
        type: "array",
        flags: {
          single: {
            "default": !1
          },
          sparse: {
            "default": !1
          }
        },
        terms: {
          items: {
            init: [],
            manifest: "schema"
          },
          ordered: {
            init: [],
            manifest: "schema"
          },
          _exclusions: {
            init: []
          },
          _inclusions: {
            init: []
          },
          _requireds: {
            init: []
          }
        },
        coerce: {
          from: "object",
          method: function method(e, _ref0) {
            var t = _ref0.schema,
              r = _ref0.state,
              s = _ref0.prefs;
            if (!Array.isArray(e)) return;
            var n = t.$_getRule("sort");
            return n ? c.sort(t, e, n.args.options, r, s) : void 0;
          }
        },
        validate: function validate(e, _ref1) {
          var t = _ref1.schema,
            r = _ref1.error;
          if (!Array.isArray(e)) {
            if (t._flags.single) {
              var _t41 = [e];
              return _t41[o.symbols.arraySingle] = !0, {
                value: _t41
              };
            }
            return {
              errors: r("array.base")
            };
          }
          if (t.$_getRule("items") || t.$_terms.externals) return {
            value: e.slice()
          };
        },
        rules: {
          has: {
            method: function method(e) {
              e = this.$_compile(e, {
                appendPath: !0
              });
              var t = this.$_addRule({
                name: "has",
                args: {
                  schema: e
                }
              });
              return t.$_mutateRegister(e), t;
            },
            validate: function validate(e, _ref10, _ref11) {
              var t = _ref10.state,
                r = _ref10.prefs,
                s = _ref10.error;
              var n = _ref11.schema;
              var a = [e].concat(_toConsumableArray(t.ancestors));
              for (var _s20 = 0; _s20 < e.length; ++_s20) {
                var _i19 = t.localize([].concat(_toConsumableArray(t.path), [_s20]), a, n);
                if (n.$_match(e[_s20], _i19, r)) return e;
              }
              var i = n._flags.label;
              return i ? s("array.hasKnown", {
                patternLabel: i
              }) : s("array.hasUnknown", null);
            },
            multi: !0
          },
          items: {
            method: function method() {
              var _this6 = this;
              for (var _len16 = arguments.length, e = new Array(_len16), _key16 = 0; _key16 < _len16; _key16++) {
                e[_key16] = arguments[_key16];
              }
              o.verifyFlat(e, "items");
              var t = this.$_addRule("items");
              var _loop8 = function _loop8(_r32) {
                var s = o.tryWithPath(function () {
                  return _this6.$_compile(e[_r32]);
                }, _r32, {
                  append: !0
                });
                t.$_terms.items.push(s);
              };
              for (var _r32 = 0; _r32 < e.length; ++_r32) {
                _loop8(_r32);
              }
              return t.$_mutateRebuild();
            },
            validate: function validate(e, _ref12) {
              var t = _ref12.schema,
                r = _ref12.error,
                s = _ref12.state,
                n = _ref12.prefs,
                a = _ref12.errorsArray;
              var i = t.$_terms._requireds.slice(),
                l = t.$_terms.ordered.slice(),
                u = [].concat(_toConsumableArray(t.$_terms._inclusions), _toConsumableArray(i)),
                f = !e[o.symbols.arraySingle];
              delete e[o.symbols.arraySingle];
              var m = a();
              var h = e.length;
              for (var _a1 = 0; _a1 < h; ++_a1) {
                var _o10 = e[_a1];
                var d = !1,
                  p = !1;
                var g = f ? _a1 : new Number(_a1),
                  y = [].concat(_toConsumableArray(s.path), [g]);
                if (!t._flags.sparse && void 0 === _o10) {
                  if (m.push(r("array.sparse", {
                    key: g,
                    path: y,
                    pos: _a1,
                    value: void 0
                  }, s.localize(y))), n.abortEarly) return m;
                  l.shift();
                  continue;
                }
                var b = [e].concat(_toConsumableArray(s.ancestors));
                var _iterator36 = _createForOfIteratorHelper(t.$_terms._exclusions),
                  _step36;
                try {
                  for (_iterator36.s(); !(_step36 = _iterator36.n()).done;) {
                    var _e32 = _step36.value;
                    if (_e32.$_match(_o10, s.localize(y, b, _e32), n, {
                      presence: "ignore"
                    })) {
                      if (m.push(r("array.excludes", {
                        pos: _a1,
                        value: _o10
                      }, s.localize(y))), n.abortEarly) return m;
                      d = !0, l.shift();
                      break;
                    }
                  }
                } catch (err) {
                  _iterator36.e(err);
                } finally {
                  _iterator36.f();
                }
                if (d) continue;
                if (t.$_terms.ordered.length) {
                  if (l.length) {
                    var _i20 = l.shift(),
                      _u3 = _i20.$_validate(_o10, s.localize(y, b, _i20), n);
                    if (_u3.errors) {
                      if (m.push.apply(m, _toConsumableArray(_u3.errors)), n.abortEarly) return m;
                    } else if ("strip" === _i20._flags.result) c.fastSplice(e, _a1), --_a1, --h;else {
                      if (!t._flags.sparse && void 0 === _u3.value) {
                        if (m.push(r("array.sparse", {
                          key: g,
                          path: y,
                          pos: _a1,
                          value: void 0
                        }, s.localize(y))), n.abortEarly) return m;
                        continue;
                      }
                      e[_a1] = _u3.value;
                    }
                    continue;
                  }
                  if (!t.$_terms.items.length) {
                    if (m.push(r("array.orderedLength", {
                      pos: _a1,
                      limit: t.$_terms.ordered.length
                    })), n.abortEarly) return m;
                    break;
                  }
                }
                var v = [];
                var _ = i.length;
                for (var _l7 = 0; _l7 < _; ++_l7) {
                  var _u4 = s.localize(y, b, i[_l7]);
                  _u4.snapshot();
                  var _f2 = i[_l7].$_validate(_o10, _u4, n);
                  if (v[_l7] = _f2, !_f2.errors) {
                    if (_u4.commit(), e[_a1] = _f2.value, p = !0, c.fastSplice(i, _l7), --_l7, --_, !t._flags.sparse && void 0 === _f2.value && (m.push(r("array.sparse", {
                      key: g,
                      path: y,
                      pos: _a1,
                      value: void 0
                    }, s.localize(y))), n.abortEarly)) return m;
                    break;
                  }
                  _u4.restore();
                }
                if (p) continue;
                var w = n.stripUnknown && !!n.stripUnknown.arrays || !1;
                _ = u.length;
                var _iterator37 = _createForOfIteratorHelper(u),
                  _step37;
                try {
                  for (_iterator37.s(); !(_step37 = _iterator37.n()).done;) {
                    var _l8 = _step37.value;
                    var _u5 = void 0;
                    var _f3 = i.indexOf(_l8);
                    if (-1 !== _f3) _u5 = v[_f3];else {
                      var _i21 = s.localize(y, b, _l8);
                      if (_i21.snapshot(), _u5 = _l8.$_validate(_o10, _i21, n), !_u5.errors) {
                        _i21.commit(), "strip" === _l8._flags.result ? (c.fastSplice(e, _a1), --_a1, --h) : t._flags.sparse || void 0 !== _u5.value ? e[_a1] = _u5.value : (m.push(r("array.sparse", {
                          key: g,
                          path: y,
                          pos: _a1,
                          value: void 0
                        }, s.localize(y))), d = !0), p = !0;
                        break;
                      }
                      _i21.restore();
                    }
                    if (1 === _) {
                      if (w) {
                        c.fastSplice(e, _a1), --_a1, --h, p = !0;
                        break;
                      }
                      if (m.push.apply(m, _toConsumableArray(_u5.errors)), n.abortEarly) return m;
                      d = !0;
                      break;
                    }
                  }
                } catch (err) {
                  _iterator37.e(err);
                } finally {
                  _iterator37.f();
                }
                if (!d && (t.$_terms._inclusions.length || t.$_terms._requireds.length) && !p) {
                  if (w) {
                    c.fastSplice(e, _a1), --_a1, --h;
                    continue;
                  }
                  if (m.push(r("array.includes", {
                    pos: _a1,
                    value: _o10
                  }, s.localize(y))), n.abortEarly) return m;
                }
              }
              return i.length && c.fillMissedErrors(t, m, i, e, s, n), l.length && (c.fillOrderedErrors(t, m, l, e, s, n), m.length || c.fillDefault(l, e, s, n)), m.length ? m : e;
            },
            priority: !0,
            manifest: !1
          },
          length: {
            method: function method(e) {
              return this.$_addRule({
                name: "length",
                args: {
                  limit: e
                },
                operator: "="
              });
            },
            validate: function validate(e, t, _ref13, _ref14) {
              var r = _ref13.limit;
              var s = _ref14.name,
                n = _ref14.operator,
                a = _ref14.args;
              return o.compare(e.length, r, n) ? e : t.error("array." + s, {
                limit: a.limit,
                value: e
              });
            },
            args: [{
              name: "limit",
              ref: !0,
              assert: o.limit,
              message: "must be a positive integer"
            }]
          },
          max: {
            method: function method(e) {
              return this.$_addRule({
                name: "max",
                method: "length",
                args: {
                  limit: e
                },
                operator: "<="
              });
            }
          },
          min: {
            method: function method(e) {
              return this.$_addRule({
                name: "min",
                method: "length",
                args: {
                  limit: e
                },
                operator: ">="
              });
            }
          },
          ordered: {
            method: function method() {
              var _this7 = this;
              for (var _len17 = arguments.length, e = new Array(_len17), _key17 = 0; _key17 < _len17; _key17++) {
                e[_key17] = arguments[_key17];
              }
              o.verifyFlat(e, "ordered");
              var t = this.$_addRule("items");
              var _loop9 = function _loop9(_r33) {
                var s = o.tryWithPath(function () {
                  return _this7.$_compile(e[_r33]);
                }, _r33, {
                  append: !0
                });
                c.validateSingle(s, t), t.$_mutateRegister(s), t.$_terms.ordered.push(s);
              };
              for (var _r33 = 0; _r33 < e.length; ++_r33) {
                _loop9(_r33);
              }
              return t.$_mutateRebuild();
            }
          },
          single: {
            method: function method(e) {
              var t = void 0 === e || !!e;
              return s(!t || !this._flags._arrayItems, "Cannot specify single rule when array has array items"), this.$_setFlag("single", t);
            }
          },
          sort: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
              o.assertOptions(e, ["by", "order"]);
              var t = {
                order: e.order || "ascending"
              };
              return e.by && (t.by = l.ref(e.by, {
                ancestor: 0
              }), s(!t.by.ancestor, "Cannot sort by ancestor")), this.$_addRule({
                name: "sort",
                args: {
                  options: t
                }
              });
            },
            validate: function validate(e, _ref15, _ref16) {
              var t = _ref15.error,
                r = _ref15.state,
                s = _ref15.prefs,
                n = _ref15.schema;
              var a = _ref16.options;
              var _c$sort = c.sort(n, e, a, r, s),
                i = _c$sort.value,
                o = _c$sort.errors;
              if (o) return o;
              for (var _r34 = 0; _r34 < e.length; ++_r34) if (e[_r34] !== i[_r34]) return t("array.sort", {
                order: a.order,
                by: a.by ? a.by.key : "value"
              });
              return e;
            },
            convert: !0
          },
          sparse: {
            method: function method(e) {
              var t = void 0 === e || !!e;
              return this._flags.sparse === t ? this : (t ? this.clone() : this.$_addRule("items")).$_setFlag("sparse", t, {
                clone: !1
              });
            }
          },
          unique: {
            method: function method(e) {
              var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
              s(!e || "function" == typeof e || "string" == typeof e, "comparator must be a function or a string"), o.assertOptions(t, ["ignoreUndefined", "separator"]);
              var r = {
                name: "unique",
                args: {
                  options: t,
                  comparator: e
                }
              };
              if (e) if ("string" == typeof e) {
                var _s21 = o["default"](t.separator, ".");
                r.path = _s21 ? e.split(_s21) : [e];
              } else r.comparator = e;
              return this.$_addRule(r);
            },
            validate: function validate(e, _ref17, _ref18, _ref19) {
              var t = _ref17.state,
                r = _ref17.error,
                i = _ref17.schema;
              var o = _ref18.comparator,
                l = _ref18.options;
              var c = _ref19.comparator,
                u = _ref19.path;
              var f = {
                  string: Object.create(null),
                  number: Object.create(null),
                  undefined: Object.create(null),
                  "boolean": Object.create(null),
                  bigint: Object.create(null),
                  object: new Map(),
                  "function": new Map(),
                  custom: new Map()
                },
                m = c || n,
                h = l.ignoreUndefined;
              for (var _n24 = 0; _n24 < e.length; ++_n24) {
                var _i22 = u ? a(e[_n24], u) : e[_n24],
                  _l9 = c ? f.custom : f[_typeof(_i22)];
                if (s(_l9, "Failed to find unique map container for type", _typeof(_i22)), _l9 instanceof Map) {
                  var _s22 = _l9.entries();
                  var _a10 = void 0;
                  for (; !(_a10 = _s22.next()).done;) if (m(_a10.value[0], _i22)) {
                    var _s23 = t.localize([].concat(_toConsumableArray(t.path), [_n24]), [e].concat(_toConsumableArray(t.ancestors))),
                      _i23 = {
                        pos: _n24,
                        value: e[_n24],
                        dupePos: _a10.value[1],
                        dupeValue: e[_a10.value[1]]
                      };
                    return u && (_i23.path = o), r("array.unique", _i23, _s23);
                  }
                  _l9.set(_i22, _n24);
                } else {
                  if ((!h || void 0 !== _i22) && void 0 !== _l9[_i22]) {
                    var _s24 = {
                      pos: _n24,
                      value: e[_n24],
                      dupePos: _l9[_i22],
                      dupeValue: e[_l9[_i22]]
                    };
                    return u && (_s24.path = o), r("array.unique", _s24, t.localize([].concat(_toConsumableArray(t.path), [_n24]), [e].concat(_toConsumableArray(t.ancestors))));
                  }
                  _l9[_i22] = _n24;
                }
              }
              return e;
            },
            args: ["comparator", "options"],
            multi: !0
          }
        },
        cast: {
          set: {
            from: Array.isArray,
            to: function to(e, t) {
              return new Set(e);
            }
          }
        },
        rebuild: function rebuild(e) {
          e.$_terms._inclusions = [], e.$_terms._exclusions = [], e.$_terms._requireds = [];
          var _iterator38 = _createForOfIteratorHelper(e.$_terms.items),
            _step38;
          try {
            for (_iterator38.s(); !(_step38 = _iterator38.n()).done;) {
              var _t42 = _step38.value;
              c.validateSingle(_t42, e), "required" === _t42._flags.presence ? e.$_terms._requireds.push(_t42) : "forbidden" === _t42._flags.presence ? e.$_terms._exclusions.push(_t42) : e.$_terms._inclusions.push(_t42);
            }
          } catch (err) {
            _iterator38.e(err);
          } finally {
            _iterator38.f();
          }
          var _iterator39 = _createForOfIteratorHelper(e.$_terms.ordered),
            _step39;
          try {
            for (_iterator39.s(); !(_step39 = _iterator39.n()).done;) {
              var _t43 = _step39.value;
              c.validateSingle(_t43, e);
            }
          } catch (err) {
            _iterator39.e(err);
          } finally {
            _iterator39.f();
          }
        },
        manifest: {
          build: function build(e, t) {
            var _e33, _e34;
            return t.items && (e = (_e33 = e).items.apply(_e33, _toConsumableArray(t.items))), t.ordered && (e = (_e34 = e).ordered.apply(_e34, _toConsumableArray(t.ordered))), e;
          }
        },
        messages: {
          "array.base": "{{#label}} must be an array",
          "array.excludes": "{{#label}} contains an excluded value",
          "array.hasKnown": "{{#label}} does not contain at least one required match for type {:#patternLabel}",
          "array.hasUnknown": "{{#label}} does not contain at least one required match",
          "array.includes": "{{#label}} does not match any of the allowed types",
          "array.includesRequiredBoth": "{{#label}} does not contain {{#knownMisses}} and {{#unknownMisses}} other required value(s)",
          "array.includesRequiredKnowns": "{{#label}} does not contain {{#knownMisses}}",
          "array.includesRequiredUnknowns": "{{#label}} does not contain {{#unknownMisses}} required value(s)",
          "array.length": "{{#label}} must contain {{#limit}} items",
          "array.max": "{{#label}} must contain less than or equal to {{#limit}} items",
          "array.min": "{{#label}} must contain at least {{#limit}} items",
          "array.orderedLength": "{{#label}} must contain at most {{#limit}} items",
          "array.sort": "{{#label}} must be sorted in {#order} order by {{#by}}",
          "array.sort.mismatching": "{{#label}} cannot be sorted due to mismatching types",
          "array.sort.unsupported": "{{#label}} cannot be sorted due to unsupported type {#type}",
          "array.sparse": "{{#label}} must not be a sparse array item",
          "array.unique": "{{#label}} contains a duplicate value"
        }
      }), c.fillMissedErrors = function (e, t, r, s, n, a) {
        var i = [];
        var o = 0;
        var _iterator40 = _createForOfIteratorHelper(r),
          _step40;
        try {
          for (_iterator40.s(); !(_step40 = _iterator40.n()).done;) {
            var _e35 = _step40.value;
            var _t44 = _e35._flags.label;
            _t44 ? i.push(_t44) : ++o;
          }
        } catch (err) {
          _iterator40.e(err);
        } finally {
          _iterator40.f();
        }
        i.length ? o ? t.push(e.$_createError("array.includesRequiredBoth", s, {
          knownMisses: i,
          unknownMisses: o
        }, n, a)) : t.push(e.$_createError("array.includesRequiredKnowns", s, {
          knownMisses: i
        }, n, a)) : t.push(e.$_createError("array.includesRequiredUnknowns", s, {
          unknownMisses: o
        }, n, a));
      }, c.fillOrderedErrors = function (e, t, r, s, n, a) {
        var i = [];
        var _iterator41 = _createForOfIteratorHelper(r),
          _step41;
        try {
          for (_iterator41.s(); !(_step41 = _iterator41.n()).done;) {
            var _e36 = _step41.value;
            "required" === _e36._flags.presence && i.push(_e36);
          }
        } catch (err) {
          _iterator41.e(err);
        } finally {
          _iterator41.f();
        }
        i.length && c.fillMissedErrors(e, t, i, s, n, a);
      }, c.fillDefault = function (e, t, r, s) {
        var n = [];
        var a = !0;
        for (var _i24 = e.length - 1; _i24 >= 0; --_i24) {
          var _o11 = e[_i24],
            _l0 = [t].concat(_toConsumableArray(r.ancestors)),
            _c1 = _o11.$_validate(void 0, r.localize(r.path, _l0, _o11), s).value;
          if (a) {
            if (void 0 === _c1) continue;
            a = !1;
          }
          n.unshift(_c1);
        }
        n.length && t.push.apply(t, n);
      }, c.fastSplice = function (e, t) {
        var r = t;
        for (; r < e.length;) e[r++] = e[r];
        --e.length;
      }, c.validateSingle = function (e, t) {
        ("array" === e.type || e._flags._arrayItems) && (s(!t._flags.single, "Cannot specify array item with single rule enabled"), t.$_setFlag("_arrayItems", !0, {
          clone: !1
        }));
      }, c.sort = function (e, t, r, s, n) {
        var a = "ascending" === r.order ? 1 : -1,
          i = -1 * a,
          o = a,
          l = function l(_l1, u) {
            var f = c.compare(_l1, u, i, o);
            if (null !== f) return f;
            if (r.by && (_l1 = r.by.resolve(_l1, s, n), u = r.by.resolve(u, s, n)), f = c.compare(_l1, u, i, o), null !== f) return f;
            var m = _typeof(_l1);
            if (m !== _typeof(u)) throw e.$_createError("array.sort.mismatching", t, null, s, n);
            if ("number" !== m && "string" !== m) throw e.$_createError("array.sort.unsupported", t, {
              type: m
            }, s, n);
            return "number" === m ? (_l1 - u) * a : _l1 < u ? i : o;
          };
        try {
          return {
            value: t.slice().sort(l)
          };
        } catch (e) {
          return {
            errors: e
          };
        }
      }, c.compare = function (e, t, r, s) {
        return e === t ? 0 : void 0 === e ? 1 : void 0 === t ? -1 : null === e ? s : null === t ? r : null;
      };
    },
    4937: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8068),
        a = r(8160),
        i = r(2036),
        o = {
          isBool: function isBool(e) {
            return "boolean" == typeof e;
          }
        };
      e.exports = n.extend({
        type: "boolean",
        flags: {
          sensitive: {
            "default": !1
          }
        },
        terms: {
          falsy: {
            init: null,
            manifest: "values"
          },
          truthy: {
            init: null,
            manifest: "values"
          }
        },
        coerce: function coerce(e, _ref20) {
          var t = _ref20.schema;
          if ("boolean" != typeof e) {
            if ("string" == typeof e) {
              var _r35 = t._flags.sensitive ? e : e.toLowerCase();
              e = "true" === _r35 || "false" !== _r35 && e;
            }
            return "boolean" != typeof e && (e = t.$_terms.truthy && t.$_terms.truthy.has(e, null, null, !t._flags.sensitive) || (!t.$_terms.falsy || !t.$_terms.falsy.has(e, null, null, !t._flags.sensitive)) && e), {
              value: e
            };
          }
        },
        validate: function validate(e, _ref21) {
          var t = _ref21.error;
          if ("boolean" != typeof e) return {
            value: e,
            errors: t("boolean.base")
          };
        },
        rules: {
          truthy: {
            method: function method() {
              for (var _len18 = arguments.length, e = new Array(_len18), _key18 = 0; _key18 < _len18; _key18++) {
                e[_key18] = arguments[_key18];
              }
              a.verifyFlat(e, "truthy");
              var t = this.clone();
              t.$_terms.truthy = t.$_terms.truthy || new i();
              for (var _r36 = 0; _r36 < e.length; ++_r36) {
                var _n25 = e[_r36];
                s(void 0 !== _n25, "Cannot call truthy with undefined"), t.$_terms.truthy.add(_n25);
              }
              return t;
            }
          },
          falsy: {
            method: function method() {
              for (var _len19 = arguments.length, e = new Array(_len19), _key19 = 0; _key19 < _len19; _key19++) {
                e[_key19] = arguments[_key19];
              }
              a.verifyFlat(e, "falsy");
              var t = this.clone();
              t.$_terms.falsy = t.$_terms.falsy || new i();
              for (var _r37 = 0; _r37 < e.length; ++_r37) {
                var _n26 = e[_r37];
                s(void 0 !== _n26, "Cannot call falsy with undefined"), t.$_terms.falsy.add(_n26);
              }
              return t;
            }
          },
          sensitive: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : !0;
              return this.$_setFlag("sensitive", e);
            }
          }
        },
        cast: {
          number: {
            from: o.isBool,
            to: function to(e, t) {
              return e ? 1 : 0;
            }
          },
          string: {
            from: o.isBool,
            to: function to(e, t) {
              return e ? "true" : "false";
            }
          }
        },
        manifest: {
          build: function build(e, t) {
            var _e37, _e38;
            return t.truthy && (e = (_e37 = e).truthy.apply(_e37, _toConsumableArray(t.truthy))), t.falsy && (e = (_e38 = e).falsy.apply(_e38, _toConsumableArray(t.falsy))), e;
          }
        },
        messages: {
          "boolean.base": "{{#label}} must be a boolean"
        }
      });
    },
    7500: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8068),
        a = r(8160),
        i = r(3328),
        o = {
          isDate: function isDate(e) {
            return e instanceof Date;
          }
        };
      e.exports = n.extend({
        type: "date",
        coerce: {
          from: ["number", "string"],
          method: function method(e, _ref22) {
            var t = _ref22.schema;
            return {
              value: o.parse(e, t._flags.format) || e
            };
          }
        },
        validate: function validate(e, _ref23) {
          var t = _ref23.schema,
            r = _ref23.error,
            s = _ref23.prefs;
          if (e instanceof Date && !isNaN(e.getTime())) return;
          var n = t._flags.format;
          return s.convert && n && "string" == typeof e ? {
            value: e,
            errors: r("date.format", {
              format: n
            })
          } : {
            value: e,
            errors: r("date.base")
          };
        },
        rules: {
          compare: {
            method: !1,
            validate: function validate(e, t, _ref24, _ref25) {
              var r = _ref24.date;
              var s = _ref25.name,
                n = _ref25.operator,
                i = _ref25.args;
              var o = "now" === r ? Date.now() : r.getTime();
              return a.compare(e.getTime(), o, n) ? e : t.error("date." + s, {
                limit: i.date,
                value: e
              });
            },
            args: [{
              name: "date",
              ref: !0,
              normalize: function normalize(e) {
                return "now" === e ? e : o.parse(e);
              },
              assert: function assert(e) {
                return null !== e;
              },
              message: "must have a valid date format"
            }]
          },
          format: {
            method: function method(e) {
              return s(["iso", "javascript", "unix"].includes(e), "Unknown date format", e), this.$_setFlag("format", e);
            }
          },
          greater: {
            method: function method(e) {
              return this.$_addRule({
                name: "greater",
                method: "compare",
                args: {
                  date: e
                },
                operator: ">"
              });
            }
          },
          iso: {
            method: function method() {
              return this.format("iso");
            }
          },
          less: {
            method: function method(e) {
              return this.$_addRule({
                name: "less",
                method: "compare",
                args: {
                  date: e
                },
                operator: "<"
              });
            }
          },
          max: {
            method: function method(e) {
              return this.$_addRule({
                name: "max",
                method: "compare",
                args: {
                  date: e
                },
                operator: "<="
              });
            }
          },
          min: {
            method: function method(e) {
              return this.$_addRule({
                name: "min",
                method: "compare",
                args: {
                  date: e
                },
                operator: ">="
              });
            }
          },
          timestamp: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : "javascript";
              return s(["javascript", "unix"].includes(e), '"type" must be one of "javascript, unix"'), this.format(e);
            }
          }
        },
        cast: {
          number: {
            from: o.isDate,
            to: function to(e, t) {
              return e.getTime();
            }
          },
          string: {
            from: o.isDate,
            to: function to(e, _ref26) {
              var t = _ref26.prefs;
              return i.date(e, t);
            }
          }
        },
        messages: {
          "date.base": "{{#label}} must be a valid date",
          "date.format": '{{#label}} must be in {msg("date.format." + #format) || #format} format',
          "date.greater": "{{#label}} must be greater than {{:#limit}}",
          "date.less": "{{#label}} must be less than {{:#limit}}",
          "date.max": "{{#label}} must be less than or equal to {{:#limit}}",
          "date.min": "{{#label}} must be greater than or equal to {{:#limit}}",
          "date.format.iso": "ISO 8601 date",
          "date.format.javascript": "timestamp or number of milliseconds",
          "date.format.unix": "timestamp or number of seconds"
        }
      }), o.parse = function (e, t) {
        if (e instanceof Date) return e;
        if ("string" != typeof e && (isNaN(e) || !isFinite(e))) return null;
        if (/^\s*$/.test(e)) return null;
        if ("iso" === t) return a.isIsoDate(e) ? o.date(e.toString()) : null;
        var r = e;
        if ("string" == typeof e && /^[+-]?\d+(\.\d+)?$/.test(e) && (e = parseFloat(e)), t) {
          if ("javascript" === t) return o.date(1 * e);
          if ("unix" === t) return o.date(1e3 * e);
          if ("string" == typeof r) return null;
        }
        return o.date(e);
      }, o.date = function (e) {
        var t = new Date(e);
        return isNaN(t.getTime()) ? null : t;
      };
    },
    390: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(7824);
      e.exports = n.extend({
        type: "function",
        properties: {
          "typeof": "function"
        },
        rules: {
          arity: {
            method: function method(e) {
              return s(Number.isSafeInteger(e) && e >= 0, "n must be a positive integer"), this.$_addRule({
                name: "arity",
                args: {
                  n: e
                }
              });
            },
            validate: function validate(e, t, _ref27) {
              var r = _ref27.n;
              return e.length === r ? e : t.error("function.arity", {
                n: r
              });
            }
          },
          "class": {
            method: function method() {
              return this.$_addRule("class");
            },
            validate: function validate(e, t) {
              return /^\s*class\s/.test(e.toString()) ? e : t.error("function.class", {
                value: e
              });
            }
          },
          minArity: {
            method: function method(e) {
              return s(Number.isSafeInteger(e) && e > 0, "n must be a strict positive integer"), this.$_addRule({
                name: "minArity",
                args: {
                  n: e
                }
              });
            },
            validate: function validate(e, t, _ref28) {
              var r = _ref28.n;
              return e.length >= r ? e : t.error("function.minArity", {
                n: r
              });
            }
          },
          maxArity: {
            method: function method(e) {
              return s(Number.isSafeInteger(e) && e >= 0, "n must be a positive integer"), this.$_addRule({
                name: "maxArity",
                args: {
                  n: e
                }
              });
            },
            validate: function validate(e, t, _ref29) {
              var r = _ref29.n;
              return e.length <= r ? e : t.error("function.maxArity", {
                n: r
              });
            }
          }
        },
        messages: {
          "function.arity": "{{#label}} must have an arity of {{#n}}",
          "function.class": "{{#label}} must be a class",
          "function.maxArity": "{{#label}} must have an arity lesser or equal to {{#n}}",
          "function.minArity": "{{#label}} must have an arity greater or equal to {{#n}}"
        }
      });
    },
    7824: function _(e, t, r) {
      "use strict";

      var s = r(978),
        n = r(375),
        a = r(8571),
        i = r(3652),
        o = r(8068),
        l = r(8160),
        c = r(3292),
        u = r(6354),
        f = r(6133),
        m = r(3328),
        h = {
          renameDefaults: {
            alias: !1,
            multiple: !1,
            override: !1
          }
        };
      e.exports = o.extend({
        type: "_keys",
        properties: {
          "typeof": "object"
        },
        flags: {
          unknown: {
            "default": void 0
          }
        },
        terms: {
          dependencies: {
            init: null
          },
          keys: {
            init: null,
            manifest: {
              mapped: {
                from: "schema",
                to: "key"
              }
            }
          },
          patterns: {
            init: null
          },
          renames: {
            init: null
          }
        },
        args: function args(e, t) {
          return e.keys(t);
        },
        validate: function validate(e, _ref30) {
          var t = _ref30.schema,
            r = _ref30.error,
            s = _ref30.state,
            n = _ref30.prefs;
          if (!e || _typeof(e) !== t.$_property("typeof") || Array.isArray(e)) return {
            value: e,
            errors: r("object.base", {
              type: t.$_property("typeof")
            })
          };
          if (!(t.$_terms.renames || t.$_terms.dependencies || t.$_terms.keys || t.$_terms.patterns || t.$_terms.externals)) return;
          e = h.clone(e, n);
          var a = [];
          if (t.$_terms.renames && !h.rename(t, e, s, n, a)) return {
            value: e,
            errors: a
          };
          if (!t.$_terms.keys && !t.$_terms.patterns && !t.$_terms.dependencies) return {
            value: e,
            errors: a
          };
          var i = new Set(Object.keys(e));
          if (t.$_terms.keys) {
            var _r38 = [e].concat(_toConsumableArray(s.ancestors));
            var _iterator42 = _createForOfIteratorHelper(t.$_terms.keys),
              _step42;
            try {
              for (_iterator42.s(); !(_step42 = _iterator42.n()).done;) {
                var _o12 = _step42.value;
                var _t45 = _o12.key,
                  _l10 = e[_t45];
                i["delete"](_t45);
                var _c10 = s.localize([].concat(_toConsumableArray(s.path), [_t45]), _r38, _o12),
                  _u6 = _o12.schema.$_validate(_l10, _c10, n);
                if (_u6.errors) {
                  if (n.abortEarly) return {
                    value: e,
                    errors: _u6.errors
                  };
                  void 0 !== _u6.value && (e[_t45] = _u6.value), a.push.apply(a, _toConsumableArray(_u6.errors));
                } else "strip" === _o12.schema._flags.result || void 0 === _u6.value && void 0 !== _l10 ? delete e[_t45] : void 0 !== _u6.value && (e[_t45] = _u6.value);
              }
            } catch (err) {
              _iterator42.e(err);
            } finally {
              _iterator42.f();
            }
          }
          if (i.size || t._flags._hasPatternMatch) {
            var _r39 = h.unknown(t, e, i, a, s, n);
            if (_r39) return _r39;
          }
          if (t.$_terms.dependencies) {
            var _iterator43 = _createForOfIteratorHelper(t.$_terms.dependencies),
              _step43;
            try {
              for (_iterator43.s(); !(_step43 = _iterator43.n()).done;) {
                var _r40 = _step43.value;
                if (null !== _r40.key && !1 === h.isPresent(_r40.options)(_r40.key.resolve(e, s, n, null, {
                  shadow: !1
                }))) continue;
                var _i25 = h.dependencies[_r40.rel](t, _r40, e, s, n);
                if (_i25) {
                  var _r41 = t.$_createError(_i25.code, e, _i25.context, s, n);
                  if (n.abortEarly) return {
                    value: e,
                    errors: _r41
                  };
                  a.push(_r41);
                }
              }
            } catch (err) {
              _iterator43.e(err);
            } finally {
              _iterator43.f();
            }
          }
          return {
            value: e,
            errors: a
          };
        },
        rules: {
          and: {
            method: function method() {
              for (var _len20 = arguments.length, e = new Array(_len20), _key20 = 0; _key20 < _len20; _key20++) {
                e[_key20] = arguments[_key20];
              }
              return l.verifyFlat(e, "and"), h.dependency(this, "and", null, e);
            }
          },
          append: {
            method: function method(e) {
              return null == e || 0 === Object.keys(e).length ? this : this.keys(e);
            }
          },
          assert: {
            method: function method(e, t, r) {
              m.isTemplate(e) || (e = c.ref(e)), n(void 0 === r || "string" == typeof r, "Message must be a string"), t = this.$_compile(t, {
                appendPath: !0
              });
              var s = this.$_addRule({
                name: "assert",
                args: {
                  subject: e,
                  schema: t,
                  message: r
                }
              });
              return s.$_mutateRegister(e), s.$_mutateRegister(t), s;
            },
            validate: function validate(e, _ref31, _ref32) {
              var t = _ref31.error,
                r = _ref31.prefs,
                s = _ref31.state;
              var n = _ref32.subject,
                a = _ref32.schema,
                i = _ref32.message;
              var o = n.resolve(e, s, r),
                l = f.isRef(n) ? n.absolute(s) : [];
              return a.$_match(o, s.localize(l, [e].concat(_toConsumableArray(s.ancestors)), a), r) ? e : t("object.assert", {
                subject: n,
                message: i
              });
            },
            args: ["subject", "schema", "message"],
            multi: !0
          },
          instance: {
            method: function method(e, t) {
              return n("function" == typeof e, "constructor must be a function"), t = t || e.name, this.$_addRule({
                name: "instance",
                args: {
                  constructor: e,
                  name: t
                }
              });
            },
            validate: function validate(e, t, _ref33) {
              var r = _ref33.constructor,
                s = _ref33.name;
              return e instanceof r ? e : t.error("object.instance", {
                type: s,
                value: e
              });
            },
            args: ["constructor", "name"]
          },
          keys: {
            method: function method(e) {
              var _this8 = this;
              n(void 0 === e || "object" == _typeof(e), "Object schema must be a valid object"), n(!l.isSchema(e), "Object schema cannot be a joi schema");
              var t = this.clone();
              if (e) {
                if (Object.keys(e).length) {
                  t.$_terms.keys = t.$_terms.keys ? t.$_terms.keys.filter(function (t) {
                    return !e.hasOwnProperty(t.key);
                  }) : new h.Keys();
                  var _loop0 = function _loop0(_r42) {
                    l.tryWithPath(function () {
                      return t.$_terms.keys.push({
                        key: _r42,
                        schema: _this8.$_compile(e[_r42])
                      });
                    }, _r42);
                  };
                  for (var _r42 in e) {
                    _loop0(_r42);
                  }
                } else t.$_terms.keys = new h.Keys();
              } else t.$_terms.keys = null;
              return t.$_mutateRebuild();
            }
          },
          length: {
            method: function method(e) {
              return this.$_addRule({
                name: "length",
                args: {
                  limit: e
                },
                operator: "="
              });
            },
            validate: function validate(e, t, _ref34, _ref35) {
              var r = _ref34.limit;
              var s = _ref35.name,
                n = _ref35.operator,
                a = _ref35.args;
              return l.compare(Object.keys(e).length, r, n) ? e : t.error("object." + s, {
                limit: a.limit,
                value: e
              });
            },
            args: [{
              name: "limit",
              ref: !0,
              assert: l.limit,
              message: "must be a positive integer"
            }]
          },
          max: {
            method: function method(e) {
              return this.$_addRule({
                name: "max",
                method: "length",
                args: {
                  limit: e
                },
                operator: "<="
              });
            }
          },
          min: {
            method: function method(e) {
              return this.$_addRule({
                name: "min",
                method: "length",
                args: {
                  limit: e
                },
                operator: ">="
              });
            }
          },
          nand: {
            method: function method() {
              for (var _len21 = arguments.length, e = new Array(_len21), _key21 = 0; _key21 < _len21; _key21++) {
                e[_key21] = arguments[_key21];
              }
              return l.verifyFlat(e, "nand"), h.dependency(this, "nand", null, e);
            }
          },
          or: {
            method: function method() {
              for (var _len22 = arguments.length, e = new Array(_len22), _key22 = 0; _key22 < _len22; _key22++) {
                e[_key22] = arguments[_key22];
              }
              return l.verifyFlat(e, "or"), h.dependency(this, "or", null, e);
            }
          },
          oxor: {
            method: function method() {
              for (var _len23 = arguments.length, e = new Array(_len23), _key23 = 0; _key23 < _len23; _key23++) {
                e[_key23] = arguments[_key23];
              }
              return h.dependency(this, "oxor", null, e);
            }
          },
          pattern: {
            method: function method(e, t) {
              var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
              var s = e instanceof RegExp;
              s || (e = this.$_compile(e, {
                appendPath: !0
              })), n(void 0 !== t, "Invalid rule"), l.assertOptions(r, ["fallthrough", "matches"]), s && n(!e.flags.includes("g") && !e.flags.includes("y"), "pattern should not use global or sticky mode"), t = this.$_compile(t, {
                appendPath: !0
              });
              var a = this.clone();
              a.$_terms.patterns = a.$_terms.patterns || [];
              var i = _defineProperty(_defineProperty({}, s ? "regex" : "schema", e), "rule", t);
              return r.matches && (i.matches = this.$_compile(r.matches), "array" !== i.matches.type && (i.matches = i.matches.$_root.array().items(i.matches)), a.$_mutateRegister(i.matches), a.$_setFlag("_hasPatternMatch", !0, {
                clone: !1
              })), r.fallthrough && (i.fallthrough = !0), a.$_terms.patterns.push(i), a.$_mutateRegister(t), a;
            }
          },
          ref: {
            method: function method() {
              return this.$_addRule("ref");
            },
            validate: function validate(e, t) {
              return f.isRef(e) ? e : t.error("object.refType", {
                value: e
              });
            }
          },
          regex: {
            method: function method() {
              return this.$_addRule("regex");
            },
            validate: function validate(e, t) {
              return e instanceof RegExp ? e : t.error("object.regex", {
                value: e
              });
            }
          },
          rename: {
            method: function method(e, t) {
              var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
              n("string" == typeof e || e instanceof RegExp, "Rename missing the from argument"), n("string" == typeof t || t instanceof m, "Invalid rename to argument"), n(t !== e, "Cannot rename key to same name:", e), l.assertOptions(r, ["alias", "ignoreUndefined", "override", "multiple"]);
              var a = this.clone();
              a.$_terms.renames = a.$_terms.renames || [];
              var _iterator44 = _createForOfIteratorHelper(a.$_terms.renames),
                _step44;
              try {
                for (_iterator44.s(); !(_step44 = _iterator44.n()).done;) {
                  var _t46 = _step44.value;
                  n(_t46.from !== e, "Cannot rename the same key multiple times");
                }
              } catch (err) {
                _iterator44.e(err);
              } finally {
                _iterator44.f();
              }
              return t instanceof m && a.$_mutateRegister(t), a.$_terms.renames.push({
                from: e,
                to: t,
                options: s(h.renameDefaults, r)
              }), a;
            }
          },
          schema: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : "any";
              return this.$_addRule({
                name: "schema",
                args: {
                  type: e
                }
              });
            },
            validate: function validate(e, t, _ref36) {
              var r = _ref36.type;
              return !l.isSchema(e) || "any" !== r && e.type !== r ? t.error("object.schema", {
                type: r
              }) : e;
            }
          },
          unknown: {
            method: function method(e) {
              return this.$_setFlag("unknown", !1 !== e);
            }
          },
          "with": {
            method: function method(e, t) {
              var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
              return h.dependency(this, "with", e, t, r);
            }
          },
          without: {
            method: function method(e, t) {
              var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
              return h.dependency(this, "without", e, t, r);
            }
          },
          xor: {
            method: function method() {
              for (var _len24 = arguments.length, e = new Array(_len24), _key24 = 0; _key24 < _len24; _key24++) {
                e[_key24] = arguments[_key24];
              }
              return l.verifyFlat(e, "xor"), h.dependency(this, "xor", null, e);
            }
          }
        },
        overrides: {
          "default": function _default(e, t) {
            return void 0 === e && (e = l.symbols.deepDefault), this.$_parent("default", e, t);
          }
        },
        rebuild: function rebuild(e) {
          if (e.$_terms.keys) {
            var _t47 = new i.Sorter();
            var _iterator45 = _createForOfIteratorHelper(e.$_terms.keys),
              _step45;
            try {
              var _loop1 = function _loop1() {
                var r = _step45.value;
                l.tryWithPath(function () {
                  return _t47.add(r, {
                    after: r.schema.$_rootReferences(),
                    group: r.key
                  });
                }, r.key);
              };
              for (_iterator45.s(); !(_step45 = _iterator45.n()).done;) {
                _loop1();
              }
            } catch (err) {
              _iterator45.e(err);
            } finally {
              _iterator45.f();
            }
            e.$_terms.keys = _construct(h.Keys, _toConsumableArray(_t47.nodes));
          }
        },
        manifest: {
          build: function build(e, t) {
            if (t.keys && (e = e.keys(t.keys)), t.dependencies) {
              var _iterator46 = _createForOfIteratorHelper(t.dependencies),
                _step46;
              try {
                for (_iterator46.s(); !(_step46 = _iterator46.n()).done;) {
                  var _step46$value = _step46.value,
                    _r43 = _step46$value.rel,
                    _step46$value$key = _step46$value.key,
                    _s25 = _step46$value$key === void 0 ? null : _step46$value$key,
                    _n27 = _step46$value.peers,
                    _a11 = _step46$value.options;
                  e = h.dependency(e, _r43, _s25, _n27, _a11);
                }
              } catch (err) {
                _iterator46.e(err);
              } finally {
                _iterator46.f();
              }
            }
            if (t.patterns) {
              var _iterator47 = _createForOfIteratorHelper(t.patterns),
                _step47;
              try {
                for (_iterator47.s(); !(_step47 = _iterator47.n()).done;) {
                  var _step47$value = _step47.value,
                    _r44 = _step47$value.regex,
                    _s26 = _step47$value.schema,
                    _n28 = _step47$value.rule,
                    _a12 = _step47$value.fallthrough,
                    _i27 = _step47$value.matches;
                  e = e.pattern(_r44 || _s26, _n28, {
                    fallthrough: _a12,
                    matches: _i27
                  });
                }
              } catch (err) {
                _iterator47.e(err);
              } finally {
                _iterator47.f();
              }
            }
            if (t.renames) {
              var _iterator48 = _createForOfIteratorHelper(t.renames),
                _step48;
              try {
                for (_iterator48.s(); !(_step48 = _iterator48.n()).done;) {
                  var _step48$value = _step48.value,
                    _r45 = _step48$value.from,
                    _s27 = _step48$value.to,
                    _n29 = _step48$value.options;
                  e = e.rename(_r45, _s27, _n29);
                }
              } catch (err) {
                _iterator48.e(err);
              } finally {
                _iterator48.f();
              }
            }
            return e;
          }
        },
        messages: {
          "object.and": "{{#label}} contains {{#presentWithLabels}} without its required peers {{#missingWithLabels}}",
          "object.assert": '{{#label}} is invalid because {if(#subject.key, \x60"\x60 + #subject.key + \x60" failed to \x60 + (#message || "pass the assertion test"), #message || "the assertion failed")}',
          "object.base": "{{#label}} must be of type {{#type}}",
          "object.instance": "{{#label}} must be an instance of {{:#type}}",
          "object.length": '{{#label}} must have {{#limit}} key{if(#limit == 1, "", "s")}',
          "object.max": '{{#label}} must have less than or equal to {{#limit}} key{if(#limit == 1, "", "s")}',
          "object.min": '{{#label}} must have at least {{#limit}} key{if(#limit == 1, "", "s")}',
          "object.missing": "{{#label}} must contain at least one of {{#peersWithLabels}}",
          "object.nand": "{{:#mainWithLabel}} must not exist simultaneously with {{#peersWithLabels}}",
          "object.oxor": "{{#label}} contains a conflict between optional exclusive peers {{#peersWithLabels}}",
          "object.pattern.match": "{{#label}} keys failed to match pattern requirements",
          "object.refType": "{{#label}} must be a Joi reference",
          "object.regex": "{{#label}} must be a RegExp object",
          "object.rename.multiple": "{{#label}} cannot rename {{:#from}} because multiple renames are disabled and another key was already renamed to {{:#to}}",
          "object.rename.override": "{{#label}} cannot rename {{:#from}} because override is disabled and target {{:#to}} exists",
          "object.schema": "{{#label}} must be a Joi schema of {{#type}} type",
          "object.unknown": "{{#label}} is not allowed",
          "object.with": "{{:#mainWithLabel}} missing required peer {{:#peerWithLabel}}",
          "object.without": "{{:#mainWithLabel}} conflict with forbidden peer {{:#peerWithLabel}}",
          "object.xor": "{{#label}} contains a conflict between exclusive peers {{#peersWithLabels}}"
        }
      }), h.clone = function (e, t) {
        if ("object" == _typeof(e)) {
          if (t.nonEnumerables) return a(e, {
            shallow: !0
          });
          var _r46 = Object.create(Object.getPrototypeOf(e));
          return Object.assign(_r46, e), _r46;
        }
        var r = function r() {
          for (var _len25 = arguments.length, t = new Array(_len25), _key25 = 0; _key25 < _len25; _key25++) {
            t[_key25] = arguments[_key25];
          }
          return e.apply(this, t);
        };
        return r.prototype = a(e.prototype), Object.defineProperty(r, "name", {
          value: e.name,
          writable: !1
        }), Object.defineProperty(r, "length", {
          value: e.length,
          writable: !1
        }), Object.assign(r, e), r;
      }, h.dependency = function (e, t, r, s, a) {
        n(null === r || "string" == typeof r, t, "key must be a strings"), a || (a = s.length > 1 && "object" == _typeof(s[s.length - 1]) ? s.pop() : {}), l.assertOptions(a, ["separator", "isPresent"]), s = [].concat(s);
        var i = l["default"](a.separator, "."),
          o = [];
        var _iterator49 = _createForOfIteratorHelper(s),
          _step49;
        try {
          for (_iterator49.s(); !(_step49 = _iterator49.n()).done;) {
            var _e39 = _step49.value;
            n("string" == typeof _e39, t, "peers must be strings"), o.push(c.ref(_e39, {
              separator: i,
              ancestor: 0,
              prefix: !1
            }));
          }
        } catch (err) {
          _iterator49.e(err);
        } finally {
          _iterator49.f();
        }
        null !== r && (r = c.ref(r, {
          separator: i,
          ancestor: 0,
          prefix: !1
        }));
        var u = e.clone();
        return u.$_terms.dependencies = u.$_terms.dependencies || [], u.$_terms.dependencies.push(new h.Dependency(t, r, o, s, a)), u;
      }, h.dependencies = {
        and: function and(e, t, r, s, n) {
          var a = [],
            i = [],
            o = t.peers.length,
            l = h.isPresent(t.options);
          var _iterator50 = _createForOfIteratorHelper(t.peers),
            _step50;
          try {
            for (_iterator50.s(); !(_step50 = _iterator50.n()).done;) {
              var _e40 = _step50.value;
              !1 === l(_e40.resolve(r, s, n, null, {
                shadow: !1
              })) ? a.push(_e40.key) : i.push(_e40.key);
            }
          } catch (err) {
            _iterator50.e(err);
          } finally {
            _iterator50.f();
          }
          if (a.length !== o && i.length !== o) return {
            code: "object.and",
            context: {
              present: i,
              presentWithLabels: h.keysToLabels(e, i),
              missing: a,
              missingWithLabels: h.keysToLabels(e, a)
            }
          };
        },
        nand: function nand(e, t, r, s, n) {
          var a = [],
            i = h.isPresent(t.options);
          var _iterator51 = _createForOfIteratorHelper(t.peers),
            _step51;
          try {
            for (_iterator51.s(); !(_step51 = _iterator51.n()).done;) {
              var _e41 = _step51.value;
              i(_e41.resolve(r, s, n, null, {
                shadow: !1
              })) && a.push(_e41.key);
            }
          } catch (err) {
            _iterator51.e(err);
          } finally {
            _iterator51.f();
          }
          if (a.length !== t.peers.length) return;
          var o = t.paths[0],
            l = t.paths.slice(1);
          return {
            code: "object.nand",
            context: {
              main: o,
              mainWithLabel: h.keysToLabels(e, o),
              peers: l,
              peersWithLabels: h.keysToLabels(e, l)
            }
          };
        },
        or: function or(e, t, r, s, n) {
          var a = h.isPresent(t.options);
          var _iterator52 = _createForOfIteratorHelper(t.peers),
            _step52;
          try {
            for (_iterator52.s(); !(_step52 = _iterator52.n()).done;) {
              var _e42 = _step52.value;
              if (a(_e42.resolve(r, s, n, null, {
                shadow: !1
              }))) return;
            }
          } catch (err) {
            _iterator52.e(err);
          } finally {
            _iterator52.f();
          }
          return {
            code: "object.missing",
            context: {
              peers: t.paths,
              peersWithLabels: h.keysToLabels(e, t.paths)
            }
          };
        },
        oxor: function oxor(e, t, r, s, n) {
          var a = [],
            i = h.isPresent(t.options);
          var _iterator53 = _createForOfIteratorHelper(t.peers),
            _step53;
          try {
            for (_iterator53.s(); !(_step53 = _iterator53.n()).done;) {
              var _e43 = _step53.value;
              i(_e43.resolve(r, s, n, null, {
                shadow: !1
              })) && a.push(_e43.key);
            }
          } catch (err) {
            _iterator53.e(err);
          } finally {
            _iterator53.f();
          }
          if (!a.length || 1 === a.length) return;
          var o = {
            peers: t.paths,
            peersWithLabels: h.keysToLabels(e, t.paths)
          };
          return o.present = a, o.presentWithLabels = h.keysToLabels(e, a), {
            code: "object.oxor",
            context: o
          };
        },
        "with": function _with(e, t, r, s, n) {
          var a = h.isPresent(t.options);
          var _iterator54 = _createForOfIteratorHelper(t.peers),
            _step54;
          try {
            for (_iterator54.s(); !(_step54 = _iterator54.n()).done;) {
              var _i28 = _step54.value;
              if (!1 === a(_i28.resolve(r, s, n, null, {
                shadow: !1
              }))) return {
                code: "object.with",
                context: {
                  main: t.key.key,
                  mainWithLabel: h.keysToLabels(e, t.key.key),
                  peer: _i28.key,
                  peerWithLabel: h.keysToLabels(e, _i28.key)
                }
              };
            }
          } catch (err) {
            _iterator54.e(err);
          } finally {
            _iterator54.f();
          }
        },
        without: function without(e, t, r, s, n) {
          var a = h.isPresent(t.options);
          var _iterator55 = _createForOfIteratorHelper(t.peers),
            _step55;
          try {
            for (_iterator55.s(); !(_step55 = _iterator55.n()).done;) {
              var _i29 = _step55.value;
              if (a(_i29.resolve(r, s, n, null, {
                shadow: !1
              }))) return {
                code: "object.without",
                context: {
                  main: t.key.key,
                  mainWithLabel: h.keysToLabels(e, t.key.key),
                  peer: _i29.key,
                  peerWithLabel: h.keysToLabels(e, _i29.key)
                }
              };
            }
          } catch (err) {
            _iterator55.e(err);
          } finally {
            _iterator55.f();
          }
        },
        xor: function xor(e, t, r, s, n) {
          var a = [],
            i = h.isPresent(t.options);
          var _iterator56 = _createForOfIteratorHelper(t.peers),
            _step56;
          try {
            for (_iterator56.s(); !(_step56 = _iterator56.n()).done;) {
              var _e44 = _step56.value;
              i(_e44.resolve(r, s, n, null, {
                shadow: !1
              })) && a.push(_e44.key);
            }
          } catch (err) {
            _iterator56.e(err);
          } finally {
            _iterator56.f();
          }
          if (1 === a.length) return;
          var o = {
            peers: t.paths,
            peersWithLabels: h.keysToLabels(e, t.paths)
          };
          return 0 === a.length ? {
            code: "object.missing",
            context: o
          } : (o.present = a, o.presentWithLabels = h.keysToLabels(e, a), {
            code: "object.xor",
            context: o
          });
        }
      }, h.keysToLabels = function (e, t) {
        return Array.isArray(t) ? t.map(function (t) {
          return e.$_mapLabels(t);
        }) : e.$_mapLabels(t);
      }, h.isPresent = function (e) {
        return "function" == typeof e.isPresent ? e.isPresent : function (e) {
          return void 0 !== e;
        };
      }, h.rename = function (e, t, r, s, n) {
        var a = {};
        var _iterator57 = _createForOfIteratorHelper(e.$_terms.renames),
          _step57;
        try {
          for (_iterator57.s(); !(_step57 = _iterator57.n()).done;) {
            var _i30 = _step57.value;
            var _o13 = [],
              _l11 = "string" != typeof _i30.from;
            if (_l11) for (var _e45 in t) {
              if (void 0 === t[_e45] && _i30.options.ignoreUndefined) continue;
              if (_e45 === _i30.to) continue;
              var _r47 = _i30.from.exec(_e45);
              _r47 && _o13.push({
                from: _e45,
                to: _i30.to,
                match: _r47
              });
            } else !Object.prototype.hasOwnProperty.call(t, _i30.from) || void 0 === t[_i30.from] && _i30.options.ignoreUndefined || _o13.push(_i30);
            for (var _i31 = 0, _o14 = _o13; _i31 < _o14.length; _i31++) {
              var _c11 = _o14[_i31];
              var _o15 = _c11.from;
              var _u7 = _c11.to;
              if (_u7 instanceof m && (_u7 = _u7.render(t, r, s, _c11.match)), _o15 !== _u7) {
                if (!_i30.options.multiple && a[_u7] && (n.push(e.$_createError("object.rename.multiple", t, {
                  from: _o15,
                  to: _u7,
                  pattern: _l11
                }, r, s)), s.abortEarly)) return !1;
                if (Object.prototype.hasOwnProperty.call(t, _u7) && !_i30.options.override && !a[_u7] && (n.push(e.$_createError("object.rename.override", t, {
                  from: _o15,
                  to: _u7,
                  pattern: _l11
                }, r, s)), s.abortEarly)) return !1;
                void 0 === t[_o15] ? delete t[_u7] : t[_u7] = t[_o15], a[_u7] = !0, _i30.options.alias || delete t[_o15];
              }
            }
          }
        } catch (err) {
          _iterator57.e(err);
        } finally {
          _iterator57.f();
        }
        return !0;
      }, h.unknown = function (e, t, r, s, n, a) {
        if (e.$_terms.patterns) {
          var _i32 = !1;
          var _o16 = e.$_terms.patterns.map(function (e) {
              if (e.matches) return _i32 = !0, [];
            }),
            _l12 = [t].concat(_toConsumableArray(n.ancestors));
          var _iterator58 = _createForOfIteratorHelper(r),
            _step58;
          try {
            for (_iterator58.s(); !(_step58 = _iterator58.n()).done;) {
              var _i34 = _step58.value;
              var _c13 = t[_i34],
                _u8 = [].concat(_toConsumableArray(n.path), [_i34]);
              for (var _f5 = 0; _f5 < e.$_terms.patterns.length; ++_f5) {
                var _m3 = e.$_terms.patterns[_f5];
                if (_m3.regex) {
                  var _e46 = _m3.regex.test(_i34);
                  if (n.mainstay.tracer.debug(n, "rule", "pattern.".concat(_f5), _e46 ? "pass" : "error"), !_e46) continue;
                } else if (!_m3.schema.$_match(_i34, n.nest(_m3.schema, "pattern.".concat(_f5)), a)) continue;
                r["delete"](_i34);
                var _h2 = n.localize(_u8, _l12, {
                    schema: _m3.rule,
                    key: _i34
                  }),
                  d = _m3.rule.$_validate(_c13, _h2, a);
                if (d.errors) {
                  if (a.abortEarly) return {
                    value: t,
                    errors: d.errors
                  };
                  s.push.apply(s, _toConsumableArray(d.errors));
                }
                if (_m3.matches && _o16[_f5].push(_i34), t[_i34] = d.value, !_m3.fallthrough) break;
              }
            }
          } catch (err) {
            _iterator58.e(err);
          } finally {
            _iterator58.f();
          }
          if (_i32) for (var _r48 = 0; _r48 < _o16.length; ++_r48) {
            var _i33 = _o16[_r48];
            if (!_i33) continue;
            var _c12 = e.$_terms.patterns[_r48].matches,
              _f4 = n.localize(n.path, _l12, _c12),
              _m2 = _c12.$_validate(_i33, _f4, a);
            if (_m2.errors) {
              var _r49 = u.details(_m2.errors, {
                override: !1
              });
              _r49.matches = _i33;
              var _o17 = e.$_createError("object.pattern.match", t, _r49, n, a);
              if (a.abortEarly) return {
                value: t,
                errors: _o17
              };
              s.push(_o17);
            }
          }
        }
        if (r.size && (e.$_terms.keys || e.$_terms.patterns)) {
          if (a.stripUnknown && void 0 === e._flags.unknown || a.skipFunctions) {
            var _e47 = !(!a.stripUnknown || !0 !== a.stripUnknown && !a.stripUnknown.objects);
            var _iterator59 = _createForOfIteratorHelper(r),
              _step59;
            try {
              for (_iterator59.s(); !(_step59 = _iterator59.n()).done;) {
                var _s28 = _step59.value;
                _e47 ? (delete t[_s28], r["delete"](_s28)) : "function" == typeof t[_s28] && r["delete"](_s28);
              }
            } catch (err) {
              _iterator59.e(err);
            } finally {
              _iterator59.f();
            }
          }
          if (!l["default"](e._flags.unknown, a.allowUnknown)) {
            var _iterator60 = _createForOfIteratorHelper(r),
              _step60;
            try {
              for (_iterator60.s(); !(_step60 = _iterator60.n()).done;) {
                var _i35 = _step60.value;
                var _r50 = n.localize([].concat(_toConsumableArray(n.path), [_i35]), []),
                  _o18 = e.$_createError("object.unknown", t[_i35], {
                    child: _i35
                  }, _r50, a, {
                    flags: !1
                  });
                if (a.abortEarly) return {
                  value: t,
                  errors: _o18
                };
                s.push(_o18);
              }
            } catch (err) {
              _iterator60.e(err);
            } finally {
              _iterator60.f();
            }
          }
        }
      }, h.Dependency = /*#__PURE__*/function () {
        function _class1(e, t, r, s, n) {
          _classCallCheck(this, _class1);
          this.rel = e, this.key = t, this.peers = r, this.paths = s, this.options = n;
        }
        return _createClass(_class1, [{
          key: "describe",
          value: function describe() {
            var e = {
              rel: this.rel,
              peers: this.paths
            };
            return null !== this.key && (e.key = this.key.key), "." !== this.peers[0].separator && (e.options = _objectSpread(_objectSpread({}, e.options), {}, {
              separator: this.peers[0].separator
            })), this.options.isPresent && (e.options = _objectSpread(_objectSpread({}, e.options), {}, {
              isPresent: this.options.isPresent
            })), e;
          }
        }]);
      }(), h.Keys = /*#__PURE__*/function (_Array) {
        function _class10() {
          _classCallCheck(this, _class10);
          return _callSuper(this, _class10, arguments);
        }
        _inherits(_class10, _Array);
        return _createClass(_class10, [{
          key: "concat",
          value: function concat(e) {
            var t = this.slice(),
              r = new Map();
            for (var _e48 = 0; _e48 < t.length; ++_e48) r.set(t[_e48].key, _e48);
            var _iterator61 = _createForOfIteratorHelper(e),
              _step61;
            try {
              for (_iterator61.s(); !(_step61 = _iterator61.n()).done;) {
                var _s29 = _step61.value;
                var _e49 = _s29.key,
                  _n30 = r.get(_e49);
                void 0 !== _n30 ? t[_n30] = {
                  key: _e49,
                  schema: t[_n30].schema.concat(_s29.schema)
                } : t.push(_s29);
              }
            } catch (err) {
              _iterator61.e(err);
            } finally {
              _iterator61.f();
            }
            return t;
          }
        }]);
      }(/*#__PURE__*/_wrapNativeSuper(Array));
    },
    8785: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8068),
        a = r(8160),
        i = r(3292),
        o = r(6354),
        l = {};
      e.exports = n.extend({
        type: "link",
        properties: {
          schemaChain: !0
        },
        terms: {
          link: {
            init: null,
            manifest: "single",
            register: !1
          }
        },
        args: function args(e, t) {
          return e.ref(t);
        },
        validate: function validate(e, _ref37) {
          var t = _ref37.schema,
            r = _ref37.state,
            n = _ref37.prefs;
          s(t.$_terms.link, "Uninitialized link schema");
          var a = l.generate(t, e, r, n),
            i = t.$_terms.link[0].ref;
          return a.$_validate(e, r.nest(a, "link:".concat(i.display, ":").concat(a.type)), n);
        },
        generate: function generate(e, t, r, s) {
          return l.generate(e, t, r, s);
        },
        rules: {
          ref: {
            method: function method(e) {
              s(!this.$_terms.link, "Cannot reinitialize schema"), e = i.ref(e), s("value" === e.type || "local" === e.type, "Invalid reference type:", e.type), s("local" === e.type || "root" === e.ancestor || e.ancestor > 0, "Link cannot reference itself");
              var t = this.clone();
              return t.$_terms.link = [{
                ref: e
              }], t;
            }
          },
          relative: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : !0;
              return this.$_setFlag("relative", e);
            }
          }
        },
        overrides: {
          concat: function concat(e) {
            s(this.$_terms.link, "Uninitialized link schema"), s(a.isSchema(e), "Invalid schema object"), s("link" !== e.type, "Cannot merge type link with another link");
            var t = this.clone();
            return t.$_terms.whens || (t.$_terms.whens = []), t.$_terms.whens.push({
              concat: e
            }), t.$_mutateRebuild();
          }
        },
        manifest: {
          build: function build(e, t) {
            return s(t.link, "Invalid link description missing link"), e.ref(t.link);
          }
        }
      }), l.generate = function (e, t, r, s) {
        var n = r.mainstay.links.get(e);
        if (n) return n._generate(t, r, s).schema;
        var a = e.$_terms.link[0].ref,
          _l$perspective = l.perspective(a, r),
          i = _l$perspective.perspective,
          o = _l$perspective.path;
        l.assert(i, "which is outside of schema boundaries", a, e, r, s);
        try {
          n = o.length ? i.$_reach(o) : i;
        } catch (t) {
          l.assert(!1, "to non-existing schema", a, e, r, s);
        }
        return l.assert("link" !== n.type, "which is another link", a, e, r, s), e._flags.relative || r.mainstay.links.set(e, n), n._generate(t, r, s).schema;
      }, l.perspective = function (e, t) {
        if ("local" === e.type) {
          var _iterator62 = _createForOfIteratorHelper(t.schemas),
            _step62;
          try {
            for (_iterator62.s(); !(_step62 = _iterator62.n()).done;) {
              var _step62$value = _step62.value,
                _r51 = _step62$value.schema,
                _s30 = _step62$value.key;
              if ((_r51._flags.id || _s30) === e.path[0]) return {
                perspective: _r51,
                path: e.path.slice(1)
              };
              if (_r51.$_terms.shared) {
                var _iterator63 = _createForOfIteratorHelper(_r51.$_terms.shared),
                  _step63;
                try {
                  for (_iterator63.s(); !(_step63 = _iterator63.n()).done;) {
                    var _t48 = _step63.value;
                    if (_t48._flags.id === e.path[0]) return {
                      perspective: _t48,
                      path: e.path.slice(1)
                    };
                  }
                } catch (err) {
                  _iterator63.e(err);
                } finally {
                  _iterator63.f();
                }
              }
            }
          } catch (err) {
            _iterator62.e(err);
          } finally {
            _iterator62.f();
          }
          return {
            perspective: null,
            path: null
          };
        }
        return "root" === e.ancestor ? {
          perspective: t.schemas[t.schemas.length - 1].schema,
          path: e.path
        } : {
          perspective: t.schemas[e.ancestor] && t.schemas[e.ancestor].schema,
          path: e.path
        };
      }, l.assert = function (e, t, r, n, a, i) {
        e || s(!1, "\"".concat(o.label(n._flags, a, i), "\" contains link reference \"").concat(r.display, "\" ").concat(t));
      };
    },
    3832: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8068),
        a = r(8160),
        i = {
          numberRx: /^\s*[+-]?(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:e([+-]?\d+))?\s*$/i,
          precisionRx: /(?:\.(\d+))?(?:[eE]([+-]?\d+))?$/,
          exponentialPartRegex: /[eE][+-]?\d+$/,
          leadingSignAndZerosRegex: /^[+-]?(0*)?/,
          dotRegex: /\./,
          trailingZerosRegex: /0+$/,
          decimalPlaces: function decimalPlaces(e) {
            var t = e.toString(),
              r = t.indexOf("."),
              s = t.indexOf("e");
            return (r < 0 ? 0 : (s < 0 ? t.length : s) - r - 1) + (s < 0 ? 0 : Math.max(0, -parseInt(t.slice(s + 1))));
          }
        };
      e.exports = n.extend({
        type: "number",
        flags: {
          unsafe: {
            "default": !1
          }
        },
        coerce: {
          from: "string",
          method: function method(e, _ref38) {
            var t = _ref38.schema,
              r = _ref38.error;
            if (!e.match(i.numberRx)) return;
            e = e.trim();
            var s = {
              value: parseFloat(e)
            };
            if (0 === s.value && (s.value = 0), !t._flags.unsafe) if (e.match(/e/i)) {
              if (i.extractSignificantDigits(e) !== i.extractSignificantDigits(String(s.value))) return s.errors = r("number.unsafe"), s;
            } else {
              var _t49 = s.value.toString();
              if (_t49.match(/e/i)) return s;
              if (_t49 !== i.normalizeDecimal(e)) return s.errors = r("number.unsafe"), s;
            }
            return s;
          }
        },
        validate: function validate(e, _ref39) {
          var t = _ref39.schema,
            r = _ref39.error,
            s = _ref39.prefs;
          if (e === 1 / 0 || e === -1 / 0) return {
            value: e,
            errors: r("number.infinity")
          };
          if (!a.isNumber(e)) return {
            value: e,
            errors: r("number.base")
          };
          var n = {
            value: e
          };
          if (s.convert) {
            var _e50 = t.$_getRule("precision");
            if (_e50) {
              var _t50 = Math.pow(10, _e50.args.limit);
              n.value = Math.round(n.value * _t50) / _t50;
            }
          }
          return 0 === n.value && (n.value = 0), !t._flags.unsafe && (e > Number.MAX_SAFE_INTEGER || e < Number.MIN_SAFE_INTEGER) && (n.errors = r("number.unsafe")), n;
        },
        rules: {
          compare: {
            method: !1,
            validate: function validate(e, t, _ref40, _ref41) {
              var r = _ref40.limit;
              var s = _ref41.name,
                n = _ref41.operator,
                i = _ref41.args;
              return a.compare(e, r, n) ? e : t.error("number." + s, {
                limit: i.limit,
                value: e
              });
            },
            args: [{
              name: "limit",
              ref: !0,
              assert: a.isNumber,
              message: "must be a number"
            }]
          },
          greater: {
            method: function method(e) {
              return this.$_addRule({
                name: "greater",
                method: "compare",
                args: {
                  limit: e
                },
                operator: ">"
              });
            }
          },
          integer: {
            method: function method() {
              return this.$_addRule("integer");
            },
            validate: function validate(e, t) {
              return Math.trunc(e) - e == 0 ? e : t.error("number.integer");
            }
          },
          less: {
            method: function method(e) {
              return this.$_addRule({
                name: "less",
                method: "compare",
                args: {
                  limit: e
                },
                operator: "<"
              });
            }
          },
          max: {
            method: function method(e) {
              return this.$_addRule({
                name: "max",
                method: "compare",
                args: {
                  limit: e
                },
                operator: "<="
              });
            }
          },
          min: {
            method: function method(e) {
              return this.$_addRule({
                name: "min",
                method: "compare",
                args: {
                  limit: e
                },
                operator: ">="
              });
            }
          },
          multiple: {
            method: function method(e) {
              var t = "number" == typeof e ? i.decimalPlaces(e) : null,
                r = Math.pow(10, t);
              return this.$_addRule({
                name: "multiple",
                args: {
                  base: e,
                  baseDecimalPlace: t,
                  pfactor: r
                }
              });
            },
            validate: function validate(e, t, _ref42, a) {
              var r = _ref42.base,
                s = _ref42.baseDecimalPlace,
                n = _ref42.pfactor;
              return i.decimalPlaces(e) > s ? t.error("number.multiple", {
                multiple: a.args.base,
                value: e
              }) : Math.round(n * e) % Math.round(n * r) == 0 ? e : t.error("number.multiple", {
                multiple: a.args.base,
                value: e
              });
            },
            args: [{
              name: "base",
              ref: !0,
              assert: function assert(e) {
                return "number" == typeof e && isFinite(e) && e > 0;
              },
              message: "must be a positive number"
            }, "baseDecimalPlace", "pfactor"],
            multi: !0
          },
          negative: {
            method: function method() {
              return this.sign("negative");
            }
          },
          port: {
            method: function method() {
              return this.$_addRule("port");
            },
            validate: function validate(e, t) {
              return Number.isSafeInteger(e) && e >= 0 && e <= 65535 ? e : t.error("number.port");
            }
          },
          positive: {
            method: function method() {
              return this.sign("positive");
            }
          },
          precision: {
            method: function method(e) {
              return s(Number.isSafeInteger(e), "limit must be an integer"), this.$_addRule({
                name: "precision",
                args: {
                  limit: e
                }
              });
            },
            validate: function validate(e, t, _ref43) {
              var r = _ref43.limit;
              var s = e.toString().match(i.precisionRx);
              return Math.max((s[1] ? s[1].length : 0) - (s[2] ? parseInt(s[2], 10) : 0), 0) <= r ? e : t.error("number.precision", {
                limit: r,
                value: e
              });
            },
            convert: !0
          },
          sign: {
            method: function method(e) {
              return s(["negative", "positive"].includes(e), "Invalid sign", e), this.$_addRule({
                name: "sign",
                args: {
                  sign: e
                }
              });
            },
            validate: function validate(e, t, _ref44) {
              var r = _ref44.sign;
              return "negative" === r && e < 0 || "positive" === r && e > 0 ? e : t.error("number.".concat(r));
            }
          },
          unsafe: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : !0;
              return s("boolean" == typeof e, "enabled must be a boolean"), this.$_setFlag("unsafe", e);
            }
          }
        },
        cast: {
          string: {
            from: function from(e) {
              return "number" == typeof e;
            },
            to: function to(e, t) {
              return e.toString();
            }
          }
        },
        messages: {
          "number.base": "{{#label}} must be a number",
          "number.greater": "{{#label}} must be greater than {{#limit}}",
          "number.infinity": "{{#label}} cannot be infinity",
          "number.integer": "{{#label}} must be an integer",
          "number.less": "{{#label}} must be less than {{#limit}}",
          "number.max": "{{#label}} must be less than or equal to {{#limit}}",
          "number.min": "{{#label}} must be greater than or equal to {{#limit}}",
          "number.multiple": "{{#label}} must be a multiple of {{#multiple}}",
          "number.negative": "{{#label}} must be a negative number",
          "number.port": "{{#label}} must be a valid port",
          "number.positive": "{{#label}} must be a positive number",
          "number.precision": "{{#label}} must have no more than {{#limit}} decimal places",
          "number.unsafe": "{{#label}} must be a safe number"
        }
      }), i.extractSignificantDigits = function (e) {
        return e.replace(i.exponentialPartRegex, "").replace(i.dotRegex, "").replace(i.trailingZerosRegex, "").replace(i.leadingSignAndZerosRegex, "");
      }, i.normalizeDecimal = function (e) {
        return (e = e.replace(/^\+/, "").replace(/\.0*$/, "").replace(/^(-?)\.([^\.]*)$/, "$10.$2").replace(/^(-?)0+([0-9])/, "$1$2")).includes(".") && e.endsWith("0") && (e = e.replace(/0+$/, "")), "-0" === e ? "0" : e;
      };
    },
    8966: function _(e, t, r) {
      "use strict";

      var s = r(7824);
      e.exports = s.extend({
        type: "object",
        cast: {
          map: {
            from: function from(e) {
              return e && "object" == _typeof(e);
            },
            to: function to(e, t) {
              return new Map(Object.entries(e));
            }
          }
        }
      });
    },
    7417: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(5380),
        a = r(1745),
        i = r(9959),
        o = r(6064),
        l = r(9926),
        c = r(5752),
        u = r(8068),
        f = r(8160),
        m = {
          tlds: l instanceof Set && {
            tlds: {
              allow: l,
              deny: null
            }
          },
          base64Regex: {
            "true": {
              "true": /^(?:[\w\-]{2}[\w\-]{2})*(?:[\w\-]{2}==|[\w\-]{3}=)?$/,
              "false": /^(?:[A-Za-z0-9+\/]{2}[A-Za-z0-9+\/]{2})*(?:[A-Za-z0-9+\/]{2}==|[A-Za-z0-9+\/]{3}=)?$/
            },
            "false": {
              "true": /^(?:[\w\-]{2}[\w\-]{2})*(?:[\w\-]{2}(==)?|[\w\-]{3}=?)?$/,
              "false": /^(?:[A-Za-z0-9+\/]{2}[A-Za-z0-9+\/]{2})*(?:[A-Za-z0-9+\/]{2}(==)?|[A-Za-z0-9+\/]{3}=?)?$/
            }
          },
          dataUriRegex: /^data:[\w+.-]+\/[\w+.-]+;((charset=[\w-]+|base64),)?(.*)$/,
          hexRegex: {
            withPrefix: /^0x[0-9a-f]+$/i,
            withOptionalPrefix: /^(?:0x)?[0-9a-f]+$/i,
            withoutPrefix: /^[0-9a-f]+$/i
          },
          ipRegex: i.regex({
            cidr: "forbidden"
          }).regex,
          isoDurationRegex: /^P(?!$)(\d+Y)?(\d+M)?(\d+W)?(\d+D)?(T(?=\d)(\d+H)?(\d+M)?(\d+S)?)?$/,
          guidBrackets: {
            "{": "}",
            "[": "]",
            "(": ")",
            "": ""
          },
          guidVersions: {
            uuidv1: "1",
            uuidv2: "2",
            uuidv3: "3",
            uuidv4: "4",
            uuidv5: "5",
            uuidv6: "6",
            uuidv7: "7",
            uuidv8: "8"
          },
          guidSeparators: new Set([void 0, !0, !1, "-", ":"]),
          normalizationForms: ["NFC", "NFD", "NFKC", "NFKD"]
        };
      e.exports = u.extend({
        type: "string",
        flags: {
          insensitive: {
            "default": !1
          },
          truncate: {
            "default": !1
          }
        },
        terms: {
          replacements: {
            init: null
          }
        },
        coerce: {
          from: "string",
          method: function method(e, _ref45) {
            var t = _ref45.schema,
              r = _ref45.state,
              s = _ref45.prefs;
            var n = t.$_getRule("normalize");
            n && (e = e.normalize(n.args.form));
            var a = t.$_getRule("case");
            a && (e = "upper" === a.args.direction ? e.toLocaleUpperCase() : e.toLocaleLowerCase());
            var i = t.$_getRule("trim");
            if (i && i.args.enabled && (e = e.trim()), t.$_terms.replacements) {
              var _iterator64 = _createForOfIteratorHelper(t.$_terms.replacements),
                _step64;
              try {
                for (_iterator64.s(); !(_step64 = _iterator64.n()).done;) {
                  var _r52 = _step64.value;
                  e = e.replace(_r52.pattern, _r52.replacement);
                }
              } catch (err) {
                _iterator64.e(err);
              } finally {
                _iterator64.f();
              }
            }
            var o = t.$_getRule("hex");
            if (o && o.args.options.byteAligned && e.length % 2 != 0 && (e = "0".concat(e)), t.$_getRule("isoDate")) {
              var _t51 = m.isoDate(e);
              _t51 && (e = _t51);
            }
            if (t._flags.truncate) {
              var _n31 = t.$_getRule("max");
              if (_n31) {
                var _a13 = _n31.args.limit;
                if (f.isResolvable(_a13) && (_a13 = _a13.resolve(e, r, s), !f.limit(_a13))) return {
                  value: e,
                  errors: t.$_createError("any.ref", _a13, {
                    ref: _n31.args.limit,
                    arg: "limit",
                    reason: "must be a positive integer"
                  }, r, s)
                };
                e = e.slice(0, _a13);
              }
            }
            return {
              value: e
            };
          }
        },
        validate: function validate(e, _ref46) {
          var t = _ref46.schema,
            r = _ref46.error;
          if ("string" != typeof e) return {
            value: e,
            errors: r("string.base")
          };
          if ("" === e) {
            var _s31 = t.$_getRule("min");
            if (_s31 && 0 === _s31.args.limit) return;
            return {
              value: e,
              errors: r("string.empty")
            };
          }
        },
        rules: {
          alphanum: {
            method: function method() {
              return this.$_addRule("alphanum");
            },
            validate: function validate(e, t) {
              return /^[a-zA-Z0-9]+$/.test(e) ? e : t.error("string.alphanum");
            }
          },
          base64: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
              return f.assertOptions(e, ["paddingRequired", "urlSafe"]), e = _objectSpread({
                urlSafe: !1,
                paddingRequired: !0
              }, e), s("boolean" == typeof e.paddingRequired, "paddingRequired must be boolean"), s("boolean" == typeof e.urlSafe, "urlSafe must be boolean"), this.$_addRule({
                name: "base64",
                args: {
                  options: e
                }
              });
            },
            validate: function validate(e, t, _ref47) {
              var r = _ref47.options;
              return m.base64Regex[r.paddingRequired][r.urlSafe].test(e) ? e : t.error("string.base64");
            }
          },
          "case": {
            method: function method(e) {
              return s(["lower", "upper"].includes(e), "Invalid case:", e), this.$_addRule({
                name: "case",
                args: {
                  direction: e
                }
              });
            },
            validate: function validate(e, t, _ref48) {
              var r = _ref48.direction;
              return "lower" === r && e === e.toLocaleLowerCase() || "upper" === r && e === e.toLocaleUpperCase() ? e : t.error("string.".concat(r, "case"));
            },
            convert: !0
          },
          creditCard: {
            method: function method() {
              return this.$_addRule("creditCard");
            },
            validate: function validate(e, t) {
              var r = e.length,
                s = 0,
                n = 1;
              for (; r--;) {
                var _t52 = e.charAt(r) * n;
                s += _t52 - 9 * (_t52 > 9), n ^= 3;
              }
              return s > 0 && s % 10 == 0 ? e : t.error("string.creditCard");
            }
          },
          dataUri: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
              return f.assertOptions(e, ["paddingRequired"]), e = _objectSpread({
                paddingRequired: !0
              }, e), s("boolean" == typeof e.paddingRequired, "paddingRequired must be boolean"), this.$_addRule({
                name: "dataUri",
                args: {
                  options: e
                }
              });
            },
            validate: function validate(e, t, _ref49) {
              var r = _ref49.options;
              var s = e.match(m.dataUriRegex);
              if (s) {
                if (!s[2]) return e;
                if ("base64" !== s[2]) return e;
                if (m.base64Regex[r.paddingRequired]["false"].test(s[3])) return e;
              }
              return t.error("string.dataUri");
            }
          },
          domain: {
            method: function method(e) {
              e && f.assertOptions(e, ["allowFullyQualified", "allowUnicode", "maxDomainSegments", "minDomainSegments", "tlds"]);
              var t = m.addressOptions(e);
              return this.$_addRule({
                name: "domain",
                args: {
                  options: e
                },
                address: t
              });
            },
            validate: function validate(e, t, r, _ref50) {
              var s = _ref50.address;
              return n.isValid(e, s) ? e : t.error("string.domain");
            }
          },
          email: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
              f.assertOptions(e, ["allowFullyQualified", "allowUnicode", "ignoreLength", "maxDomainSegments", "minDomainSegments", "multiple", "separator", "tlds"]), s(void 0 === e.multiple || "boolean" == typeof e.multiple, "multiple option must be an boolean");
              var t = m.addressOptions(e),
                r = new RegExp("\\s*[".concat(e.separator ? o(e.separator) : ",", "]\\s*"));
              return this.$_addRule({
                name: "email",
                args: {
                  options: e
                },
                regex: r,
                address: t
              });
            },
            validate: function validate(e, t, _ref51, _ref52) {
              var r = _ref51.options;
              var s = _ref52.regex,
                n = _ref52.address;
              var i = r.multiple ? e.split(s) : [e],
                o = [];
              var _iterator65 = _createForOfIteratorHelper(i),
                _step65;
              try {
                for (_iterator65.s(); !(_step65 = _iterator65.n()).done;) {
                  var _e51 = _step65.value;
                  a.isValid(_e51, n) || o.push(_e51);
                }
              } catch (err) {
                _iterator65.e(err);
              } finally {
                _iterator65.f();
              }
              return o.length ? t.error("string.email", {
                value: e,
                invalids: o
              }) : e;
            }
          },
          guid: {
            alias: "uuid",
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
              f.assertOptions(e, ["version", "separator"]);
              var t = "";
              if (e.version) {
                var _r53 = [].concat(e.version);
                s(_r53.length >= 1, "version must have at least 1 valid version specified");
                var _n32 = new Set();
                for (var _e52 = 0; _e52 < _r53.length; ++_e52) {
                  var _a14 = _r53[_e52];
                  s("string" == typeof _a14, "version at position " + _e52 + " must be a string");
                  var _i36 = m.guidVersions[_a14.toLowerCase()];
                  s(_i36, "version at position " + _e52 + " must be one of " + Object.keys(m.guidVersions).join(", ")), s(!_n32.has(_i36), "version at position " + _e52 + " must not be a duplicate"), t += _i36, _n32.add(_i36);
                }
              }
              s(m.guidSeparators.has(e.separator), 'separator must be one of true, false, "-", or ":"');
              var r = void 0 === e.separator ? "[:-]?" : !0 === e.separator ? "[:-]" : !1 === e.separator ? "[]?" : "\\".concat(e.separator),
                n = new RegExp("^([\\[{\\(]?)[0-9A-F]{8}(".concat(r, ")[0-9A-F]{4}\\2?[").concat(t || "0-9A-F", "][0-9A-F]{3}\\2?[").concat(t ? "89AB" : "0-9A-F", "][0-9A-F]{3}\\2?[0-9A-F]{12}([\\]}\\)]?)$"), "i");
              return this.$_addRule({
                name: "guid",
                args: {
                  options: e
                },
                regex: n
              });
            },
            validate: function validate(e, t, r, _ref53) {
              var s = _ref53.regex;
              var n = s.exec(e);
              return n ? m.guidBrackets[n[1]] !== n[n.length - 1] ? t.error("string.guid") : e : t.error("string.guid");
            }
          },
          hex: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
              return f.assertOptions(e, ["byteAligned", "prefix"]), e = _objectSpread({
                byteAligned: !1,
                prefix: !1
              }, e), s("boolean" == typeof e.byteAligned, "byteAligned must be boolean"), s("boolean" == typeof e.prefix || "optional" === e.prefix, 'prefix must be boolean or "optional"'), this.$_addRule({
                name: "hex",
                args: {
                  options: e
                }
              });
            },
            validate: function validate(e, t, _ref54) {
              var r = _ref54.options;
              return ("optional" === r.prefix ? m.hexRegex.withOptionalPrefix : !0 === r.prefix ? m.hexRegex.withPrefix : m.hexRegex.withoutPrefix).test(e) ? r.byteAligned && e.length % 2 != 0 ? t.error("string.hexAlign") : e : t.error("string.hex");
            }
          },
          hostname: {
            method: function method() {
              return this.$_addRule("hostname");
            },
            validate: function validate(e, t) {
              return n.isValid(e, {
                minDomainSegments: 1
              }) || m.ipRegex.test(e) ? e : t.error("string.hostname");
            }
          },
          insensitive: {
            method: function method() {
              return this.$_setFlag("insensitive", !0);
            }
          },
          ip: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
              f.assertOptions(e, ["cidr", "version"]);
              var _i$regex = i.regex(e),
                t = _i$regex.cidr,
                r = _i$regex.versions,
                s = _i$regex.regex,
                n = e.version ? r : void 0;
              return this.$_addRule({
                name: "ip",
                args: {
                  options: {
                    cidr: t,
                    version: n
                  }
                },
                regex: s
              });
            },
            validate: function validate(e, t, _ref55, _ref56) {
              var r = _ref55.options;
              var s = _ref56.regex;
              return s.test(e) ? e : r.version ? t.error("string.ipVersion", {
                value: e,
                cidr: r.cidr,
                version: r.version
              }) : t.error("string.ip", {
                value: e,
                cidr: r.cidr
              });
            }
          },
          isoDate: {
            method: function method() {
              return this.$_addRule("isoDate");
            },
            validate: function validate(e, _ref57) {
              var t = _ref57.error;
              return m.isoDate(e) ? e : t("string.isoDate");
            }
          },
          isoDuration: {
            method: function method() {
              return this.$_addRule("isoDuration");
            },
            validate: function validate(e, t) {
              return m.isoDurationRegex.test(e) ? e : t.error("string.isoDuration");
            }
          },
          length: {
            method: function method(e, t) {
              return m.length(this, "length", e, "=", t);
            },
            validate: function validate(e, t, _ref58, _ref59) {
              var r = _ref58.limit,
                s = _ref58.encoding;
              var n = _ref59.name,
                a = _ref59.operator,
                i = _ref59.args;
              var o = !s && e.length;
              return f.compare(o, r, a) ? e : t.error("string." + n, {
                limit: i.limit,
                value: e,
                encoding: s
              });
            },
            args: [{
              name: "limit",
              ref: !0,
              assert: f.limit,
              message: "must be a positive integer"
            }, "encoding"]
          },
          lowercase: {
            method: function method() {
              return this["case"]("lower");
            }
          },
          max: {
            method: function method(e, t) {
              return m.length(this, "max", e, "<=", t);
            },
            args: ["limit", "encoding"]
          },
          min: {
            method: function method(e, t) {
              return m.length(this, "min", e, ">=", t);
            },
            args: ["limit", "encoding"]
          },
          normalize: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : "NFC";
              return s(m.normalizationForms.includes(e), "normalization form must be one of " + m.normalizationForms.join(", ")), this.$_addRule({
                name: "normalize",
                args: {
                  form: e
                }
              });
            },
            validate: function validate(e, _ref60, _ref61) {
              var t = _ref60.error;
              var r = _ref61.form;
              return e === e.normalize(r) ? e : t("string.normalize", {
                value: e,
                form: r
              });
            },
            convert: !0
          },
          pattern: {
            alias: "regex",
            method: function method(e) {
              var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
              s(e instanceof RegExp, "regex must be a RegExp"), s(!e.flags.includes("g") && !e.flags.includes("y"), "regex should not use global or sticky mode"), "string" == typeof t && (t = {
                name: t
              }), f.assertOptions(t, ["invert", "name"]);
              var r = ["string.pattern", t.invert ? ".invert" : "", t.name ? ".name" : ".base"].join("");
              return this.$_addRule({
                name: "pattern",
                args: {
                  regex: e,
                  options: t
                },
                errorCode: r
              });
            },
            validate: function validate(e, t, _ref62, _ref63) {
              var r = _ref62.regex,
                s = _ref62.options;
              var n = _ref63.errorCode;
              return r.test(e) ^ s.invert ? e : t.error(n, {
                name: s.name,
                regex: r,
                value: e
              });
            },
            args: ["regex", "options"],
            multi: !0
          },
          replace: {
            method: function method(e, t) {
              "string" == typeof e && (e = new RegExp(o(e), "g")), s(e instanceof RegExp, "pattern must be a RegExp"), s("string" == typeof t, "replacement must be a String");
              var r = this.clone();
              return r.$_terms.replacements || (r.$_terms.replacements = []), r.$_terms.replacements.push({
                pattern: e,
                replacement: t
              }), r;
            }
          },
          token: {
            method: function method() {
              return this.$_addRule("token");
            },
            validate: function validate(e, t) {
              return /^\w+$/.test(e) ? e : t.error("string.token");
            }
          },
          trim: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : !0;
              return s("boolean" == typeof e, "enabled must be a boolean"), this.$_addRule({
                name: "trim",
                args: {
                  enabled: e
                }
              });
            },
            validate: function validate(e, t, _ref64) {
              var r = _ref64.enabled;
              return r && e !== e.trim() ? t.error("string.trim") : e;
            },
            convert: !0
          },
          truncate: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : !0;
              return s("boolean" == typeof e, "enabled must be a boolean"), this.$_setFlag("truncate", e);
            }
          },
          uppercase: {
            method: function method() {
              return this["case"]("upper");
            }
          },
          uri: {
            method: function method() {
              var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
              f.assertOptions(e, ["allowRelative", "allowQuerySquareBrackets", "domain", "relativeOnly", "scheme", "encodeUri"]), e.domain && f.assertOptions(e.domain, ["allowFullyQualified", "allowUnicode", "maxDomainSegments", "minDomainSegments", "tlds"]);
              var _c$regex = c.regex(e),
                t = _c$regex.regex,
                r = _c$regex.scheme,
                s = e.domain ? m.addressOptions(e.domain) : null;
              return this.$_addRule({
                name: "uri",
                args: {
                  options: e
                },
                regex: t,
                domain: s,
                scheme: r
              });
            },
            validate: function validate(e, t, _ref65, _ref66) {
              var r = _ref65.options;
              var s = _ref66.regex,
                a = _ref66.domain,
                i = _ref66.scheme;
              if (["http:/", "https:/"].includes(e)) return t.error("string.uri");
              var o = s.exec(e);
              if (!o && t.prefs.convert && r.encodeUri) {
                var _t53 = encodeURI(e);
                o = s.exec(_t53), o && (e = _t53);
              }
              if (o) {
                var _s32 = o[1] || o[2];
                return !a || r.allowRelative && !_s32 || n.isValid(_s32, a) ? e : t.error("string.domain", {
                  value: _s32
                });
              }
              return r.relativeOnly ? t.error("string.uriRelativeOnly") : r.scheme ? t.error("string.uriCustomScheme", {
                scheme: i,
                value: e
              }) : t.error("string.uri");
            }
          }
        },
        manifest: {
          build: function build(e, t) {
            if (t.replacements) {
              var _iterator66 = _createForOfIteratorHelper(t.replacements),
                _step66;
              try {
                for (_iterator66.s(); !(_step66 = _iterator66.n()).done;) {
                  var _step66$value = _step66.value,
                    _r54 = _step66$value.pattern,
                    _s33 = _step66$value.replacement;
                  e = e.replace(_r54, _s33);
                }
              } catch (err) {
                _iterator66.e(err);
              } finally {
                _iterator66.f();
              }
            }
            return e;
          }
        },
        messages: {
          "string.alphanum": "{{#label}} must only contain alpha-numeric characters",
          "string.base": "{{#label}} must be a string",
          "string.base64": "{{#label}} must be a valid base64 string",
          "string.creditCard": "{{#label}} must be a credit card",
          "string.dataUri": "{{#label}} must be a valid dataUri string",
          "string.domain": "{{#label}} must contain a valid domain name",
          "string.email": "{{#label}} must be a valid email",
          "string.empty": "{{#label}} is not allowed to be empty",
          "string.guid": "{{#label}} must be a valid GUID",
          "string.hex": "{{#label}} must only contain hexadecimal characters",
          "string.hexAlign": "{{#label}} hex decoded representation must be byte aligned",
          "string.hostname": "{{#label}} must be a valid hostname",
          "string.ip": "{{#label}} must be a valid ip address with a {{#cidr}} CIDR",
          "string.ipVersion": "{{#label}} must be a valid ip address of one of the following versions {{#version}} with a {{#cidr}} CIDR",
          "string.isoDate": "{{#label}} must be in iso format",
          "string.isoDuration": "{{#label}} must be a valid ISO 8601 duration",
          "string.length": "{{#label}} length must be {{#limit}} characters long",
          "string.lowercase": "{{#label}} must only contain lowercase characters",
          "string.max": "{{#label}} length must be less than or equal to {{#limit}} characters long",
          "string.min": "{{#label}} length must be at least {{#limit}} characters long",
          "string.normalize": "{{#label}} must be unicode normalized in the {{#form}} form",
          "string.token": "{{#label}} must only contain alpha-numeric and underscore characters",
          "string.pattern.base": "{{#label}} with value {:[.]} fails to match the required pattern: {{#regex}}",
          "string.pattern.name": "{{#label}} with value {:[.]} fails to match the {{#name}} pattern",
          "string.pattern.invert.base": "{{#label}} with value {:[.]} matches the inverted pattern: {{#regex}}",
          "string.pattern.invert.name": "{{#label}} with value {:[.]} matches the inverted {{#name}} pattern",
          "string.trim": "{{#label}} must not have leading or trailing whitespace",
          "string.uri": "{{#label}} must be a valid uri",
          "string.uriCustomScheme": "{{#label}} must be a valid uri with a scheme matching the {{#scheme}} pattern",
          "string.uriRelativeOnly": "{{#label}} must be a valid relative uri",
          "string.uppercase": "{{#label}} must only contain uppercase characters"
        }
      }), m.addressOptions = function (e) {
        if (!e) return m.tlds || e;
        if (s(void 0 === e.minDomainSegments || Number.isSafeInteger(e.minDomainSegments) && e.minDomainSegments > 0, "minDomainSegments must be a positive integer"), s(void 0 === e.maxDomainSegments || Number.isSafeInteger(e.maxDomainSegments) && e.maxDomainSegments > 0, "maxDomainSegments must be a positive integer"), !1 === e.tlds) return e;
        if (!0 === e.tlds || void 0 === e.tlds) return s(m.tlds, "Built-in TLD list disabled"), Object.assign({}, e, m.tlds);
        s("object" == _typeof(e.tlds), "tlds must be true, false, or an object");
        var t = e.tlds.deny;
        if (t) return Array.isArray(t) && (e = Object.assign({}, e, {
          tlds: {
            deny: new Set(t)
          }
        })), s(e.tlds.deny instanceof Set, "tlds.deny must be an array, Set, or boolean"), s(!e.tlds.allow, "Cannot specify both tlds.allow and tlds.deny lists"), m.validateTlds(e.tlds.deny, "tlds.deny"), e;
        var r = e.tlds.allow;
        return r ? !0 === r ? (s(m.tlds, "Built-in TLD list disabled"), Object.assign({}, e, m.tlds)) : (Array.isArray(r) && (e = Object.assign({}, e, {
          tlds: {
            allow: new Set(r)
          }
        })), s(e.tlds.allow instanceof Set, "tlds.allow must be an array, Set, or boolean"), m.validateTlds(e.tlds.allow, "tlds.allow"), e) : e;
      }, m.validateTlds = function (e, t) {
        var _iterator67 = _createForOfIteratorHelper(e),
          _step67;
        try {
          for (_iterator67.s(); !(_step67 = _iterator67.n()).done;) {
            var _r55 = _step67.value;
            s(n.isValid(_r55, {
              minDomainSegments: 1,
              maxDomainSegments: 1
            }), "".concat(t, " must contain valid top level domain names"));
          }
        } catch (err) {
          _iterator67.e(err);
        } finally {
          _iterator67.f();
        }
      }, m.isoDate = function (e) {
        if (!f.isIsoDate(e)) return null;
        /.*T.*[+-]\d\d$/.test(e) && (e += "00");
        var t = new Date(e);
        return isNaN(t.getTime()) ? null : t.toISOString();
      }, m.length = function (e, t, r, n, a) {
        return s(!a || !1, "Invalid encoding:", a), e.$_addRule({
          name: t,
          method: "length",
          args: {
            limit: r,
            encoding: a
          },
          operator: n
        });
      };
    },
    8826: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8068),
        a = {};
      a.Map = /*#__PURE__*/function (_Map) {
        function _class11() {
          _classCallCheck(this, _class11);
          return _callSuper(this, _class11, arguments);
        }
        _inherits(_class11, _Map);
        return _createClass(_class11, [{
          key: "slice",
          value: function slice() {
            return new a.Map(this);
          }
        }]);
      }(/*#__PURE__*/_wrapNativeSuper(Map)), e.exports = n.extend({
        type: "symbol",
        terms: {
          map: {
            init: new a.Map()
          }
        },
        coerce: {
          method: function method(e, _ref67) {
            var t = _ref67.schema,
              r = _ref67.error;
            var s = t.$_terms.map.get(e);
            return s && (e = s), t._flags.only && "symbol" != _typeof(e) ? {
              value: e,
              errors: r("symbol.map", {
                map: t.$_terms.map
              })
            } : {
              value: e
            };
          }
        },
        validate: function validate(e, _ref68) {
          var t = _ref68.error;
          if ("symbol" != _typeof(e)) return {
            value: e,
            errors: t("symbol.base")
          };
        },
        rules: {
          map: {
            method: function method(e) {
              e && !e[Symbol.iterator] && "object" == _typeof(e) && (e = Object.entries(e)), s(e && e[Symbol.iterator], "Iterable must be an iterable or object");
              var t = this.clone(),
                r = [];
              var _iterator68 = _createForOfIteratorHelper(e),
                _step68;
              try {
                for (_iterator68.s(); !(_step68 = _iterator68.n()).done;) {
                  var _n33 = _step68.value;
                  s(_n33 && _n33[Symbol.iterator], "Entry must be an iterable");
                  var _n34 = _slicedToArray(_n33, 2),
                    _e53 = _n34[0],
                    _a15 = _n34[1];
                  s("object" != _typeof(_e53) && "function" != typeof _e53 && "symbol" != _typeof(_e53), "Key must not be of type object, function, or Symbol"), s("symbol" == _typeof(_a15), "Value must be a Symbol"), t.$_terms.map.set(_e53, _a15), r.push(_a15);
                }
              } catch (err) {
                _iterator68.e(err);
              } finally {
                _iterator68.f();
              }
              return t.valid.apply(t, r);
            }
          }
        },
        manifest: {
          build: function build(e, t) {
            return t.map && (e = e.map(t.map)), e;
          }
        },
        messages: {
          "symbol.base": "{{#label}} must be a symbol",
          "symbol.map": "{{#label}} must be one of {{#map}}"
        }
      });
    },
    8863: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8571),
        a = r(738),
        i = r(9621),
        o = r(8160),
        l = r(6354),
        c = r(493),
        u = {
          result: Symbol("result")
        };
      t.entry = function (e, t, r) {
        var n = o.defaults;
        r && (s(void 0 === r.warnings, "Cannot override warnings preference in synchronous validation"), s(void 0 === r.artifacts, "Cannot override artifacts preference in synchronous validation"), n = o.preferences(o.defaults, r));
        var a = u.entry(e, t, n);
        s(!a.mainstay.externals.length, "Schema with external rules must use validateAsync()");
        var i = {
          value: a.value
        };
        return a.error && (i.error = a.error), a.mainstay.warnings.length && (i.warning = l.details(a.mainstay.warnings)), a.mainstay.debug && (i.debug = a.mainstay.debug), a.mainstay.artifacts && (i.artifacts = a.mainstay.artifacts), i;
      }, t.entryAsync = /*#__PURE__*/function () {
        var _ref69 = _asyncToGenerator(/*#__PURE__*/_regenerator().m(function _callee(e, t, r) {
          var s, n, a, _t54, _c14, _iterator69, _step69, _loop10, _ret, c, _t57;
          return _regenerator().w(function (_context2) {
            while (1) switch (_context2.p = _context2.n) {
              case 0:
                s = o.defaults;
                r && (s = o.preferences(o.defaults, r));
                n = u.entry(e, t, s), a = n.mainstay;
                if (!n.error) {
                  _context2.n = 1;
                  break;
                }
                throw a.debug && (n.error.debug = a.debug), n.error;
              case 1:
                if (!a.externals.length) {
                  _context2.n = 11;
                  break;
                }
                _t54 = n.value;
                _c14 = [];
                _iterator69 = _createForOfIteratorHelper(a.externals);
                _context2.p = 2;
                _loop10 = /*#__PURE__*/_regenerator().m(function _loop10() {
                  var n, f, m, h, d, p, g, y, _e54, _iterator70, _step70, _t55, _e55, _i37, _t56;
                  return _regenerator().w(function (_context) {
                    while (1) switch (_context.p = _context.n) {
                      case 0:
                        n = _step69.value;
                        f = n.state.path, m = "link" === n.schema.type ? a.links.get(n.schema) : null;
                        p = _t54;
                        g = f.length ? [_t54] : [], y = f.length ? i(e, f) : e;
                        if (f.length) {
                          h = f[f.length - 1];
                          _e54 = _t54;
                          _iterator70 = _createForOfIteratorHelper(f.slice(0, -1));
                          try {
                            for (_iterator70.s(); !(_step70 = _iterator70.n()).done;) {
                              _t55 = _step70.value;
                              _e54 = _e54[_t55], g.unshift(_e54);
                            }
                          } catch (err) {
                            _iterator70.e(err);
                          } finally {
                            _iterator70.f();
                          }
                          d = g[0], p = d[h];
                        }
                        _context.p = 1;
                        _e55 = function _e55(e, t) {
                          return (m || n.schema).$_createError(e, p, t, n.state, s);
                        };
                        _context.n = 2;
                        return n.method(p, {
                          schema: n.schema,
                          linked: m,
                          state: n.state,
                          prefs: r,
                          original: y,
                          error: _e55,
                          errorsArray: u.errorsArray,
                          warn: function warn(e, t) {
                            return a.warnings.push((m || n.schema).$_createError(e, p, t, n.state, s));
                          },
                          message: function message(e, t) {
                            return (m || n.schema).$_createError("external", p, t, n.state, s, {
                              messages: e
                            });
                          }
                        });
                      case 2:
                        _i37 = _context.v;
                        if (!(void 0 === _i37 || _i37 === p)) {
                          _context.n = 3;
                          break;
                        }
                        return _context.a(2, 0);
                      case 3:
                        if (!(_i37 instanceof l.Report)) {
                          _context.n = 5;
                          break;
                        }
                        if (!(a.tracer.log(n.schema, n.state, "rule", "external", "error"), _c14.push(_i37), s.abortEarly)) {
                          _context.n = 4;
                          break;
                        }
                        return _context.a(2, 1);
                      case 4:
                        return _context.a(2, 0);
                      case 5:
                        if (!(Array.isArray(_i37) && _i37[o.symbols.errors])) {
                          _context.n = 7;
                          break;
                        }
                        if (!(a.tracer.log(n.schema, n.state, "rule", "external", "error"), _c14.push.apply(_c14, _toConsumableArray(_i37)), s.abortEarly)) {
                          _context.n = 6;
                          break;
                        }
                        return _context.a(2, 1);
                      case 6:
                        return _context.a(2, 0);
                      case 7:
                        d ? (a.tracer.value(n.state, "rule", p, _i37, "external"), d[h] = _i37) : (a.tracer.value(n.state, "rule", _t54, _i37, "external"), _t54 = _i37);
                        _context.n = 9;
                        break;
                      case 8:
                        _context.p = 8;
                        _t56 = _context.v;
                        throw s.errors.label && (_t56.message += " (".concat(n.label, ")")), _t56;
                      case 9:
                        return _context.a(2);
                    }
                  }, _loop10, null, [[1, 8]]);
                });
                _iterator69.s();
              case 3:
                if ((_step69 = _iterator69.n()).done) {
                  _context2.n = 7;
                  break;
                }
                return _context2.d(_regeneratorValues(_loop10()), 4);
              case 4:
                _ret = _context2.v;
                if (!(_ret === 0)) {
                  _context2.n = 5;
                  break;
                }
                return _context2.a(3, 6);
              case 5:
                if (!(_ret === 1)) {
                  _context2.n = 6;
                  break;
                }
                return _context2.a(3, 7);
              case 6:
                _context2.n = 3;
                break;
              case 7:
                _context2.n = 9;
                break;
              case 8:
                _context2.p = 8;
                _t57 = _context2.v;
                _iterator69.e(_t57);
              case 9:
                _context2.p = 9;
                _iterator69.f();
                return _context2.f(9);
              case 10:
                if (!(n.value = _t54, _c14.length)) {
                  _context2.n = 11;
                  break;
                }
                throw n.error = l.process(_c14, e, s), a.debug && (n.error.debug = a.debug), n.error;
              case 11:
                if (!(!s.warnings && !s.debug && !s.artifacts)) {
                  _context2.n = 12;
                  break;
                }
                return _context2.a(2, n.value);
              case 12:
                c = {
                  value: n.value
                };
                return _context2.a(2, (a.warnings.length && (c.warning = l.details(a.warnings)), a.debug && (c.debug = a.debug), a.artifacts && (c.artifacts = a.artifacts), c));
            }
          }, _callee, null, [[2, 8, 9, 10]]);
        }));
        return function (_x, _x2, _x3) {
          return _ref69.apply(this, arguments);
        };
      }(), u.Mainstay = /*#__PURE__*/function () {
        function _class12(e, t, r) {
          _classCallCheck(this, _class12);
          this.externals = [], this.warnings = [], this.tracer = e, this.debug = t, this.links = r, this.shadow = null, this.artifacts = null, this._snapshots = [];
        }
        return _createClass(_class12, [{
          key: "snapshot",
          value: function snapshot() {
            this._snapshots.push({
              externals: this.externals.slice(),
              warnings: this.warnings.slice()
            });
          }
        }, {
          key: "restore",
          value: function restore() {
            var e = this._snapshots.pop();
            this.externals = e.externals, this.warnings = e.warnings;
          }
        }, {
          key: "commit",
          value: function commit() {
            this._snapshots.pop();
          }
        }]);
      }(), u.entry = function (e, r, s) {
        var _u$tracer = u.tracer(r, s),
          n = _u$tracer.tracer,
          a = _u$tracer.cleanup,
          i = s.debug ? [] : null,
          o = r._ids._schemaChain ? new Map() : null,
          f = new u.Mainstay(n, i, o),
          m = r._ids._schemaChain ? [{
            schema: r
          }] : null,
          h = new c([], [], {
            mainstay: f,
            schemas: m
          }),
          d = t.validate(e, r, h, s);
        a && r.$_root.untrace();
        var p = l.process(d.errors, e, s);
        return {
          value: d.value,
          error: p,
          mainstay: f
        };
      }, u.tracer = function (e, t) {
        return e.$_root._tracer ? {
          tracer: e.$_root._tracer._register(e)
        } : t.debug ? (s(e.$_root.trace, "Debug mode not supported"), {
          tracer: e.$_root.trace()._register(e),
          cleanup: !0
        }) : {
          tracer: u.ignore
        };
      }, t.validate = function (e, t, r, s) {
        var n = arguments.length > 4 && arguments[4] !== undefined ? arguments[4] : {};
        if (t.$_terms.whens && (t = t._generate(e, r, s).schema), t._preferences && (s = u.prefs(t, s)), t._cache && s.cache) {
          var _s34 = t._cache.get(e);
          if (r.mainstay.tracer.debug(r, "validate", "cached", !!_s34), _s34) return _s34;
        }
        var a = function a(n, _a16, i) {
            return t.$_createError(n, e, _a16, i || r, s);
          },
          i = {
            original: e,
            prefs: s,
            schema: t,
            state: r,
            error: a,
            errorsArray: u.errorsArray,
            warn: function warn(e, t, s) {
              return r.mainstay.warnings.push(a(e, t, s));
            },
            message: function message(n, a) {
              return t.$_createError("custom", e, a, r, s, {
                messages: n
              });
            }
          };
        r.mainstay.tracer.entry(t, r);
        var l = t._definition;
        if (l.prepare && void 0 !== e && s.convert) {
          var _t58 = l.prepare(e, i);
          if (_t58) {
            if (r.mainstay.tracer.value(r, "prepare", e, _t58.value), _t58.errors) return u.finalize(_t58.value, [].concat(_t58.errors), i);
            e = _t58.value;
          }
        }
        if (l.coerce && void 0 !== e && s.convert && (!l.coerce.from || l.coerce.from.includes(_typeof(e)))) {
          var _t59 = l.coerce.method(e, i);
          if (_t59) {
            if (r.mainstay.tracer.value(r, "coerced", e, _t59.value), _t59.errors) return u.finalize(_t59.value, [].concat(_t59.errors), i);
            e = _t59.value;
          }
        }
        var c = t._flags.empty;
        c && c.$_match(u.trim(e, t), r.nest(c), o.defaults) && (r.mainstay.tracer.value(r, "empty", e, void 0), e = void 0);
        var f = n.presence || t._flags.presence || (t._flags._endedSwitch ? null : s.presence);
        if (void 0 === e) {
          if ("forbidden" === f) return u.finalize(e, null, i);
          if ("required" === f) return u.finalize(e, [t.$_createError("any.required", e, null, r, s)], i);
          if ("optional" === f) {
            if (t._flags["default"] !== o.symbols.deepDefault) return u.finalize(e, null, i);
            r.mainstay.tracer.value(r, "default", e, {}), e = {};
          }
        } else if ("forbidden" === f) return u.finalize(e, [t.$_createError("any.unknown", e, null, r, s)], i);
        var m = [];
        if (t._valids) {
          var _n35 = t._valids.get(e, r, s, t._flags.insensitive);
          if (_n35) return s.convert && (r.mainstay.tracer.value(r, "valids", e, _n35.value), e = _n35.value), r.mainstay.tracer.filter(t, r, "valid", _n35), u.finalize(e, null, i);
          if (t._flags.only) {
            var _n36 = t.$_createError("any.only", e, {
              valids: t._valids.values({
                display: !0
              })
            }, r, s);
            if (s.abortEarly) return u.finalize(e, [_n36], i);
            m.push(_n36);
          }
        }
        if (t._invalids) {
          var _n37 = t._invalids.get(e, r, s, t._flags.insensitive);
          if (_n37) {
            r.mainstay.tracer.filter(t, r, "invalid", _n37);
            var _a17 = t.$_createError("any.invalid", e, {
              invalids: t._invalids.values({
                display: !0
              })
            }, r, s);
            if (s.abortEarly) return u.finalize(e, [_a17], i);
            m.push(_a17);
          }
        }
        if (l.validate) {
          var _t60 = l.validate(e, i);
          if (_t60 && (r.mainstay.tracer.value(r, "base", e, _t60.value), e = _t60.value, _t60.errors)) {
            if (!Array.isArray(_t60.errors)) return m.push(_t60.errors), u.finalize(e, m, i);
            if (_t60.errors.length) return m.push.apply(m, _toConsumableArray(_t60.errors)), u.finalize(e, m, i);
          }
        }
        return t._rules.length ? u.rules(e, m, i) : u.finalize(e, m, i);
      }, u.rules = function (e, t, r) {
        var s = r.schema,
          n = r.state,
          a = r.prefs;
        var _iterator71 = _createForOfIteratorHelper(s._rules),
          _step71;
        try {
          for (_iterator71.s(); !(_step71 = _iterator71.n()).done;) {
            var _i38 = _step71.value;
            var _l13 = s._definition.rules[_i38.method];
            if (_l13.convert && a.convert) {
              n.mainstay.tracer.log(s, n, "rule", _i38.name, "full");
              continue;
            }
            var _c15 = void 0,
              f = _i38.args;
            if (_i38._resolve.length) {
              f = Object.assign({}, f);
              var _iterator72 = _createForOfIteratorHelper(_i38._resolve),
                _step72;
              try {
                for (_iterator72.s(); !(_step72 = _iterator72.n()).done;) {
                  var _t61 = _step72.value;
                  var _r56 = _l13.argsByName.get(_t61),
                    _i39 = f[_t61].resolve(e, n, a),
                    _u9 = _r56.normalize ? _r56.normalize(_i39) : _i39,
                    _m4 = o.validateArg(_u9, null, _r56);
                  if (_m4) {
                    _c15 = s.$_createError("any.ref", _i39, {
                      arg: _t61,
                      ref: f[_t61],
                      reason: _m4
                    }, n, a);
                    break;
                  }
                  f[_t61] = _u9;
                }
              } catch (err) {
                _iterator72.e(err);
              } finally {
                _iterator72.f();
              }
            }
            _c15 = _c15 || _l13.validate(e, r, f, _i38);
            var _m5 = u.rule(_c15, _i38);
            if (_m5.errors) {
              if (n.mainstay.tracer.log(s, n, "rule", _i38.name, "error"), _i38.warn) {
                var _n$mainstay$warnings;
                (_n$mainstay$warnings = n.mainstay.warnings).push.apply(_n$mainstay$warnings, _toConsumableArray(_m5.errors));
                continue;
              }
              if (a.abortEarly) return u.finalize(e, _m5.errors, r);
              t.push.apply(t, _toConsumableArray(_m5.errors));
            } else n.mainstay.tracer.log(s, n, "rule", _i38.name, "pass"), n.mainstay.tracer.value(n, "rule", e, _m5.value, _i38.name), e = _m5.value;
          }
        } catch (err) {
          _iterator71.e(err);
        } finally {
          _iterator71.f();
        }
        return u.finalize(e, t, r);
      }, u.rule = function (e, t) {
        return e instanceof l.Report ? (u.error(e, t), {
          errors: [e],
          value: null
        }) : Array.isArray(e) && e[o.symbols.errors] ? (e.forEach(function (e) {
          return u.error(e, t);
        }), {
          errors: e,
          value: null
        }) : {
          errors: null,
          value: e
        };
      }, u.error = function (e, t) {
        return t.message && e._setTemplate(t.message), e;
      }, u.finalize = function (e, t, r) {
        t = t || [];
        var n = r.schema,
          a = r.state,
          i = r.prefs;
        if (t.length) {
          var _s35 = u["default"]("failover", void 0, t, r);
          void 0 !== _s35 && (a.mainstay.tracer.value(a, "failover", e, _s35), e = _s35, t = []);
        }
        if (t.length && n._flags.error) if ("function" == typeof n._flags.error) {
          t = n._flags.error(t), Array.isArray(t) || (t = [t]);
          var _iterator73 = _createForOfIteratorHelper(t),
            _step73;
          try {
            for (_iterator73.s(); !(_step73 = _iterator73.n()).done;) {
              var _e56 = _step73.value;
              s(_e56 instanceof Error || _e56 instanceof l.Report, "error() must return an Error object");
            }
          } catch (err) {
            _iterator73.e(err);
          } finally {
            _iterator73.f();
          }
        } else t = [n._flags.error];
        if (void 0 === e) {
          var _s36 = u["default"]("default", e, t, r);
          a.mainstay.tracer.value(a, "default", e, _s36), e = _s36;
        }
        if (n._flags.cast && void 0 !== e) {
          var _t62 = n._definition.cast[n._flags.cast];
          if (_t62.from(e)) {
            var _s37 = _t62.to(e, r);
            a.mainstay.tracer.value(a, "cast", e, _s37, n._flags.cast), e = _s37;
          }
        }
        if (n.$_terms.externals && i.externals && !1 !== i._externals) {
          var _iterator74 = _createForOfIteratorHelper(n.$_terms.externals),
            _step74;
          try {
            for (_iterator74.s(); !(_step74 = _iterator74.n()).done;) {
              var _e57 = _step74.value.method;
              a.mainstay.externals.push({
                method: _e57,
                schema: n,
                state: a,
                label: l.label(n._flags, a, i)
              });
            }
          } catch (err) {
            _iterator74.e(err);
          } finally {
            _iterator74.f();
          }
        }
        var o = {
          value: e,
          errors: t.length ? t : null
        };
        return n._flags.result && (o.value = "strip" === n._flags.result ? void 0 : r.original, a.mainstay.tracer.value(a, n._flags.result, e, o.value), a.shadow(e, n._flags.result)), n._cache && !1 !== i.cache && !n._refs.length && n._cache.set(r.original, o), void 0 === e || o.errors || void 0 === n._flags.artifact || (a.mainstay.artifacts = a.mainstay.artifacts || new Map(), a.mainstay.artifacts.has(n._flags.artifact) || a.mainstay.artifacts.set(n._flags.artifact, []), a.mainstay.artifacts.get(n._flags.artifact).push(a.path)), o;
      }, u.prefs = function (e, t) {
        var r = t === o.defaults;
        return r && e._preferences[o.symbols.prefs] ? e._preferences[o.symbols.prefs] : (t = o.preferences(t, e._preferences), r && (e._preferences[o.symbols.prefs] = t), t);
      }, u["default"] = function (e, t, r, s) {
        var a = s.schema,
          i = s.state,
          l = s.prefs,
          c = a._flags[e];
        if (l.noDefaults || void 0 === c) return t;
        if (i.mainstay.tracer.log(a, i, "rule", e, "full"), !c) return c;
        if ("function" == typeof c) {
          var _t63 = c.length ? [n(i.ancestors[0]), s] : [];
          try {
            return c.apply(void 0, _t63);
          } catch (t) {
            return void r.push(a.$_createError("any.".concat(e), null, {
              error: t
            }, i, l));
          }
        }
        return "object" != _typeof(c) ? c : c[o.symbols.literal] ? c.literal : o.isResolvable(c) ? c.resolve(t, i, l) : n(c);
      }, u.trim = function (e, t) {
        if ("string" != typeof e) return e;
        var r = t.$_getRule("trim");
        return r && r.args.enabled ? e.trim() : e;
      }, u.ignore = {
        active: !1,
        debug: a,
        entry: a,
        filter: a,
        log: a,
        resolve: a,
        value: a
      }, u.errorsArray = function () {
        var e = [];
        return e[o.symbols.errors] = !0, e;
      };
    },
    2036: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(9474),
        a = r(8160),
        i = {};
      e.exports = i.Values = /*#__PURE__*/function () {
        function _class13(e, t) {
          _classCallCheck(this, _class13);
          this._values = new Set(e), this._refs = new Set(t), this._lowercase = i.lowercases(e), this._override = !1;
        }
        return _createClass(_class13, [{
          key: "length",
          get: function get() {
            return this._values.size + this._refs.size;
          }
        }, {
          key: "add",
          value: function add(e, t) {
            a.isResolvable(e) ? this._refs.has(e) || (this._refs.add(e), t && t.register(e)) : this.has(e, null, null, !1) || (this._values.add(e), "string" == typeof e && this._lowercase.set(e.toLowerCase(), e));
          }
        }, {
          key: "remove",
          value: function remove(e) {
            a.isResolvable(e) ? this._refs["delete"](e) : (this._values["delete"](e), "string" == typeof e && this._lowercase["delete"](e.toLowerCase()));
          }
        }, {
          key: "has",
          value: function has(e, t, r, s) {
            return !!this.get(e, t, r, s);
          }
        }, {
          key: "get",
          value: function get(e, t, r, s) {
            if (!this.length) return !1;
            if (this._values.has(e)) return {
              value: e
            };
            if ("string" == typeof e && e && s) {
              var _t64 = this._lowercase.get(e.toLowerCase());
              if (_t64) return {
                value: _t64
              };
            }
            if (!this._refs.size && "object" != _typeof(e)) return !1;
            if ("object" == _typeof(e)) {
              var _iterator75 = _createForOfIteratorHelper(this._values),
                _step75;
              try {
                for (_iterator75.s(); !(_step75 = _iterator75.n()).done;) {
                  var _t65 = _step75.value;
                  if (n(_t65, e)) return {
                    value: _t65
                  };
                }
              } catch (err) {
                _iterator75.e(err);
              } finally {
                _iterator75.f();
              }
            }
            if (t) {
              var _iterator76 = _createForOfIteratorHelper(this._refs),
                _step76;
              try {
                for (_iterator76.s(); !(_step76 = _iterator76.n()).done;) {
                  var _a18 = _step76.value;
                  var _i40 = _a18.resolve(e, t, r, null, {
                    "in": !0
                  });
                  if (void 0 === _i40) continue;
                  var o = _a18["in"] && "object" == _typeof(_i40) ? Array.isArray(_i40) ? _i40 : Object.keys(_i40) : [_i40];
                  var _iterator77 = _createForOfIteratorHelper(o),
                    _step77;
                  try {
                    for (_iterator77.s(); !(_step77 = _iterator77.n()).done;) {
                      var _t66 = _step77.value;
                      if (_typeof(_t66) == _typeof(e)) if (s && e && "string" == typeof e) {
                        if (_t66.toLowerCase() === e.toLowerCase()) return {
                          value: _t66,
                          ref: _a18
                        };
                      } else if (n(_t66, e)) return {
                        value: _t66,
                        ref: _a18
                      };
                    }
                  } catch (err) {
                    _iterator77.e(err);
                  } finally {
                    _iterator77.f();
                  }
                }
              } catch (err) {
                _iterator76.e(err);
              } finally {
                _iterator76.f();
              }
            }
            return !1;
          }
        }, {
          key: "override",
          value: function override() {
            this._override = !0;
          }
        }, {
          key: "values",
          value: function values(e) {
            if (e && e.display) {
              var _e58 = [];
              for (var _i41 = 0, _arr4 = [].concat(_toConsumableArray(this._values), _toConsumableArray(this._refs)); _i41 < _arr4.length; _i41++) {
                var _t67 = _arr4[_i41];
                void 0 !== _t67 && _e58.push(_t67);
              }
              return _e58;
            }
            return Array.from([].concat(_toConsumableArray(this._values), _toConsumableArray(this._refs)));
          }
        }, {
          key: "clone",
          value: function clone() {
            var e = new i.Values(this._values, this._refs);
            return e._override = this._override, e;
          }
        }, {
          key: "concat",
          value: function concat(e) {
            s(!e._override, "Cannot concat override set of values");
            var t = new i.Values([].concat(_toConsumableArray(this._values), _toConsumableArray(e._values)), [].concat(_toConsumableArray(this._refs), _toConsumableArray(e._refs)));
            return t._override = this._override, t;
          }
        }, {
          key: "describe",
          value: function describe() {
            var e = [];
            this._override && e.push({
              override: !0
            });
            var _iterator78 = _createForOfIteratorHelper(this._values.values()),
              _step78;
            try {
              for (_iterator78.s(); !(_step78 = _iterator78.n()).done;) {
                var _t68 = _step78.value;
                e.push(_t68 && "object" == _typeof(_t68) ? {
                  value: _t68
                } : _t68);
              }
            } catch (err) {
              _iterator78.e(err);
            } finally {
              _iterator78.f();
            }
            var _iterator79 = _createForOfIteratorHelper(this._refs.values()),
              _step79;
            try {
              for (_iterator79.s(); !(_step79 = _iterator79.n()).done;) {
                var _t69 = _step79.value;
                e.push(_t69.describe());
              }
            } catch (err) {
              _iterator79.e(err);
            } finally {
              _iterator79.f();
            }
            return e;
          }
        }], [{
          key: "merge",
          value: function merge(e, t, r) {
            if (e = e || new i.Values(), t) {
              if (t._override) return t.clone();
              for (var _i42 = 0, _arr5 = [].concat(_toConsumableArray(t._values), _toConsumableArray(t._refs)); _i42 < _arr5.length; _i42++) {
                var _r57 = _arr5[_i42];
                e.add(_r57);
              }
            }
            if (r) for (var _i43 = 0, _arr6 = [].concat(_toConsumableArray(r._values), _toConsumableArray(r._refs)); _i43 < _arr6.length; _i43++) {
              var _t70 = _arr6[_i43];
              e.remove(_t70);
            }
            return e.length ? e : null;
          }
        }]);
      }(), i.Values.prototype[a.symbols.values] = !0, i.Values.prototype.slice = i.Values.prototype.clone, i.lowercases = function (e) {
        var t = new Map();
        if (e) {
          var _iterator80 = _createForOfIteratorHelper(e),
            _step80;
          try {
            for (_iterator80.s(); !(_step80 = _iterator80.n()).done;) {
              var _r58 = _step80.value;
              "string" == typeof _r58 && t.set(_r58.toLowerCase(), _r58);
            }
          } catch (err) {
            _iterator80.e(err);
          } finally {
            _iterator80.f();
          }
        }
        return t;
      };
    },
    978: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8571),
        a = r(1687),
        i = r(9621),
        o = {};
      e.exports = function (e, t) {
        var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
        if (s(e && "object" == _typeof(e), "Invalid defaults value: must be an object"), s(!t || !0 === t || "object" == _typeof(t), "Invalid source value: must be true, falsy or an object"), s("object" == _typeof(r), "Invalid options: must be an object"), !t) return null;
        if (r.shallow) return o.applyToDefaultsWithShallow(e, t, r);
        var i = n(e);
        if (!0 === t) return i;
        var l = void 0 !== r.nullOverride && r.nullOverride;
        return a(i, t, {
          nullOverride: l,
          mergeArrays: !1
        });
      }, o.applyToDefaultsWithShallow = function (e, t, r) {
        var l = r.shallow;
        s(Array.isArray(l), "Invalid keys");
        var c = new Map(),
          u = !0 === t ? null : new Set();
        var _iterator81 = _createForOfIteratorHelper(l),
          _step81;
        try {
          for (_iterator81.s(); !(_step81 = _iterator81.n()).done;) {
            var _r59 = _step81.value;
            _r59 = Array.isArray(_r59) ? _r59 : _r59.split(".");
            var _s38 = i(e, _r59);
            _s38 && "object" == _typeof(_s38) ? c.set(_s38, u && i(t, _r59) || _s38) : u && u.add(_r59);
          }
        } catch (err) {
          _iterator81.e(err);
        } finally {
          _iterator81.f();
        }
        var f = n(e, {}, c);
        if (!u) return f;
        var _iterator82 = _createForOfIteratorHelper(u),
          _step82;
        try {
          for (_iterator82.s(); !(_step82 = _iterator82.n()).done;) {
            var _e59 = _step82.value;
            o.reachCopy(f, t, _e59);
          }
        } catch (err) {
          _iterator82.e(err);
        } finally {
          _iterator82.f();
        }
        var m = void 0 !== r.nullOverride && r.nullOverride;
        return a(f, t, {
          nullOverride: m,
          mergeArrays: !1
        });
      }, o.reachCopy = function (e, t, r) {
        var _iterator83 = _createForOfIteratorHelper(r),
          _step83;
        try {
          for (_iterator83.s(); !(_step83 = _iterator83.n()).done;) {
            var _e61 = _step83.value;
            if (!(_e61 in t)) return;
            var _r60 = t[_e61];
            if ("object" != _typeof(_r60) || null === _r60) return;
            t = _r60;
          }
        } catch (err) {
          _iterator83.e(err);
        } finally {
          _iterator83.f();
        }
        var s = t;
        var n = e;
        for (var _e60 = 0; _e60 < r.length - 1; ++_e60) {
          var _t71 = r[_e60];
          "object" != _typeof(n[_t71]) && (n[_t71] = {}), n = n[_t71];
        }
        n[r[r.length - 1]] = s;
      };
    },
    375: function _(e, t, r) {
      "use strict";

      var s = r(7916);
      e.exports = function (e) {
        if (!e) {
          for (var _len26 = arguments.length, t = new Array(_len26 > 1 ? _len26 - 1 : 0), _key26 = 1; _key26 < _len26; _key26++) {
            t[_key26 - 1] = arguments[_key26];
          }
          if (1 === t.length && t[0] instanceof Error) throw t[0];
          throw new s(t);
        }
      };
    },
    8571: function _(e, t, r) {
      "use strict";

      var s = r(9621),
        n = r(4277),
        a = r(7043),
        i = {
          needsProtoHack: new Set([n.set, n.map, n.weakSet, n.weakMap])
        };
      e.exports = i.clone = function (e) {
        var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
        var r = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : null;
        if ("object" != _typeof(e) || null === e) return e;
        var s = i.clone,
          o = r;
        if (t.shallow) {
          if (!0 !== t.shallow) return i.cloneWithShallow(e, t);
          s = function s(e) {
            return e;
          };
        } else if (o) {
          var _t72 = o.get(e);
          if (_t72) return _t72;
        } else o = new Map();
        var l = n.getInternalProto(e);
        if (l === n.buffer) return !1;
        if (l === n.date) return new Date(e.getTime());
        if (l === n.regex) return new RegExp(e);
        var c = i.base(e, l, t);
        if (c === e) return e;
        if (o && o.set(e, c), l === n.set) {
          var _iterator84 = _createForOfIteratorHelper(e),
            _step84;
          try {
            for (_iterator84.s(); !(_step84 = _iterator84.n()).done;) {
              var _r61 = _step84.value;
              c.add(s(_r61, t, o));
            }
          } catch (err) {
            _iterator84.e(err);
          } finally {
            _iterator84.f();
          }
        } else if (l === n.map) {
          var _iterator85 = _createForOfIteratorHelper(e),
            _step85;
          try {
            for (_iterator85.s(); !(_step85 = _iterator85.n()).done;) {
              var _step85$value = _slicedToArray(_step85.value, 2),
                _r62 = _step85$value[0],
                _n38 = _step85$value[1];
              c.set(_r62, s(_n38, t, o));
            }
          } catch (err) {
            _iterator85.e(err);
          } finally {
            _iterator85.f();
          }
        }
        var u = a.keys(e, t);
        var _iterator86 = _createForOfIteratorHelper(u),
          _step86;
        try {
          for (_iterator86.s(); !(_step86 = _iterator86.n()).done;) {
            var _r63 = _step86.value;
            if ("__proto__" === _r63) continue;
            if (l === n.array && "length" === _r63) {
              c.length = e.length;
              continue;
            }
            var _a19 = Object.getOwnPropertyDescriptor(e, _r63);
            _a19 ? _a19.get || _a19.set ? Object.defineProperty(c, _r63, _a19) : _a19.enumerable ? c[_r63] = s(e[_r63], t, o) : Object.defineProperty(c, _r63, {
              enumerable: !1,
              writable: !0,
              configurable: !0,
              value: s(e[_r63], t, o)
            }) : Object.defineProperty(c, _r63, {
              enumerable: !0,
              writable: !0,
              configurable: !0,
              value: s(e[_r63], t, o)
            });
          }
        } catch (err) {
          _iterator86.e(err);
        } finally {
          _iterator86.f();
        }
        return c;
      }, i.cloneWithShallow = function (e, t) {
        var r = t.shallow;
        (t = Object.assign({}, t)).shallow = !1;
        var n = new Map();
        var _iterator87 = _createForOfIteratorHelper(r),
          _step87;
        try {
          for (_iterator87.s(); !(_step87 = _iterator87.n()).done;) {
            var _t73 = _step87.value;
            var _r64 = s(e, _t73);
            "object" != _typeof(_r64) && "function" != typeof _r64 || n.set(_r64, _r64);
          }
        } catch (err) {
          _iterator87.e(err);
        } finally {
          _iterator87.f();
        }
        return i.clone(e, t, n);
      }, i.base = function (e, t, r) {
        if (!1 === r.prototype) return i.needsProtoHack.has(t) ? new t.constructor() : t === n.array ? [] : {};
        var s = Object.getPrototypeOf(e);
        if (s && s.isImmutable) return e;
        if (t === n.array) {
          var _e62 = [];
          return s !== t && Object.setPrototypeOf(_e62, s), _e62;
        }
        if (i.needsProtoHack.has(t)) {
          var _e63 = new s.constructor();
          return s !== t && Object.setPrototypeOf(_e63, s), _e63;
        }
        return Object.create(s);
      };
    },
    9474: function _(e, t, r) {
      "use strict";

      var s = r(4277),
        n = {
          mismatched: null
        };
      e.exports = function (e, t, r) {
        return r = Object.assign({
          prototype: !0
        }, r), !!n.isDeepEqual(e, t, r, []);
      }, n.isDeepEqual = function (e, t, r, a) {
        if (e === t) return 0 !== e || 1 / e == 1 / t;
        var i = _typeof(e);
        if (i !== _typeof(t)) return !1;
        if (null === e || null === t) return !1;
        if ("function" === i) {
          if (!r.deepFunction || e.toString() !== t.toString()) return !1;
        } else if ("object" !== i) return e != e && t != t;
        var o = n.getSharedType(e, t, !!r.prototype);
        switch (o) {
          case s.buffer:
            return !1;
          case s.promise:
            return e === t;
          case s.regex:
            return e.toString() === t.toString();
          case n.mismatched:
            return !1;
        }
        for (var _r65 = a.length - 1; _r65 >= 0; --_r65) if (a[_r65].isSame(e, t)) return !0;
        a.push(new n.SeenEntry(e, t));
        try {
          return !!n.isDeepEqualObj(o, e, t, r, a);
        } finally {
          a.pop();
        }
      }, n.getSharedType = function (e, t, r) {
        if (r) return Object.getPrototypeOf(e) !== Object.getPrototypeOf(t) ? n.mismatched : s.getInternalProto(e);
        var a = s.getInternalProto(e);
        return a !== s.getInternalProto(t) ? n.mismatched : a;
      }, n.valueOf = function (e) {
        var t = e.valueOf;
        if (void 0 === t) return e;
        try {
          return t.call(e);
        } catch (e) {
          return e;
        }
      }, n.hasOwnEnumerableProperty = function (e, t) {
        return Object.prototype.propertyIsEnumerable.call(e, t);
      }, n.isSetSimpleEqual = function (e, t) {
        var _iterator88 = _createForOfIteratorHelper(Set.prototype.values.call(e)),
          _step88;
        try {
          for (_iterator88.s(); !(_step88 = _iterator88.n()).done;) {
            var _r66 = _step88.value;
            if (!Set.prototype.has.call(t, _r66)) return !1;
          }
        } catch (err) {
          _iterator88.e(err);
        } finally {
          _iterator88.f();
        }
        return !0;
      }, n.isDeepEqualObj = function (e, t, r, a, i) {
        var o = n.isDeepEqual,
          l = n.valueOf,
          c = n.hasOwnEnumerableProperty,
          u = Object.keys,
          f = Object.getOwnPropertySymbols;
        if (e === s.array) {
          if (!a.part) {
            if (t.length !== r.length) return !1;
            for (var _e64 = 0; _e64 < t.length; ++_e64) if (!o(t[_e64], r[_e64], a, i)) return !1;
            return !0;
          }
          var _iterator89 = _createForOfIteratorHelper(t),
            _step89;
          try {
            for (_iterator89.s(); !(_step89 = _iterator89.n()).done;) {
              var _e65 = _step89.value;
              var _iterator90 = _createForOfIteratorHelper(r),
                _step90;
              try {
                for (_iterator90.s(); !(_step90 = _iterator90.n()).done;) {
                  var _t74 = _step90.value;
                  if (o(_e65, _t74, a, i)) return !0;
                }
              } catch (err) {
                _iterator90.e(err);
              } finally {
                _iterator90.f();
              }
            }
          } catch (err) {
            _iterator89.e(err);
          } finally {
            _iterator89.f();
          }
        } else if (e === s.set) {
          if (t.size !== r.size) return !1;
          if (!n.isSetSimpleEqual(t, r)) {
            var _e66 = new Set(Set.prototype.values.call(r));
            var _iterator91 = _createForOfIteratorHelper(Set.prototype.values.call(t)),
              _step91;
            try {
              for (_iterator91.s(); !(_step91 = _iterator91.n()).done;) {
                var _r67 = _step91.value;
                if (_e66["delete"](_r67)) continue;
                var _t75 = !1;
                var _iterator92 = _createForOfIteratorHelper(_e66),
                  _step92;
                try {
                  for (_iterator92.s(); !(_step92 = _iterator92.n()).done;) {
                    var _s39 = _step92.value;
                    if (o(_r67, _s39, a, i)) {
                      _e66["delete"](_s39), _t75 = !0;
                      break;
                    }
                  }
                } catch (err) {
                  _iterator92.e(err);
                } finally {
                  _iterator92.f();
                }
                if (!_t75) return !1;
              }
            } catch (err) {
              _iterator91.e(err);
            } finally {
              _iterator91.f();
            }
          }
        } else if (e === s.map) {
          if (t.size !== r.size) return !1;
          var _iterator93 = _createForOfIteratorHelper(Map.prototype.entries.call(t)),
            _step93;
          try {
            for (_iterator93.s(); !(_step93 = _iterator93.n()).done;) {
              var _step93$value = _slicedToArray(_step93.value, 2),
                _e67 = _step93$value[0],
                _s40 = _step93$value[1];
              if (void 0 === _s40 && !Map.prototype.has.call(r, _e67)) return !1;
              if (!o(_s40, Map.prototype.get.call(r, _e67), a, i)) return !1;
            }
          } catch (err) {
            _iterator93.e(err);
          } finally {
            _iterator93.f();
          }
        } else if (e === s.error && (t.name !== r.name || t.message !== r.message)) return !1;
        var m = l(t),
          h = l(r);
        if ((t !== m || r !== h) && !o(m, h, a, i)) return !1;
        var d = u(t);
        if (!a.part && d.length !== u(r).length && !a.skip) return !1;
        var p = 0;
        var _iterator94 = _createForOfIteratorHelper(d),
          _step94;
        try {
          for (_iterator94.s(); !(_step94 = _iterator94.n()).done;) {
            var _e70 = _step94.value;
            if (a.skip && a.skip.includes(_e70)) void 0 === r[_e70] && ++p;else {
              if (!c(r, _e70)) return !1;
              if (!o(t[_e70], r[_e70], a, i)) return !1;
            }
          }
        } catch (err) {
          _iterator94.e(err);
        } finally {
          _iterator94.f();
        }
        if (!a.part && d.length - p !== u(r).length) return !1;
        if (!1 !== a.symbols) {
          var _e68 = f(t),
            _s41 = new Set(f(r));
          var _iterator95 = _createForOfIteratorHelper(_e68),
            _step95;
          try {
            for (_iterator95.s(); !(_step95 = _iterator95.n()).done;) {
              var _n39 = _step95.value;
              if (!a.skip || !a.skip.includes(_n39)) if (c(t, _n39)) {
                if (!c(r, _n39)) return !1;
                if (!o(t[_n39], r[_n39], a, i)) return !1;
              } else if (c(r, _n39)) return !1;
              _s41["delete"](_n39);
            }
          } catch (err) {
            _iterator95.e(err);
          } finally {
            _iterator95.f();
          }
          var _iterator96 = _createForOfIteratorHelper(_s41),
            _step96;
          try {
            for (_iterator96.s(); !(_step96 = _iterator96.n()).done;) {
              var _e69 = _step96.value;
              if (c(r, _e69)) return !1;
            }
          } catch (err) {
            _iterator96.e(err);
          } finally {
            _iterator96.f();
          }
        }
        return !0;
      }, n.SeenEntry = /*#__PURE__*/function () {
        function _class14(e, t) {
          _classCallCheck(this, _class14);
          this.obj = e, this.ref = t;
        }
        return _createClass(_class14, [{
          key: "isSame",
          value: function isSame(e, t) {
            return this.obj === e && this.ref === t;
          }
        }]);
      }();
    },
    7916: function _(e, t, r) {
      "use strict";

      var s = r(8761);
      e.exports = /*#__PURE__*/function (_Error2) {
        function _class15(e) {
          var _this9;
          _classCallCheck(this, _class15);
          _this9 = _callSuper(this, _class15, [e.filter(function (e) {
            return "" !== e;
          }).map(function (e) {
            return "string" == typeof e ? e : e instanceof Error ? e.message : s(e);
          }).join(" ") || "Unknown error"]), "function" == typeof Error.captureStackTrace && Error.captureStackTrace(_assertThisInitialized(_this9), t.assert);
          return _this9;
        }
        _inherits(_class15, _Error2);
        return _createClass(_class15);
      }(/*#__PURE__*/_wrapNativeSuper(Error));
    },
    5277: function _(e) {
      "use strict";

      var t = {};
      e.exports = function (e) {
        if (!e) return "";
        var r = "";
        for (var _s42 = 0; _s42 < e.length; ++_s42) {
          var _n40 = e.charCodeAt(_s42);
          t.isSafe(_n40) ? r += e[_s42] : r += t.escapeHtmlChar(_n40);
        }
        return r;
      }, t.escapeHtmlChar = function (e) {
        return t.namedHtml.get(e) || (e >= 256 ? "&#" + e + ";" : "&#x".concat(e.toString(16).padStart(2, "0"), ";"));
      }, t.isSafe = function (e) {
        return t.safeCharCodes.has(e);
      }, t.namedHtml = new Map([[38, "&amp;"], [60, "&lt;"], [62, "&gt;"], [34, "&quot;"], [160, "&nbsp;"], [162, "&cent;"], [163, "&pound;"], [164, "&curren;"], [169, "&copy;"], [174, "&reg;"]]), t.safeCharCodes = function () {
        var e = new Set();
        for (var _t76 = 32; _t76 < 123; ++_t76) ((_t76 >= 97) || (_t76 >= 65) && (_t76 <= 90) || (_t76 >= 48) && (_t76 <= 57) || 32 === _t76 || 46 === _t76 || 44 === _t76 || 45 === _t76 || 58 === _t76 || 95 === _t76) && e.add(_t76);
        return e;
      }();
    },
    6064: function _(e) {
      "use strict";

      e.exports = function (e) {
        return e.replace(/[\^\$\.\*\+\-\?\=\!\:\|\\\/\(\)\[\]\{\}\,]/g, "\\$&");
      };
    },
    738: function _(e) {
      "use strict";

      e.exports = function () {};
    },
    1687: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(8571),
        a = r(7043),
        i = {};
      e.exports = i.merge = function (e, t, r) {
        if (s(e && "object" == _typeof(e), "Invalid target value: must be an object"), s(null == t || "object" == _typeof(t), "Invalid source value: must be null, undefined, or an object"), !t) return e;
        if (r = Object.assign({
          nullOverride: !0,
          mergeArrays: !0
        }, r), Array.isArray(t)) {
          s(Array.isArray(e), "Cannot merge array onto an object"), r.mergeArrays || (e.length = 0);
          for (var _s43 = 0; _s43 < t.length; ++_s43) e.push(n(t[_s43], {
            symbols: r.symbols
          }));
          return e;
        }
        var o = a.keys(t, r);
        for (var _s44 = 0; _s44 < o.length; ++_s44) {
          var _a20 = o[_s44];
          if ("__proto__" === _a20 || !Object.prototype.propertyIsEnumerable.call(t, _a20)) continue;
          var l = t[_a20];
          if (l && "object" == _typeof(l)) {
            if (e[_a20] === l) continue;
            !e[_a20] || "object" != _typeof(e[_a20]) || Array.isArray(e[_a20]) !== Array.isArray(l) || l instanceof Date || l instanceof RegExp ? e[_a20] = n(l, {
              symbols: r.symbols
            }) : i.merge(e[_a20], l, r);
          } else (null != l || r.nullOverride) && (e[_a20] = l);
        }
        return e;
      };
    },
    9621: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = {};
      e.exports = function (e, t, r) {
        if (!1 === t || null == t) return e;
        "string" == typeof (r = r || {}) && (r = {
          separator: r
        });
        var a = Array.isArray(t);
        s(!a || !r.separator, "Separator option is not valid for array-based chain");
        var i = a ? t : t.split(r.separator || ".");
        var o = e;
        for (var _e71 = 0; _e71 < i.length; ++_e71) {
          var _a21 = i[_e71];
          var l = r.iterables && n.iterables(o);
          if (Array.isArray(o) || "set" === l) {
            var _e72 = Number(_a21);
            Number.isInteger(_e72) && (_a21 = _e72 < 0 ? o.length + _e72 : _e72);
          }
          if (!o || "function" == typeof o && !1 === r.functions || !l && void 0 === o[_a21]) {
            s(!r.strict || _e71 + 1 === i.length, "Missing segment", _a21, "in reach path ", t), s("object" == _typeof(o) || !0 === r.functions || "function" != typeof o, "Invalid segment", _a21, "in reach path ", t), o = r["default"];
            break;
          }
          o = l ? "set" === l ? _toConsumableArray(o)[_a21] : o.get(_a21) : o[_a21];
        }
        return o;
      }, n.iterables = function (e) {
        return e instanceof Set ? "set" : e instanceof Map ? "map" : void 0;
      };
    },
    8761: function _(e) {
      "use strict";

      e.exports = function () {
        try {
          return JSON.stringify.apply(JSON, arguments);
        } catch (e) {
          return "[Cannot display object: " + e.message + "]";
        }
      };
    },
    4277: function _(e, t) {
      "use strict";

      var r = {};
      t = e.exports = {
        array: Array.prototype,
        buffer: !1,
        date: Date.prototype,
        error: Error.prototype,
        generic: Object.prototype,
        map: Map.prototype,
        promise: Promise.prototype,
        regex: RegExp.prototype,
        set: Set.prototype,
        weakMap: WeakMap.prototype,
        weakSet: WeakSet.prototype
      }, r.typeMap = new Map([["[object Error]", t.error], ["[object Map]", t.map], ["[object Promise]", t.promise], ["[object Set]", t.set], ["[object WeakMap]", t.weakMap], ["[object WeakSet]", t.weakSet]]), t.getInternalProto = function (e) {
        if (Array.isArray(e)) return t.array;
        if (e instanceof Date) return t.date;
        if (e instanceof RegExp) return t.regex;
        if (e instanceof Error) return t.error;
        var s = Object.prototype.toString.call(e);
        return r.typeMap.get(s) || t.generic;
      };
    },
    7043: function _(e, t) {
      "use strict";

      t.keys = function (e) {
        var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
        return !1 !== t.symbols ? Reflect.ownKeys(e) : Object.getOwnPropertyNames(e);
      };
    },
    3652: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = {};
      t.Sorter = /*#__PURE__*/function () {
        function _class16() {
          _classCallCheck(this, _class16);
          this._items = [], this.nodes = [];
        }
        return _createClass(_class16, [{
          key: "add",
          value: function add(e, t) {
            var r = [].concat((t = t || {}).before || []),
              n = [].concat(t.after || []),
              a = t.group || "?",
              i = t.sort || 0;
            s(!r.includes(a), "Item cannot come before itself: ".concat(a)), s(!r.includes("?"), "Item cannot come before unassociated items"), s(!n.includes(a), "Item cannot come after itself: ".concat(a)), s(!n.includes("?"), "Item cannot come after unassociated items"), Array.isArray(e) || (e = [e]);
            var _iterator97 = _createForOfIteratorHelper(e),
              _step97;
            try {
              for (_iterator97.s(); !(_step97 = _iterator97.n()).done;) {
                var _t77 = _step97.value;
                var _e74 = {
                  seq: this._items.length,
                  sort: i,
                  before: r,
                  after: n,
                  group: a,
                  node: _t77
                };
                this._items.push(_e74);
              }
            } catch (err) {
              _iterator97.e(err);
            } finally {
              _iterator97.f();
            }
            if (!t.manual) {
              var _e73 = this._sort();
              s(_e73, "item", "?" !== a ? "added into group ".concat(a) : "", "created a dependencies error");
            }
            return this.nodes;
          }
        }, {
          key: "merge",
          value: function merge(e) {
            Array.isArray(e) || (e = [e]);
            var _iterator98 = _createForOfIteratorHelper(e),
              _step98;
            try {
              for (_iterator98.s(); !(_step98 = _iterator98.n()).done;) {
                var _t78 = _step98.value;
                if (_t78) {
                  var _iterator99 = _createForOfIteratorHelper(_t78._items),
                    _step99;
                  try {
                    for (_iterator99.s(); !(_step99 = _iterator99.n()).done;) {
                      var _e76 = _step99.value;
                      this._items.push(Object.assign({}, _e76));
                    }
                  } catch (err) {
                    _iterator99.e(err);
                  } finally {
                    _iterator99.f();
                  }
                }
              }
            } catch (err) {
              _iterator98.e(err);
            } finally {
              _iterator98.f();
            }
            this._items.sort(n.mergeSort);
            for (var _e75 = 0; _e75 < this._items.length; ++_e75) this._items[_e75].seq = _e75;
            var t = this._sort();
            return s(t, "merge created a dependencies error"), this.nodes;
          }
        }, {
          key: "sort",
          value: function sort() {
            var e = this._sort();
            return s(e, "sort created a dependencies error"), this.nodes;
          }
        }, {
          key: "_sort",
          value: function _sort() {
            var e = {},
              t = Object.create(null),
              r = Object.create(null);
            var _iterator100 = _createForOfIteratorHelper(this._items),
              _step100;
            try {
              for (_iterator100.s(); !(_step100 = _iterator100.n()).done;) {
                var _s47 = _step100.value;
                var _n43 = _s47.seq,
                  _a25 = _s47.group;
                r[_a25] = r[_a25] || [], r[_a25].push(_n43), e[_n43] = _s47.before;
                var _iterator104 = _createForOfIteratorHelper(_s47.after),
                  _step104;
                try {
                  for (_iterator104.s(); !(_step104 = _iterator104.n()).done;) {
                    var _e81 = _step104.value;
                    t[_e81] = t[_e81] || [], t[_e81].push(_n43);
                  }
                } catch (err) {
                  _iterator104.e(err);
                } finally {
                  _iterator104.f();
                }
              }
            } catch (err) {
              _iterator100.e(err);
            } finally {
              _iterator100.f();
            }
            for (var _t79 in e) {
              var _s45 = [];
              for (var _n41 in e[_t79]) {
                var _a22 = e[_t79][_n41];
                r[_a22] = r[_a22] || [], _s45.push.apply(_s45, _toConsumableArray(r[_a22]));
              }
              e[_t79] = _s45;
            }
            for (var _s46 in t) if (r[_s46]) {
              var _iterator101 = _createForOfIteratorHelper(r[_s46]),
                _step101;
              try {
                for (_iterator101.s(); !(_step101 = _iterator101.n()).done;) {
                  var _e$_n;
                  var _n42 = _step101.value;
                  (_e$_n = e[_n42]).push.apply(_e$_n, _toConsumableArray(t[_s46]));
                }
              } catch (err) {
                _iterator101.e(err);
              } finally {
                _iterator101.f();
              }
            }
            var s = {};
            for (var _t80 in e) {
              var _r68 = e[_t80];
              var _iterator102 = _createForOfIteratorHelper(_r68),
                _step102;
              try {
                for (_iterator102.s(); !(_step102 = _iterator102.n()).done;) {
                  var _e77 = _step102.value;
                  s[_e77] = s[_e77] || [], s[_e77].push(_t80);
                }
              } catch (err) {
                _iterator102.e(err);
              } finally {
                _iterator102.f();
              }
            }
            var n = {},
              a = [];
            for (var _e78 = 0; _e78 < this._items.length; ++_e78) {
              var _t81 = _e78;
              if (s[_e78]) {
                _t81 = null;
                for (var _e79 = 0; _e79 < this._items.length; ++_e79) {
                  if (!0 === n[_e79]) continue;
                  s[_e79] || (s[_e79] = []);
                  var _r69 = s[_e79].length;
                  var _a23 = 0;
                  for (var _t82 = 0; _t82 < _r69; ++_t82) n[s[_e79][_t82]] && ++_a23;
                  if (_a23 === _r69) {
                    _t81 = _e79;
                    break;
                  }
                }
              }
              null !== _t81 && (n[_t81] = !0, a.push(_t81));
            }
            if (a.length !== this._items.length) return !1;
            var i = {};
            var _iterator103 = _createForOfIteratorHelper(this._items),
              _step103;
            try {
              for (_iterator103.s(); !(_step103 = _iterator103.n()).done;) {
                var _e82 = _step103.value;
                i[_e82.seq] = _e82;
              }
            } catch (err) {
              _iterator103.e(err);
            } finally {
              _iterator103.f();
            }
            this._items = [], this.nodes = [];
            for (var _i44 = 0, _a24 = a; _i44 < _a24.length; _i44++) {
              var _e80 = _a24[_i44];
              var _t83 = i[_e80];
              this.nodes.push(_t83.node), this._items.push(_t83);
            }
            return !0;
          }
        }]);
      }(), n.mergeSort = function (e, t) {
        return e.sort === t.sort ? 0 : e.sort < t.sort ? -1 : 1;
      };
    },
    5380: function _(e, t, r) {
      "use strict";

      var s = r(443),
        n = r(2178),
        a = {
          minDomainSegments: 2,
          nonAsciiRx: /[^\x00-\x7f]/,
          domainControlRx: /[\x00-\x20@\:\/\\#!\$&\'\(\)\*\+,;=\?]/,
          tldSegmentRx: /^[a-zA-Z](?:[a-zA-Z0-9\-]*[a-zA-Z0-9])?$/,
          domainSegmentRx: /^[a-zA-Z0-9](?:[a-zA-Z0-9\-]*[a-zA-Z0-9])?$/,
          URL: s.URL || URL
        };
      t.analyze = function (e) {
        var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
        if (!e) return n.code("DOMAIN_NON_EMPTY_STRING");
        if ("string" != typeof e) throw new Error("Invalid input: domain must be a string");
        if (e.length > 256) return n.code("DOMAIN_TOO_LONG");
        if (a.nonAsciiRx.test(e)) {
          if (!1 === t.allowUnicode) return n.code("DOMAIN_INVALID_UNICODE_CHARS");
          e = e.normalize("NFC");
        }
        if (a.domainControlRx.test(e)) return n.code("DOMAIN_INVALID_CHARS");
        e = a.punycode(e), t.allowFullyQualified && "." === e[e.length - 1] && (e = e.slice(0, -1));
        var r = t.minDomainSegments || a.minDomainSegments,
          s = e.split(".");
        if (s.length < r) return n.code("DOMAIN_SEGMENTS_COUNT");
        if (t.maxDomainSegments && s.length > t.maxDomainSegments) return n.code("DOMAIN_SEGMENTS_COUNT_MAX");
        var i = t.tlds;
        if (i) {
          var _e83 = s[s.length - 1].toLowerCase();
          if (i.deny && i.deny.has(_e83) || i.allow && !i.allow.has(_e83)) return n.code("DOMAIN_FORBIDDEN_TLDS");
        }
        for (var _e84 = 0; _e84 < s.length; ++_e84) {
          var _t84 = s[_e84];
          if (!_t84.length) return n.code("DOMAIN_EMPTY_SEGMENT");
          if (_t84.length > 63) return n.code("DOMAIN_LONG_SEGMENT");
          if (_e84 < s.length - 1) {
            if (!a.domainSegmentRx.test(_t84)) return n.code("DOMAIN_INVALID_CHARS");
          } else if (!a.tldSegmentRx.test(_t84)) return n.code("DOMAIN_INVALID_TLDS_CHARS");
        }
        return null;
      }, t.isValid = function (e, r) {
        return !t.analyze(e, r);
      }, a.punycode = function (e) {
        e.includes("%") && (e = e.replace(/%/g, "%25"));
        try {
          return new a.URL("http://".concat(e)).host;
        } catch (t) {
          return e;
        }
      };
    },
    1745: function _(e, t, r) {
      "use strict";

      var s = r(9848),
        n = r(5380),
        a = r(2178),
        i = {
          nonAsciiRx: /[^\x00-\x7f]/,
          encoder: new (s.TextEncoder || TextEncoder)()
        };
      t.analyze = function (e, t) {
        return i.email(e, t);
      }, t.isValid = function (e, t) {
        return !i.email(e, t);
      }, i.email = function (e) {
        var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
        if ("string" != typeof e) throw new Error("Invalid input: email must be a string");
        if (!e) return a.code("EMPTY_STRING");
        var r = !i.nonAsciiRx.test(e);
        if (!r) {
          if (!1 === t.allowUnicode) return a.code("FORBIDDEN_UNICODE");
          e = e.normalize("NFC");
        }
        var s = e.split("@");
        if (2 !== s.length) return s.length > 2 ? a.code("MULTIPLE_AT_CHAR") : a.code("MISSING_AT_CHAR");
        var _s48 = _slicedToArray(s, 2),
          o = _s48[0],
          l = _s48[1];
        if (!o) return a.code("EMPTY_LOCAL");
        if (!t.ignoreLength) {
          if (e.length > 254) return a.code("ADDRESS_TOO_LONG");
          if (i.encoder.encode(o).length > 64) return a.code("LOCAL_TOO_LONG");
        }
        return i.local(o, r) || n.analyze(l, t);
      }, i.local = function (e, t) {
        var r = e.split(".");
        var _iterator105 = _createForOfIteratorHelper(r),
          _step105;
        try {
          for (_iterator105.s(); !(_step105 = _iterator105.n()).done;) {
            var _e85 = _step105.value;
            if (!_e85.length) return a.code("EMPTY_LOCAL_SEGMENT");
            if (t) {
              if (!i.atextRx.test(_e85)) return a.code("INVALID_LOCAL_CHARS");
            } else {
              var _iterator106 = _createForOfIteratorHelper(_e85),
                _step106;
              try {
                for (_iterator106.s(); !(_step106 = _iterator106.n()).done;) {
                  var _t85 = _step106.value;
                  if (i.atextRx.test(_t85)) continue;
                  var _e86 = i.binary(_t85);
                  if (!i.atomRx.test(_e86)) return a.code("INVALID_LOCAL_CHARS");
                }
              } catch (err) {
                _iterator106.e(err);
              } finally {
                _iterator106.f();
              }
            }
          }
        } catch (err) {
          _iterator105.e(err);
        } finally {
          _iterator105.f();
        }
      }, i.binary = function (e) {
        return Array.from(i.encoder.encode(e)).map(function (e) {
          return String.fromCharCode(e);
        }).join("");
      }, i.atextRx = /^[\w!#\$%&'\*\+\-/=\?\^\x60\{\|\}~]+$/, i.atomRx = new RegExp(["(?:[\\xc2-\\xdf][\\x80-\\xbf])", "(?:\\xe0[\\xa0-\\xbf][\\x80-\\xbf])|(?:[\\xe1-\\xec][\\x80-\\xbf]{2})|(?:\\xed[\\x80-\\x9f][\\x80-\\xbf])|(?:[\\xee-\\xef][\\x80-\\xbf]{2})", "(?:\\xf0[\\x90-\\xbf][\\x80-\\xbf]{2})|(?:[\\xf1-\\xf3][\\x80-\\xbf]{3})|(?:\\xf4[\\x80-\\x8f][\\x80-\\xbf]{2})"].join("|"));
    },
    2178: function _(e, t) {
      "use strict";

      t.codes = {
        EMPTY_STRING: "Address must be a non-empty string",
        FORBIDDEN_UNICODE: "Address contains forbidden Unicode characters",
        MULTIPLE_AT_CHAR: "Address cannot contain more than one @ character",
        MISSING_AT_CHAR: "Address must contain one @ character",
        EMPTY_LOCAL: "Address local part cannot be empty",
        ADDRESS_TOO_LONG: "Address too long",
        LOCAL_TOO_LONG: "Address local part too long",
        EMPTY_LOCAL_SEGMENT: "Address local part contains empty dot-separated segment",
        INVALID_LOCAL_CHARS: "Address local part contains invalid character",
        DOMAIN_NON_EMPTY_STRING: "Domain must be a non-empty string",
        DOMAIN_TOO_LONG: "Domain too long",
        DOMAIN_INVALID_UNICODE_CHARS: "Domain contains forbidden Unicode characters",
        DOMAIN_INVALID_CHARS: "Domain contains invalid character",
        DOMAIN_INVALID_TLDS_CHARS: "Domain contains invalid tld character",
        DOMAIN_SEGMENTS_COUNT: "Domain lacks the minimum required number of segments",
        DOMAIN_SEGMENTS_COUNT_MAX: "Domain contains too many segments",
        DOMAIN_FORBIDDEN_TLDS: "Domain uses forbidden TLD",
        DOMAIN_EMPTY_SEGMENT: "Domain contains empty dot-separated segment",
        DOMAIN_LONG_SEGMENT: "Domain contains dot-separated segment that is too long"
      }, t.code = function (e) {
        return {
          code: e,
          error: t.codes[e]
        };
      };
    },
    9959: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(5752);
      t.regex = function () {
        var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
        s(void 0 === e.cidr || "string" == typeof e.cidr, "options.cidr must be a string");
        var t = e.cidr ? e.cidr.toLowerCase() : "optional";
        s(["required", "optional", "forbidden"].includes(t), "options.cidr must be one of required, optional, forbidden"), s(void 0 === e.version || "string" == typeof e.version || Array.isArray(e.version), "options.version must be a string or an array of string");
        var r = e.version || ["ipv4", "ipv6", "ipvfuture"];
        Array.isArray(r) || (r = [r]), s(r.length >= 1, "options.version must have at least 1 version specified");
        for (var _e87 = 0; _e87 < r.length; ++_e87) s("string" == typeof r[_e87], "options.version must only contain strings"), r[_e87] = r[_e87].toLowerCase(), s(["ipv4", "ipv6", "ipvfuture"].includes(r[_e87]), "options.version contains unknown version " + r[_e87] + " - must be one of ipv4, ipv6, ipvfuture");
        r = Array.from(new Set(r));
        var a = "(?:".concat(r.map(function (e) {
            if ("forbidden" === t) return n.ip[e];
            var r = "\\/".concat("ipv4" === e ? n.ip.v4Cidr : n.ip.v6Cidr);
            return "required" === t ? "".concat(n.ip[e]).concat(r) : "".concat(n.ip[e], "(?:").concat(r, ")?");
          }).join("|"), ")"),
          i = new RegExp("^".concat(a, "$"));
        return {
          cidr: t,
          versions: r,
          regex: i,
          raw: a
        };
      };
    },
    5752: function _(e, t, r) {
      "use strict";

      var s = r(375),
        n = r(6064),
        a = {
          generate: function generate() {
            var e = {},
              t = "\\dA-Fa-f",
              r = "[" + t + "]",
              s = "\\w-\\.~",
              n = "!\\$&'\\(\\)\\*\\+,;=",
              a = "%" + t,
              i = s + a + n + ":@",
              o = "[" + i + "]",
              l = "(?:0{0,2}\\d|0?[1-9]\\d|1\\d\\d|2[0-4]\\d|25[0-5])";
            e.ipv4address = "(?:" + l + "\\.){3}" + l;
            var c = r + "{1,4}",
              u = "(?:" + c + ":" + c + "|" + e.ipv4address + ")",
              f = "(?:" + c + ":){6}" + u,
              m = "::(?:" + c + ":){5}" + u,
              h = "(?:" + c + ")?::(?:" + c + ":){4}" + u,
              d = "(?:(?:" + c + ":){0,1}" + c + ")?::(?:" + c + ":){3}" + u,
              p = "(?:(?:" + c + ":){0,2}" + c + ")?::(?:" + c + ":){2}" + u,
              g = "(?:(?:" + c + ":){0,3}" + c + ")?::" + c + ":" + u,
              y = "(?:(?:" + c + ":){0,4}" + c + ")?::" + u,
              b = "(?:(?:" + c + ":){0,5}" + c + ")?::" + c,
              v = "(?:(?:" + c + ":){0,6}" + c + ")?::";
            e.ipv4Cidr = "(?:\\d|[1-2]\\d|3[0-2])", e.ipv6Cidr = "(?:0{0,2}\\d|0?[1-9]\\d|1[01]\\d|12[0-8])", e.ipv6address = "(?:" + f + "|" + m + "|" + h + "|" + d + "|" + p + "|" + g + "|" + y + "|" + b + "|" + v + ")", e.ipvFuture = "v" + r + "+\\.[" + s + n + ":]+", e.scheme = "[a-zA-Z][a-zA-Z\\d+-\\.]*", e.schemeRegex = new RegExp(e.scheme);
            var _ = "[" + s + a + n + ":]*",
              w = "[" + s + a + n + "]{1,255}",
              $ = "(?:\\[(?:" + e.ipv6address + "|" + e.ipvFuture + ")\\]|" + e.ipv4address + "|" + w + ")",
              x = "(?:" + _ + "@)?" + $ + "(?::\\d*)?",
              j = "(?:" + _ + "@)?(" + $ + ")(?::\\d*)?",
              k = o + "*",
              R = o + "+",
              S = "(?:\\/" + k + ")*",
              A = "\\/(?:" + R + S + ")?",
              O = R + S,
              E = "[" + s + a + n + "@]+" + S,
              D = "(?:\\/\\/\\/" + k + S + ")";
            return e.hierPart = "(?:(?:\\/\\/" + x + S + ")|" + A + "|" + O + "|" + D + ")", e.hierPartCapture = "(?:(?:\\/\\/" + j + S + ")|" + A + "|" + O + ")", e.relativeRef = "(?:(?:\\/\\/" + x + S + ")|" + A + "|" + E + "|)", e.relativeRefCapture = "(?:(?:\\/\\/" + j + S + ")|" + A + "|" + E + "|)", e.query = "[" + i + "\\/\\?]*(?=#|$)", e.queryWithSquareBrackets = "[" + i + "\\[\\]\\/\\?]*(?=#|$)", e.fragment = "[" + i + "\\/\\?]*", e;
          }
        };
      a.rfc3986 = a.generate(), t.ip = {
        v4Cidr: a.rfc3986.ipv4Cidr,
        v6Cidr: a.rfc3986.ipv6Cidr,
        ipv4: a.rfc3986.ipv4address,
        ipv6: a.rfc3986.ipv6address,
        ipvfuture: a.rfc3986.ipvFuture
      }, a.createRegex = function (e) {
        var t = a.rfc3986,
          r = "(?:\\?" + (e.allowQuerySquareBrackets ? t.queryWithSquareBrackets : t.query) + ")?(?:#" + t.fragment + ")?",
          i = e.domain ? t.relativeRefCapture : t.relativeRef;
        if (e.relativeOnly) return a.wrap(i + r);
        var o = "";
        if (e.scheme) {
          s(e.scheme instanceof RegExp || "string" == typeof e.scheme || Array.isArray(e.scheme), "scheme must be a RegExp, String, or Array");
          var _r70 = [].concat(e.scheme);
          s(_r70.length >= 1, "scheme must have at least 1 scheme specified");
          var _a26 = [];
          for (var _e88 = 0; _e88 < _r70.length; ++_e88) {
            var _i45 = _r70[_e88];
            s(_i45 instanceof RegExp || "string" == typeof _i45, "scheme at position " + _e88 + " must be a RegExp or String"), _i45 instanceof RegExp ? _a26.push(_i45.source.toString()) : (s(t.schemeRegex.test(_i45), "scheme at position " + _e88 + " must be a valid scheme"), _a26.push(n(_i45)));
          }
          o = _a26.join("|");
        }
        var l = "(?:" + (o ? "(?:" + o + ")" : t.scheme) + ":" + (e.domain ? t.hierPartCapture : t.hierPart) + ")",
          c = e.allowRelative ? "(?:" + l + "|" + i + ")" : l;
        return a.wrap(c + r, o);
      }, a.wrap = function (e, t) {
        return {
          raw: e = "(?=.)(?!https?:/(?:$|[^/]))(?!https?:///)(?!https?:[^/])".concat(e),
          regex: new RegExp("^".concat(e, "$")),
          scheme: t
        };
      }, a.uriRegex = a.createRegex({}), t.regex = function () {
        var e = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
        return e.scheme || e.allowRelative || e.relativeOnly || e.allowQuerySquareBrackets || e.domain ? a.createRegex(e) : a.uriRegex;
      };
    },
    1447: function _(e, t) {
      "use strict";

      var r = {
        operators: ["!", "^", "*", "/", "%", "+", "-", "<", "<=", ">", ">=", "==", "!=", "&&", "||", "??"],
        operatorCharacters: ["!", "^", "*", "/", "%", "+", "-", "<", "=", ">", "&", "|", "?"],
        operatorsOrder: [["^"], ["*", "/", "%"], ["+", "-"], ["<", "<=", ">", ">="], ["==", "!="], ["&&"], ["||", "??"]],
        operatorsPrefix: ["!", "n"],
        literals: {
          '"': '"',
          "\x60": "\x60",
          "'": "'",
          "[": "]"
        },
        numberRx: /^(?:[0-9]*(\.[0-9]*)?){1}$/,
        tokenRx: /^[\w\$\#\.\@\:\{\}]+$/,
        symbol: Symbol("formula"),
        settings: Symbol("settings")
      };
      t.Parser = /*#__PURE__*/function () {
        function _class17(e) {
          var t = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
          _classCallCheck(this, _class17);
          if (!t[r.settings] && t.constants) for (var _e89 in t.constants) {
            var _r71 = t.constants[_e89];
            if (null !== _r71 && !["boolean", "number", "string"].includes(_typeof(_r71))) throw new Error("Formula constant ".concat(_e89, " contains invalid ").concat(_typeof(_r71), " value type"));
          }
          this.settings = t[r.settings] ? t : Object.assign(_defineProperty(_defineProperty(_defineProperty({}, r.settings, !0), "constants", {}), "functions", {}), t), this.single = null, this._parts = null, this._parse(e);
        }
        return _createClass(_class17, [{
          key: "_parse",
          value: function _parse(e) {
            var _this0 = this;
            var s = [],
              n = "",
              a = 0,
              i = !1;
            var o = function o(e) {
              if (a) throw new Error("Formula missing closing parenthesis");
              var o = s.length ? s[s.length - 1] : null;
              if (i || n || e) {
                if (o && "reference" === o.type && ")" === e) return o.type = "function", o.value = _this0._subFormula(n, o.value), void (n = "");
                if (")" === e) {
                  var _e90 = new t.Parser(n, _this0.settings);
                  s.push({
                    type: "segment",
                    value: _e90
                  });
                } else if (i) {
                  if ("]" === i) return s.push({
                    type: "reference",
                    value: n
                  }), void (n = "");
                  s.push({
                    type: "literal",
                    value: n
                  });
                } else if (r.operatorCharacters.includes(n)) o && "operator" === o.type && r.operators.includes(o.value + n) ? o.value += n : s.push({
                  type: "operator",
                  value: n
                });else if (n.match(r.numberRx)) s.push({
                  type: "constant",
                  value: parseFloat(n)
                });else if (void 0 !== _this0.settings.constants[n]) s.push({
                  type: "constant",
                  value: _this0.settings.constants[n]
                });else {
                  if (!n.match(r.tokenRx)) throw new Error("Formula contains invalid token: ".concat(n));
                  s.push({
                    type: "reference",
                    value: n
                  });
                }
                n = "";
              }
            };
            var _iterator107 = _createForOfIteratorHelper(e),
              _step107;
            try {
              for (_iterator107.s(); !(_step107 = _iterator107.n()).done;) {
                var _t86 = _step107.value;
                i ? _t86 === i ? (o(), i = !1) : n += _t86 : a ? "(" === _t86 ? (n += _t86, ++a) : ")" === _t86 ? (--a, a ? n += _t86 : o(_t86)) : n += _t86 : _t86 in r.literals ? i = r.literals[_t86] : "(" === _t86 ? (o(), ++a) : r.operatorCharacters.includes(_t86) ? (o(), n = _t86, o()) : " " !== _t86 ? n += _t86 : o();
              }
            } catch (err) {
              _iterator107.e(err);
            } finally {
              _iterator107.f();
            }
            o(), s = s.map(function (e, t) {
              return "operator" !== e.type || "-" !== e.value || t && "operator" !== s[t - 1].type ? e : {
                type: "operator",
                value: "n"
              };
            });
            var l = !1;
            var _iterator108 = _createForOfIteratorHelper(s),
              _step108;
            try {
              for (_iterator108.s(); !(_step108 = _iterator108.n()).done;) {
                var _e91 = _step108.value;
                if ("operator" === _e91.type) {
                  if (r.operatorsPrefix.includes(_e91.value)) continue;
                  if (!l) throw new Error("Formula contains an operator in invalid position");
                  if (!r.operators.includes(_e91.value)) throw new Error("Formula contains an unknown operator ".concat(_e91.value));
                } else if (l) throw new Error("Formula missing expected operator");
                l = !l;
              }
            } catch (err) {
              _iterator108.e(err);
            } finally {
              _iterator108.f();
            }
            if (!l) throw new Error("Formula contains invalid trailing operator");
            1 === s.length && ["reference", "literal", "constant"].includes(s[0].type) && (this.single = {
              type: "reference" === s[0].type ? "reference" : "value",
              value: s[0].value
            }), this._parts = s.map(function (e) {
              if ("operator" === e.type) return r.operatorsPrefix.includes(e.value) ? e : e.value;
              if ("reference" !== e.type) return e.value;
              if (_this0.settings.tokenRx && !_this0.settings.tokenRx.test(e.value)) throw new Error("Formula contains invalid reference ".concat(e.value));
              return _this0.settings.reference ? _this0.settings.reference(e.value) : r.reference(e.value);
            });
          }
        }, {
          key: "_subFormula",
          value: function _subFormula(e, s) {
            var _this1 = this;
            var n = this.settings.functions[s];
            if ("function" != typeof n) throw new Error("Formula contains unknown function ".concat(s));
            var a = [];
            if (e) {
              var _t87 = "",
                _n44 = 0,
                i = !1;
              var o = function o() {
                if (!_t87) throw new Error("Formula contains function ".concat(s, " with invalid arguments ").concat(e));
                a.push(_t87), _t87 = "";
              };
              for (var _s49 = 0; _s49 < e.length; ++_s49) {
                var _a27 = e[_s49];
                i ? (_t87 += _a27, _a27 === i && (i = !1)) : _a27 in r.literals && !_n44 ? (_t87 += _a27, i = r.literals[_a27]) : "," !== _a27 || _n44 ? (_t87 += _a27, "(" === _a27 ? ++_n44 : ")" === _a27 && --_n44) : o();
              }
              o();
            }
            return a = a.map(function (e) {
              return new t.Parser(e, _this1.settings);
            }), function (e) {
              var t = [];
              var _iterator109 = _createForOfIteratorHelper(a),
                _step109;
              try {
                for (_iterator109.s(); !(_step109 = _iterator109.n()).done;) {
                  var _r72 = _step109.value;
                  t.push(_r72.evaluate(e));
                }
              } catch (err) {
                _iterator109.e(err);
              } finally {
                _iterator109.f();
              }
              return n.call.apply(n, [e].concat(t));
            };
          }
        }, {
          key: "evaluate",
          value: function evaluate(e) {
            var t = this._parts.slice();
            for (var _s50 = t.length - 2; _s50 >= 0; --_s50) {
              var _n45 = t[_s50];
              if (_n45 && "operator" === _n45.type) {
                var a = t[_s50 + 1];
                t.splice(_s50 + 1, 1);
                var i = r.evaluate(a, e);
                t[_s50] = r.single(_n45.value, i);
              }
            }
            return r.operatorsOrder.forEach(function (s) {
              for (var _n46 = 1; _n46 < t.length - 1;) if (s.includes(t[_n46])) {
                var _s51 = t[_n46],
                  _a28 = r.evaluate(t[_n46 - 1], e),
                  _i46 = r.evaluate(t[_n46 + 1], e);
                t.splice(_n46, 2);
                var o = r.calculate(_s51, _a28, _i46);
                t[_n46 - 1] = 0 === o ? 0 : o;
              } else _n46 += 2;
            }), r.evaluate(t[0], e);
          }
        }]);
      }(), t.Parser.prototype[r.symbol] = !0, r.reference = function (e) {
        return function (t) {
          return t && void 0 !== t[e] ? t[e] : null;
        };
      }, r.evaluate = function (e, t) {
        return null === e ? null : "function" == typeof e ? e(t) : e[r.symbol] ? e.evaluate(t) : e;
      }, r.single = function (e, t) {
        if ("!" === e) return !t;
        var r = -t;
        return 0 === r ? 0 : r;
      }, r.calculate = function (e, t, s) {
        if ("??" === e) return r.exists(t) ? t : s;
        if ("string" == typeof t || "string" == typeof s) {
          if ("+" === e) return (t = r.exists(t) ? t : "") + (r.exists(s) ? s : "");
        } else switch (e) {
          case "^":
            return Math.pow(t, s);
          case "*":
            return t * s;
          case "/":
            return t / s;
          case "%":
            return t % s;
          case "+":
            return t + s;
          case "-":
            return t - s;
        }
        switch (e) {
          case "<":
            return t < s;
          case "<=":
            return t <= s;
          case ">":
            return t > s;
          case ">=":
            return t >= s;
          case "==":
            return t === s;
          case "!=":
            return t !== s;
          case "&&":
            return t && s;
          case "||":
            return t || s;
        }
        return null;
      }, r.exists = function (e) {
        return null != e;
      };
    },
    9926: function _() {},
    5688: function _() {},
    9708: function _() {},
    1152: function _() {},
    443: function _() {},
    9848: function _() {},
    5934: function _(e) {
      "use strict";

      e.exports = JSON.parse('{"version":"17.13.3"}');
    }
  }, t = {}, function r(s) {
    var n = t[s];
    if (void 0 !== n) return n.exports;
    var a = t[s] = {
      exports: {}
    };
    return e[s](a, a.exports, r), a.exports;
  }(5107);
  var e, t;
});
var Joi = module.exports;
var pass = 0, fail = 0, total = 0;
function test(name, fn) {
  total++;
  try {
    var ok = fn();
    if (ok) { pass++; console.log("PASS " + name); }
    else { fail++; console.log("FAIL " + name); }
  } catch(e) {
    fail++;
    console.log("FAIL " + name + " (" + e.message + ")");
  }
}

// === String validation ===
test("string valid", function() { return !Joi.string().validate("hello").error; });
test("string min pass", function() { return !Joi.string().min(3).validate("abc").error; });
test("string min fail", function() { return !!Joi.string().min(3).validate("ab").error; });
test("string max pass", function() { return !Joi.string().max(5).validate("hello").error; });
test("string max fail", function() { return !!Joi.string().max(5).validate("toolong").error; });
test("string length pass", function() { return !Joi.string().length(5).validate("abcde").error; });
test("string length fail", function() { return !!Joi.string().length(5).validate("abc").error; });
test("string email pass", function() { return !Joi.string().email({ tlds: false }).validate("test@example.com").error; });
test("string email fail", function() { return !!Joi.string().email({ tlds: false }).validate("notanemail").error; });
test("string uri pass", function() { return !Joi.string().uri().validate("https://example.com").error; });
test("string pattern pass", function() { return !Joi.string().pattern(/^[a-z]+$/).validate("abc").error; });
test("string pattern fail", function() { return !!Joi.string().pattern(/^[a-z]+$/).validate("ABC").error; });
test("string regex alias", function() { return !Joi.string().regex(/^[0-9]+$/).validate("123").error; });
test("string alphanum pass", function() { return !Joi.string().alphanum().validate("abc123").error; });
test("string alphanum fail", function() { return !!Joi.string().alphanum().validate("abc-123").error; });
test("string required empty", function() { return !!Joi.string().required().validate("").error; });
test("string allow empty", function() { return !Joi.string().allow("").validate("").error; });
test("string trim", function() { return Joi.string().trim().validate("  hello  ").value === "hello"; });
test("string lowercase", function() { return Joi.string().lowercase().validate("HELLO").value === "hello"; });
test("string uppercase", function() { return Joi.string().uppercase().validate("hello").value === "HELLO"; });

// === Number validation ===
test("number valid", function() { return !Joi.number().validate(42).error; });
test("number min pass", function() { return !Joi.number().min(10).validate(15).error; });
test("number min fail", function() { return !!Joi.number().min(10).validate(5).error; });
test("number max pass", function() { return !Joi.number().max(100).validate(50).error; });
test("number max fail", function() { return !!Joi.number().max(100).validate(200).error; });
test("number integer pass", function() { return !Joi.number().integer().validate(42).error; });
test("number integer fail", function() { return !!Joi.number().integer().validate(3.14).error; });
test("number positive pass", function() { return !Joi.number().positive().validate(5).error; });
test("number positive fail", function() { return !!Joi.number().positive().validate(-5).error; });
test("number negative pass", function() { return !Joi.number().negative().validate(-5).error; });
test("number negative fail", function() { return !!Joi.number().negative().validate(5).error; });
test("number greater", function() { return !Joi.number().greater(5).validate(10).error; });
test("number less", function() { return !Joi.number().less(10).validate(5).error; });
test("number port pass", function() { return !Joi.number().port().validate(8080).error; });
test("number port fail", function() { return !!Joi.number().port().validate(70000).error; });

// === Boolean validation ===
test("boolean true", function() { return !Joi.boolean().validate(true).error; });
test("boolean false", function() { return !Joi.boolean().validate(false).error; });
test("boolean truthy", function() { return Joi.boolean().truthy("yes").validate("yes").value === true; });
test("boolean falsy", function() { return Joi.boolean().falsy("no").validate("no").value === false; });

// === Array validation ===
test("array valid", function() { return !Joi.array().validate([1,2,3]).error; });
test("array items string", function() { return !Joi.array().items(Joi.string()).validate(["a","b"]).error; });
test("array items fail", function() { return !!Joi.array().items(Joi.string()).validate(["a",1]).error; });
test("array min fail", function() { return !!Joi.array().min(3).validate([1]).error; });
test("array max fail", function() { return !!Joi.array().max(2).validate([1,2,3]).error; });
test("array length pass", function() { return !Joi.array().length(3).validate([1,2,3]).error; });
test("array length fail", function() { return !!Joi.array().length(3).validate([1,2]).error; });
test("array unique pass", function() { return !Joi.array().unique().validate([1,2,3]).error; });
test("array unique fail", function() { return !!Joi.array().unique().validate([1,2,1]).error; });
test("array ordered pass", function() { return !Joi.array().ordered(Joi.string(), Joi.number()).validate(["a", 1]).error; });
test("array ordered fail", function() { return !!Joi.array().ordered(Joi.string(), Joi.number()).validate([1, "a"]).error; });
test("array sparse", function() { return !Joi.array().sparse().validate([1, undefined, 3]).error; });

// === Object validation ===
test("object valid", function() {
  return !Joi.object({ name: Joi.string().required(), age: Joi.number() }).validate({name:"Alice",age:30}).error;
});
test("object missing required", function() {
  return !!Joi.object({ name: Joi.string().required() }).validate({}).error;
});
test("object extra key", function() {
  return !!Joi.object({ name: Joi.string() }).validate({name:"a",foo:1}).error;
});
test("object nested", function() {
  return !Joi.object({ a: Joi.object({ b: Joi.string() }) }).validate({a:{b:"x"}}).error;
});
test("object unknown", function() {
  return !Joi.object({name: Joi.string()}).unknown(true).validate({name:"a",extra:1}).error;
});
test("object or", function() {
  return !Joi.object({a: Joi.string(), b: Joi.string()}).or("a","b").validate({a:"x"}).error;
});
test("object and fail", function() {
  return !!Joi.object({a: Joi.string(), b: Joi.string()}).and("a","b").validate({a:"x"}).error;
});
test("object rename", function() {
  return Joi.object({b: Joi.string()}).rename("a","b").validate({a:"x"}).value.b === "x";
});

// === Alternatives ===
test("alt string", function() { return !Joi.alternatives().try(Joi.string(), Joi.number()).validate("hello").error; });
test("alt number", function() { return !Joi.alternatives().try(Joi.string(), Joi.number()).validate(42).error; });
test("alt fail", function() { return !!Joi.alternatives().try(Joi.string(), Joi.number()).validate(true).error; });

// === Date ===
test("date iso", function() { return !Joi.date().validate("2024-01-15").error; });
test("date timestamp", function() { return !Joi.date().timestamp().validate(Date.now()).error; });

// === Any ===
test("any valid", function() { return !Joi.any().validate("anything").error; });
test("any required fail", function() { return !!Joi.any().required().validate(undefined).error; });
test("any forbidden", function() { return !!Joi.any().forbidden().validate("something").error; });
test("any default", function() { return Joi.any().default("fallback").validate(undefined).value === "fallback"; });
test("any valid values", function() { return !Joi.any().valid("a","b","c").validate("b").error; });
test("any valid values fail", function() { return !!Joi.any().valid("a","b","c").validate("d").error; });
test("any invalid values", function() { return !!Joi.any().invalid("x","y").validate("x").error; });
test("any strip", function() { return Joi.any().strip().validate("data").value === undefined; });
test("any optional", function() { return !Joi.any().optional().validate(undefined).error; });

// === Symbol ===
test("symbol valid", function() { return !Joi.symbol().validate(Symbol("test")).error; });

// === Conditional (when) ===
test("when then", function() {
  return !Joi.object({
    type: Joi.string().required(),
    value: Joi.when("type", { is: "number", then: Joi.number(), otherwise: Joi.string() })
  }).validate({type:"number", value: 42}).error;
});
test("when otherwise", function() {
  return !Joi.object({
    type: Joi.string().required(),
    value: Joi.when("type", { is: "number", then: Joi.number(), otherwise: Joi.string() })
  }).validate({type:"text", value: "hello"}).error;
});

// === Error details ===
test("error details message", function() {
  var r = Joi.string().min(3).validate("ab");
  return r.error.details[0].message.indexOf("3") >= 0;
});
test("error details type", function() {
  var r = Joi.string().min(3).validate("ab");
  return r.error.details[0].type === "string.min";
});
test("error details path", function() {
  var r = Joi.object({a: Joi.number()}).validate({a: "x"});
  return r.error.details[0].path[0] === "a";
});

// === Label ===
test("label in error", function() {
  var r = Joi.string().label("Username").required().validate(undefined);
  return r.error.details[0].message.indexOf("Username") >= 0;
});

// === Concat ===
test("concat valid", function() {
  return !Joi.string().min(1).concat(Joi.string().max(10)).validate("hello").error;
});
test("concat min fail", function() {
  return !!Joi.string().min(1).concat(Joi.string().max(10)).validate("").error;
});

// === Ref ===
test("ref valid", function() {
  return !Joi.object({
    min: Joi.number(), max: Joi.number().greater(Joi.ref("min"))
  }).validate({min:1, max:5}).error;
});
test("ref fail", function() {
  return !!Joi.object({
    min: Joi.number(), max: Joi.number().greater(Joi.ref("min"))
  }).validate({min:10, max:5}).error;
});

// === Multiple errors ===
test("abort early false", function() {
  var r = Joi.object({
    a: Joi.string().required()
  }).validate({});
  return r.error.details.length === 1 && r.error.details[0].type === "any.required";
});

// === Joi.attempt ===
test("attempt pass", function() { return Joi.attempt("hello", Joi.string().min(1)) === "hello"; });
test("attempt fail", function() {
  try { Joi.attempt("", Joi.string().required()); return false; }
  catch(e) { return true; }
});

// === Joi.assert ===
test("assert pass", function() {
  Joi.assert({name:"A"}, Joi.object({name: Joi.string().required()}));
  return true;
});
test("assert fail", function() {
  try { Joi.assert({}, Joi.object({name: Joi.string().required()})); return false; }
  catch(e) { return true; }
});

// === Joi.isSchema ===
test("isSchema true", function() { return Joi.isSchema(Joi.string()); });
test("isSchema false", function() { return !Joi.isSchema("not schema"); });

// === Custom email with tlds ===
test("email custom tlds", function() {
  return !Joi.string().email({ tlds: { allow: new Set(["com","net","org"]) } }).validate("a@b.com").error;
});

console.log("DONE: " + pass + "/" + total + " passed");
