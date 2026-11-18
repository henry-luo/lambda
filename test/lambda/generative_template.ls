// S12.1.3: apply enters the selected template through a generative block.
view int function() {
    let started = clock();
    <p "value: " ~>
}
function() {
    apply(42)
}
