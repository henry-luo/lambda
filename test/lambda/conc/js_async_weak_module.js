async function tickLater(label) {
    await new Promise((resolve) => setTimeout(resolve, 5));
    console.log("js finished " + label);
    return label;
}

async function parkForever() {
    await new Promise(() => {});
    console.log("unreachable");
}

export { tickLater, parkForever };
