#!/usr/bin/env python3
"""
Validate Radiant's declarative state-machine schema.

The checker deliberately treats the schema as partially migrated overall:
families listed in COMPLETE_FAMILIES are expected to have total coverage for
their active states and events, while future enum families may exist without
table rows yet.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER_PATH = os.path.join(ROOT, "radiant", "event.hpp")
SOURCE_PATH = os.path.join(ROOT, "radiant", "state_schema.cpp")


COMPLETE_FAMILIES = {
    "SM_FAMILY_DOCUMENT": {
        "label": "document",
        "state_enum": "DocLifecycleState",
        "active_states": [
            "DOC_LIFECYCLE_UNINITIALIZED",
            "DOC_LIFECYCLE_LOADING",
            "DOC_LIFECYCLE_COMMITTED",
            "DOC_LIFECYCLE_UNLOADED",
        ],
        "initial_states": ["DOC_LIFECYCLE_UNINITIALIZED"],
        "events": [
            "SM_EV_DOC_LOAD",
            "SM_EV_DOC_COMMIT",
            "SM_EV_DOC_UNLOAD",
        ],
    },
    "SM_FAMILY_FOCUS": {
        "label": "focus",
        "state_enum": "FocusFsmState",
        "active_states": [
            "FOCUS_NO_DOCUMENT",
            "FOCUS_DOC_ACTIVE_NONE",
            "FOCUS_ELEMENT",
            "FOCUS_TEXT_CONTROL",
            "FOCUS_CONTENTEDITABLE",
        ],
        "initial_states": ["FOCUS_NO_DOCUMENT", "FOCUS_DOC_ACTIVE_NONE"],
        "events": [
            "SM_EV_FOCUS_ELEMENT",
            "SM_EV_BLUR_CURRENT",
            "SM_EV_FOCUS_MOVE_FWD",
            "SM_EV_FOCUS_MOVE_BACK",
            "SM_EV_UI_FOCUS_WITH_BLUR",
            "SM_EV_UI_FOCUS_WITH_CHANGE",
            "SM_EV_UI_BLUR_WITH_BLUR",
            "SM_EV_UI_BLUR_WITH_CHANGE",
        ],
    },
    "SM_FAMILY_SELECTION": {
        "label": "selection",
        "state_enum": "SelectionFsmState",
        "active_states": [
            "SEL_EMPTY",
            "SEL_CARET_COLLAPSED",
            "SEL_RANGE_FORWARD",
            "SEL_RANGE_BACKWARD",
            "SEL_POINTER_SELECTING",
        ],
        "initial_states": ["SEL_EMPTY"],
        "events": [
            "SM_EV_COLLAPSE_TO_BOUNDARY",
            "SM_EV_START_POINTER_SELECTION",
            "SM_EV_UI_START_POINTER_SELECTION",
            "SM_EV_END_POINTER_SELECTION",
            "SM_EV_EXTEND_TO_BOUNDARY",
            "SM_EV_EXTEND_TO_VIEW",
            "SM_EV_SET_BASE_AND_EXTENT",
            "SM_EV_SELECT_ALL",
            "SM_EV_COLLAPSE_TO_START",
            "SM_EV_COLLAPSE_TO_END",
            "SM_EV_CLEAR_SELECTION",
        ],
    },
    "SM_FAMILY_IME": {
        "label": "ime",
        "state_enum": "ImeFsmState",
        "active_states": ["IME_IDLE", "IME_COMPOSING"],
        "initial_states": ["IME_IDLE"],
        "events": [
            "SM_EV_COMPOSITION_START",
            "SM_EV_COMPOSITION_UPDATE",
            "SM_EV_COMPOSITION_COMMIT",
            "SM_EV_COMPOSITION_CANCEL",
        ],
    },
    "SM_FAMILY_HOVER": {
        "label": "hover",
        "state_enum": "HoverFsmState",
        "active_states": ["HOVER_NONE", "HOVER_TARGET"],
        "initial_states": ["HOVER_NONE"],
        "events": ["SM_EV_HOVER_SET", "SM_EV_HOVER_CLEAR"],
    },
    "SM_FAMILY_ACTIVE": {
        "label": "active",
        "state_enum": "ActiveFsmState",
        "active_states": ["ACTIVE_NONE", "ACTIVE_PRESSED"],
        "initial_states": ["ACTIVE_NONE"],
        "events": ["SM_EV_ACTIVE_SET", "SM_EV_ACTIVE_CLEAR"],
    },
    "SM_FAMILY_DRAG_DROP": {
        "label": "drag_drop",
        "state_enum": "DragFsmState",
        "active_states": [
            "DRAG_IDLE",
            "DRAG_PENDING",
            "DRAG_ACTIVE",
            "DRAG_OVER_TARGET",
        ],
        "initial_states": ["DRAG_IDLE"],
        "events": [
            "SM_EV_DRAG_SET_STATE",
            "SM_EV_DRAG_BEGIN_DROP",
            "SM_EV_DRAG_UPDATE_MOTION",
            "SM_EV_DRAG_SET_DROP_ACTIVE",
            "SM_EV_DRAG_SET_DROP_TARGET",
            "SM_EV_DRAG_CLEAR_DROP",
        ],
    },
    "SM_FAMILY_SCROLL": {
        "label": "scroll",
        "state_enum": "ScrollFsmState",
        "active_states": [
            "SCROLL_IDLE",
            "SCROLL_BAR_HOVER",
            "SCROLL_BAR_DRAGGING",
        ],
        "initial_states": ["SCROLL_IDLE"],
        "events": [
            "SM_EV_SCROLL_SET_POSITION",
            "SM_EV_SCROLL_SET_MAX",
            "SM_EV_SCROLLBAR_HOVER",
            "SM_EV_SCROLLBAR_BEGIN_DRAG",
            "SM_EV_SCROLLBAR_CLEAR_DRAG",
        ],
    },
    "SM_FAMILY_FORM_CHECKABLE": {
        "label": "form_checkable",
        "state_enum": "CheckableFsmState",
        "active_states": ["CHK_UNCHECKED", "CHK_CHECKED"],
        "initial_states": ["CHK_UNCHECKED"],
        "events": [
            "SM_EV_FORM_SET_CHECKED",
            "SM_EV_FORM_UNCHECK_RADIO_GROUP",
            "SM_EV_FORM_SET_DISABLED",
            "SM_EV_FORM_SET_REQUIRED",
        ],
    },
    "SM_FAMILY_FORM_SELECT": {
        "label": "form_select",
        "state_enum": "SelectFsmState",
        "active_states": ["SELCTL_CLOSED", "SELCTL_OPEN"],
        "initial_states": ["SELCTL_CLOSED"],
        "events": [
            "SM_EV_FORM_SET_SELECTED_INDEX",
            "SM_EV_FORM_SET_HOVER_INDEX",
            "SM_EV_FORM_SET_DISABLED",
            "SM_EV_FORM_SET_REQUIRED",
            "SM_EV_DROPDOWN_OPEN",
            "SM_EV_DROPDOWN_CLOSE",
        ],
    },
    "SM_FAMILY_FORM_RANGE": {
        "label": "form_range",
        "state_enum": "RangeFsmState",
        "active_states": ["RANGE_VALUE"],
        "initial_states": ["RANGE_VALUE"],
        "events": [
            "SM_EV_FORM_SET_RANGE_VALUE",
            "SM_EV_FORM_SET_DISABLED",
            "SM_EV_FORM_SET_REQUIRED",
        ],
    },
    "SM_FAMILY_FORM_TEXT": {
        "label": "form_text",
        "state_enum": "TextFsmState",
        "active_states": ["TEXT_EMPTY", "TEXT_VALUE", "TEXT_SELECTION"],
        "initial_states": ["TEXT_EMPTY"],
        "events": [
            "SM_EV_FORM_SET_VALUE",
            "SM_EV_FORM_REPLACE_TEXT",
            "SM_EV_FORM_HISTORY",
            "SM_EV_FORM_SET_SELECTION",
            "SM_EV_FORM_SET_DISABLED",
            "SM_EV_FORM_SET_READONLY",
            "SM_EV_FORM_SET_REQUIRED",
        ],
    },
    "SM_FAMILY_DROPDOWN": {
        "label": "dropdown",
        "state_enum": "DropdownFsmState",
        "active_states": ["DD_CLOSED", "DD_OPEN"],
        "initial_states": ["DD_CLOSED"],
        "events": [
            "SM_EV_DROPDOWN_OPEN",
            "SM_EV_DROPDOWN_CLOSE",
            "SM_EV_DROPDOWN_SET_GEOMETRY",
        ],
    },
    "SM_FAMILY_CONTEXT_MENU": {
        "label": "context_menu",
        "state_enum": "ContextMenuFsmState",
        "active_states": ["CM_CLOSED", "CM_OPEN", "CM_HOVER"],
        "initial_states": ["CM_CLOSED"],
        "events": [
            "SM_EV_CONTEXT_MENU_OPEN",
            "SM_EV_CONTEXT_MENU_CLOSE",
            "SM_EV_CONTEXT_MENU_HOVER",
        ],
    },
}


def read_text(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*", "", text)


def parse_enums(header_text):
    enums = {}
    for match in re.finditer(r"typedef\s+enum\s+(\w+)\s*\{(.*?)\}\s*\w+\s*;",
                             header_text, re.S):
        enum_name = match.group(1)
        body = strip_comments(match.group(2))
        values = []
        for raw_entry in body.split(","):
            entry = raw_entry.strip()
            if not entry:
                continue
            values.append(entry.split("=", 1)[0].strip())
        enums[enum_name] = values
    return enums


def split_top_level_csv(text):
    fields = []
    start = 0
    paren_depth = 0
    in_string = False
    escape = False
    for index, char in enumerate(text):
        if in_string:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == "\"":
                in_string = False
            continue
        if char == "\"":
            in_string = True
        elif char == "(":
            paren_depth += 1
        elif char == ")":
            paren_depth -= 1
        elif char == "," and paren_depth == 0:
            fields.append(text[start:index].strip())
            start = index + 1
    tail = text[start:].strip()
    if tail:
        fields.append(tail)
    return fields


def parse_to_state_arrays(source_text):
    arrays = {}
    pattern = re.compile(r"static\s+const\s+int\s+(\w+)\s*\[\]\s*=\s*\{(.*?)\};",
                         re.S)
    for match in pattern.finditer(source_text):
        name = match.group(1)
        body = strip_comments(match.group(2))
        arrays[name] = [field for field in split_top_level_csv(body) if field]
    return arrays


def extract_rules_body(source_text):
    marker = "static const StateTransitionRule RADIANT_STATE_RULES[] = {"
    start = source_text.find(marker)
    if start < 0:
        raise ValueError("missing RADIANT_STATE_RULES[] table")
    start += len(marker)
    end = source_text.find("\n};", start)
    if end < 0:
        raise ValueError("missing end of RADIANT_STATE_RULES[] table")
    return source_text[start:end]


def iter_rule_entries(rules_body):
    entries = []
    depth = 0
    start = None
    in_string = False
    escape = False
    for index, char in enumerate(rules_body):
        if in_string:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == "\"":
                in_string = False
            continue
        if char == "\"":
            in_string = True
        elif char == "{":
            if depth == 0:
                start = index + 1
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0 and start is not None:
                entries.append(rules_body[start:index])
                start = None
    return entries


def parse_rules(source_text, to_state_arrays):
    rules = []
    for entry in iter_rule_entries(extract_rules_body(source_text)):
        fields = split_top_level_csv(strip_comments(entry))
        if len(fields) != 10:
            raise ValueError("rule has %d field(s), expected 10: %s" %
                             (len(fields), " ".join(entry.split())))
        to_expr = fields[5]
        macro_match = re.match(r"SM_RULE_TO\s*\(\s*(\w+)\s*\)$", to_expr)
        if not macro_match:
            raise ValueError("unsupported to-state expression: %s" % to_expr)
        array_name = macro_match.group(1)
        if array_name not in to_state_arrays:
            raise ValueError("unknown to-state array: %s" % array_name)
        name = fields[9].strip()
        if name.startswith("\"") and name.endswith("\""):
            name = name[1:-1]
        rules.append({
            "family": fields[0],
            "view_class": fields[1],
            "from_state": fields[2],
            "event": fields[3],
            "guard": fields[4],
            "to_states": to_state_arrays[array_name],
            "to_array": array_name,
            "actions": fields[6],
            "invariants": fields[7],
            "invariant_count": fields[8],
            "name": name,
        })
    return rules


def extract_invariants_body(source_text):
    marker = "RADIANT_INVARIANTS[] = {"
    start = source_text.find(marker)
    if start < 0:
        raise ValueError("missing RADIANT_INVARIANTS[] table")
    start += len(marker)
    end = source_text.find("\n};", start)
    if end < 0:
        raise ValueError("missing end of RADIANT_INVARIANTS[] table")
    return source_text[start:end]


def parse_invariant_bindings(source_text):
    bindings = []
    for entry in iter_rule_entries(extract_invariants_body(source_text)):
        fields = split_top_level_csv(strip_comments(entry))
        if len(fields) != 4:
            raise ValueError("invariant binding has %d field(s), expected 4: %s" %
                             (len(fields), " ".join(entry.split())))
        name = fields[3].strip()
        if name.startswith("\"") and name.endswith("\""):
            name = name[1:-1]
        bindings.append({
            "family": fields[0],
            "state": fields[1],
            "invariant": fields[2],
            "name": name,
        })
    return bindings


def rule_matches(rule, family, state, event):
    if rule["family"] != family or rule["event"] != event:
        return False
    return rule["from_state"] == "SM_STATE_ANY" or rule["from_state"] == state


def validate(enums, rules, invariant_bindings):
    errors = []
    warnings = []

    families = set(enums.get("SmFamily", []))
    view_classes = set(enums.get("SmViewClass", []))
    events = set(enums.get("SmEvent", []))
    actions = set(enums.get("SmActionFlag", []))

    seen_names = set()
    for rule in rules:
        if not rule["name"]:
            errors.append("rule with event %s has an empty name" % rule["event"])
        elif rule["name"] in seen_names:
            errors.append("duplicate rule name: %s" % rule["name"])
        seen_names.add(rule["name"])

        if rule["family"] not in families:
            errors.append("%s: unknown family %s" % (rule["name"], rule["family"]))
        if rule["view_class"] not in view_classes:
            errors.append("%s: unknown view class %s" %
                          (rule["name"], rule["view_class"]))
        if rule["event"] not in events:
            errors.append("%s: unknown event %s" % (rule["name"], rule["event"]))
        if not rule["to_states"]:
            errors.append("%s: empty to-state list %s" %
                          (rule["name"], rule["to_array"]))
        action_expr = rule["actions"].strip()
        if action_expr not in ("0", "SM_ACT_NONE"):
            for action_name in [part.strip() for part in action_expr.split("|")]:
                if action_name not in actions:
                    errors.append("%s: unknown action %s" %
                                  (rule["name"], action_name))

    family_summaries = []
    for family, config in COMPLETE_FAMILIES.items():
        enum_name = config["state_enum"]
        enum_states = enums.get(enum_name)
        if not enum_states:
            errors.append("%s: missing enum %s" % (config["label"], enum_name))
            continue

        enum_state_set = set(enum_states)
        active_states = config["active_states"]
        active_state_set = set(active_states)
        initial_state_set = set(config["initial_states"])

        missing_active = [state for state in active_states if state not in enum_state_set]
        missing_initial = [state for state in initial_state_set if state not in enum_state_set]
        if missing_active:
            errors.append("%s: configured active states missing from %s: %s" %
                          (config["label"], enum_name, ", ".join(missing_active)))
        if missing_initial:
            errors.append("%s: configured initial states missing from %s: %s" %
                          (config["label"], enum_name, ", ".join(missing_initial)))

        family_rules = [rule for rule in rules if rule["family"] == family]
        if not family_rules:
            errors.append("%s: no rules for complete family %s" %
                          (config["label"], family))
            continue

        for rule in family_rules:
            if rule["from_state"] != "SM_STATE_ANY" and rule["from_state"] not in enum_state_set:
                errors.append("%s: from_state %s is not valid for %s" %
                              (rule["name"], rule["from_state"], config["label"]))
            for to_state in rule["to_states"]:
                if to_state == "SM_STATE_SAME":
                    continue
                if to_state not in enum_state_set:
                    errors.append("%s: to_state %s from %s is not valid for %s" %
                                  (rule["name"], to_state, rule["to_array"],
                                   config["label"]))

        expected_pairs = [(state, event)
                          for state in active_states
                          for event in config["events"]]
        covered_pairs = [
            (state, event)
            for state, event in expected_pairs
            if any(rule_matches(rule, family, state, event) for rule in family_rules)
        ]
        if len(covered_pairs) != len(expected_pairs):
            missing = ["%s + %s" % pair
                       for pair in expected_pairs
                       if pair not in covered_pairs]
            errors.append("%s: missing state/event coverage: %s" %
                          (config["label"], ", ".join(missing)))

        incoming = set()
        for rule in family_rules:
            for to_state in rule["to_states"]:
                if to_state != "SM_STATE_SAME":
                    incoming.add(to_state)
        unreachable = [
            state for state in active_states
            if state not in initial_state_set and state not in incoming
        ]
        if unreachable:
            errors.append("%s: active states have no incoming transition: %s" %
                          (config["label"], ", ".join(unreachable)))

        inactive_states = [state for state in enum_states if state not in active_state_set]
        for event in config["events"]:
            if event not in events:
                errors.append("%s: configured event missing from SmEvent: %s" %
                              (config["label"], event))
            elif not any(rule["event"] == event for rule in family_rules):
                errors.append("%s: event has no rule: %s" %
                              (config["label"], event))

        family_summaries.append({
            "label": config["label"],
            "states": len(active_states),
            "events": len(config["events"]),
            "covered": len(covered_pairs),
            "expected": len(expected_pairs),
            "inactive_states": inactive_states,
        })

    complete_family_set = set(COMPLETE_FAMILIES.keys())
    extra_families = sorted(set(rule["family"] for rule in rules) - complete_family_set)
    if extra_families:
        warnings.append("rules exist for families not marked complete: %s" %
                        ", ".join(extra_families))

    invariants = set(enums.get("SmInvariantId", []))
    seen_invariant_names = set()
    for binding in invariant_bindings:
        if not binding["name"]:
            errors.append("invariant binding with id %s has an empty name" %
                          binding["invariant"])
        elif binding["name"] in seen_invariant_names:
            errors.append("duplicate invariant binding name: %s" % binding["name"])
        seen_invariant_names.add(binding["name"])

        if binding["family"] not in families:
            errors.append("%s: unknown invariant family %s" %
                          (binding["name"], binding["family"]))
        if binding["invariant"] not in invariants:
            errors.append("%s: unknown invariant id %s" %
                          (binding["name"], binding["invariant"]))
        if binding["state"] != "SM_STATE_ANY":
            config = COMPLETE_FAMILIES.get(binding["family"])
            if not config:
                errors.append("%s: state-specific invariant on incomplete family %s" %
                              (binding["name"], binding["family"]))
            else:
                enum_states = set(enums.get(config["state_enum"], []))
                if binding["state"] not in enum_states:
                    errors.append("%s: invariant state %s is not valid for %s" %
                                  (binding["name"], binding["state"], config["label"]))

    return errors, warnings, family_summaries


def main():
    try:
        # State schema and store declarations were consolidated into event.hpp.
        header_text = read_text(HEADER_PATH)
        source_text = read_text(SOURCE_PATH)
        enums = parse_enums(header_text)
        arrays = parse_to_state_arrays(source_text)
        rules = parse_rules(source_text, arrays)
        invariant_bindings = parse_invariant_bindings(source_text)
        errors, warnings, summaries = validate(enums, rules, invariant_bindings)
    except (OSError, ValueError) as error:
        print("check-state-machine: ERROR: %s" % error)
        return 1

    print("Radiant state-machine schema check")
    for summary in summaries:
        print("  %s: %d active state(s), %d event(s), coverage %d/%d" %
              (summary["label"], summary["states"], summary["events"],
               summary["covered"], summary["expected"]))
        if summary["inactive_states"]:
            print("    inactive enum states: %s" %
                  ", ".join(summary["inactive_states"]))
    print("  invariants: %d binding(s)" % len(invariant_bindings))

    for warning in warnings:
        print("warning: %s" % warning)
    for error in errors:
        print("error: %s" % error)

    if errors:
        print("check-state-machine: FAIL (%d error(s))" % len(errors))
        return 1

    print("check-state-machine: PASS (%d complete families, %d rules)" %
          (len(COMPLETE_FAMILIES), len(rules)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
