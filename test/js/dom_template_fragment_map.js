var id_map = new Map();
var template = document.createElement('template');

template.innerHTML = '<section id="member">member</section>';
template.content.querySelectorAll('[id]').forEach(function(element) {
    var count = id_map.has(element.id) ? id_map.get(element.id) : 0;
    id_map.set(element.id, count + 1);
});

console.log(id_map.get('member'));

window.ALL_CRATES = ['std'];
function addSidebarCrates() {
    if (!window.ALL_CRATES) return;
    const sidebarElems = document.getElementById('rustdoc-modnav');
    if (!sidebarElems) return;

    const h3 = document.createElement('h3');
    h3.innerHTML = 'Crates';
    const ul = document.createElement('ul');
    ul.className = 'block crate';
    for (const crate of window.ALL_CRATES) {
        const link = document.createElement('a');
        link.href = window.rootPath + crate + '/index.html';
        link.textContent = crate;
        const li = document.createElement('li');
        li.appendChild(link);
        ul.appendChild(li);
    }
    sidebarElems.appendChild(h3);
    sidebarElems.appendChild(ul);
}

addSidebarCrates();

console.log(document.getElementById('rustdoc-modnav').children[0].innerHTML);

function nonnull(value) {
    if (value === null) throw new Error('expected DOM value');
    return value;
}

window.searchState = {
    inputElement: () => {
        let element = document.getElementsByClassName('search-input')[0];
        if (!element) {
            const output = nonnull(nonnull(window.searchState.outputElement()).parentElement);
            const heading = document.createElement('div');
            heading.className = 'main-heading search-results-main-heading';
            heading.innerHTML = '<input class="search-input" type="search">';
            output.insertBefore(heading, window.searchState.outputElement());
            element = document.getElementsByClassName('search-input')[0];
        }
        return element instanceof HTMLInputElement ? element : null;
    },
    containerElement: () => {
        let element = document.getElementById('search');
        if (!element) {
            element = document.createElement('section');
            element.id = 'search';
            document.body.appendChild(element);
        }
        return element;
    },
    outputElement: () => {
        const container = window.searchState.containerElement();
        let element = container.querySelector('.search-out');
        if (!element) {
            element = document.createElement('div');
            element.className = 'search-out';
            container.appendChild(element);
        }
        return element;
    }
};

console.log(window.searchState.inputElement() instanceof HTMLInputElement);

(function() {
    let sidebarButton = document.getElementById('sidebar-button');
    const body = document.querySelector('.main-heading');
    if (!sidebarButton && body) {
        sidebarButton = document.createElement('div');
        sidebarButton.id = 'sidebar-button';
        const path = window.rootPath + window.currentCrate + '/all.html';
        sidebarButton.innerHTML = `<a href="${path}" title="show sidebar"></a>`;
        body.insertBefore(sidebarButton, body.firstChild);
    }
})();

console.log(document.getElementById('sidebar-button') instanceof HTMLElement);
