const hostCascade = `<!doctype html>
<style>
    body { margin: 0; background: white }
    #host { border-right: 1px solid blue }
    :host(.active) { top: 99px !important }
</style>
<div id="host" class="active shared" style="background-color:blue; border-bottom-color:yellow !important">
    <template shadowrootmode="open">
        <style>
            :host { position:fixed; left:20px; top:40px; width:60px; height:30px; box-sizing:border-box;
                --fill:lime; background-color:red; border-bottom:1px solid red !important }
            :host(.active) { width:80px; --fill:green }
            :host(#host) { border-right:1px solid red }
            :host(.active)::before { content:''; display:block; width:5px; height:5px; background:lime }
            :host > .tile { height:12px }
            :host(.active) > .tile { width:26px }
            .tile { width:15px; background-color:var(--fill) }
            .shared > .tile { height:1px !important }
            .shared:host { left:99px !important }
            :host(.active) .nested-tile { width:1px !important }
        </style>
        <div class="tile"></div>
        <div id="nested" class="shared">
            <template shadowrootmode="open">
                <style>
                    :host { --fill:blue }
                    :host(.shared) > .nested-tile { height:4px }
                    .nested-tile { width:8px; background-color:var(--fill) }
                </style>
                <div class="nested-tile"></div>
            </template>
        </div>
    </template>
</div>
<script>
    const host = document.getElementById('host');
    const root = host.shadowRoot;
    const tile = root.querySelector('.tile');
    const nested = root.getElementById('nested').shadowRoot.querySelector('.nested-tile');
    const style = root.querySelector('style');
    function read() {
        const box = host.getBoundingClientRect();
        const s = getComputedStyle(host), t = getComputedStyle(tile), n = getComputedStyle(nested);
        return [box.x, box.y, box.width, s.backgroundColor, s.borderRightColor, s.borderBottomColor,
            t.width, t.height, t.backgroundColor, n.width, n.height, n.backgroundColor];
    }
    const initial = read();
    const pseudo = getComputedStyle(host, '::before').backgroundColor;
    const matchesOutside = host.matches(':host(.active)');
    host.classList.remove('active');
    const removedClass = read();
    const css = style.sheet;
    css.insertRule(':host([data-panel]) { --fill:orange }', css.cssRules.length);
    css.insertRule(':host([data-panel]) > .tile { width:20px }', css.cssRules.length);
    host.setAttribute('data-panel', '');
    const changedAttribute = read();
    style.media = 'not all';
    const disabled = getComputedStyle(host).position;
    style.media = '';
    const reenabled = read();
    console.log('shadow host result ' + JSON.stringify({initial, pseudo, matchesOutside, removedClass,
        changedAttribute, disabled, reenabled}));
</script>`;

const expectedHostCascade = {
    initial: [20, 40, 80, 'rgb(0, 0, 255)', 'rgb(0, 0, 255)', 'rgb(255, 0, 0)',
        '26px', '12px', 'rgb(0, 128, 0)', '8px', '4px', 'rgb(0, 0, 255)'],
    pseudo: 'rgb(0, 255, 0)',
    matchesOutside: false,
    removedClass: [20, 40, 60, 'rgb(0, 0, 255)', 'rgb(0, 0, 255)', 'rgb(255, 0, 0)',
        '15px', '12px', 'rgb(0, 255, 0)', '8px', '4px', 'rgb(0, 0, 255)'],
    changedAttribute: [20, 40, 60, 'rgb(0, 0, 255)', 'rgb(0, 0, 255)', 'rgb(255, 0, 0)',
        '20px', '12px', 'rgb(255, 165, 0)', '8px', '4px', 'rgb(0, 0, 255)'],
    disabled: 'static',
};
expectedHostCascade.reenabled = expectedHostCascade.changedAttribute;

module.exports = {hostCascade, expectedHostCascade};
