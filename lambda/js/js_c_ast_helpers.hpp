#pragma once

#include "js_transpiler.hpp"

Type* js_set_type_any(JsTranspiler* tp, AnyReason reason);

JsAstNode* alloc_js_ast_node_span(JsTranspiler* tp, JsAstNodeType node_type,
                                  SourceSpan span, size_t size);
JsAstNode* build_js_literal_from_source(JsTranspiler* tp, const char* node_type,
                                        StrView source, SourceSpan span);
JsAstNode* build_js_identifier_from_source(JsTranspiler* tp, StrView source,
                                           SourceSpan span);
JsAstNode* build_js_new_target_from_span(JsTranspiler* tp, SourceSpan span);
JsAstNode* build_js_binding_identifier_from_source(JsTranspiler* tp,
                                                   StrView source,
                                                   SourceSpan span);
JsAstNode* build_js_binary_from_children(JsTranspiler* tp, SourceSpan span,
                                          JsOperator op, JsAstNode* left,
                                          JsAstNode* right);
void refresh_js_binary_type(JsTranspiler* tp, JsBinaryNode* binary);
void refresh_js_assignment_type(JsAssignmentNode* assignment);
void refresh_js_conditional_type(JsTranspiler* tp,
                                 JsConditionalNode* conditional);
JsAstNode* build_js_unary_from_child(JsTranspiler* tp, SourceSpan span,
                                     JsOperator op, JsAstNode* operand,
                                     bool prefix);
JsAstNode* build_js_call_from_children(JsTranspiler* tp, SourceSpan span,
                                        JsAstNode* callee,
                                        JsAstNode* arguments, bool optional);
JsAstNode* build_js_new_from_children(JsTranspiler* tp, SourceSpan span,
                                       JsAstNode* callee, JsAstNode* arguments);
JsAstNode* build_js_regex_from_source(JsTranspiler* tp, StrView source,
                                      SourceSpan span);
JsAstNode* build_js_template_from_source(JsTranspiler* tp, StrView source,
                                         SourceSpan span);
JsAstNode* build_js_template_element_from_source(JsTranspiler* tp,
                                                 StrView source,
                                                 SourceSpan span, bool tail);
JsAstNode* build_js_template_from_parts(JsTranspiler* tp, SourceSpan span,
                                        JsAstNode* parts, uint32_t length);
JsAstNode* build_js_tagged_template_from_children(JsTranspiler* tp,
                                                   SourceSpan span,
                                                   JsAstNode* tag,
                                                   JsAstNode* quasi);
JsAstNode* build_js_await_from_child(JsTranspiler* tp, SourceSpan span,
                                     JsAstNode* argument);
JsAstNode* build_js_yield_from_child(JsTranspiler* tp, SourceSpan span,
                                     JsAstNode* argument, bool delegate);
JsAstNode* build_js_member_from_children(JsTranspiler* tp, SourceSpan span,
                                         JsAstNode* object,
                                         JsAstNode* property, bool computed,
                                         bool optional);
JsAstNode* build_js_array_from_list(JsTranspiler* tp, SourceSpan span,
                                    JsAstNode* elements, uint32_t length);
JsAstNode* build_js_sequence_from_list(JsTranspiler* tp, SourceSpan span,
                                       JsAstNode* expressions, uint32_t length);
JsAstNode* build_js_assignment_from_children(JsTranspiler* tp, SourceSpan span,
                                              JsOperator op, JsAstNode* left,
                                              JsAstNode* right);
JsAstNode* build_js_conditional_from_children(JsTranspiler* tp, SourceSpan span,
                                               JsAstNode* test,
                                               JsAstNode* consequent,
                                               JsAstNode* alternate);
JsAstNode* build_js_property_from_children(JsTranspiler* tp, SourceSpan span,
                                           JsAstNode* key, JsAstNode* value,
                                           bool computed, bool shorthand);
JsAstNode* build_js_spread_from_child(JsTranspiler* tp, SourceSpan span,
                                      JsAstNode* argument);
void mark_js_object_spread(JsTranspiler* tp, JsAstNode* spread);
JsAstNode* build_js_array_hole(JsTranspiler* tp, SourceSpan span);
JsAstNode* build_js_pattern_array_from_list(JsTranspiler* tp, SourceSpan span,
                                             JsAstNode* elements,
                                             uint32_t length);
