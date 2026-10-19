// element scroll does not bubble; relay retained column scrolls to the Lambda session.
const finderOwner = document.getElementById('finder');
// measured CSS cells define the scroll window; directory entries stay in the Lambda model.
function finderMeasurements() {
    const mode = finderOwner.getAttribute('data-mode');
    const ports = mode === 'columns' ? finderOwner.querySelectorAll('.column-list') : [document.getElementById('files')];
    return Array.from(ports).map(port => {
        const rows = port.querySelector('.listing-rows');
        const row = rows && rows.querySelector('.file');
        if (!row) return null;
        const style = getComputedStyle(rows);
        const box = row.getBoundingClientRect();
        const gap = parseFloat(style.rowGap) || 0;
        const columnGap = parseFloat(style.columnGap) || 0;
        const padding = parseFloat(style.paddingTop) || 0;
        const width = rows.getBoundingClientRect().width - (parseFloat(style.paddingLeft) || 0) - (parseFloat(style.paddingRight) || 0);
        const column = port.closest('.file-column');
        return {path: column ? column.getAttribute('data-folder') : finderOwner.getAttribute('data-path'),
            mode, top: port.scrollTop, height: port.clientHeight, row_height: box.height, gap, padding,
            columns: mode === 'icons' ? Math.max(1, Math.round((width + columnGap) / (box.width + columnGap))) : 1};
    }).filter(value => value !== null);
}
finderOwner.addEventListener('finder_measure', () => finderRelay('finder_viewport', {viewports: finderMeasurements()}));
// window notifications target the JS realm; relay them to the retained Lambda session.
window.addEventListener('resize', () => finderOwner.dispatchEvent(new Event('finder_resize')));
finderOwner.addEventListener('scroll', event => {
    if (event.target.classList.contains('column-list')) {
        finderRelay('finder_viewport', {viewports: finderMeasurements()});
    }
}, true);
// initialize after script loading and the first layout, before measuring thumbnail slots.
requestAnimationFrame(() => finderRelay('finder_activate', {viewports: finderMeasurements()}));

let finderDragging = false;
let finderDropTarget = null;
function finderRelay(type, detail) {
    finderOwner.dispatchEvent(new CustomEvent(type, {detail}));
}
function finderClearDropTarget() {
    if (finderDropTarget) finderDropTarget.classList.remove('drop-target');
    finderDropTarget = null;
}
function finderDestination(target) {
    const row = target.closest('.file');
    if (row) return row.getAttribute('data-readonly') !== 'true' && row.getAttribute('data-dir') === 'true' ? {node: row, path: row.getAttribute('data-path')} : null;
    const location = target.closest('[data-location]');
    if (location) return location.getAttribute('data-readonly') === 'true' ? null : {node: location, path: location.getAttribute('data-location')};
    const column = target.closest('.file-column');
    if (column) return column.getAttribute('data-readonly') === 'true' ? null : {node: column, path: column.getAttribute('data-folder')};
    const files = target.closest('#files');
    return files && finderOwner.getAttribute('data-readonly') !== 'true' ? {node: files, path: finderOwner.getAttribute('data-path')} : null;
}
finderOwner.addEventListener('dragstart', event => {
    const row = event.target.closest('.file');
    if (!row || row.getAttribute('data-readonly') === 'true' || finderOwner.getAttribute('data-operation') || finderOwner.getAttribute('data-dialog') === 'true' || finderOwner.getAttribute('data-look') === 'true') {
        event.preventDefault();
        return;
    }
    const path = row.getAttribute('data-path');
    finderRelay('finder_drag_start', {path, column: Number(row.getAttribute('data-column') || -1)});
    finderDragging = finderOwner.getAttribute('data-dragging') === 'true';
    event.dataTransfer.effectAllowed = 'move';
    event.dataTransfer.setData('text/plain', path);
});
finderOwner.addEventListener('dragover', event => {
    finderClearDropTarget();
    if (!finderDragging) return;
    const destination = finderDestination(event.target);
    if (!destination) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = 'move';
    finderDropTarget = destination.node;
    finderDropTarget.classList.add('drop-target');
});
finderOwner.addEventListener('dragleave', event => {
    if (finderDropTarget && !finderDropTarget.contains(event.relatedTarget)) finderClearDropTarget();
});
finderOwner.addEventListener('drop', event => {
    const destination = finderDestination(event.target);
    finderClearDropTarget();
    if (!finderDragging || !destination) return;
    event.preventDefault();
    finderRelay('finder_drop', {folder: destination.path});
    finderDragging = false;
});
finderOwner.addEventListener('dragend', () => {
    finderClearDropTarget();
    finderDragging = false;
    finderRelay('finder_drag_end', {});
});
finderOwner.addEventListener('keydown', event => {
    if (finderDragging && event.key === 'Escape') {
        finderClearDropTarget();
        finderDragging = false;
        finderRelay('finder_drag_end', {});
        event.preventDefault();
        event.stopPropagation();
    }
}, true);
