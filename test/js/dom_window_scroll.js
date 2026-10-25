function check(name, condition) {
  if (!condition) throw new Error(name);
  console.log(name + ': pass');
}
check('window scrolling methods', typeof scrollTo === 'function' && typeof window.scroll === 'function' && typeof scrollBy === 'function');
window.scrollTo(20, 30);
check('window and document share offsets', scrollX === 20 && scrollY === 30 && document.scrollingElement.scrollLeft === 20 && document.scrollingElement.scrollTop === 30);
window.scrollBy({left: 5, top: 7});
check('relative scrolling and offset aliases', pageXOffset === 25 && pageYOffset === 37 && scrollX === pageXOffset && scrollY === pageYOffset);
window.scrollTo({top: 40});
check('dictionary preserves omitted axis', scrollX === 25 && scrollY === 40);
document.scrollingElement.scrollLeft = 10;
check('window reflects element scroll writes', scrollX === 10 && pageXOffset === 10);
window.scrollTo(0, 0);
check('scroll to top', scrollX === 0 && scrollY === 0);
