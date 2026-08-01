// @document dom2_library_probe.html
var parsed = new DOMParser().parseFromString(
  '<body><template class="probe"><span id="fragment-result">swapped</span></template></body>',
  'text/html');
var fragment = parsed.querySelector('template').content;
console.log(fragment.childNodes.length);
console.log(fragment.firstChild.tagName);
console.log(fragment.firstChild.textContent);
var target = document.getElementById('htmx-target');
while (fragment.childNodes.length > 0) {
  target.appendChild(fragment.firstChild);
}
console.log(target.textContent.trim());
