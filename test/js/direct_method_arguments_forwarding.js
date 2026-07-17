class ArgumentsSink {
  receive(first, second) {
    console.log('forward:' + arguments.length + ':' + first + ':' + typeof second);
  }
}

class ArgumentsForwarder {
  constructor() {
    this.sink = new ArgumentsSink();
  }

  forward() {
    return this.sink.receive(...arguments);
  }

  run() {
    return this.forward('event', this.run.bind(this));
  }
}

new ArgumentsForwarder().run();
