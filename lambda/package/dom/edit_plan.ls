// Immutable package plan records.  The current waist applies the supported
// step shapes synchronously; keeping the plan data explicit prevents command
// aliases from growing their own native mutation paths (D7.2.5).

pub fn make(context, descriptor, value, session) {
    {
        document: null,
        host: context.host,
        token: context.token,
        expected_epoch: context.mutation_epoch,
        descriptor: descriptor.name,
        family: descriptor.family,
        input_type: descriptor.input_type,
        value: value,
        steps: [{ kind: descriptor.step_kind, value: value }],
        selection_before: {
            start_container: context.start_container,
            end_container: context.end_container,
            start: context.start,
            end: context.end
        },
        selection_after: null,
        normalize: [],
        history: {
            class: descriptor.history_class,
            group: descriptor.history_class,
            record_unchanged: false
        },
        session_generation: session.generation,
        events: descriptor.event_contract,
        typing_state_after: null
    }
}