JsAstNode* build_js_pattern_object_from_list(JsTranspiler* tp, SourceSpan span,
                                              JsAstNode* properties,
                                              uint32_t length);
JsAstNode* build_js_assignment_pattern_from_children(JsTranspiler* tp,
                                                       SourceSpan span,
                                                       JsAstNode* left,
                                                       JsAstNode* right);
JsAstNode* build_js_rest_pattern_from_child(JsTranspiler* tp, SourceSpan span,
                                            JsAstNode* argument, bool property);
JsAstNode* build_js_pattern_property_from_children(JsTranspiler* tp,
                                                    SourceSpan span,
                                                    JsAstNode* key,
                                                    JsAstNode* value,
                                                    bool computed,
                                                    bool shorthand);
JsAstNode* build_js_pattern_hole(JsTranspiler* tp, SourceSpan span);
JsAstNode* build_js_object_from_list(JsTranspiler* tp, SourceSpan span,
                                     JsAstNode* properties, uint32_t length);
JsAstNode* build_js_declarator_from_children(JsTranspiler* tp, SourceSpan span,
                                              JsAstNode* id, JsAstNode* init);
JsAstNode* build_js_declarator_with_type_from_children(JsTranspiler* tp,
                                                        SourceSpan span,
                                                        JsAstNode* id,
                                                        JsAstNode* type_node,
                                                        JsAstNode* init);
JsAstNode* build_js_variable_declaration_from_list(JsTranspiler* tp,
                                                   SourceSpan span,
                                                   JsAstNode* declarations,
                                                   uint32_t length, int kind);
JsAstNode* build_js_block_from_list(JsTranspiler* tp, SourceSpan span,
                                    JsAstNode* statements, uint32_t length);
JsAstNode* build_js_statement_block_from_object(JsTranspiler* tp,
                                                SourceSpan span,
                                                JsAstNode* object);
JsAstNode* build_js_if_from_children(JsTranspiler* tp, SourceSpan span,
                                     JsAstNode* test, JsAstNode* consequent,
                                     JsAstNode* alternate);
JsAstNode* build_js_while_from_children(JsTranspiler* tp, SourceSpan span,
                                         JsAstNode* test, JsAstNode* body);
JsAstNode* build_js_do_while_from_children(JsTranspiler* tp, SourceSpan span,
                                            JsAstNode* body, JsAstNode* test);
JsAstNode* build_js_return_from_child(JsTranspiler* tp, SourceSpan span,
                                      JsAstNode* argument);
JsAstNode* build_js_throw_from_child(JsTranspiler* tp, SourceSpan span,
                                     JsAstNode* argument);
JsAstNode* build_js_break_continue(JsTranspiler* tp, SourceSpan span,
                                   bool is_continue, StrView label);
JsAstNode* build_js_labeled_from_child(JsTranspiler* tp, SourceSpan span,
                                       StrView label, JsAstNode* body);
JsAstNode* build_js_with_from_children(JsTranspiler* tp, SourceSpan span,
                                       JsAstNode* object, JsAstNode* body);
JsAstNode* build_js_expression_statement_from_child(JsTranspiler* tp,
                                                    SourceSpan span,
                                                    JsAstNode* expression);
JsAstNode* build_js_function_from_children(JsTranspiler* tp, SourceSpan span,
                                            JsAstNode* name, JsAstNode* params,
                                            JsAstNode* body, bool async,
                                            bool generator, bool declaration,
                                            bool arrow);
JsAstNode* build_js_function_with_return_type_from_children(
    JsTranspiler* tp, SourceSpan span, JsAstNode* name, JsAstNode* params,
    JsAstNode* body, JsAstNode* return_type, bool async, bool generator,
    bool declaration, bool arrow);
JsAstNode* build_js_parameter_from_children(JsTranspiler* tp, SourceSpan span,
                                             JsAstNode* pattern,
                                             JsAstNode* default_value,
                                             bool optional, bool rest);
JsAstNode* build_js_parameter_with_type_from_children(JsTranspiler* tp,
                                                       SourceSpan span,
                                                       JsAstNode* pattern,
                                                       JsAstNode* type_node,
                                                       JsAstNode* default_value,
                                                       bool optional, bool rest);
