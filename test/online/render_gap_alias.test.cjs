const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, loggedResult} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml, checkGapAlias} = require('./gap_alias_fixtures.cjs');
const render = renderFixtures('gap-alias');

for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {log} = await render('gap-alias-' + index, fixtureHtml(fixture));
        checkGapAlias(assert, loggedResult(log, 'GAP_ALIAS'));
    });
}
