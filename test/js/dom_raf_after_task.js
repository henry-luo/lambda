const deadline = setTimeout(() => { throw new Error('rendering opportunity was starved by a later timer'); }, 5000);
setTimeout(() => {
  requestAnimationFrame(() => {
    clearTimeout(deadline);
    console.log('frame after native task: pass');
  });
}, 10);
