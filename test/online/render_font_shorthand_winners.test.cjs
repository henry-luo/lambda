const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures,loggedResult} = require('./render_fixture.cjs');
const {fixtures,fixtureHtml} = require('./font_shorthand_winner_fixtures.cjs');
const render = renderFixtures('font-shorthand-winners');
for (const [index,f] of fixtures.entries()) test(f.name, async () => {
    const {log}=await render('case-'+index,fixtureHtml(f));
    const result=loggedResult(log,'FONT_SHORTHAND_WINNER');
    assert.equal(result.weight,f.weight);
    assert.ok(result.family.includes(f.family),result.family);
    assert.equal(result.style,f.style);
    if (f.variant) assert.equal(result.variant,f.variant);
    assert.equal(result.size,'38px');
});
