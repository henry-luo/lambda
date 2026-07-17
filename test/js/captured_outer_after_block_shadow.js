const normalize = value => String(value).toLowerCase();

function makeSearch(query) {
    const fields = [{ field: 'text' }];
    const seen = [];
    fields.forEach(field => {
        const normalize = field.field;
        seen.push(normalize);
    });
    return normalize(query) + ':' + seen.join(',');
}

console.log('captured-outer-after-shadow:', makeSearch('BET'));
