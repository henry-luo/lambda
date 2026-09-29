// The package-visible outcome of one editing operation (D7.2.5).  Keeping
// this record separate from an event verdict lets queries, command calls, and
// platform input describe the same result without an ambient native channel.

pub fn make(supported, enabled, claimed, changed, selection_changed,
            query_bool, query_value, failure, plan_id, history_group) {
    {
        supported: supported,
        enabled: enabled,
        claimed: claimed,
        changed: changed,
        selection_changed: selection_changed,
        history_recorded: false,
        query_bool: query_bool,
        query_value: query_value,
        failure: failure,
        plan_id: plan_id,
        history_group: history_group,
        selection_after: null,
        selection_space: "none",
        model_revision: null,
        // API notifications use package-selected facts after the command
        // commits. Native only transports this record to InputEvent.
        api_input: false,
        api_input_type: "",
        api_input_data: null
    }
}

// Model actions return source coordinates explicitly. The native gate applies
// them only after the matching reactive render revision is published.
pub fn with_source_selection(edit_result, selection_after, model_revision) =>
    { *: edit_result, selection_after: selection_after, selection_space: "source",
      model_revision: model_revision }

pub fn model_applied(changed, selection_changed, history_recorded,
                     history_group, selection_after, model_revision) {
    // Keep this leaf-shaped for interpreter handlers: nested adapter calls can
    // already sit near the fixed scratch-frame depth during reactive editing.
    { supported: true, enabled: true, claimed: true, changed: changed,
      selection_changed: selection_changed, history_recorded: history_recorded,
      query_bool: false, query_value: "", failure: null, plan_id: 0,
      history_group: history_group, selection_after: selection_after,
      selection_space: "source", model_revision: model_revision,
      api_input: false, api_input_type: "", api_input_data: null }
}

pub fn decline(supported, enabled, failure, plan_id) {
    make(supported, enabled, false, false, false, false, "", failure,
         plan_id, null)
}

pub fn applied(supported, enabled, changed, selection_changed, history_recorded,
               plan_id, history_group, api_input, api_input_type, api_input_data) {
    // A supported enabled command owns its default action even when its plan
    // is a compatible no-op (for example a collapsed format command).
    let result = make(supported, enabled, true, changed, selection_changed, false, "",
                      null, plan_id, history_group);
    { *: result, history_recorded: history_recorded, api_input: api_input,
      api_input_type: api_input_type, api_input_data: api_input_data }
}

// Behavior handlers still speak the existing verdict vocabulary.  This is a
// narrow adapter at the package boundary; all command planning returns the
// structured record above.
pub fn verdict(result) {
    if (result != null and result.claimed) 'prevent-default' else 'pass'
}
