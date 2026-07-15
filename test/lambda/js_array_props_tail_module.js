export function verifyArrayPropsTailBridge(int64Value, datetimeValue, floatValue) {
    const wideFirst = [int64Value, datetimeValue, floatValue];
    const identity = wideFirst;
    wideFirst.meta = {answer: 42};
    for (let i = 0; i < 96; i++) wideFirst.push(i + 0.25);
    gc();

    const propsFirst = [];
    propsFirst.meta = {answer: 43};
    propsFirst.push(int64Value);
    propsFirst.push(datetimeValue);
    propsFirst.push(floatValue);
    for (let i = 0; i < 96; i++) propsFirst.push(i + 0.5);
    gc();

    return [
        identity === wideFirst,
        wideFirst.meta.answer === 42,
        wideFirst[0] === int64Value,
        String(wideFirst[1]) === String(datetimeValue),
        wideFirst[2] === floatValue,
        wideFirst[98] === 95.25,
        propsFirst.meta.answer === 43,
        propsFirst[0] === int64Value,
        String(propsFirst[1]) === String(datetimeValue),
        propsFirst[2] === floatValue,
        propsFirst[98] === 95.5
    ];
}

export function promoteIncomingArray(array, replacement) {
    array.meta = {answer: 44};
    array[0] = replacement;
    for (let i = 0; i < 64; i++) array.push(i + 0.75);
    gc();
    // Encode companion survival into an indexed mutation Lambda can observe.
    array[1] = array.meta.answer === 44 ? replacement : 0;
    return array;
}
