// Native Lambda port of utils/check_state_machine.py (D7.2.5, D7.5.3).
import .check_state_machine_core
import .state_machine_policy

pn enum_values(enums, name: string) {
    let values = enums[name]
    return if (values == null) [] else values
}

pn rule_matches(rule, family: string, state_name: string, event: string) {
    return rule.family == family and rule.event == event and
           (rule.from_state == "SM_STATE_ANY" or rule.from_state == state_name)
}

pn has_rule(rules, family: string, state_name: string, event: string) {
    for (rule in rules) {
        if (rule_matches(rule, family, state_name, event)) { return true }
    }
    return false
}

pn validate(enums, rules, bindings) {
    var errors = []
    var warnings = []
    var summaries = []
    let families = enum_values(enums, "SmFamily")
    let view_classes = enum_values(enums, "SmViewClass")
    let events = enum_values(enums, "SmEvent")
    let actions = enum_values(enums, "SmActionFlag")
    var seen_names = []

    for (rule in rules) {
        if (rule.name == "") {
            errors = errors ++ ["rule with event " ++ rule.event ++ " has an empty name"]
        } else if (contains(seen_names, rule.name)) {
            errors = errors ++ ["duplicate rule name: " ++ rule.name]
        }
        seen_names = seen_names ++ [rule.name]
        if (not contains(families, rule.family)) {
            errors = errors ++ [rule.name ++ ": unknown family " ++ rule.family]
        }
        if (not contains(view_classes, rule.view_class)) {
            errors = errors ++ [rule.name ++ ": unknown view class " ++ rule.view_class]
        }
        if (not contains(events, rule.event)) {
            errors = errors ++ [rule.name ++ ": unknown event " ++ rule.event]
        }
        if (len(rule.to_states) == 0) {
            errors = errors ++ [rule.name ++ ": empty to-state list " ++ rule.to_array]
        }
        let action_expr = trim(rule.actions)
        if (action_expr != "0" and action_expr != "SM_ACT_NONE") {
            for (part in split(action_expr, "|")) {
                let action_name = trim(part)
                if (not contains(actions, action_name)) {
                    errors = errors ++ [rule.name ++ ": unknown action " ++ action_name]
                }
            }
        }
    }

    for (config in complete_families) {
        let enum_states = enum_values(enums, config.state_enum)
        if (len(enum_states) == 0) {
            errors = errors ++ [config.label ++ ": missing enum " ++ config.state_enum]
        } else {
            var missing_active = []
            var missing_initial = []
            for (state_name in config.active) {
                if (not contains(enum_states, state_name)) {
                    missing_active = missing_active ++ [state_name]
                }
            }
            for (state_name in config.initial) {
                if (not contains(enum_states, state_name)) {
                    missing_initial = missing_initial ++ [state_name]
                }
            }
            if (len(missing_active) > 0) {
                errors = errors ++ [config.label ++ ": configured active states missing from " ++
                                    config.state_enum ++ ": " ++ join(missing_active, ", ")]
            }
            if (len(missing_initial) > 0) {
                errors = errors ++ [config.label ++ ": configured initial states missing from " ++
                                    config.state_enum ++ ": " ++ join(missing_initial, ", ")]
            }

            var family_rules = []
            for (rule in rules) {
                if (rule.family == config.family) { family_rules = family_rules ++ [rule] }
            }
            if (len(family_rules) == 0) {
                errors = errors ++ [config.label ++ ": no rules for complete family " ++ config.family]
            } else {
                for (rule in family_rules) {
                    if (rule.from_state != "SM_STATE_ANY" and
                        not contains(enum_states, rule.from_state)) {
                        errors = errors ++ [rule.name ++ ": from_state " ++ rule.from_state ++
                                            " is not valid for " ++ config.label]
                    }
                    for (state_name in rule.to_states) {
                        if (state_name != "SM_STATE_SAME" and not contains(enum_states, state_name)) {
                            errors = errors ++ [rule.name ++ ": to_state " ++ state_name ++
                                                " from " ++ rule.to_array ++
                                                " is not valid for " ++ config.label]
                        }
                    }
                }

                // Every configured state/event pair needs a matching rule.
                var covered = 0
                var missing = []
                for (state_name in config.active) {
                    for (event in config.events) {
                        if (has_rule(family_rules, config.family, state_name, event)) {
                            covered = covered + 1
                        } else {
                            missing = missing ++ [state_name ++ " + " ++ event]
                        }
                    }
                }
                let expected = len(config.active) * len(config.events)
                if (covered != expected) {
                    errors = errors ++ [config.label ++ ": missing state/event coverage: " ++
                                        join(missing, ", ")]
                }

                var incoming = []
                for (rule in family_rules) {
                    for (state_name in rule.to_states) {
                        if (state_name != "SM_STATE_SAME" and not contains(incoming, state_name)) {
                            incoming = incoming ++ [state_name]
                        }
                    }
                }
                var unreachable = []
                for (state_name in config.active) {
                    if (not contains(config.initial, state_name) and
                        not contains(incoming, state_name)) {
                        unreachable = unreachable ++ [state_name]
                    }
                }
                if (len(unreachable) > 0) {
                    errors = errors ++ [config.label ++ ": active states have no incoming transition: " ++
                                        join(unreachable, ", ")]
                }

                var inactive = []
                for (state_name in enum_states) {
                    if (not contains(config.active, state_name)) { inactive = inactive ++ [state_name] }
                }
                for (event in config.events) {
                    if (not contains(events, event)) {
                        errors = errors ++ [config.label ++ ": configured event missing from SmEvent: " ++ event]
                    } else {
                        var event_has_rule = false
                        for (rule in family_rules) {
                            if (rule.event == event) { event_has_rule = true }
                        }
                        if (not event_has_rule) {
                            errors = errors ++ [config.label ++ ": event has no rule: " ++ event]
                        }
                    }
                }
                summaries = summaries ++ [{label: config.label, states: len(config.active),
                                           events: len(config.events), covered: covered,
                                           expected: expected, inactive: inactive}]
            }
        }
    }

    var extra_families = []
    for (rule in rules) {
        var complete = false
        for (config in complete_families) {
            if (rule.family == config.family) { complete = true }
        }
        if (not complete and not contains(extra_families, rule.family)) {
            extra_families = extra_families ++ [rule.family]
        }
    }
    if (len(extra_families) > 0) {
        warnings = warnings ++ ["rules exist for families not marked complete: " ++
                                join(sort(extra_families), ", ")]
    }

    let invariants = enum_values(enums, "SmInvariantId")
    var seen_invariant_names = []
    for (binding in bindings) {
        if (binding.name == "") {
            errors = errors ++ ["invariant binding with id " ++ binding.invariant ++
                                " has an empty name"]
        } else if (contains(seen_invariant_names, binding.name)) {
            errors = errors ++ ["duplicate invariant binding name: " ++ binding.name]
        }
        seen_invariant_names = seen_invariant_names ++ [binding.name]
        if (not contains(families, binding.family)) {
            errors = errors ++ [binding.name ++ ": unknown invariant family " ++ binding.family]
        }
        if (not contains(invariants, binding.invariant)) {
            errors = errors ++ [binding.name ++ ": unknown invariant id " ++ binding.invariant]
        }
        if (binding.state != "SM_STATE_ANY") {
            var matching = null
            for (config in complete_families) {
                if (config.family == binding.family) { matching = config }
            }
            if (matching == null) {
                errors = errors ++ [binding.name ++ ": state-specific invariant on incomplete family " ++
                                    binding.family]
            } else if (not contains(enum_values(enums, matching.state_enum), binding.state)) {
                errors = errors ++ [binding.name ++ ": invariant state " ++ binding.state ++
                                    " is not valid for " ++ matching.label]
            }
        }
    }
    return {errors: errors, warnings: warnings, summaries: summaries}
}

