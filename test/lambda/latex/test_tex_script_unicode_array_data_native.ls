import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let opts={script_expl3:true,echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let r=profile.preload_expl3_kernel(kernel,opts,20000000)^;
let names=["c__codepoint_category_index_intarray","c__codepoint_uppercase_index_intarray","c__codepoint_lowercase_index_intarray","c__codepoint_category_blocks_intarray","c__codepoint_uppercase_blocks_intarray","c__codepoint_lowercase_blocks_intarray"];
let text=join([for (name in names, value in r.fonts[r.definitions["control:" ++ name].index].format_values) string(value)],",")^ ++ ",";
text
