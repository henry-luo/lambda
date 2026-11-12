function check(name, run) {
    try { console.log(name, run()); }
    catch (error) { console.log(name, error.name); }
}
check('null', () => delete null?.ref);
check('undefined', () => delete undefined?.ref);
check('missing props', () => delete ({}).props?.ref);
check('continuation', () => delete null?.props.ref);
check('call continuation', () => delete null?.make().ref);
check('computed skipped', () => {
    let effects = 0;
    return delete null?.[effects++] && effects === 0;
});
check('base once', () => {
    let reads = 0, keys = 0;
    const value = {get props() { reads++; return null; }};
    return delete value.props?.[keys++] && reads === 1 && keys === 0;
});
check('real deletion', () => {
    const value = {props: {ref: 3}};
    return delete value.props?.ref && !('ref' in value.props);
});
check('no final getter', () => {
    let reads = 0;
    const value = {get ref() { reads++; return 3; }};
    return delete value?.ref && reads === 0 && !('ref' in value);
});
check('proxy deletion', () => {
    let deletes = 0;
    const value = new Proxy({ref: 3}, {deleteProperty(target, key) {
        deletes++; return Reflect.deleteProperty(target, key);
    }});
    return delete value?.ref && deletes === 1 && !('ref' in value);
});
check('group ends chain', () => {
    try { delete (null?.props).ref; }
    catch (error) { return error instanceof TypeError; }
    return false;
});
check('nonnull missing child throws', () => {
    try { delete ({props: null})?.props.ref; }
    catch (error) { return error instanceof TypeError; }
    return false;
});
check('nonconfigurable', () => {
    const value = {};
    Object.defineProperty(value, 'ref', {value: 3});
    return delete value?.ref === false;
});
check('strict nonconfigurable', () => {
    'use strict';
    const value = {};
    Object.defineProperty(value, 'ref', {value: 3});
    try { delete value?.ref; }
    catch (error) { return error instanceof TypeError; }
    return false;
});
check('unresolvable base', () => {
    try { delete optionalDeleteAbsent?.ref; }
    catch (error) { return error instanceof ReferenceError; }
    return false;
});
