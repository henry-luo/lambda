// @document dom_module_props.html
var forms = document.forms;
var formCount = forms.length;
var form = document.createElement("form");
document.body.appendChild(form);
console.log("formsLive:" + (forms.length === formCount + 1));
console.log("formsBrand:" + (forms instanceof HTMLCollection));

var controls = form.elements;
var input = document.createElement("input");
input.setAttribute("name", "field");
form.appendChild(input);
console.log("controlsLive:" + (controls.length === 1));
console.log("controlsNamed:" + (controls.namedItem("field") === input));
console.log("controlsBrand:" +
    (controls instanceof HTMLFormControlsCollection));
var input2 = document.createElement("input");
input2.setAttribute("name", "field");
form.appendChild(input2);
var radio = form["field"];
console.log("radioBrand:" + (radio instanceof RadioNodeList));
console.log("radioArray:" + Array.isArray(radio));

var byClass = document.getElementsByClassName("virtual-probe");
var first = document.createElement("div");
first.setAttribute("class", "virtual-probe");
document.body.appendChild(first);
console.log("lookupLive:" + (byClass.length === 1));
var staticMatches = document.querySelectorAll(".virtual-probe");
var second = document.createElement("div");
second.setAttribute("class", "virtual-probe");
document.body.appendChild(second);
console.log("lookupUpdates:" + (byClass.length === 2));
console.log("queryStatic:" + (staticMatches.length === 1));
console.log("queryBrand:" + (staticMatches instanceof NodeList));
console.log("queryArray:" + Array.isArray(staticMatches));

var attributes = first.attributes;
var attributeCount = attributes.length;
first.setAttribute("data-live", "yes");
console.log("attributesLive:" + (attributes.length === attributeCount + 1));
console.log("attributesNamed:" +
    (attributes.getNamedItem("data-live").value === "yes" &&
     attributes["data-live"].nodeValue === "yes"));
console.log("attributesBrand:" + (attributes instanceof NamedNodeMap));
console.log("attributesArray:" + Array.isArray(attributes));

var tokens = first.classList;
tokens.add("second-token");
console.log("tokensLive:" +
    (tokens.length === 2 && first.getAttribute("class") ===
     "virtual-probe second-token"));
console.log("tokensStable:" + (tokens === first.classList));
console.log("tokensBrand:" + (tokens instanceof DOMTokenList));
console.log("tokensArray:" + Array.isArray(tokens));
console.log("tokensIterable:" + ([...tokens].join(",") ===
    "virtual-probe,second-token"));
var rects = first.getClientRects();
console.log("rectsBrand:" + (rects instanceof DOMRectList));
console.log("rectsArray:" + Array.isArray(rects));
var sheets = document.styleSheets;
console.log("sheetsBrand:" + (sheets instanceof StyleSheetList));
console.log("sheetsArray:" + Array.isArray(sheets));
var rules = sheets[0].cssRules;
var ruleCount = rules.length;
sheets[0].insertRule(".virtual-css-rule { display: block; }", ruleCount);
console.log("rulesLive:" + (rules.length === ruleCount + 1));
console.log("rulesBrand:" + (rules instanceof CSSRuleList));
console.log("rulesArray:" + Array.isArray(rules));

var select = document.createElement("select");
select.setAttribute("multiple", "");
var options = select.options;
var optionA = document.createElement("option");
optionA.setAttribute("id", "option-a");
options.add(optionA);
var optionB = document.createElement("option");
select.appendChild(optionB);
console.log("optionsLive:" + (options.length === 2));
console.log("optionsNamed:" + (options.namedItem("option-a") === optionA));
console.log("optionsBrand:" + (options instanceof HTMLOptionsCollection));
console.log("optionsArray:" + Array.isArray(options));
var childKeys = [];
for (var childKey in select.children) {
    if (select.children.hasOwnProperty(childKey)) childKeys.push(childKey);
}
console.log("childrenForIn:" + (childKeys.join(",") === "0,1"));
optionA.selected = true;
var selected = select.selectedOptions;
console.log("selectedFirst:" + (selected.length === 1));
optionB.selected = true;
console.log("selectedLive:" + (selected.length === 2));
console.log("selectedBrand:" + (selected instanceof HTMLCollection));
