// element scroll does not bubble; relay retained column scrolls to the Lambda session.
const finderOwner = document.getElementById('finder');
finderOwner.addEventListener('scroll', event => {
    if (event.target.classList.contains('column-list')) {
        finderOwner.dispatchEvent(new Event('finder_viewport'));
    }
}, true);
// initialize after script loading and the first layout, before measuring thumbnail slots.
requestAnimationFrame(() => finderOwner.dispatchEvent(new Event('finder_activate')));

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
    if (row) return row.getAttribute('data-dir') === 'true' ? {node: row, path: row.getAttribute('data-path')} : null;
    const location = target.closest('[data-location]');
    if (location) return {node: location, path: location.getAttribute('data-location')};
    const column = target.closest('.file-column');
    if (column) return {node: column, path: column.getAttribute('data-folder')};
    const files = target.closest('#files');
    return files ? {node: files, path: finderOwner.getAttribute('data-path')} : null;
}
finderOwner.addEventListener('dragstart', event => {
    const row = event.target.closest('.file');
    if (!row || finderOwner.getAttribute('data-operation') || finderOwner.getAttribute('data-dialog') === 'true' || finderOwner.getAttribute('data-look') === 'true') {
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