pn main() {
    let header = input("radiant/event.hpp", "text")^
    let source = input("radiant/state_schema.cpp", "text")^
    let enums = parse_enums(header)
    let arrays = parse_to_state_arrays(source)
    let rules = parse_rules(source, arrays)^
    let bindings = parse_invariant_bindings(source)^
    let report = validate(enums, rules, bindings)

    print("Radiant state-machine schema check\n")
    for (summary in report.summaries) {
        print("  " ++ summary.label ++ ": " ++ string(summary.states) ++
              " active state(s), " ++ string(summary.events) ++ " event(s), coverage " ++
              string(summary.covered) ++ "/" ++ string(summary.expected) ++ "\n")
        if (len(summary.inactive) > 0) {
            print("    inactive enum states: " ++ join(summary.inactive, ", ") ++ "\n")
        }
    }
    print("  invariants: " ++ string(len(bindings)) ++ " binding(s)\n")
    for (warning in report.warnings) { print("warning: " ++ warning ++ "\n") }
    for (failure in report.errors) { print("error: " ++ failure ++ "\n") }
    if (len(report.errors) > 0) {
        print("check-state-machine: FAIL (" ++ string(len(report.errors)) ++ " error(s))\n")
        raise error("check-state-machine: schema validation failed")
    }
    print("check-state-machine: PASS (" ++ string(len(complete_families)) ++
          " complete families, " ++ string(len(rules)) ++ " rules)")
}
