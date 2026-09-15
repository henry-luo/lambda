var foreign = document.implementation.createHTMLDocument("");
foreign.body.innerHTML = "<a id='sizzle1789480863110'></a><select id='sizzle1789480863110-\r\\' msallowcapture=''><option selected=''></option></select>";
console.log(foreign.body.querySelectorAll("a").length);
console.log(foreign.body.querySelectorAll("select").length);
console.log(foreign.body.querySelectorAll("option").length);
