// S12.1.3v2/S7.6.3v3: state setup cannot propagate through a clean view return.
fn failed() int^ => error("state outcome")
view clean: <clean_state>() int state value:(failed()^) { value }
apply(<clean_state>)
