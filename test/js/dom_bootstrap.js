var fs = require('fs');
(0, eval)(fs.readFileSync('test/js/popper_src_min.js', 'utf8'));
(0, eval)(fs.readFileSync('test/js/bootstrap.min.js', 'utf8'));

function events(element, names) {
  var seen = [];
  names.forEach(function (name) {
    element.addEventListener(name, function () { seen.push(name); });
  });
  return seen;
}

function line(name, value) { console.log(name + ':' + value); }

var alertElement = document.getElementById('alert');
var alertEvents = events(alertElement, ['close.bs.alert', 'closed.bs.alert']);
bootstrap.Alert.getOrCreateInstance(alertElement).close();
line('alert', !document.getElementById('alert') + ':' + alertEvents.join(','));

var buttonElement = document.getElementById('button');
bootstrap.Button.getOrCreateInstance(buttonElement).toggle();
line('button', buttonElement.classList.contains('active') + ':' + buttonElement.getAttribute('aria-pressed'));

var collapseElement = document.getElementById('collapse');
var collapseEvents = events(collapseElement,
  ['show.bs.collapse', 'shown.bs.collapse', 'hide.bs.collapse', 'hidden.bs.collapse']);
var collapseWasShown = false;
collapseElement.addEventListener('shown.bs.collapse', function () {
  collapseWasShown = collapseElement.classList.contains('show');
  bootstrap.Collapse.getInstance(collapseElement).hide();
}, { once: true });
collapseElement.addEventListener('hidden.bs.collapse', function () {
  line('collapse', collapseWasShown + ':' + !collapseElement.classList.contains('show') + ':' + collapseEvents.join(','));
  run_remaining_plugins();
}, { once: true });
bootstrap.Collapse.getOrCreateInstance(collapseElement, { toggle: false }).show();

function run_remaining_plugins() {
  var dropdownElement = document.getElementById('dropdown-toggle');
  var dropdownEvents = events(dropdownElement,
    ['show.bs.dropdown', 'shown.bs.dropdown', 'hide.bs.dropdown', 'hidden.bs.dropdown']);
  var dropdown = bootstrap.Dropdown.getOrCreateInstance(dropdownElement, { display: 'static' });
  dropdown.show();
  var dropdownWasShown = document.querySelector('.dropdown-menu').classList.contains('show');
  dropdown.hide();
  line('dropdown', dropdownWasShown + ':' + dropdownElement.getAttribute('aria-expanded') + ':' + dropdownEvents.join(','));

  var modalElement = document.getElementById('modal');
  var modalEvents = events(modalElement,
    ['show.bs.modal', 'shown.bs.modal', 'hide.bs.modal', 'hidden.bs.modal']);
  var modal = bootstrap.Modal.getOrCreateInstance(modalElement, { backdrop: false, focus: false });
  modal.show();
  var modalWasShown = modalElement.classList.contains('show');
  modal.hide();
  line('modal', modalWasShown + ':' + !modalElement.classList.contains('show') + ':' + modalEvents.join(','));

  var offcanvasElement = document.getElementById('offcanvas');
  var offcanvasEvents = events(offcanvasElement,
    ['show.bs.offcanvas', 'shown.bs.offcanvas', 'hide.bs.offcanvas', 'hidden.bs.offcanvas']);
  var offcanvasWasShown = false;
  offcanvasElement.addEventListener('shown.bs.offcanvas', function () {
    offcanvasWasShown = offcanvasElement.classList.contains('show');
    bootstrap.Offcanvas.getInstance(offcanvasElement).hide();
  }, { once: true });
  offcanvasElement.addEventListener('hidden.bs.offcanvas', function () {
    line('offcanvas', offcanvasWasShown + ':' + !offcanvasElement.classList.contains('show') + ':' + offcanvasEvents.join(','));
    run_synchronous_tail();
  }, { once: true });
  bootstrap.Offcanvas.getOrCreateInstance(offcanvasElement, { backdrop: false, scroll: true }).show();
}

function run_synchronous_tail() {
  var tabElement = document.getElementById('tab2');
  var tabEvents = events(tabElement, ['show.bs.tab', 'shown.bs.tab']);
  bootstrap.Tab.getOrCreateInstance(tabElement).show();
  line('tab', tabElement.classList.contains('active') + ':' +
    document.getElementById('pane2').classList.contains('active') + ':' + tabEvents.join(','));

  var tooltipElement = document.getElementById('tooltip');
  var tooltipEvents = events(tooltipElement,
    ['show.bs.tooltip', 'shown.bs.tooltip', 'hide.bs.tooltip', 'hidden.bs.tooltip']);
  var tooltip = bootstrap.Tooltip.getOrCreateInstance(tooltipElement,
    { trigger: 'manual', animation: false });
  tooltip.show();
  var tooltipWasShown = !!document.querySelector('.tooltip.show');
  tooltip.hide();
  line('tooltip', tooltipWasShown + ':' + tooltipEvents.join(','));

  var popoverElement = document.getElementById('popover');
  var popoverEvents = events(popoverElement,
    ['show.bs.popover', 'shown.bs.popover', 'hide.bs.popover', 'hidden.bs.popover']);
  var popover = bootstrap.Popover.getOrCreateInstance(popoverElement,
    { trigger: 'manual', animation: false });
  popover.show();
  var popoverWasShown = !!document.querySelector('.popover.show');
  popover.hide();
  line('popover', popoverWasShown + ':' + popoverEvents.join(','));

  var toastElement = document.getElementById('toast');
  var toastEvents = events(toastElement,
    ['show.bs.toast', 'shown.bs.toast', 'hide.bs.toast', 'hidden.bs.toast']);
  var toast = bootstrap.Toast.getOrCreateInstance(toastElement,
    { animation: false, autohide: false });
  toast.show();
  var toastWasShown = toastElement.classList.contains('show');
  toast.hide();
  line('toast', toastWasShown + ':' + !toastElement.classList.contains('show') + ':' + toastEvents.join(','));

  var spy = bootstrap.ScrollSpy.getOrCreateInstance(document.getElementById('spy'),
    { target: '#spy-nav', smoothScroll: false });
  line('scrollspy', spy instanceof bootstrap.ScrollSpy);

  var carouselElement = document.getElementById('carousel');
  var carouselEvents = events(carouselElement, ['slide.bs.carousel', 'slid.bs.carousel']);
  bootstrap.Carousel.getOrCreateInstance(carouselElement, { interval: false, touch: true }).next();
  line('carousel', document.getElementById('slide2').classList.contains('active') + ':' + carouselEvents.join(','));
  line('plugins', Object.keys(bootstrap).length);
}
