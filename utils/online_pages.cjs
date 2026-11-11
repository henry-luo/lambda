const fs = require('fs');

function parseCsv(text) {
    const rows = [];
    let row = [];
    let field = '';
    let quoted = false;
    for (let index = 0; index < text.length; index++) {
        const character = text[index];
        if (character === '"') {
            if (quoted && text[index + 1] === '"') {
                field += '"';
                index++;
            } else {
                quoted = !quoted;
            }
        } else if (character === ',' && !quoted) {
            row.push(field);
            field = '';
        } else if ((character === '\r' || character === '\n') && !quoted) {
            if (character === '\r' && text[index + 1] === '\n') index++;
            if (field || row.length) rows.push([...row, field]);
            row = [];
            field = '';
        } else {
            field += character;
        }
    }
    if (quoted) throw new Error('unterminated quoted CSV field');
    if (field || row.length) rows.push([...row, field]);
    return rows;
}

function csvField(value) {
    const text = String(value ?? '');
    return /[",\r\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
}

function readCsvObjects(file) {
    const [header, ...rows] = parseCsv(fs.readFileSync(file, 'utf8'));
    if (!header || new Set(header).size !== header.length) {
        throw new Error(`${file} has an invalid header`);
    }
    return rows.map((row, index) => {
        if (row.length !== header.length) throw new Error(`${file}: row ${index + 2} has invalid fields`);
        return Object.fromEntries(header.map((key, field) => [key, row[field]]));
    });
}

function readPageCatalog(file) {
    const pages = readCsvObjects(file);
    if (!pages.length) throw new Error(`${file} has no page cases`);
    const labels = new Set();
    for (const page of pages) {
        if (typeof page.label !== 'string' || !/^[A-Za-z0-9_-]+$/.test(page.label) ||
                !/^https?:\/\//.test(page.url) ||
                !['true', 'false'].includes(page.expect_linked_resources)) {
            throw new Error(`${file} contains an invalid page row`);
        }
        if (labels.has(page.label)) throw new Error(`duplicate page label: ${page.label}`);
        labels.add(page.label);
    }
    return pages;
}

function serializeCsv(columns, rows) {
    return [columns, ...rows.map((row) => columns.map((key) => row[key]))]
        .map((row) => row.map(csvField).join(',')).join('\n') + '\n';
}

module.exports = {parseCsv, csvField, readCsvObjects, readPageCatalog, serializeCsv};