JsAstNode* build_js_type_expression_from_children(JsTranspiler* tp,
                                                   SourceSpan span,
                                                   JsAstNode* inner,
                                                   JsAstNode* target_type,
                                                   bool satisfies,
                                                   bool assertion);
JsAstNode* build_js_non_null_from_child(JsTranspiler* tp, SourceSpan span,
                                        JsAstNode* inner);
JsAstNode* build_js_class_body_from_list(JsTranspiler* tp, SourceSpan span,
                                         JsAstNode* members, uint32_t length);
JsAstNode* build_js_class_from_children(JsTranspiler* tp, SourceSpan span,
                                         JsAstNode* name,
                                         JsAstNode* superclass,
                                         JsAstNode* body, bool declaration);
JsAstNode* build_js_method_from_children(JsTranspiler* tp, SourceSpan span,
                                         JsAstNode* key, JsAstNode* params,
                                         JsAstNode* body, uint32_t flags);
JsAstNode* build_js_this_assignment_from_name(JsTranspiler* tp,
                                               SourceSpan span, String* name);
JsAstNode* build_js_field_from_children(JsTranspiler* tp, SourceSpan span,
                                         JsAstNode* key, JsAstNode* value,
                                         uint32_t flags);
JsAstNode* build_js_static_block_from_child(JsTranspiler* tp, SourceSpan span,
                                             JsAstNode* body);
JsAstNode* build_js_for_from_children(JsTranspiler* tp, SourceSpan span,
                                      JsAstNode* init, JsAstNode* test,
                                      JsAstNode* update, JsAstNode* body);
JsAstNode* build_js_for_of_from_children(JsTranspiler* tp, SourceSpan span,
                                         JsAstNode* left, JsAstNode* right,
                                         JsAstNode* body, int kind,
                                         bool declares_binding,
                                         bool is_for_await, bool is_for_in);
JsAstNode* build_js_switch_from_children(JsTranspiler* tp, SourceSpan span,
                                         JsAstNode* discriminant,
                                         JsAstNode* cases, uint32_t length);
JsAstNode* build_js_switch_case_from_children(JsTranspiler* tp, SourceSpan span,
                                              JsAstNode* test,
                                              JsAstNode* consequent,
                                              bool is_default);
JsAstNode* build_js_try_from_children(JsTranspiler* tp, SourceSpan span,
                                      JsAstNode* block, JsAstNode* handler,
                                      JsAstNode* finalizer);
JsAstNode* build_js_finally_block_from_child(JsTranspiler* tp, SourceSpan span,
                                              JsAstNode* body);
JsAstNode* build_js_catch_from_children(JsTranspiler* tp, SourceSpan span,
                                        JsAstNode* parameter, JsAstNode* body);
JsAstNode* build_js_import_specifier_from_children(JsTranspiler* tp,
                                                   SourceSpan span,
                                                   JsAstNode* remote,
                                                   JsAstNode* local);
JsAstNode* build_js_import_from_children(JsTranspiler* tp, SourceSpan span,
                                         JsAstNode* source,
                                         JsAstNode* default_name,
                                         JsAstNode* namespace_name,
                                         JsAstNode* specifiers);
JsAstNode* build_js_export_specifier_from_children(JsTranspiler* tp,
                                                   SourceSpan span,
                                                   JsAstNode* local,
                                                   JsAstNode* export_name);
JsAstNode* build_js_export_from_children(JsTranspiler* tp, SourceSpan span,
                                         JsAstNode* declaration,
                                         JsAstNode* specifiers,
                                         JsAstNode* source, uint32_t flags);
JsAstNode* build_js_object_method_from_children(JsTranspiler* tp,
                                                SourceSpan span,
                                                JsAstNode* key,
                                                JsAstNode* params,
                                                JsAstNode* body,
                                                uint32_t flags);
JsOperator js_unary_operator_from_string(const char* op_str, size_t len);
bool js_ast_statement_list_has_use_strict_directive(JsAstNode* statements);
SourceSpan js_ts_annotation_span(JsTranspiler* tp, SourceSpan type_span);
