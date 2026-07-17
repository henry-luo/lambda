function prepare(query) {
  const options = {};
  if (true) {
    const query = [];
    [{ field: 'text', weight: 1 }].forEach(option => query.push(option));
    options.fields = query;
  }
  return [query.toLowerCase(), options.fields[0].field];
}

console.log('captured-block-shadow:', prepare('Bet').join(' '));
