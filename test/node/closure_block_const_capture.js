'use strict';

{
  const marker = { name: 'first' };
  process.nextTick(() => console.log(marker.name));
}

{
  const marker = { name: 'second' };
  process.nextTick(() => console.log(marker.name));
}
