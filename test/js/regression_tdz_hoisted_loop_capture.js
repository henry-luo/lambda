'use strict';

for (const enabled of [true]) {
  const callbacks = [];
  const writer = {
    write() {
      callbacks.push(1);
    }
  };

  let value = 42;
  function readValue() {
    return value;
  }

  writer.write();
  console.log(readValue());
}
