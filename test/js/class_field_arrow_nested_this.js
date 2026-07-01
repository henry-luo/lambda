"use strict";

(() => {
  class NestedDecl {
    tag = "decl";
    handler = () => this.tag;
    static marker = "static-decl";
    static staticHandler = () => this.marker;
  }
  var a = new NestedDecl();
  var b = new NestedDecl();
  a.tag = "a";
  b.tag = "b";
  console.log("decl: " + a.handler() + "," + b.handler());
  console.log("static decl: " + NestedDecl.staticHandler());
})();

(function(){
  var NestedExpr = class {
    tag = "expr";
    handler = () => this.tag;
  };
  console.log("expr: " + new NestedExpr().handler());
})();
