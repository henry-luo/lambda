function assertPixels(png, events, scale) {
  return events.filter(event => event.type === 'assert_pixel').map(event => {
    const x = event.x * scale + Math.floor(scale / 2);
    const y = event.y * scale + Math.floor(scale / 2);
    const offset = 4 * (y * png.width + x);
    const rgba = Array.from(png.data.subarray(offset, offset + 4));
    const passed = x >= 0 && y >= 0 && x < png.width && y < png.height &&
      ['r', 'g', 'b', 'a'].every((channel, index) =>
        (event['min_' + channel] === undefined || rgba[index] >= event['min_' + channel]) &&
        (event['max_' + channel] === undefined || rgba[index] <= event['max_' + channel]));
    return {x, y, rgba, passed, event};
  });
}

module.exports = {assertPixels};
