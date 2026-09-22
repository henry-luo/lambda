var e = (component, entries) => {
    let result = component;
    for (let [e, value] of entries) result[e] = value;
    return result;
};

export { e as helper };
