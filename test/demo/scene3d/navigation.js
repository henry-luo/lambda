// static HTML pages share the same breadcrumb markup as the Lambda pages.
for (const navigation of document.querySelectorAll('.scene3d-breadcrumbs[data-current]')) {
    const separator = document.createElement('span');
    separator.className = 'scene3d-separator';
    separator.setAttribute('aria-hidden', 'true');
    separator.textContent = '/';
    const current = document.createElement('span');
    current.setAttribute('aria-current', 'page');
    current.textContent = navigation.getAttribute('data-current');
    navigation.appendChild(separator);
    navigation.appendChild(current);
}
