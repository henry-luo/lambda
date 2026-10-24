console.log('fonts:api:' + (typeof window.queryLocalFonts === 'function'));
async function checkLocalFonts() {
    var pending = window.queryLocalFonts();
    console.log('fonts:promise:' + (pending instanceof Promise));
    var faces = await pending;
    console.log('fonts:metadata:' + (faces.length > 0 && faces.every(function(face) {
        return typeof face.postscriptName === 'string' && face.postscriptName.length > 0 &&
            typeof face.fullName === 'string' && face.fullName.length > 0 &&
            typeof face.family === 'string' && face.family.length > 0 && typeof face.style === 'string';
    })));
    console.log('fonts:ordered:' + faces.every(function(face, i) {
        return i === 0 || faces[i - 1].postscriptName < face.postscriptName;
    }));
    var name = faces[0].postscriptName;
    var filtered = await window.queryLocalFonts({postscriptNames: [name, name]});
    console.log('fonts:filter:' + (filtered.length === 1 && filtered[0].postscriptName === name));
    var empty = await window.queryLocalFonts({postscriptNames: []});
    console.log('fonts:empty:' + (empty.length === 0));
    var missing = await window.queryLocalFonts({postscriptNames: ['LambdaMissingNativeFace-e33149']});
    console.log('fonts:missing:' + (missing.length === 0));
    var fromSet = await window.queryLocalFonts({postscriptNames: new Set([name])});
    console.log('fonts:iterable:' + (fromSet.length === 1 && fromSet[0].postscriptName === name));
    try { await window.queryLocalFonts({postscriptNames: 17}); console.log('fonts:invalid:false'); }
    catch (error) { console.log('fonts:invalid:' + (error instanceof TypeError)); }
    try { await window.queryLocalFonts({get postscriptNames() { throw new Error('font getter'); }}); console.log('fonts:getter:false'); }
    catch (error) { console.log('fonts:getter:' + (error.message === 'font getter')); }
    faces[0].family = 'Changed snapshot';
    var fresh = await window.queryLocalFonts({postscriptNames: [name]});
    console.log('fonts:fresh:' + (fresh[0].family !== 'Changed snapshot'));
}
checkLocalFonts().catch(function(error) { console.log('fonts:failure:' + error); });
