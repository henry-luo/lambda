class EventBase {
  emit(label) {
    return "emit:" + label;
  }
}

class EventChild extends EventBase {
  close() {
    return this.emit("close");
  }
}

console.log(new EventChild().close());

const EventExpressionChild = class InnerEventChild extends EventBase {
  close() {
    return this.emit("expression");
  }
};

console.log(new EventExpressionChild().close());

const MinifiedChild = class MinifiedInner extends EventBase {
  constructor() {
    super();
    this.opened = false;
  }
  close() {
    this.opened = false;
    return this.emit("minified");
  }
};

console.log(new MinifiedChild().close());

const Ct = class an extends EventBase {
  constructor() {
    super();
    this.opened = false;
  }
  close() {
    this.opened = false;
    return this.emit("alias");
  }
};

console.log(new Ct().close());

function usable(value) {
  return value === null || value === undefined;
}

class Dispatcher {
  constructor() {
    this.subscribers = {};
  }
  emit(name, data) {
    usable(this.subscribers) || !this.subscribers[name] || this.subscribers[name].reduce(
      (value, listener) => listener(value), data);
  }
}

const Toolbox = class an extends Dispatcher {
  constructor() {
    super();
    this.opened = false;
    this.popover = null;
  }
  close() {
    this.opened = false;
    this.emit("toolbox-closed");
  }
};

new Toolbox().close();
console.log("toolbox-close");

class GetterToolbar {
  constructor() {
    this.count = 0;
  }
  get blockActions() {
    return {
      hide: () => {
        this.count++;
      }
    };
  }
  close() {
    this.blockActions.hide();
    return this.count;
  }
}

console.log(new GetterToolbar().close());

class PopoverItem {
  constructor() {
    this.didReset = false;
  }
  reset() {
    this.didReset = true;
  }
}

class PopoverBase {
  constructor() {
    this.items = [new PopoverItem()];
  }
  get itemsDefault() {
    return this.items;
  }
}

class NestedPopover extends PopoverBase {
  hide() {
    this.itemsDefault.forEach((item) => item.reset());
    return this.items[0].didReset;
  }
}

console.log(new NestedPopover().hide());

class BasePopoverWithHide extends PopoverBase {
  hide() {
    this.itemsDefault
      .filter((item) => item instanceof PopoverItem)
      .forEach((item) => item.reset());
    return this.items[0].didReset;
  }
}

class ArrowPopover extends BasePopoverWithHide {
  constructor() {
    super();
    this.hide = () => super.hide();
  }
}

console.log(new ArrowPopover().hide());

const CommaArrowPopover = class MinifiedPopover extends BasePopoverWithHide {
  constructor() {
    super(), this.marker = 1, this.hide = () => {
      super.hide();
      return this.items[0].didReset;
    };
  }
};

console.log(new CommaArrowPopover().hide());

class ArrowInvoker {
  invoke(callback) {
    return callback();
  }
}

const NestedArrowPopover = class NestedPopover extends BasePopoverWithHide {
  constructor() {
    super();
    this.hide = () => super.hide();
  }
};

const nestedArrowPopover = new NestedArrowPopover();
console.log(new ArrowInvoker().invoke(nestedArrowPopover.hide));

class EventSource {
  constructor() {
    this.subscribers = {};
  }
  on(name, callback) {
    this.subscribers[name] = this.subscribers[name] || [];
    this.subscribers[name].push(callback);
  }
  emit(name, data) {
    this.subscribers[name] && this.subscribers[name].reduce(
      (value, callback) => callback(value), data);
  }
}

class SearchField extends EventSource {
  constructor(items) {
    super();
    this.items = items;
    this.query = "active";
  }
  clear() {
    this.query = "";
    this.emit("search", { query: this.query, items: this.items });
  }
}

class SearchablePopover extends EventSource {
  constructor() {
    super();
    this.items = [new PopoverItem()];
    this.flipper = {
      isActivated: true,
      deactivate() { this.isActivated = false; },
      activate(items) { this.items = items; }
    };
    this.onSearch = (event) => {
      this.items.forEach((item) => item.reset());
      if (this.flipper.isActivated) {
        this.flipper.deactivate();
        this.flipper.activate(event.items);
      }
    };
    this.search = new SearchField(this.items);
    this.search.on("search", this.onSearch);
  }
  hide() {
    this.search.clear();
    return this.flipper.items[0].didReset;
  }
}

console.log(new SearchablePopover().hide());

class FieldClose {
  constructor() {
    this.closed = false;
    this.close = () => {
      this.closed = true;
    };
  }
}

class FieldToolbar {
  constructor() {
    this.settings = new FieldClose();
  }
  close() {
    this.settings.close();
    return this.settings.closed;
  }
}

console.log(new FieldToolbar().close());

const sparseListeners = [() => "unexpected"];
delete sparseListeners[0];
console.log(sparseListeners.reduce((value, listener) => listener(value), "safe"));
