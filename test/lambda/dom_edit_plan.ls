// D7.2.5: command aliases, descriptors, and plan/result records remain
// package data. This fixture has no DOM invocation; it pins the pure registry.
import commands: lambda.dom.commands
import plan: lambda.dom.edit_plan
import result: lambda.dom.edit_result

let bold = commands.descriptor("FORMATBOLD");
let font_name = commands.descriptor("fontName");
let indent = commands.descriptor("indent");
let clipboard = commands.descriptor("paste");
let undo_command = commands.descriptor("UNDO");
let redo_intent = commands.descriptor_for_intent("historyRedo");
let replacement = commands.descriptor_for_intent("insertReplacementText");
let composition = commands.descriptor_for_intent("insertFromComposition");
let context = {
    host: null,
    token: 7,
    start_container: null,
    end_container: null,
    start: 2,
    end: 5
};
let command_plan = plan.make(context, bold, null, { generation: 3 });
let applied = result.applied(true, true, true, false, true, 17, "format",
                             true, "formatBold", null);

{
  canonical: [commands.canonical("BoLd"), commands.canonical("formatBold"),
              commands.canonical("fontName")],
  descriptor: { name: bold.name, family: bold.family, tag: bold.format_tag,
                history: bold.history_class },
  font_name: { name: font_name.name, family: font_name.family,
               tag: font_name.format_tag, attribute: font_name.attribute_name },
  structural: { indent: indent.input_type, clipboard: clipboard.input_type,
                clipboard_family: clipboard.family },
  history: { undo: undo_command.input_type, undo_family: undo_command.family,
             redo: redo_intent.name },
  platform: { replacement: replacement.name, composition: composition.name,
              composition_family: composition.family },
  plan: { family: command_plan.family, step: command_plan.steps[0].kind,
          before: command_plan.selection_before.start,
          history: command_plan.history.class },
  result: { claimed: applied.claimed, changed: applied.changed,
            recorded: applied.history_recorded, history: applied.history_group,
            api_input: applied.api_input, api_type: applied.api_input_type,
            api_data: applied.api_input_data,
            verdict: result.verdict(applied) }
}
