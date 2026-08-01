const mixin = {
  batchUpdate(run) {
    run();
  },

  cellsAdded(cells, parent, absolute = false) {
    this.batchUpdate(() => {
      const parentState = absolute ? { origin: { x: 1 } } : null;
      const origin = parentState ? parentState.origin : null;
      const zero = { x: 0 };

      cells.forEach((cell) => {
        const previous = cell.parent;
        if (origin && cell !== parent && parent !== previous) {
          console.log("absolute");
        } else {
          console.log(`relative:${zero.x}`);
        }
      });
    });
  },
};

mixin.cellsAdded([{ parent: null }], {}, false);
