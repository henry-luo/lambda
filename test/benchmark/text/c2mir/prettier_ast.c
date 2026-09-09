/* Native C2MIR port of text/prettier_ast.ls.
 *
 * The same compact document IR and AST printer, over the same fixture AST. The
 * fixture is embedded as JSON (as awfy/c2mir/json.c embeds its input) and parsed
 * into typed Node structs once; the measured region of the Lambda port excludes
 * its own `input()` call, so this port's one-off parse (well under 1% of the run)
 * is the only accounting difference between them.
 */
extern int printf(const char *, ...);

/* The prettier_ast.json fixture, embedded so the port is self-contained. */
static const char AST_JSON[] = "{\"type\":\"Program\",\"start\":0,\"end\":4938,\"sourceType\":\"module\",\"interpreter\":null,\"body\":[{\"type\":\"VariableDeclaration\",\"start\":0,\"end\":86,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":6,\"end\":85,\"id\":{\"type\":\"Identifier\",\"start\":6,\"end\":21,\"name\":\"DEFAULT_OPTIONS\"},\"init\":{\"type\":\"ObjectExpression\",\"start\":22,\"end\":85,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":23,\"end\":36,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":23,\"end\":33,\"name\":\"printWidth\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":34,\"end\":36,\"value\":80}},{\"type\":\"ObjectProperty\",\"start\":37,\"end\":46,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":37,\"end\":41,\"name\":\"semi\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":42,\"end\":46,\"value\":true}},{\"type\":\"ObjectProperty\",\"start\":47,\"end\":64,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":47,\"end\":58,\"name\":\"singleQuote\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":59,\"end\":64,\"value\":false}},{\"type\":\"ObjectProperty\",\"start\":65,\"end\":84,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":65,\"end\":78,\"name\":\"trailingComma\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":79,\"end\":84,\"value\":\"all\"}}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":87,\"end\":389,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":93,\"end\":388,\"id\":{\"type\":\"Identifier\",\"start\":93,\"end\":99,\"name\":\"BLOCKS\"},\"init\":{\"type\":\"ArrayExpression\",\"start\":100,\"end\":388,\"elements\":[{\"type\":\"ObjectExpression\",\"start\":101,\"end\":152,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":102,\"end\":116,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":102,\"end\":106,\"name\":\"type\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":107,\"end\":116,\"value\":\"heading\"}},{\"type\":\"ObjectProperty\",\"start\":117,\"end\":124,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":117,\"end\":122,\"name\":\"level\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":123,\"end\":124,\"value\":1}},{\"type\":\"ObjectProperty\",\"start\":125,\"end\":151,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":125,\"end\":129,\"name\":\"text\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":130,\"end\":151,\"value\":\"Formatter benchmark\"}}]},{\"type\":\"ObjectExpression\",\"start\":153,\"end\":231,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":154,\"end\":170,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":154,\"end\":158,\"name\":\"type\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":159,\"end\":170,\"value\":\"paragraph\"}},{\"type\":\"ObjectProperty\",\"start\":171,\"end\":230,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":171,\"end\":175,\"name\":\"text\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":176,\"end\":230,\"value\":\"A larger source tree exercises nested AST traversal.\"}}]},{\"type\":\"ObjectExpression\",\"start\":232,\"end\":292,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":233,\"end\":244,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":233,\"end\":237,\"name\":\"type\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":238,\"end\":244,\"value\":\"list\"}},{\"type\":\"ObjectProperty\",\"start\":245,\"end\":291,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":245,\"end\":250,\"name\":\"items\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":251,\"end\":291,\"elements\":[{\"type\":\"StringLiteral\",\"start\":252,\"end\":259,\"value\":\"parse\"},{\"type\":\"StringLiteral\",\"start\":260,\"end\":277,\"value\":\"attach comments\"},{\"type\":\"StringLiteral\",\"start\":278,\"end\":290,\"value\":\"print docs\"}]}}]},{\"type\":\"ObjectExpression\",\"start\":293,\"end\":387,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":294,\"end\":310,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":294,\"end\":298,\"name\":\"type\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":299,\"end\":310,\"value\":\"paragraph\"}},{\"type\":\"ObjectProperty\",\"start\":311,\"end\":323,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":311,\"end\":317,\"name\":\"hidden\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":318,\"end\":323,\"value\":false}},{\"type\":\"ObjectProperty\",\"start\":324,\"end\":386,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":324,\"end\":328,\"name\":\"text\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":329,\"end\":386,\"value\":\"The output must be stable across JavaScript and Lambda.\"}}]}]}}],\"kind\":\"const\"},{\"type\":\"FunctionDeclaration\",\"start\":390,\"end\":464,\"id\":{\"type\":\"Identifier\",\"start\":399,\"end\":411,\"name\":\"cloneOptions\"},\"generator\":false,\"async\":false,\"params\":[{\"type\":\"AssignmentPattern\",\"start\":412,\"end\":422,\"left\":{\"type\":\"Identifier\",\"start\":412,\"end\":419,\"name\":\"options\"},\"right\":{\"type\":\"ObjectExpression\",\"start\":420,\"end\":422,\"properties\":[]}}],\"body\":{\"type\":\"BlockStatement\",\"start\":423,\"end\":464,\"body\":[{\"type\":\"ReturnStatement\",\"start\":424,\"end\":463,\"argument\":{\"type\":\"ObjectExpression\",\"start\":431,\"end\":462,\"properties\":[{\"type\":\"SpreadElement\",\"start\":432,\"end\":450,\"argument\":{\"type\":\"Identifier\",\"start\":435,\"end\":450,\"name\":\"DEFAULT_OPTIONS\"}},{\"type\":\"SpreadElement\",\"start\":451,\"end\":461,\"argument\":{\"type\":\"Identifier\",\"start\":454,\"end\":461,\"name\":\"options\"}}]}}],\"directives\":[]}},{\"type\":\"FunctionDeclaration\",\"start\":465,\"end\":536,\"id\":{\"type\":\"Identifier\",\"start\":474,\"end\":487,\"name\":\"normalizeText\"},\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":488,\"end\":493,\"name\":\"value\"}],\"body\":{\"type\":\"BlockStatement\",\"start\":494,\"end\":536,\"body\":[{\"type\":\"ReturnStatement\",\"start\":495,\"end\":535,\"argument\":{\"type\":\"CallExpression\",\"start\":502,\"end\":534,\"callee\":{\"type\":\"MemberExpression\",\"start\":502,\"end\":522,\"object\":{\"type\":\"CallExpression\",\"start\":502,\"end\":514,\"callee\":{\"type\":\"MemberExpression\",\"start\":502,\"end\":512,\"object\":{\"type\":\"Identifier\",\"start\":502,\"end\":507,\"name\":\"value\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":508,\"end\":512,\"name\":\"trim\"}},\"arguments\":[]},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":515,\"end\":522,\"name\":\"replace\"}},\"arguments\":[{\"type\":\"RegExpLiteral\",\"start\":523,\"end\":529,\"pattern\":\"\\\\s+\",\"flags\":\"g\"},{\"type\":\"StringLiteral\",\"start\":530,\"end\":533,\"value\":\" \"}]}}],\"directives\":[]}},{\"type\":\"FunctionDeclaration\",\"start\":537,\"end\":601,\"id\":{\"type\":\"Identifier\",\"start\":546,\"end\":555,\"name\":\"isVisible\"},\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":556,\"end\":560,\"name\":\"node\"}],\"body\":{\"type\":\"BlockStatement\",\"start\":561,\"end\":601,\"body\":[{\"type\":\"ReturnStatement\",\"start\":562,\"end\":600,\"argument\":{\"type\":\"LogicalExpression\",\"start\":569,\"end\":599,\"left\":{\"type\":\"BinaryExpression\",\"start\":569,\"end\":579,\"left\":{\"type\":\"Identifier\",\"start\":569,\"end\":573,\"name\":\"node\"},\"operator\":\"!=\",\"right\":{\"type\":\"NullLiteral\",\"start\":575,\"end\":579}},\"operator\":\"&&\",\"right\":{\"type\":\"BinaryExpression\",\"start\":581,\"end\":599,\"left\":{\"type\":\"MemberExpression\",\"start\":581,\"end\":592,\"object\":{\"type\":\"Identifier\",\"start\":581,\"end\":585,\"name\":\"node\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":586,\"end\":592,\"name\":\"hidden\"}},\"operator\":\"!==\",\"right\":{\"type\":\"BooleanLiteral\",\"start\":595,\"end\":599,\"value\":true}}}}],\"directives\":[]}},{\"type\":\"FunctionDeclaration\",\"start\":602,\"end\":730,\"id\":{\"type\":\"Identifier\",\"start\":611,\"end\":623,\"name\":\"renderInline\"},\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":624,\"end\":628,\"name\":\"node\"},{\"type\":\"Identifier\",\"start\":629,\"end\":634,\"name\":\"index\"}],\"body\":{\"type\":\"BlockStatement\",\"start\":635,\"end\":730,\"body\":[{\"type\":\"VariableDeclaration\",\"start\":636,\"end\":676,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":642,\"end\":675,\"id\":{\"type\":\"Identifier\",\"start\":642,\"end\":646,\"name\":\"text\"},\"init\":{\"type\":\"CallExpression\",\"start\":647,\"end\":675,\"callee\":{\"type\":\"Identifier\",\"start\":647,\"end\":660,\"name\":\"normalizeText\"},\"arguments\":[{\"type\":\"LogicalExpression\",\"start\":661,\"end\":674,\"left\":{\"type\":\"MemberExpression\",\"start\":661,\"end\":670,\"object\":{\"type\":\"Identifier\",\"start\":661,\"end\":665,\"name\":\"node\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":666,\"end\":670,\"name\":\"text\"}},\"operator\":\"||\",\"right\":{\"type\":\"StringLiteral\",\"start\":672,\"end\":674,\"value\":\"\"}}]}}],\"kind\":\"const\"},{\"type\":\"ReturnStatement\",\"start\":676,\"end\":729,\"argument\":{\"type\":\"ObjectExpression\",\"start\":683,\"end\":728,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":684,\"end\":695,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":684,\"end\":688,\"name\":\"kind\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":689,\"end\":695,\"value\":\"text\"}},{\"type\":\"ObjectProperty\",\"start\":696,\"end\":701,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":696,\"end\":701,\"name\":\"index\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":696,\"end\":701,\"name\":\"index\"}},{\"type\":\"ObjectProperty\",\"start\":702,\"end\":706,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":702,\"end\":706,\"name\":\"text\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":702,\"end\":706,\"name\":\"text\"}},{\"type\":\"ObjectProperty\",\"start\":707,\"end\":727,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":707,\"end\":712,\"name\":\"marks\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"LogicalExpression\",\"start\":713,\"end\":727,\"left\":{\"type\":\"MemberExpression\",\"start\":713,\"end\":723,\"object\":{\"type\":\"Identifier\",\"start\":713,\"end\":717,\"name\":\"node\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":718,\"end\":723,\"name\":\"marks\"}},\"operator\":\"||\",\"right\":{\"type\":\"ArrayExpression\",\"start\":725,\"end\":727,\"elements\":[]}}}]}}],\"directives\":[]}},{\"type\":\"FunctionDeclaration\",\"start\":731,\"end\":1151,\"id\":{\"type\":\"Identifier\",\"start\":740,\"end\":751,\"name\":\"renderBlock\"},\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":752,\"end\":756,\"name\":\"node\"},{\"type\":\"Identifier\",\"start\":757,\"end\":762,\"name\":\"index\"},{\"type\":\"Identifier\",\"start\":763,\"end\":770,\"name\":\"context\"}],\"body\":{\"type\":\"BlockStatement\",\"start\":771,\"end\":1151,\"body\":[{\"type\":\"IfStatement\",\"start\":773,\"end\":805,\"test\":{\"type\":\"UnaryExpression\",\"start\":776,\"end\":792,\"operator\":\"!\",\"prefix\":true,\"argument\":{\"type\":\"CallExpression\",\"start\":777,\"end\":792,\"callee\":{\"type\":\"Identifier\",\"start\":777,\"end\":786,\"name\":\"isVisible\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":787,\"end\":791,\"name\":\"node\"}]}},\"consequent\":{\"type\":\"ReturnStatement\",\"start\":793,\"end\":805,\"argument\":{\"type\":\"NullLiteral\",\"start\":800,\"end\":804}},\"alternate\":null},{\"type\":\"IfStatement\",\"start\":806,\"end\":903,\"test\":{\"type\":\"BinaryExpression\",\"start\":809,\"end\":830,\"left\":{\"type\":\"MemberExpression\",\"start\":809,\"end\":818,\"object\":{\"type\":\"Identifier\",\"start\":809,\"end\":813,\"name\":\"node\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":814,\"end\":818,\"name\":\"type\"}},\"operator\":\"===\",\"right\":{\"type\":\"StringLiteral\",\"start\":821,\"end\":830,\"value\":\"heading\"}},\"consequent\":{\"type\":\"ReturnStatement\",\"start\":831,\"end\":903,\"argument\":{\"type\":\"ObjectExpression\",\"start\":838,\"end\":902,\"properties\":[{\"type\":\"SpreadElement\",\"start\":839,\"end\":866,\"argument\":{\"type\":\"CallExpression\",\"start\":842,\"end\":866,\"callee\":{\"type\":\"Identifier\",\"start\":842,\"end\":854,\"name\":\"renderInline\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":855,\"end\":859,\"name\":\"node\"},{\"type\":\"Identifier\",\"start\":860,\"end\":865,\"name\":\"index\"}]}},{\"type\":\"ObjectProperty\",\"start\":867,\"end\":886,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":867,\"end\":872,\"name\":\"level\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"LogicalExpression\",\"start\":873,\"end\":886,\"left\":{\"type\":\"MemberExpression\",\"start\":873,\"end\":883,\"object\":{\"type\":\"Identifier\",\"start\":873,\"end\":877,\"name\":\"node\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":878,\"end\":883,\"name\":\"level\"}},\"operator\":\"||\",\"right\":{\"type\":\"NumericLiteral\",\"start\":885,\"end\":886,\"value\":1}}},{\"type\":\"ObjectProperty\",\"start\":887,\"end\":901,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":887,\"end\":891,\"name\":\"kind\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":892,\"end\":901,\"value\":\"heading\"}}]}},\"alternate\":null},{\"type\":\"IfStatement\",\"start\":904,\"end\":985,\"test\":{\"type\":\"BinaryExpression\",\"start\":907,\"end\":930,\"left\":{\"type\":\"MemberExpression\",\"start\":907,\"end\":916,\"object\":{\"type\":\"Identifier\",\"start\":907,\"end\":911,\"name\":\"node\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":912,\"end\":916,\"name\":\"type\"}},\"operator\":\"===\",\"right\":{\"type\":\"StringLiteral\",\"start\":919,\"end\":930,\"value\":\"paragraph\"}},\"consequent\":{\"type\":\"ReturnStatement\",\"start\":931,\"end\":985,\"argument\":{\"type\":\"ObjectExpression\",\"start\":938,\"end\":984,\"properties\":[{\"type\":\"SpreadElement\",\"start\":939,\"end\":966,\"argument\":{\"type\":\"CallExpression\",\"start\":942,\"end\":966,\"callee\":{\"type\":\"Identifier\",\"start\":942,\"end\":954,\"name\":\"renderInline\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":955,\"end\":959,\"name\":\"node\"},{\"type\":\"Identifier\",\"start\":960,\"end\":965,\"name\":\"index\"}]}},{\"type\":\"ObjectProperty\",\"start\":967,\"end\":983,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":967,\"end\":971,\"name\":\"kind\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":972,\"end\":983,\"value\":\"paragraph\"}}]}},\"alternate\":null},{\"type\":\"IfStatement\",\"start\":986,\"end\":1113,\"test\":{\"type\":\"BinaryExpression\",\"start\":989,\"end\":1007,\"left\":{\"type\":\"MemberExpression\",\"start\":989,\"end\":998,\"object\":{\"type\":\"Identifier\",\"start\":989,\"end\":993,\"name\":\"node\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":994,\"end\":998,\"name\":\"type\"}},\"operator\":\"===\",\"right\":{\"type\":\"StringLiteral\",\"start\":1001,\"end\":1007,\"value\":\"list\"}},\"consequent\":{\"type\":\"ReturnStatement\",\"start\":1008,\"end\":1113,\"argument\":{\"type\":\"ObjectExpression\",\"start\":1015,\"end\":1112,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":1016,\"end\":1027,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1016,\"end\":1020,\"name\":\"kind\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":1021,\"end\":1027,\"value\":\"list\"}},{\"type\":\"ObjectProperty\",\"start\":1028,\"end\":1103,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1028,\"end\":1033,\"name\":\"items\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"CallExpression\",\"start\":1034,\"end\":1103,\"callee\":{\"type\":\"MemberExpression\",\"start\":1034,\"end\":1048,\"object\":{\"type\":\"MemberExpression\",\"start\":1034,\"end\":1044,\"object\":{\"type\":\"Identifier\",\"start\":1034,\"end\":1038,\"name\":\"node\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1039,\"end\":1044,\"name\":\"items\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1045,\"end\":1048,\"name\":\"map\"}},\"arguments\":[{\"type\":\"ArrowFunctionExpression\",\"start\":1049,\"end\":1102,\"id\":null,\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":1050,\"end\":1054,\"name\":\"item\"},{\"type\":\"Identifier\",\"start\":1055,\"end\":1064,\"name\":\"itemIndex\"}],\"body\":{\"type\":\"CallExpression\",\"start\":1067,\"end\":1102,\"callee\":{\"type\":\"Identifier\",\"start\":1067,\"end\":1079,\"name\":\"renderInline\"},\"arguments\":[{\"type\":\"ObjectExpression\",\"start\":1080,\"end\":1091,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":1081,\"end\":1090,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1081,\"end\":1085,\"name\":\"text\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"Identifier\",\"start\":1086,\"end\":1090,\"name\":\"item\"}}]},{\"type\":\"Identifier\",\"start\":1092,\"end\":1101,\"name\":\"itemIndex\"}]}}]}},{\"type\":\"ObjectProperty\",\"start\":1104,\"end\":1111,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1104,\"end\":1111,\"name\":\"context\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":1104,\"end\":1111,\"name\":\"context\"}}]}},\"alternate\":null},{\"type\":\"ReturnStatement\",\"start\":1114,\"end\":1149,\"argument\":{\"type\":\"ObjectExpression\",\"start\":1121,\"end\":1148,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":1122,\"end\":1136,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1122,\"end\":1126,\"name\":\"kind\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":1127,\"end\":1136,\"value\":\"unknown\"}},{\"type\":\"ObjectProperty\",\"start\":1137,\"end\":1147,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1137,\"end\":1142,\"name\":\"value\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"Identifier\",\"start\":1143,\"end\":1147,\"name\":\"node\"}}]}}],\"directives\":[]}},{\"type\":\"FunctionDeclaration\",\"start\":1152,\"end\":1448,\"id\":{\"type\":\"Identifier\",\"start\":1161,\"end\":1175,\"name\":\"renderDocument\"},\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":1176,\"end\":1184,\"name\":\"document\"},{\"type\":\"Identifier\",\"start\":1185,\"end\":1192,\"name\":\"options\"}],\"body\":{\"type\":\"BlockStatement\",\"start\":1193,\"end\":1448,\"body\":[{\"type\":\"VariableDeclaration\",\"start\":1195,\"end\":1230,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":1201,\"end\":1229,\"id\":{\"type\":\"Identifier\",\"start\":1201,\"end\":1207,\"name\":\"config\"},\"init\":{\"type\":\"CallExpression\",\"start\":1208,\"end\":1229,\"callee\":{\"type\":\"Identifier\",\"start\":1208,\"end\":1220,\"name\":\"cloneOptions\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":1221,\"end\":1228,\"name\":\"options\"}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":1231,\"end\":1247,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":1237,\"end\":1246,\"id\":{\"type\":\"Identifier\",\"start\":1237,\"end\":1243,\"name\":\"result\"},\"init\":{\"type\":\"ArrayExpression\",\"start\":1244,\"end\":1246,\"elements\":[]}}],\"kind\":\"const\"},{\"type\":\"ForOfStatement\",\"start\":1248,\"end\":1394,\"await\":false,\"left\":{\"type\":\"VariableDeclaration\",\"start\":1252,\"end\":1270,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":1258,\"end\":1270,\"id\":{\"type\":\"ArrayPattern\",\"start\":1258,\"end\":1270,\"elements\":[{\"type\":\"Identifier\",\"start\":1259,\"end\":1264,\"name\":\"index\"},{\"type\":\"Identifier\",\"start\":1265,\"end\":1269,\"name\":\"node\"}]},\"init\":null}],\"kind\":\"const\"},\"right\":{\"type\":\"CallExpression\",\"start\":1274,\"end\":1299,\"callee\":{\"type\":\"MemberExpression\",\"start\":1274,\"end\":1297,\"object\":{\"type\":\"MemberExpression\",\"start\":1274,\"end\":1289,\"object\":{\"type\":\"Identifier\",\"start\":1274,\"end\":1282,\"name\":\"document\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1283,\"end\":1289,\"name\":\"blocks\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1290,\"end\":1297,\"name\":\"entries\"}},\"arguments\":[]},\"body\":{\"type\":\"BlockStatement\",\"start\":1300,\"end\":1394,\"body\":[{\"type\":\"VariableDeclaration\",\"start\":1302,\"end\":1350,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":1308,\"end\":1349,\"id\":{\"type\":\"Identifier\",\"start\":1308,\"end\":1316,\"name\":\"rendered\"},\"init\":{\"type\":\"CallExpression\",\"start\":1317,\"end\":1349,\"callee\":{\"type\":\"Identifier\",\"start\":1317,\"end\":1328,\"name\":\"renderBlock\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":1329,\"end\":1333,\"name\":\"node\"},{\"type\":\"Identifier\",\"start\":1334,\"end\":1339,\"name\":\"index\"},{\"type\":\"ObjectExpression\",\"start\":1340,\"end\":1348,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":1341,\"end\":1347,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1341,\"end\":1347,\"name\":\"config\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":1341,\"end\":1347,\"name\":\"config\"}}]}]}}],\"kind\":\"const\"},{\"type\":\"IfStatement\",\"start\":1351,\"end\":1392,\"test\":{\"type\":\"BinaryExpression\",\"start\":1354,\"end\":1369,\"left\":{\"type\":\"Identifier\",\"start\":1354,\"end\":1362,\"name\":\"rendered\"},\"operator\":\"!==\",\"right\":{\"type\":\"NullLiteral\",\"start\":1365,\"end\":1369}},\"consequent\":{\"type\":\"ExpressionStatement\",\"start\":1370,\"end\":1392,\"expression\":{\"type\":\"CallExpression\",\"start\":1370,\"end\":1391,\"callee\":{\"type\":\"MemberExpression\",\"start\":1370,\"end\":1381,\"object\":{\"type\":\"Identifier\",\"start\":1370,\"end\":1376,\"name\":\"result\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1377,\"end\":1381,\"name\":\"push\"}},\"arguments\":[{\"type\":\"Identifier\",\"start\":1382,\"end\":1390,\"name\":\"rendered\"}]}},\"alternate\":null}],\"directives\":[]}},{\"type\":\"ReturnStatement\",\"start\":1395,\"end\":1446,\"argument\":{\"type\":\"ObjectExpression\",\"start\":1402,\"end\":1445,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":1403,\"end\":1423,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1403,\"end\":1408,\"name\":\"title\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":1409,\"end\":1423,\"object\":{\"type\":\"Identifier\",\"start\":1409,\"end\":1417,\"name\":\"document\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1418,\"end\":1423,\"name\":\"title\"}}},{\"type\":\"ObjectProperty\",\"start\":1424,\"end\":1430,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1424,\"end\":1430,\"name\":\"config\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":1424,\"end\":1430,\"name\":\"config\"}},{\"type\":\"ObjectProperty\",\"start\":1431,\"end\":1444,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1431,\"end\":1437,\"name\":\"blocks\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"Identifier\",\"start\":1438,\"end\":1444,\"name\":\"result\"}}]}}],\"directives\":[]}},{\"type\":\"ClassDeclaration\",\"start\":1449,\"end\":1773,\"id\":{\"type\":\"Identifier\",\"start\":1455,\"end\":1468,\"name\":\"SourcePrinter\"},\"superClass\":null,\"body\":{\"type\":\"ClassBody\",\"start\":1468,\"end\":1773,\"body\":[{\"type\":\"ClassMethod\",\"start\":1470,\"end\":1530,\"static\":false,\"key\":{\"type\":\"Identifier\",\"start\":1470,\"end\":1481,\"name\":\"constructor\"},\"computed\":false,\"kind\":\"constructor\",\"id\":null,\"generator\":false,\"async\":false,\"params\":[{\"type\":\"AssignmentPattern\",\"start\":1482,\"end\":1492,\"left\":{\"type\":\"Identifier\",\"start\":1482,\"end\":1489,\"name\":\"options\"},\"right\":{\"type\":\"ObjectExpression\",\"start\":1490,\"end\":1492,\"properties\":[]}}],\"body\":{\"type\":\"BlockStatement\",\"start\":1493,\"end\":1530,\"body\":[{\"type\":\"ExpressionStatement\",\"start\":1494,\"end\":1529,\"expression\":{\"type\":\"AssignmentExpression\",\"start\":1494,\"end\":1528,\"operator\":\"=\",\"left\":{\"type\":\"MemberExpression\",\"start\":1494,\"end\":1506,\"object\":{\"type\":\"ThisExpression\",\"start\":1494,\"end\":1498},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1499,\"end\":1506,\"name\":\"options\"}},\"right\":{\"type\":\"CallExpression\",\"start\":1507,\"end\":1528,\"callee\":{\"type\":\"Identifier\",\"start\":1507,\"end\":1519,\"name\":\"cloneOptions\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":1520,\"end\":1527,\"name\":\"options\"}]}}}],\"directives\":[]}},{\"type\":\"ClassMethod\",\"start\":1531,\"end\":1771,\"static\":false,\"key\":{\"type\":\"Identifier\",\"start\":1531,\"end\":1536,\"name\":\"print\"},\"computed\":false,\"kind\":\"method\",\"id\":null,\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":1537,\"end\":1545,\"name\":\"document\"}],\"body\":{\"type\":\"BlockStatement\",\"start\":1546,\"end\":1771,\"body\":[{\"type\":\"VariableDeclaration\",\"start\":1548,\"end\":1599,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":1554,\"end\":1598,\"id\":{\"type\":\"Identifier\",\"start\":1554,\"end\":1560,\"name\":\"output\"},\"init\":{\"type\":\"CallExpression\",\"start\":1561,\"end\":1598,\"callee\":{\"type\":\"Identifier\",\"start\":1561,\"end\":1575,\"name\":\"renderDocument\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":1576,\"end\":1584,\"name\":\"document\"},{\"type\":\"MemberExpression\",\"start\":1585,\"end\":1597,\"object\":{\"type\":\"ThisExpression\",\"start\":1585,\"end\":1589},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1590,\"end\":1597,\"name\":\"options\"}}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":1600,\"end\":1615,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":1606,\"end\":1614,\"id\":{\"type\":\"Identifier\",\"start\":1606,\"end\":1611,\"name\":\"lines\"},\"init\":{\"type\":\"ArrayExpression\",\"start\":1612,\"end\":1614,\"elements\":[]}}],\"kind\":\"const\"},{\"type\":\"ForOfStatement\",\"start\":1616,\"end\":1744,\"await\":false,\"left\":{\"type\":\"VariableDeclaration\",\"start\":1620,\"end\":1639,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":1626,\"end\":1639,\"id\":{\"type\":\"ArrayPattern\",\"start\":1626,\"end\":1639,\"elements\":[{\"type\":\"Identifier\",\"start\":1627,\"end\":1632,\"name\":\"index\"},{\"type\":\"Identifier\",\"start\":1633,\"end\":1638,\"name\":\"block\"}]},\"init\":null}],\"kind\":\"const\"},\"right\":{\"type\":\"CallExpression\",\"start\":1643,\"end\":1666,\"callee\":{\"type\":\"MemberExpression\",\"start\":1643,\"end\":1664,\"object\":{\"type\":\"MemberExpression\",\"start\":1643,\"end\":1656,\"object\":{\"type\":\"Identifier\",\"start\":1643,\"end\":1649,\"name\":\"output\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1650,\"end\":1656,\"name\":\"blocks\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1657,\"end\":1664,\"name\":\"entries\"}},\"arguments\":[]},\"body\":{\"type\":\"BlockStatement\",\"start\":1667,\"end\":1744,\"body\":[{\"type\":\"ExpressionStatement\",\"start\":1669,\"end\":1742,\"expression\":{\"type\":\"CallExpression\",\"start\":1669,\"end\":1741,\"callee\":{\"type\":\"MemberExpression\",\"start\":1669,\"end\":1679,\"object\":{\"type\":\"Identifier\",\"start\":1669,\"end\":1674,\"name\":\"lines\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1675,\"end\":1679,\"name\":\"push\"}},\"arguments\":[{\"type\":\"BinaryExpression\",\"start\":1680,\"end\":1740,\"left\":{\"type\":\"BinaryExpression\",\"start\":1680,\"end\":1710,\"left\":{\"type\":\"BinaryExpression\",\"start\":1680,\"end\":1705,\"left\":{\"type\":\"BinaryExpression\",\"start\":1680,\"end\":1694,\"left\":{\"type\":\"BinaryExpression\",\"start\":1681,\"end\":1688,\"left\":{\"type\":\"Identifier\",\"start\":1681,\"end\":1686,\"name\":\"index\"},\"operator\":\"+\",\"right\":{\"type\":\"NumericLiteral\",\"start\":1687,\"end\":1688,\"value\":1}},\"operator\":\"+\",\"right\":{\"type\":\"StringLiteral\",\"start\":1690,\"end\":1694,\"value\":\". \"}},\"operator\":\"+\",\"right\":{\"type\":\"MemberExpression\",\"start\":1695,\"end\":1705,\"object\":{\"type\":\"Identifier\",\"start\":1695,\"end\":1700,\"name\":\"block\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1701,\"end\":1705,\"name\":\"kind\"}}},\"operator\":\"+\",\"right\":{\"type\":\"StringLiteral\",\"start\":1706,\"end\":1710,\"value\":\": \"}},\"operator\":\"+\",\"right\":{\"type\":\"LogicalExpression\",\"start\":1712,\"end\":1739,\"left\":{\"type\":\"LogicalExpression\",\"start\":1712,\"end\":1735,\"left\":{\"type\":\"MemberExpression\",\"start\":1712,\"end\":1722,\"object\":{\"type\":\"Identifier\",\"start\":1712,\"end\":1717,\"name\":\"block\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1718,\"end\":1722,\"name\":\"text\"}},\"operator\":\"||\",\"right\":{\"type\":\"MemberExpression\",\"start\":1724,\"end\":1735,\"object\":{\"type\":\"Identifier\",\"start\":1724,\"end\":1729,\"name\":\"block\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1730,\"end\":1735,\"name\":\"value\"}}},\"operator\":\"||\",\"right\":{\"type\":\"StringLiteral\",\"start\":1737,\"end\":1739,\"value\":\"\"}}}]}}],\"directives\":[]}},{\"type\":\"ReturnStatement\",\"start\":1745,\"end\":1769,\"argument\":{\"type\":\"CallExpression\",\"start\":1752,\"end\":1768,\"callee\":{\"type\":\"MemberExpression\",\"start\":1752,\"end\":1762,\"object\":{\"type\":\"Identifier\",\"start\":1752,\"end\":1757,\"name\":\"lines\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1758,\"end\":1762,\"name\":\"join\"}},\"arguments\":[{\"type\":\"StringLiteral\",\"start\":1763,\"end\":1767,\"value\":\"\\n\"}]}}],\"directives\":[]}}]}},{\"type\":\"FunctionDeclaration\",\"start\":1774,\"end\":1870,\"id\":{\"type\":\"Identifier\",\"start\":1783,\"end\":1797,\"name\":\"formatDocument\"},\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":1798,\"end\":1806,\"name\":\"document\"},{\"type\":\"AssignmentPattern\",\"start\":1807,\"end\":1817,\"left\":{\"type\":\"Identifier\",\"start\":1807,\"end\":1814,\"name\":\"options\"},\"right\":{\"type\":\"ObjectExpression\",\"start\":1815,\"end\":1817,\"properties\":[]}}],\"body\":{\"type\":\"BlockStatement\",\"start\":1818,\"end\":1870,\"body\":[{\"type\":\"ReturnStatement\",\"start\":1819,\"end\":1869,\"argument\":{\"type\":\"CallExpression\",\"start\":1826,\"end\":1868,\"callee\":{\"type\":\"MemberExpression\",\"start\":1826,\"end\":1858,\"object\":{\"type\":\"NewExpression\",\"start\":1826,\"end\":1852,\"callee\":{\"type\":\"Identifier\",\"start\":1830,\"end\":1843,\"name\":\"SourcePrinter\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":1844,\"end\":1851,\"name\":\"options\"}]},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":1853,\"end\":1858,\"name\":\"print\"}},\"arguments\":[{\"type\":\"Identifier\",\"start\":1859,\"end\":1867,\"name\":\"document\"}]}}],\"directives\":[]}},{\"type\":\"VariableDeclaration\",\"start\":1871,\"end\":1930,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":1877,\"end\":1929,\"id\":{\"type\":\"Identifier\",\"start\":1877,\"end\":1885,\"name\":\"document\"},\"init\":{\"type\":\"ObjectExpression\",\"start\":1886,\"end\":1929,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":1887,\"end\":1914,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1887,\"end\":1892,\"name\":\"title\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":1893,\"end\":1914,\"value\":\"Formatter benchmark\"}},{\"type\":\"ObjectProperty\",\"start\":1915,\"end\":1928,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1915,\"end\":1921,\"name\":\"blocks\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"Identifier\",\"start\":1922,\"end\":1928,\"name\":\"BLOCKS\"}}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":1931,\"end\":1989,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":1937,\"end\":1988,\"id\":{\"type\":\"Identifier\",\"start\":1937,\"end\":1954,\"name\":\"configuredOptions\"},\"init\":{\"type\":\"ObjectExpression\",\"start\":1955,\"end\":1988,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":1956,\"end\":1970,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1956,\"end\":1966,\"name\":\"printWidth\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":1967,\"end\":1970,\"value\":100}},{\"type\":\"ObjectProperty\",\"start\":1971,\"end\":1987,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":1971,\"end\":1982,\"name\":\"singleQuote\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":1983,\"end\":1987,\"value\":true}}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":1990,\"end\":2050,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":1996,\"end\":2049,\"id\":{\"type\":\"Identifier\",\"start\":1996,\"end\":2006,\"name\":\"configured\"},\"init\":{\"type\":\"CallExpression\",\"start\":2007,\"end\":2049,\"callee\":{\"type\":\"Identifier\",\"start\":2007,\"end\":2021,\"name\":\"formatDocument\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":2022,\"end\":2030,\"name\":\"document\"},{\"type\":\"Identifier\",\"start\":2031,\"end\":2048,\"name\":\"configuredOptions\"}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":2051,\"end\":2091,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":2057,\"end\":2090,\"id\":{\"type\":\"Identifier\",\"start\":2057,\"end\":2065,\"name\":\"baseline\"},\"init\":{\"type\":\"CallExpression\",\"start\":2066,\"end\":2090,\"callee\":{\"type\":\"Identifier\",\"start\":2066,\"end\":2080,\"name\":\"formatDocument\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":2081,\"end\":2089,\"name\":\"document\"}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":2092,\"end\":2202,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":2098,\"end\":2201,\"id\":{\"type\":\"Identifier\",\"start\":2098,\"end\":2104,\"name\":\"report\"},\"init\":{\"type\":\"ObjectExpression\",\"start\":2105,\"end\":2201,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2106,\"end\":2116,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2106,\"end\":2116,\"name\":\"configured\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":2106,\"end\":2116,\"name\":\"configured\"}},{\"type\":\"ObjectProperty\",\"start\":2117,\"end\":2125,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2117,\"end\":2125,\"name\":\"baseline\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":2117,\"end\":2125,\"name\":\"baseline\"}},{\"type\":\"ObjectProperty\",\"start\":2126,\"end\":2159,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2126,\"end\":2136,\"name\":\"blockCount\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":2137,\"end\":2159,\"object\":{\"type\":\"MemberExpression\",\"start\":2137,\"end\":2152,\"object\":{\"type\":\"Identifier\",\"start\":2137,\"end\":2145,\"name\":\"document\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":2146,\"end\":2152,\"name\":\"blocks\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":2153,\"end\":2159,\"name\":\"length\"}}},{\"type\":\"ObjectProperty\",\"start\":2160,\"end\":2200,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2160,\"end\":2172,\"name\":\"visibleCount\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":2173,\"end\":2200,\"object\":{\"type\":\"CallExpression\",\"start\":2173,\"end\":2193,\"callee\":{\"type\":\"MemberExpression\",\"start\":2173,\"end\":2187,\"object\":{\"type\":\"Identifier\",\"start\":2173,\"end\":2181,\"name\":\"baseline\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":2182,\"end\":2187,\"name\":\"split\"}},\"arguments\":[{\"type\":\"StringLiteral\",\"start\":2188,\"end\":2192,\"value\":\"\\n\"}]},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":2194,\"end\":2200,\"name\":\"length\"}}}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":2203,\"end\":3023,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":2209,\"end\":3022,\"id\":{\"type\":\"Identifier\",\"start\":2209,\"end\":2218,\"name\":\"DEEP_TREE\"},\"init\":{\"type\":\"ObjectExpression\",\"start\":2219,\"end\":3022,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2220,\"end\":2331,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2220,\"end\":2224,\"name\":\"meta\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2225,\"end\":2331,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2226,\"end\":2247,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2226,\"end\":2230,\"name\":\"name\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":2231,\"end\":2247,\"value\":\"nested-fixture\"}},{\"type\":\"ObjectProperty\",\"start\":2248,\"end\":2257,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2248,\"end\":2255,\"name\":\"version\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":2256,\"end\":2257,\"value\":3}},{\"type\":\"ObjectProperty\",\"start\":2258,\"end\":2296,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2258,\"end\":2263,\"name\":\"flags\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2264,\"end\":2296,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2265,\"end\":2276,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2265,\"end\":2271,\"name\":\"stable\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":2272,\"end\":2276,\"value\":true}},{\"type\":\"ObjectProperty\",\"start\":2277,\"end\":2295,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2277,\"end\":2289,\"name\":\"experimental\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":2290,\"end\":2295,\"value\":false}}]}},{\"type\":\"ObjectProperty\",\"start\":2297,\"end\":2330,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2297,\"end\":2301,\"name\":\"path\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2302,\"end\":2330,\"elements\":[{\"type\":\"StringLiteral\",\"start\":2303,\"end\":2309,\"value\":\"root\"},{\"type\":\"StringLiteral\",\"start\":2310,\"end\":2319,\"value\":\"content\"},{\"type\":\"StringLiteral\",\"start\":2320,\"end\":2329,\"value\":\"records\"}]}}]}},{\"type\":\"ObjectProperty\",\"start\":2332,\"end\":2487,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2332,\"end\":2338,\"name\":\"layer0\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2339,\"end\":2487,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2340,\"end\":2486,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2340,\"end\":2346,\"name\":\"layer1\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2347,\"end\":2486,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2348,\"end\":2485,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2348,\"end\":2354,\"name\":\"layer2\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2355,\"end\":2485,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2356,\"end\":2484,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2356,\"end\":2362,\"name\":\"layer3\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2363,\"end\":2484,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2364,\"end\":2483,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2364,\"end\":2370,\"name\":\"layer4\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2371,\"end\":2483,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2372,\"end\":2482,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2372,\"end\":2378,\"name\":\"layer5\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2379,\"end\":2482,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2380,\"end\":2481,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2380,\"end\":2386,\"name\":\"layer6\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2387,\"end\":2481,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2388,\"end\":2480,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2388,\"end\":2394,\"name\":\"layer7\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2395,\"end\":2480,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2396,\"end\":2479,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2396,\"end\":2402,\"name\":\"layer8\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2403,\"end\":2479,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2404,\"end\":2478,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2404,\"end\":2410,\"name\":\"layer9\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2411,\"end\":2478,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2412,\"end\":2477,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2412,\"end\":2419,\"name\":\"layer10\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2420,\"end\":2477,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2421,\"end\":2476,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2421,\"end\":2428,\"name\":\"layer11\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2429,\"end\":2476,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2430,\"end\":2475,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2430,\"end\":2434,\"name\":\"leaf\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2435,\"end\":2475,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2436,\"end\":2451,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2436,\"end\":2440,\"name\":\"kind\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":2441,\"end\":2451,\"value\":\"terminal\"}},{\"type\":\"ObjectProperty\",\"start\":2452,\"end\":2463,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2452,\"end\":2458,\"name\":\"active\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":2459,\"end\":2463,\"value\":true}},{\"type\":\"ObjectProperty\",\"start\":2464,\"end\":2474,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2464,\"end\":2469,\"name\":\"value\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NullLiteral\",\"start\":2470,\"end\":2474}}]}}]}}]}}]}}]}}]}}]}}]}}]}}]}}]}}]}}]}},{\"type\":\"ObjectProperty\",\"start\":2488,\"end\":3021,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2488,\"end\":2496,\"name\":\"branches\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2497,\"end\":3021,\"elements\":[{\"type\":\"ObjectExpression\",\"start\":2498,\"end\":2672,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2499,\"end\":2509,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2499,\"end\":2501,\"name\":\"id\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":2502,\"end\":2509,\"value\":\"alpha\"}},{\"type\":\"ObjectProperty\",\"start\":2510,\"end\":2522,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2510,\"end\":2517,\"name\":\"enabled\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":2518,\"end\":2522,\"value\":true}},{\"type\":\"ObjectProperty\",\"start\":2523,\"end\":2671,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2523,\"end\":2530,\"name\":\"payload\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2531,\"end\":2671,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2532,\"end\":2560,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2532,\"end\":2539,\"name\":\"metrics\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2540,\"end\":2560,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2541,\"end\":2548,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2541,\"end\":2546,\"name\":\"count\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":2547,\"end\":2548,\"value\":3}},{\"type\":\"ObjectProperty\",\"start\":2549,\"end\":2559,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2549,\"end\":2555,\"name\":\"weight\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":2556,\"end\":2559,\"value\":1.5}}]}},{\"type\":\"ObjectProperty\",\"start\":2561,\"end\":2581,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2561,\"end\":2565,\"name\":\"tags\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2566,\"end\":2581,\"elements\":[{\"type\":\"StringLiteral\",\"start\":2567,\"end\":2573,\"value\":\"fast\"},{\"type\":\"StringLiteral\",\"start\":2574,\"end\":2580,\"value\":\"safe\"}]}},{\"type\":\"ObjectProperty\",\"start\":2582,\"end\":2670,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2582,\"end\":2590,\"name\":\"children\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2591,\"end\":2670,\"elements\":[{\"type\":\"ObjectExpression\",\"start\":2592,\"end\":2618,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2593,\"end\":2602,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2593,\"end\":2597,\"name\":\"name\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":2598,\"end\":2602,\"value\":\"a1\"}},{\"type\":\"ObjectProperty\",\"start\":2603,\"end\":2617,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2603,\"end\":2609,\"name\":\"values\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2610,\"end\":2617,\"elements\":[{\"type\":\"NumericLiteral\",\"start\":2611,\"end\":2612,\"value\":1},{\"type\":\"NumericLiteral\",\"start\":2613,\"end\":2614,\"value\":2},{\"type\":\"NumericLiteral\",\"start\":2615,\"end\":2616,\"value\":3}]}}]},{\"type\":\"ObjectExpression\",\"start\":2619,\"end\":2669,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2620,\"end\":2629,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2620,\"end\":2624,\"name\":\"name\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":2625,\"end\":2629,\"value\":\"a2\"}},{\"type\":\"ObjectProperty\",\"start\":2630,\"end\":2668,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2630,\"end\":2636,\"name\":\"values\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2637,\"end\":2668,\"elements\":[{\"type\":\"NumericLiteral\",\"start\":2638,\"end\":2639,\"value\":4},{\"type\":\"ObjectExpression\",\"start\":2640,\"end\":2667,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2641,\"end\":2666,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2641,\"end\":2645,\"name\":\"deep\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2646,\"end\":2666,\"elements\":[{\"type\":\"NumericLiteral\",\"start\":2647,\"end\":2648,\"value\":5},{\"type\":\"NumericLiteral\",\"start\":2649,\"end\":2650,\"value\":6},{\"type\":\"ObjectExpression\",\"start\":2651,\"end\":2665,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2652,\"end\":2664,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2652,\"end\":2660,\"name\":\"terminal\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":2661,\"end\":2664,\"value\":\"a\"}}]}]}}]}]}}]}]}}]}}]},{\"type\":\"ObjectExpression\",\"start\":2673,\"end\":2826,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2674,\"end\":2683,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2674,\"end\":2676,\"name\":\"id\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":2677,\"end\":2683,\"value\":\"beta\"}},{\"type\":\"ObjectProperty\",\"start\":2684,\"end\":2697,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2684,\"end\":2691,\"name\":\"enabled\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":2692,\"end\":2697,\"value\":false}},{\"type\":\"ObjectProperty\",\"start\":2698,\"end\":2825,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2698,\"end\":2705,\"name\":\"payload\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2706,\"end\":2825,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2707,\"end\":2736,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2707,\"end\":2714,\"name\":\"metrics\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2715,\"end\":2736,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2716,\"end\":2723,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2716,\"end\":2721,\"name\":\"count\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":2722,\"end\":2723,\"value\":5}},{\"type\":\"ObjectProperty\",\"start\":2724,\"end\":2735,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2724,\"end\":2730,\"name\":\"weight\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":2731,\"end\":2735,\"value\":2.25}}]}},{\"type\":\"ObjectProperty\",\"start\":2737,\"end\":2759,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2737,\"end\":2741,\"name\":\"tags\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2742,\"end\":2759,\"elements\":[{\"type\":\"StringLiteral\",\"start\":2743,\"end\":2749,\"value\":\"slow\"},{\"type\":\"StringLiteral\",\"start\":2750,\"end\":2758,\"value\":\"review\"}]}},{\"type\":\"ObjectProperty\",\"start\":2760,\"end\":2824,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2760,\"end\":2768,\"name\":\"children\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2769,\"end\":2824,\"elements\":[{\"type\":\"ObjectExpression\",\"start\":2770,\"end\":2823,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2771,\"end\":2780,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2771,\"end\":2775,\"name\":\"name\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":2776,\"end\":2780,\"value\":\"b1\"}},{\"type\":\"ObjectProperty\",\"start\":2781,\"end\":2822,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2781,\"end\":2787,\"name\":\"values\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2788,\"end\":2822,\"elements\":[{\"type\":\"NumericLiteral\",\"start\":2789,\"end\":2790,\"value\":7},{\"type\":\"NumericLiteral\",\"start\":2791,\"end\":2792,\"value\":8},{\"type\":\"ObjectExpression\",\"start\":2793,\"end\":2821,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2794,\"end\":2820,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2794,\"end\":2798,\"name\":\"deep\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2799,\"end\":2820,\"elements\":[{\"type\":\"NumericLiteral\",\"start\":2800,\"end\":2801,\"value\":9},{\"type\":\"NumericLiteral\",\"start\":2802,\"end\":2804,\"value\":10},{\"type\":\"ObjectExpression\",\"start\":2805,\"end\":2819,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2806,\"end\":2818,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2806,\"end\":2814,\"name\":\"terminal\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":2815,\"end\":2818,\"value\":\"b\"}}]}]}}]}]}}]}]}}]}}]},{\"type\":\"ObjectExpression\",\"start\":2827,\"end\":3020,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2828,\"end\":2838,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2828,\"end\":2830,\"name\":\"id\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":2831,\"end\":2838,\"value\":\"gamma\"}},{\"type\":\"ObjectProperty\",\"start\":2839,\"end\":2851,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2839,\"end\":2846,\"name\":\"enabled\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":2847,\"end\":2851,\"value\":true}},{\"type\":\"ObjectProperty\",\"start\":2852,\"end\":3019,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2852,\"end\":2859,\"name\":\"payload\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2860,\"end\":3019,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2861,\"end\":2890,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2861,\"end\":2868,\"name\":\"metrics\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":2869,\"end\":2890,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2870,\"end\":2877,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2870,\"end\":2875,\"name\":\"count\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":2876,\"end\":2877,\"value\":8}},{\"type\":\"ObjectProperty\",\"start\":2878,\"end\":2889,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2878,\"end\":2884,\"name\":\"weight\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":2885,\"end\":2889,\"value\":3.75}}]}},{\"type\":\"ObjectProperty\",\"start\":2891,\"end\":2920,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2891,\"end\":2895,\"name\":\"tags\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2896,\"end\":2920,\"elements\":[{\"type\":\"StringLiteral\",\"start\":2897,\"end\":2903,\"value\":\"wide\"},{\"type\":\"StringLiteral\",\"start\":2904,\"end\":2910,\"value\":\"safe\"},{\"type\":\"StringLiteral\",\"start\":2911,\"end\":2919,\"value\":\"export\"}]}},{\"type\":\"ObjectProperty\",\"start\":2921,\"end\":3018,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2921,\"end\":2929,\"name\":\"children\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2930,\"end\":3018,\"elements\":[{\"type\":\"ObjectExpression\",\"start\":2931,\"end\":2960,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2932,\"end\":2941,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2932,\"end\":2936,\"name\":\"name\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":2937,\"end\":2941,\"value\":\"g1\"}},{\"type\":\"ObjectProperty\",\"start\":2942,\"end\":2959,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2942,\"end\":2948,\"name\":\"values\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2949,\"end\":2959,\"elements\":[{\"type\":\"NumericLiteral\",\"start\":2950,\"end\":2952,\"value\":11},{\"type\":\"NumericLiteral\",\"start\":2953,\"end\":2955,\"value\":12},{\"type\":\"NumericLiteral\",\"start\":2956,\"end\":2958,\"value\":13}]}}]},{\"type\":\"ObjectExpression\",\"start\":2961,\"end\":3017,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2962,\"end\":2971,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2962,\"end\":2966,\"name\":\"name\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":2967,\"end\":2971,\"value\":\"g2\"}},{\"type\":\"ObjectProperty\",\"start\":2972,\"end\":3016,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2972,\"end\":2978,\"name\":\"values\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2979,\"end\":3016,\"elements\":[{\"type\":\"NumericLiteral\",\"start\":2980,\"end\":2982,\"value\":14},{\"type\":\"NumericLiteral\",\"start\":2983,\"end\":2985,\"value\":15},{\"type\":\"ObjectExpression\",\"start\":2986,\"end\":3015,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":2987,\"end\":3014,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":2987,\"end\":2991,\"name\":\"deep\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":2992,\"end\":3014,\"elements\":[{\"type\":\"NumericLiteral\",\"start\":2993,\"end\":2995,\"value\":16},{\"type\":\"NumericLiteral\",\"start\":2996,\"end\":2998,\"value\":17},{\"type\":\"ObjectExpression\",\"start\":2999,\"end\":3013,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":3000,\"end\":3012,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3000,\"end\":3008,\"name\":\"terminal\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":3009,\"end\":3012,\"value\":\"g\"}}]}]}}]}]}}]}]}}]}}]}]}}]}}],\"kind\":\"const\"},{\"type\":\"FunctionDeclaration\",\"start\":3024,\"end\":3481,\"id\":{\"type\":\"Identifier\",\"start\":3033,\"end\":3045,\"name\":\"collectNodes\"},\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":3046,\"end\":3050,\"name\":\"root\"},{\"type\":\"AssignmentPattern\",\"start\":3051,\"end\":3116,\"left\":{\"type\":\"Identifier\",\"start\":3051,\"end\":3058,\"name\":\"options\"},\"right\":{\"type\":\"ObjectExpression\",\"start\":3059,\"end\":3116,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":3060,\"end\":3071,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3060,\"end\":3064,\"name\":\"mode\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":3065,\"end\":3071,\"value\":\"full\"}},{\"type\":\"ObjectProperty\",\"start\":3072,\"end\":3080,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3072,\"end\":3077,\"name\":\"limit\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":3078,\"end\":3080,\"value\":64}},{\"type\":\"ObjectProperty\",\"start\":3081,\"end\":3115,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3081,\"end\":3089,\"name\":\"features\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":3090,\"end\":3115,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":3091,\"end\":3103,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3091,\"end\":3098,\"name\":\"metrics\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":3099,\"end\":3103,\"value\":true}},{\"type\":\"ObjectProperty\",\"start\":3104,\"end\":3114,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3104,\"end\":3109,\"name\":\"paths\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":3110,\"end\":3114,\"value\":true}}]}}]}}],\"body\":{\"type\":\"BlockStatement\",\"start\":3117,\"end\":3481,\"body\":[{\"type\":\"VariableDeclaration\",\"start\":3118,\"end\":3135,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":3124,\"end\":3134,\"id\":{\"type\":\"Identifier\",\"start\":3124,\"end\":3131,\"name\":\"entries\"},\"init\":{\"type\":\"ArrayExpression\",\"start\":3132,\"end\":3134,\"elements\":[]}}],\"kind\":\"const\"},{\"type\":\"ForOfStatement\",\"start\":3135,\"end\":3450,\"await\":false,\"left\":{\"type\":\"VariableDeclaration\",\"start\":3139,\"end\":3151,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":3145,\"end\":3151,\"id\":{\"type\":\"Identifier\",\"start\":3145,\"end\":3151,\"name\":\"branch\"},\"init\":null}],\"kind\":\"const\"},\"right\":{\"type\":\"MemberExpression\",\"start\":3155,\"end\":3168,\"object\":{\"type\":\"Identifier\",\"start\":3155,\"end\":3159,\"name\":\"root\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3160,\"end\":3168,\"name\":\"branches\"}},\"body\":{\"type\":\"BlockStatement\",\"start\":3169,\"end\":3450,\"body\":[{\"type\":\"IfStatement\",\"start\":3170,\"end\":3449,\"test\":{\"type\":\"LogicalExpression\",\"start\":3173,\"end\":3203,\"left\":{\"type\":\"MemberExpression\",\"start\":3173,\"end\":3187,\"object\":{\"type\":\"Identifier\",\"start\":3173,\"end\":3179,\"name\":\"branch\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3180,\"end\":3187,\"name\":\"enabled\"}},\"operator\":\"&&\",\"right\":{\"type\":\"MemberExpression\",\"start\":3189,\"end\":3203,\"object\":{\"type\":\"Identifier\",\"start\":3189,\"end\":3195,\"name\":\"branch\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3196,\"end\":3203,\"name\":\"payload\"}}},\"consequent\":{\"type\":\"BlockStatement\",\"start\":3204,\"end\":3383,\"body\":[{\"type\":\"ExpressionStatement\",\"start\":3205,\"end\":3382,\"expression\":{\"type\":\"CallExpression\",\"start\":3205,\"end\":3381,\"callee\":{\"type\":\"MemberExpression\",\"start\":3205,\"end\":3217,\"object\":{\"type\":\"Identifier\",\"start\":3205,\"end\":3212,\"name\":\"entries\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3213,\"end\":3217,\"name\":\"push\"}},\"arguments\":[{\"type\":\"ObjectExpression\",\"start\":3218,\"end\":3380,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":3219,\"end\":3231,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3219,\"end\":3221,\"name\":\"id\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":3222,\"end\":3231,\"object\":{\"type\":\"Identifier\",\"start\":3222,\"end\":3228,\"name\":\"branch\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3229,\"end\":3231,\"name\":\"id\"}}},{\"type\":\"ObjectProperty\",\"start\":3232,\"end\":3313,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3232,\"end\":3239,\"name\":\"metrics\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":3240,\"end\":3313,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":3241,\"end\":3275,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3241,\"end\":3246,\"name\":\"count\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":3247,\"end\":3275,\"object\":{\"type\":\"MemberExpression\",\"start\":3247,\"end\":3269,\"object\":{\"type\":\"MemberExpression\",\"start\":3247,\"end\":3261,\"object\":{\"type\":\"Identifier\",\"start\":3247,\"end\":3253,\"name\":\"branch\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3254,\"end\":3261,\"name\":\"payload\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3262,\"end\":3269,\"name\":\"metrics\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3270,\"end\":3275,\"name\":\"count\"}}},{\"type\":\"ObjectProperty\",\"start\":3276,\"end\":3312,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3276,\"end\":3282,\"name\":\"weight\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":3283,\"end\":3312,\"object\":{\"type\":\"MemberExpression\",\"start\":3283,\"end\":3305,\"object\":{\"type\":\"MemberExpression\",\"start\":3283,\"end\":3297,\"object\":{\"type\":\"Identifier\",\"start\":3283,\"end\":3289,\"name\":\"branch\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3290,\"end\":3297,\"name\":\"payload\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3298,\"end\":3305,\"name\":\"metrics\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3306,\"end\":3312,\"name\":\"weight\"}}}]}},{\"type\":\"ObjectProperty\",\"start\":3314,\"end\":3338,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3314,\"end\":3318,\"name\":\"tags\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":3319,\"end\":3338,\"object\":{\"type\":\"MemberExpression\",\"start\":3319,\"end\":3333,\"object\":{\"type\":\"Identifier\",\"start\":3319,\"end\":3325,\"name\":\"branch\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3326,\"end\":3333,\"name\":\"payload\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3334,\"end\":3338,\"name\":\"tags\"}}},{\"type\":\"ObjectProperty\",\"start\":3339,\"end\":3371,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3339,\"end\":3347,\"name\":\"children\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":3348,\"end\":3371,\"object\":{\"type\":\"MemberExpression\",\"start\":3348,\"end\":3362,\"object\":{\"type\":\"Identifier\",\"start\":3348,\"end\":3354,\"name\":\"branch\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3355,\"end\":3362,\"name\":\"payload\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3363,\"end\":3371,\"name\":\"children\"}}},{\"type\":\"ObjectProperty\",\"start\":3372,\"end\":3379,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3372,\"end\":3379,\"name\":\"options\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":3372,\"end\":3379,\"name\":\"options\"}}]}]}}],\"directives\":[]},\"alternate\":{\"type\":\"BlockStatement\",\"start\":3387,\"end\":3449,\"body\":[{\"type\":\"ExpressionStatement\",\"start\":3388,\"end\":3448,\"expression\":{\"type\":\"CallExpression\",\"start\":3388,\"end\":3447,\"callee\":{\"type\":\"MemberExpression\",\"start\":3388,\"end\":3400,\"object\":{\"type\":\"Identifier\",\"start\":3388,\"end\":3395,\"name\":\"entries\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3396,\"end\":3400,\"name\":\"push\"}},\"arguments\":[{\"type\":\"ObjectExpression\",\"start\":3401,\"end\":3446,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":3402,\"end\":3414,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3402,\"end\":3404,\"name\":\"id\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":3405,\"end\":3414,\"object\":{\"type\":\"Identifier\",\"start\":3405,\"end\":3411,\"name\":\"branch\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3412,\"end\":3414,\"name\":\"id\"}}},{\"type\":\"ObjectProperty\",\"start\":3415,\"end\":3427,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3415,\"end\":3422,\"name\":\"skipped\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":3423,\"end\":3427,\"value\":true}},{\"type\":\"ObjectProperty\",\"start\":3428,\"end\":3445,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3428,\"end\":3434,\"name\":\"reason\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":3435,\"end\":3445,\"value\":\"disabled\"}}]}]}}],\"directives\":[]}}],\"directives\":[]}},{\"type\":\"ReturnStatement\",\"start\":3450,\"end\":3480,\"argument\":{\"type\":\"ObjectExpression\",\"start\":3457,\"end\":3479,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":3458,\"end\":3462,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3458,\"end\":3462,\"name\":\"root\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":3458,\"end\":3462,\"name\":\"root\"}},{\"type\":\"ObjectProperty\",\"start\":3463,\"end\":3470,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3463,\"end\":3470,\"name\":\"entries\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":3463,\"end\":3470,\"name\":\"entries\"}},{\"type\":\"ObjectProperty\",\"start\":3471,\"end\":3478,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3471,\"end\":3478,\"name\":\"options\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":3471,\"end\":3478,\"name\":\"options\"}}]}}],\"directives\":[]}},{\"type\":\"FunctionDeclaration\",\"start\":3482,\"end\":3774,\"id\":{\"type\":\"Identifier\",\"start\":3491,\"end\":3502,\"name\":\"scoreRecord\"},\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":3503,\"end\":3509,\"name\":\"record\"}],\"body\":{\"type\":\"BlockStatement\",\"start\":3510,\"end\":3774,\"body\":[{\"type\":\"VariableDeclaration\",\"start\":3511,\"end\":3566,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":3515,\"end\":3565,\"id\":{\"type\":\"Identifier\",\"start\":3515,\"end\":3520,\"name\":\"score\"},\"init\":{\"type\":\"BinaryExpression\",\"start\":3521,\"end\":3565,\"left\":{\"type\":\"BinaryExpression\",\"start\":3521,\"end\":3543,\"left\":{\"type\":\"MemberExpression\",\"start\":3521,\"end\":3541,\"object\":{\"type\":\"MemberExpression\",\"start\":3521,\"end\":3535,\"object\":{\"type\":\"Identifier\",\"start\":3521,\"end\":3527,\"name\":\"record\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3528,\"end\":3535,\"name\":\"metrics\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3536,\"end\":3541,\"name\":\"count\"}},\"operator\":\"*\",\"right\":{\"type\":\"NumericLiteral\",\"start\":3542,\"end\":3543,\"value\":2}},\"operator\":\"+\",\"right\":{\"type\":\"MemberExpression\",\"start\":3544,\"end\":3565,\"object\":{\"type\":\"MemberExpression\",\"start\":3544,\"end\":3558,\"object\":{\"type\":\"Identifier\",\"start\":3544,\"end\":3550,\"name\":\"record\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3551,\"end\":3558,\"name\":\"metrics\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3559,\"end\":3565,\"name\":\"weight\"}}}}],\"kind\":\"let\"},{\"type\":\"IfStatement\",\"start\":3566,\"end\":3623,\"test\":{\"type\":\"BinaryExpression\",\"start\":3569,\"end\":3589,\"left\":{\"type\":\"MemberExpression\",\"start\":3569,\"end\":3587,\"object\":{\"type\":\"MemberExpression\",\"start\":3569,\"end\":3580,\"object\":{\"type\":\"Identifier\",\"start\":3569,\"end\":3575,\"name\":\"record\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3576,\"end\":3580,\"name\":\"tags\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3581,\"end\":3587,\"name\":\"length\"}},\"operator\":\">\",\"right\":{\"type\":\"NumericLiteral\",\"start\":3588,\"end\":3589,\"value\":2}},\"consequent\":{\"type\":\"BlockStatement\",\"start\":3590,\"end\":3623,\"body\":[{\"type\":\"ExpressionStatement\",\"start\":3591,\"end\":3622,\"expression\":{\"type\":\"AssignmentExpression\",\"start\":3591,\"end\":3621,\"operator\":\"=\",\"left\":{\"type\":\"Identifier\",\"start\":3591,\"end\":3596,\"name\":\"score\"},\"right\":{\"type\":\"BinaryExpression\",\"start\":3597,\"end\":3621,\"left\":{\"type\":\"Identifier\",\"start\":3597,\"end\":3602,\"name\":\"score\"},\"operator\":\"+\",\"right\":{\"type\":\"MemberExpression\",\"start\":3603,\"end\":3621,\"object\":{\"type\":\"MemberExpression\",\"start\":3603,\"end\":3614,\"object\":{\"type\":\"Identifier\",\"start\":3603,\"end\":3609,\"name\":\"record\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3610,\"end\":3614,\"name\":\"tags\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3615,\"end\":3621,\"name\":\"length\"}}}}}],\"directives\":[]},\"alternate\":null},{\"type\":\"IfStatement\",\"start\":3623,\"end\":3694,\"test\":{\"type\":\"BinaryExpression\",\"start\":3626,\"end\":3650,\"left\":{\"type\":\"MemberExpression\",\"start\":3626,\"end\":3648,\"object\":{\"type\":\"MemberExpression\",\"start\":3626,\"end\":3641,\"object\":{\"type\":\"Identifier\",\"start\":3626,\"end\":3632,\"name\":\"record\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3633,\"end\":3641,\"name\":\"children\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3642,\"end\":3648,\"name\":\"length\"}},\"operator\":\">\",\"right\":{\"type\":\"NumericLiteral\",\"start\":3649,\"end\":3650,\"value\":1}},\"consequent\":{\"type\":\"BlockStatement\",\"start\":3651,\"end\":3694,\"body\":[{\"type\":\"ExpressionStatement\",\"start\":3652,\"end\":3693,\"expression\":{\"type\":\"AssignmentExpression\",\"start\":3652,\"end\":3692,\"operator\":\"=\",\"left\":{\"type\":\"Identifier\",\"start\":3652,\"end\":3657,\"name\":\"score\"},\"right\":{\"type\":\"BinaryExpression\",\"start\":3658,\"end\":3692,\"left\":{\"type\":\"Identifier\",\"start\":3658,\"end\":3663,\"name\":\"score\"},\"operator\":\"+\",\"right\":{\"type\":\"MemberExpression\",\"start\":3664,\"end\":3692,\"object\":{\"type\":\"MemberExpression\",\"start\":3664,\"end\":3689,\"object\":{\"type\":\"MemberExpression\",\"start\":3664,\"end\":3682,\"object\":{\"type\":\"MemberExpression\",\"start\":3664,\"end\":3679,\"object\":{\"type\":\"Identifier\",\"start\":3664,\"end\":3670,\"name\":\"record\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3671,\"end\":3679,\"name\":\"children\"}},\"computed\":true,\"property\":{\"type\":\"NumericLiteral\",\"start\":3680,\"end\":3681,\"value\":1}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3683,\"end\":3689,\"name\":\"values\"}},\"computed\":true,\"property\":{\"type\":\"NumericLiteral\",\"start\":3690,\"end\":3691,\"value\":0}}}}}],\"directives\":[]},\"alternate\":null},{\"type\":\"ReturnStatement\",\"start\":3694,\"end\":3773,\"argument\":{\"type\":\"ObjectExpression\",\"start\":3701,\"end\":3772,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":3702,\"end\":3714,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3702,\"end\":3704,\"name\":\"id\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":3705,\"end\":3714,\"object\":{\"type\":\"Identifier\",\"start\":3705,\"end\":3711,\"name\":\"record\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3712,\"end\":3714,\"name\":\"id\"}}},{\"type\":\"ObjectProperty\",\"start\":3715,\"end\":3720,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3715,\"end\":3720,\"name\":\"score\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":3715,\"end\":3720,\"name\":\"score\"}},{\"type\":\"ObjectProperty\",\"start\":3721,\"end\":3771,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3721,\"end\":3727,\"name\":\"detail\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":3728,\"end\":3771,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":3729,\"end\":3745,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3729,\"end\":3733,\"name\":\"tags\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":3734,\"end\":3745,\"object\":{\"type\":\"Identifier\",\"start\":3734,\"end\":3740,\"name\":\"record\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3741,\"end\":3745,\"name\":\"tags\"}}},{\"type\":\"ObjectProperty\",\"start\":3746,\"end\":3770,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3746,\"end\":3751,\"name\":\"first\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":3752,\"end\":3770,\"object\":{\"type\":\"MemberExpression\",\"start\":3752,\"end\":3767,\"object\":{\"type\":\"Identifier\",\"start\":3752,\"end\":3758,\"name\":\"record\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3759,\"end\":3767,\"name\":\"children\"}},\"computed\":true,\"property\":{\"type\":\"NumericLiteral\",\"start\":3768,\"end\":3769,\"value\":0}}}]}}]}}],\"directives\":[]}},{\"type\":\"FunctionDeclaration\",\"start\":3775,\"end\":3900,\"id\":{\"type\":\"Identifier\",\"start\":3784,\"end\":3794,\"name\":\"walkLayers\"},\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":3795,\"end\":3799,\"name\":\"tree\"}],\"body\":{\"type\":\"BlockStatement\",\"start\":3800,\"end\":3900,\"body\":[{\"type\":\"VariableDeclaration\",\"start\":3801,\"end\":3827,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":3807,\"end\":3826,\"id\":{\"type\":\"Identifier\",\"start\":3807,\"end\":3811,\"name\":\"path\"},\"init\":{\"type\":\"MemberExpression\",\"start\":3812,\"end\":3826,\"object\":{\"type\":\"MemberExpression\",\"start\":3812,\"end\":3821,\"object\":{\"type\":\"Identifier\",\"start\":3812,\"end\":3816,\"name\":\"tree\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3817,\"end\":3821,\"name\":\"meta\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3822,\"end\":3826,\"name\":\"path\"}}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":3827,\"end\":3854,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":3833,\"end\":3853,\"id\":{\"type\":\"Identifier\",\"start\":3833,\"end\":3841,\"name\":\"terminal\"},\"init\":{\"type\":\"MemberExpression\",\"start\":3842,\"end\":3853,\"object\":{\"type\":\"Identifier\",\"start\":3842,\"end\":3846,\"name\":\"tree\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3847,\"end\":3853,\"name\":\"layer0\"}}}],\"kind\":\"const\"},{\"type\":\"ReturnStatement\",\"start\":3854,\"end\":3899,\"argument\":{\"type\":\"ObjectExpression\",\"start\":3861,\"end\":3898,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":3862,\"end\":3866,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3862,\"end\":3866,\"name\":\"path\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":3862,\"end\":3866,\"name\":\"path\"}},{\"type\":\"ObjectProperty\",\"start\":3867,\"end\":3875,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3867,\"end\":3875,\"name\":\"terminal\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":3867,\"end\":3875,\"name\":\"terminal\"}},{\"type\":\"ObjectProperty\",\"start\":3876,\"end\":3897,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3876,\"end\":3881,\"name\":\"flags\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":3882,\"end\":3897,\"object\":{\"type\":\"MemberExpression\",\"start\":3882,\"end\":3891,\"object\":{\"type\":\"Identifier\",\"start\":3882,\"end\":3886,\"name\":\"tree\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3887,\"end\":3891,\"name\":\"meta\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3892,\"end\":3897,\"name\":\"flags\"}}}]}}],\"directives\":[]}},{\"type\":\"FunctionDeclaration\",\"start\":3901,\"end\":4032,\"id\":{\"type\":\"Identifier\",\"start\":3910,\"end\":3920,\"name\":\"scoreEntry\"},\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":3921,\"end\":3926,\"name\":\"entry\"}],\"body\":{\"type\":\"BlockStatement\",\"start\":3927,\"end\":4032,\"body\":[{\"type\":\"IfStatement\",\"start\":3928,\"end\":4005,\"test\":{\"type\":\"MemberExpression\",\"start\":3931,\"end\":3944,\"object\":{\"type\":\"Identifier\",\"start\":3931,\"end\":3936,\"name\":\"entry\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3937,\"end\":3944,\"name\":\"skipped\"}},\"consequent\":{\"type\":\"BlockStatement\",\"start\":3945,\"end\":4005,\"body\":[{\"type\":\"ReturnStatement\",\"start\":3946,\"end\":4004,\"argument\":{\"type\":\"ObjectExpression\",\"start\":3953,\"end\":4003,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":3954,\"end\":3965,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3954,\"end\":3956,\"name\":\"id\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":3957,\"end\":3965,\"object\":{\"type\":\"Identifier\",\"start\":3957,\"end\":3962,\"name\":\"entry\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3963,\"end\":3965,\"name\":\"id\"}}},{\"type\":\"ObjectProperty\",\"start\":3966,\"end\":3973,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3966,\"end\":3971,\"name\":\"score\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":3972,\"end\":3973,\"value\":0}},{\"type\":\"ObjectProperty\",\"start\":3974,\"end\":4002,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3974,\"end\":3980,\"name\":\"detail\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":3981,\"end\":4002,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":3982,\"end\":4001,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":3982,\"end\":3988,\"name\":\"reason\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":3989,\"end\":4001,\"object\":{\"type\":\"Identifier\",\"start\":3989,\"end\":3994,\"name\":\"entry\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":3995,\"end\":4001,\"name\":\"reason\"}}}]}}]}}],\"directives\":[]},\"alternate\":null},{\"type\":\"ReturnStatement\",\"start\":4005,\"end\":4031,\"argument\":{\"type\":\"CallExpression\",\"start\":4012,\"end\":4030,\"callee\":{\"type\":\"Identifier\",\"start\":4012,\"end\":4023,\"name\":\"scoreRecord\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":4024,\"end\":4029,\"name\":\"entry\"}]}}],\"directives\":[]}},{\"type\":\"ClassDeclaration\",\"start\":4033,\"end\":4346,\"id\":{\"type\":\"Identifier\",\"start\":4039,\"end\":4046,\"name\":\"Archive\"},\"superClass\":null,\"body\":{\"type\":\"ClassBody\",\"start\":4046,\"end\":4346,\"body\":[{\"type\":\"ClassMethod\",\"start\":4047,\"end\":4123,\"static\":false,\"key\":{\"type\":\"Identifier\",\"start\":4047,\"end\":4058,\"name\":\"constructor\"},\"computed\":false,\"kind\":\"constructor\",\"id\":null,\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":4059,\"end\":4067,\"name\":\"snapshot\"}],\"body\":{\"type\":\"BlockStatement\",\"start\":4068,\"end\":4123,\"body\":[{\"type\":\"ExpressionStatement\",\"start\":4069,\"end\":4092,\"expression\":{\"type\":\"AssignmentExpression\",\"start\":4069,\"end\":4091,\"operator\":\"=\",\"left\":{\"type\":\"MemberExpression\",\"start\":4069,\"end\":4082,\"object\":{\"type\":\"ThisExpression\",\"start\":4069,\"end\":4073},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4074,\"end\":4082,\"name\":\"snapshot\"}},\"right\":{\"type\":\"Identifier\",\"start\":4083,\"end\":4091,\"name\":\"snapshot\"}}},{\"type\":\"ExpressionStatement\",\"start\":4092,\"end\":4122,\"expression\":{\"type\":\"AssignmentExpression\",\"start\":4092,\"end\":4121,\"operator\":\"=\",\"left\":{\"type\":\"MemberExpression\",\"start\":4092,\"end\":4104,\"object\":{\"type\":\"ThisExpression\",\"start\":4092,\"end\":4096},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4097,\"end\":4104,\"name\":\"entries\"}},\"right\":{\"type\":\"MemberExpression\",\"start\":4105,\"end\":4121,\"object\":{\"type\":\"Identifier\",\"start\":4105,\"end\":4113,\"name\":\"snapshot\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4114,\"end\":4121,\"name\":\"entries\"}}}}],\"directives\":[]}},{\"type\":\"ClassMethod\",\"start\":4123,\"end\":4261,\"static\":false,\"key\":{\"type\":\"Identifier\",\"start\":4123,\"end\":4131,\"name\":\"describe\"},\"computed\":false,\"kind\":\"method\",\"id\":null,\"generator\":false,\"async\":false,\"params\":[],\"body\":{\"type\":\"BlockStatement\",\"start\":4133,\"end\":4261,\"body\":[{\"type\":\"ReturnStatement\",\"start\":4134,\"end\":4260,\"argument\":{\"type\":\"ObjectExpression\",\"start\":4141,\"end\":4259,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":4142,\"end\":4175,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4142,\"end\":4146,\"name\":\"name\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":4147,\"end\":4175,\"object\":{\"type\":\"MemberExpression\",\"start\":4147,\"end\":4170,\"object\":{\"type\":\"MemberExpression\",\"start\":4147,\"end\":4165,\"object\":{\"type\":\"MemberExpression\",\"start\":4147,\"end\":4160,\"object\":{\"type\":\"ThisExpression\",\"start\":4147,\"end\":4151},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4152,\"end\":4160,\"name\":\"snapshot\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4161,\"end\":4165,\"name\":\"root\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4166,\"end\":4170,\"name\":\"meta\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4171,\"end\":4175,\"name\":\"name\"}}},{\"type\":\"ObjectProperty\",\"start\":4176,\"end\":4201,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4176,\"end\":4181,\"name\":\"count\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":4182,\"end\":4201,\"object\":{\"type\":\"MemberExpression\",\"start\":4182,\"end\":4194,\"object\":{\"type\":\"ThisExpression\",\"start\":4182,\"end\":4186},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4187,\"end\":4194,\"name\":\"entries\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4195,\"end\":4201,\"name\":\"length\"}}},{\"type\":\"ObjectProperty\",\"start\":4202,\"end\":4223,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4202,\"end\":4207,\"name\":\"first\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":4208,\"end\":4223,\"object\":{\"type\":\"MemberExpression\",\"start\":4208,\"end\":4220,\"object\":{\"type\":\"ThisExpression\",\"start\":4208,\"end\":4212},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4213,\"end\":4220,\"name\":\"entries\"}},\"computed\":true,\"property\":{\"type\":\"NumericLiteral\",\"start\":4221,\"end\":4222,\"value\":0}}},{\"type\":\"ObjectProperty\",\"start\":4224,\"end\":4258,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4224,\"end\":4232,\"name\":\"terminal\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":4233,\"end\":4258,\"object\":{\"type\":\"MemberExpression\",\"start\":4233,\"end\":4251,\"object\":{\"type\":\"MemberExpression\",\"start\":4233,\"end\":4246,\"object\":{\"type\":\"ThisExpression\",\"start\":4233,\"end\":4237},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4238,\"end\":4246,\"name\":\"snapshot\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4247,\"end\":4251,\"name\":\"root\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4252,\"end\":4258,\"name\":\"layer0\"}}}]}}],\"directives\":[]}},{\"type\":\"ClassMethod\",\"start\":4261,\"end\":4345,\"static\":false,\"key\":{\"type\":\"Identifier\",\"start\":4261,\"end\":4267,\"name\":\"append\"},\"computed\":false,\"kind\":\"method\",\"id\":null,\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":4268,\"end\":4273,\"name\":\"entry\"}],\"body\":{\"type\":\"BlockStatement\",\"start\":4274,\"end\":4345,\"body\":[{\"type\":\"ExpressionStatement\",\"start\":4275,\"end\":4317,\"expression\":{\"type\":\"AssignmentExpression\",\"start\":4275,\"end\":4316,\"operator\":\"=\",\"left\":{\"type\":\"MemberExpression\",\"start\":4275,\"end\":4287,\"object\":{\"type\":\"ThisExpression\",\"start\":4275,\"end\":4279},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4280,\"end\":4287,\"name\":\"entries\"}},\"right\":{\"type\":\"CallExpression\",\"start\":4288,\"end\":4316,\"callee\":{\"type\":\"MemberExpression\",\"start\":4288,\"end\":4307,\"object\":{\"type\":\"MemberExpression\",\"start\":4288,\"end\":4300,\"object\":{\"type\":\"ThisExpression\",\"start\":4288,\"end\":4292},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4293,\"end\":4300,\"name\":\"entries\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4301,\"end\":4307,\"name\":\"concat\"}},\"arguments\":[{\"type\":\"ArrayExpression\",\"start\":4308,\"end\":4315,\"elements\":[{\"type\":\"Identifier\",\"start\":4309,\"end\":4314,\"name\":\"entry\"}]}]}}},{\"type\":\"ReturnStatement\",\"start\":4317,\"end\":4344,\"argument\":{\"type\":\"MemberExpression\",\"start\":4324,\"end\":4343,\"object\":{\"type\":\"MemberExpression\",\"start\":4324,\"end\":4336,\"object\":{\"type\":\"ThisExpression\",\"start\":4324,\"end\":4328},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4329,\"end\":4336,\"name\":\"entries\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4337,\"end\":4343,\"name\":\"length\"}}}],\"directives\":[]}}]}},{\"type\":\"VariableDeclaration\",\"start\":4347,\"end\":4477,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":4353,\"end\":4476,\"id\":{\"type\":\"Identifier\",\"start\":4353,\"end\":4361,\"name\":\"prepared\"},\"init\":{\"type\":\"CallExpression\",\"start\":4362,\"end\":4476,\"callee\":{\"type\":\"Identifier\",\"start\":4362,\"end\":4374,\"name\":\"collectNodes\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":4375,\"end\":4384,\"name\":\"DEEP_TREE\"},{\"type\":\"ObjectExpression\",\"start\":4385,\"end\":4475,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":4386,\"end\":4397,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4386,\"end\":4390,\"name\":\"mode\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":4391,\"end\":4397,\"value\":\"full\"}},{\"type\":\"ObjectProperty\",\"start\":4398,\"end\":4407,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4398,\"end\":4403,\"name\":\"limit\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":4404,\"end\":4407,\"value\":128}},{\"type\":\"ObjectProperty\",\"start\":4408,\"end\":4474,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4408,\"end\":4416,\"name\":\"features\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":4417,\"end\":4474,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":4418,\"end\":4430,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4418,\"end\":4425,\"name\":\"metrics\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":4426,\"end\":4430,\"value\":true}},{\"type\":\"ObjectProperty\",\"start\":4431,\"end\":4441,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4431,\"end\":4436,\"name\":\"paths\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":4437,\"end\":4441,\"value\":true}},{\"type\":\"ObjectProperty\",\"start\":4442,\"end\":4473,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4442,\"end\":4449,\"name\":\"history\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":4450,\"end\":4473,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":4451,\"end\":4463,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4451,\"end\":4458,\"name\":\"enabled\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"BooleanLiteral\",\"start\":4459,\"end\":4463,\"value\":true}},{\"type\":\"ObjectProperty\",\"start\":4464,\"end\":4472,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4464,\"end\":4469,\"name\":\"depth\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":4470,\"end\":4472,\"value\":12}}]}}]}}]}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":4478,\"end\":4540,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":4484,\"end\":4539,\"id\":{\"type\":\"Identifier\",\"start\":4484,\"end\":4490,\"name\":\"scored\"},\"init\":{\"type\":\"CallExpression\",\"start\":4491,\"end\":4539,\"callee\":{\"type\":\"MemberExpression\",\"start\":4491,\"end\":4511,\"object\":{\"type\":\"MemberExpression\",\"start\":4491,\"end\":4507,\"object\":{\"type\":\"Identifier\",\"start\":4491,\"end\":4499,\"name\":\"prepared\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4500,\"end\":4507,\"name\":\"entries\"}},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4508,\"end\":4511,\"name\":\"map\"}},\"arguments\":[{\"type\":\"ArrowFunctionExpression\",\"start\":4512,\"end\":4538,\"id\":null,\"generator\":false,\"async\":false,\"params\":[{\"type\":\"Identifier\",\"start\":4513,\"end\":4518,\"name\":\"entry\"}],\"body\":{\"type\":\"CallExpression\",\"start\":4521,\"end\":4538,\"callee\":{\"type\":\"Identifier\",\"start\":4521,\"end\":4531,\"name\":\"scoreEntry\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":4532,\"end\":4537,\"name\":\"entry\"}]}}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":4541,\"end\":4576,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":4547,\"end\":4575,\"id\":{\"type\":\"Identifier\",\"start\":4547,\"end\":4553,\"name\":\"walked\"},\"init\":{\"type\":\"CallExpression\",\"start\":4554,\"end\":4575,\"callee\":{\"type\":\"Identifier\",\"start\":4554,\"end\":4564,\"name\":\"walkLayers\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":4565,\"end\":4574,\"name\":\"DEEP_TREE\"}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":4577,\"end\":4613,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":4583,\"end\":4612,\"id\":{\"type\":\"Identifier\",\"start\":4583,\"end\":4590,\"name\":\"archive\"},\"init\":{\"type\":\"NewExpression\",\"start\":4591,\"end\":4612,\"callee\":{\"type\":\"Identifier\",\"start\":4595,\"end\":4602,\"name\":\"Archive\"},\"arguments\":[{\"type\":\"Identifier\",\"start\":4603,\"end\":4611,\"name\":\"prepared\"}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":4614,\"end\":4792,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":4620,\"end\":4791,\"id\":{\"type\":\"Identifier\",\"start\":4620,\"end\":4631,\"name\":\"archiveSize\"},\"init\":{\"type\":\"CallExpression\",\"start\":4632,\"end\":4791,\"callee\":{\"type\":\"MemberExpression\",\"start\":4632,\"end\":4646,\"object\":{\"type\":\"Identifier\",\"start\":4632,\"end\":4639,\"name\":\"archive\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4640,\"end\":4646,\"name\":\"append\"}},\"arguments\":[{\"type\":\"ObjectExpression\",\"start\":4647,\"end\":4790,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":4648,\"end\":4658,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4648,\"end\":4650,\"name\":\"id\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":4651,\"end\":4658,\"value\":\"delta\"}},{\"type\":\"ObjectProperty\",\"start\":4659,\"end\":4688,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4659,\"end\":4666,\"name\":\"metrics\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ObjectExpression\",\"start\":4667,\"end\":4688,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":4668,\"end\":4676,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4668,\"end\":4673,\"name\":\"count\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":4674,\"end\":4676,\"value\":13}},{\"type\":\"ObjectProperty\",\"start\":4677,\"end\":4687,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4677,\"end\":4683,\"name\":\"weight\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"NumericLiteral\",\"start\":4684,\"end\":4687,\"value\":4.5}}]}},{\"type\":\"ObjectProperty\",\"start\":4689,\"end\":4711,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4689,\"end\":4693,\"name\":\"tags\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":4694,\"end\":4711,\"elements\":[{\"type\":\"StringLiteral\",\"start\":4695,\"end\":4701,\"value\":\"deep\"},{\"type\":\"StringLiteral\",\"start\":4702,\"end\":4710,\"value\":\"stable\"}]}},{\"type\":\"ObjectProperty\",\"start\":4712,\"end\":4764,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4712,\"end\":4720,\"name\":\"children\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":4721,\"end\":4764,\"elements\":[{\"type\":\"ObjectExpression\",\"start\":4722,\"end\":4763,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":4723,\"end\":4732,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4723,\"end\":4727,\"name\":\"name\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":4728,\"end\":4732,\"value\":\"d1\"}},{\"type\":\"ObjectProperty\",\"start\":4733,\"end\":4762,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4733,\"end\":4739,\"name\":\"values\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"ArrayExpression\",\"start\":4740,\"end\":4762,\"elements\":[{\"type\":\"NumericLiteral\",\"start\":4741,\"end\":4743,\"value\":18},{\"type\":\"NumericLiteral\",\"start\":4744,\"end\":4746,\"value\":19},{\"type\":\"ObjectExpression\",\"start\":4747,\"end\":4761,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":4748,\"end\":4760,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4748,\"end\":4756,\"name\":\"terminal\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"StringLiteral\",\"start\":4757,\"end\":4760,\"value\":\"d\"}}]}]}}]}]}},{\"type\":\"ObjectProperty\",\"start\":4765,\"end\":4789,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4765,\"end\":4772,\"name\":\"options\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"MemberExpression\",\"start\":4773,\"end\":4789,\"object\":{\"type\":\"Identifier\",\"start\":4773,\"end\":4781,\"name\":\"prepared\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4782,\"end\":4789,\"name\":\"options\"}}}]}]}}],\"kind\":\"const\"},{\"type\":\"VariableDeclaration\",\"start\":4793,\"end\":4875,\"declarations\":[{\"type\":\"VariableDeclarator\",\"start\":4799,\"end\":4874,\"id\":{\"type\":\"Identifier\",\"start\":4799,\"end\":4810,\"name\":\"finalReport\"},\"init\":{\"type\":\"ObjectExpression\",\"start\":4811,\"end\":4874,\"properties\":[{\"type\":\"ObjectProperty\",\"start\":4812,\"end\":4820,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4812,\"end\":4820,\"name\":\"prepared\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":4812,\"end\":4820,\"name\":\"prepared\"}},{\"type\":\"ObjectProperty\",\"start\":4821,\"end\":4827,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4821,\"end\":4827,\"name\":\"scored\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":4821,\"end\":4827,\"name\":\"scored\"}},{\"type\":\"ObjectProperty\",\"start\":4828,\"end\":4834,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4828,\"end\":4834,\"name\":\"walked\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":4828,\"end\":4834,\"name\":\"walked\"}},{\"type\":\"ObjectProperty\",\"start\":4835,\"end\":4861,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4835,\"end\":4842,\"name\":\"archive\"},\"computed\":false,\"shorthand\":false,\"value\":{\"type\":\"CallExpression\",\"start\":4843,\"end\":4861,\"callee\":{\"type\":\"MemberExpression\",\"start\":4843,\"end\":4859,\"object\":{\"type\":\"Identifier\",\"start\":4843,\"end\":4850,\"name\":\"archive\"},\"computed\":false,\"property\":{\"type\":\"Identifier\",\"start\":4851,\"end\":4859,\"name\":\"describe\"}},\"arguments\":[]}},{\"type\":\"ObjectProperty\",\"start\":4862,\"end\":4873,\"method\":false,\"key\":{\"type\":\"Identifier\",\"start\":4862,\"end\":4873,\"name\":\"archiveSize\"},\"computed\":false,\"shorthand\":true,\"value\":{\"type\":\"Identifier\",\"start\":4862,\"end\":4873,\"name\":\"archiveSize\"}}]}}],\"kind\":\"const\"},{\"type\":\"ExportNamedDeclaration\",\"start\":4876,\"end\":4937,\"specifiers\":[{\"type\":\"ExportSpecifier\",\"start\":4884,\"end\":4899,\"local\":{\"type\":\"Identifier\",\"start\":4884,\"end\":4899,\"name\":\"DEFAULT_OPTIONS\"},\"exported\":{\"type\":\"Identifier\",\"start\":4884,\"end\":4899,\"name\":\"DEFAULT_OPTIONS\"}},{\"type\":\"ExportSpecifier\",\"start\":4900,\"end\":4913,\"local\":{\"type\":\"Identifier\",\"start\":4900,\"end\":4913,\"name\":\"SourcePrinter\"},\"exported\":{\"type\":\"Identifier\",\"start\":4900,\"end\":4913,\"name\":\"SourcePrinter\"}},{\"type\":\"ExportSpecifier\",\"start\":4914,\"end\":4928,\"local\":{\"type\":\"Identifier\",\"start\":4914,\"end\":4928,\"name\":\"formatDocument\"},\"exported\":{\"type\":\"Identifier\",\"start\":4914,\"end\":4928,\"name\":\"formatDocument\"}},{\"type\":\"ExportSpecifier\",\"start\":4929,\"end\":4935,\"local\":{\"type\":\"Identifier\",\"start\":4929,\"end\":4935,\"name\":\"report\"},\"exported\":{\"type\":\"Identifier\",\"start\":4929,\"end\":4935,\"name\":\"report\"}}],\"source\":null,\"declaration\":null}],\"directives\":[]}\n";


#define PRINT_WIDTH 80
#define ITERATIONS 256
#define LARGE_LENGTH 1000000000L

#define MAX_NODES 4096
#define MAX_CHILDREN 8192
#define MAX_PARSE_LIST 64
#define TEXT_ARENA (1 << 20)
#define DOC_ARENA (1 << 19)
#define DOC_PARTS_ARENA (1 << 21)
#define OUT_SIZE (1 << 20)

/* ---------------------------------------------------------------- node model */

enum {
    NT_OTHER = 0, NT_Program, NT_Identifier, NT_ThisExpression, NT_StringLiteral,
    NT_NumericLiteral, NT_BooleanLiteral, NT_NullLiteral, NT_RegExpLiteral,
    NT_ArrayExpression, NT_ObjectExpression, NT_ObjectProperty, NT_VariableDeclarator,
    NT_SpreadElement, NT_AssignmentPattern, NT_ArrayPattern, NT_MemberExpression,
    NT_CallExpression, NT_NewExpression, NT_BinaryExpression, NT_LogicalExpression,
    NT_UnaryExpression, NT_AssignmentExpression, NT_ArrowFunctionExpression,
    NT_FunctionDeclaration, NT_ClassDeclaration, NT_ClassBody, NT_ClassMethod,
    NT_VariableDeclaration, NT_ReturnStatement, NT_ExpressionStatement,
    NT_BlockStatement, NT_IfStatement, NT_ForOfStatement,
    NT_ExportNamedDeclaration, NT_ExportSpecifier
};

typedef struct Node Node;
struct Node {
    int type;
    int computed, shorthand, is_static, is_async, generator;
    int has_num, bool_value;
    double num;
    const char *name;
    const char *text_value;
    const char *pattern;
    const char *flags;
    const char *operator_text;
    const char *kind;
    Node *key, *value, *argument, *left, *right, *object, *property, *callee;
    Node *id, *init, *expression, *test, *consequent, *alternate, *declaration;
    Node *local, *exported, *body_node;
    Node **elements; int elements_len;
    Node **properties; int properties_len;
    Node **arguments; int arguments_len;
    Node **params; int params_len;
    Node **declarations; int declarations_len;
    Node **specifiers; int specifiers_len;
    Node **body_list; int body_list_len;
};

static Node node_pool[MAX_NODES];
static int node_count = 0;
static Node *child_pool[MAX_CHILDREN];
static int child_count = 0;

static char text_arena[TEXT_ARENA];
static int text_used = 0;

static int str_equal(const char *a, const char *b) {
    int i = 0;
    while (a[i] != 0 && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static int str_length(const char *s) {
    int n = 0;
    while (s[n] != 0) n++;
    return n;
}

static char *text_alloc(int bytes) {
    char *out = &text_arena[text_used];
    text_used += bytes;
    return out;
}

/* ------------------------------------------------------------- JSON scanning */

static const char *json_src;
static int json_at;

static void skip_ws(void) {
    while (json_src[json_at] == ' ' || json_src[json_at] == '\n' ||
           json_src[json_at] == '\r' || json_src[json_at] == '\t') json_at++;
}

/* Reads a JSON string into the text arena and returns it NUL-terminated. */
static const char *read_string(void) {
    char *out;
    int n = 0;
    json_at++; /* opening quote */
    out = &text_arena[text_used];
    while (json_src[json_at] != '"') {
        char c = json_src[json_at];
        if (c == '\\') {
            json_at++;
            c = json_src[json_at];
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == 'r') c = '\r';
            else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
            else if (c == 'u') {
                /* the fixture has no non-ASCII escapes; keep the code point low byte */
                int value = 0;
                int k;
                for (k = 1; k <= 4; k++) {
                    char h = json_src[json_at + k];
                    int d = h <= '9' ? h - '0' : (h <= 'F' ? h - 'A' + 10 : h - 'a' + 10);
                    value = value * 16 + d;
                }
                json_at += 4;
                c = (char) value;
            }
        }
        out[n++] = c;
        json_at++;
    }
    json_at++; /* closing quote */
    out[n] = 0;
    text_used += n + 1;
    return out;
}

static double read_number(void) {
    double value = 0;
    int negative = 0;
    if (json_src[json_at] == '-') { negative = 1; json_at++; }
    while (json_src[json_at] >= '0' && json_src[json_at] <= '9') {
        value = value * 10 + (double) (json_src[json_at] - '0');
        json_at++;
    }
    if (json_src[json_at] == '.') {
        double scale = 0.1;
        json_at++;
        while (json_src[json_at] >= '0' && json_src[json_at] <= '9') {
            value += (double) (json_src[json_at] - '0') * scale;
            scale /= 10;
            json_at++;
        }
    }
    if (json_src[json_at] == 'e' || json_src[json_at] == 'E') {
        int exponent = 0;
        int exp_negative = 0;
        int k;
        json_at++;
        if (json_src[json_at] == '-') { exp_negative = 1; json_at++; }
        else if (json_src[json_at] == '+') json_at++;
        while (json_src[json_at] >= '0' && json_src[json_at] <= '9') {
            exponent = exponent * 10 + (json_src[json_at] - '0');
            json_at++;
        }
        for (k = 0; k < exponent; k++) value = exp_negative ? value / 10 : value * 10;
    }
    return negative ? -value : value;
}

static int node_type_id(const char *name) {
    if (name == 0) return NT_OTHER;
    if (str_equal(name, "Program")) return NT_Program;
    if (str_equal(name, "Identifier")) return NT_Identifier;
    if (str_equal(name, "ThisExpression")) return NT_ThisExpression;
    if (str_equal(name, "StringLiteral")) return NT_StringLiteral;
    if (str_equal(name, "NumericLiteral")) return NT_NumericLiteral;
    if (str_equal(name, "BooleanLiteral")) return NT_BooleanLiteral;
    if (str_equal(name, "NullLiteral")) return NT_NullLiteral;
    if (str_equal(name, "RegExpLiteral")) return NT_RegExpLiteral;
    if (str_equal(name, "ArrayExpression")) return NT_ArrayExpression;
    if (str_equal(name, "ObjectExpression")) return NT_ObjectExpression;
    if (str_equal(name, "ObjectProperty")) return NT_ObjectProperty;
    if (str_equal(name, "VariableDeclarator")) return NT_VariableDeclarator;
    if (str_equal(name, "SpreadElement")) return NT_SpreadElement;
    if (str_equal(name, "AssignmentPattern")) return NT_AssignmentPattern;
    if (str_equal(name, "ArrayPattern")) return NT_ArrayPattern;
    if (str_equal(name, "MemberExpression")) return NT_MemberExpression;
    if (str_equal(name, "CallExpression")) return NT_CallExpression;
    if (str_equal(name, "NewExpression")) return NT_NewExpression;
    if (str_equal(name, "BinaryExpression")) return NT_BinaryExpression;
    if (str_equal(name, "LogicalExpression")) return NT_LogicalExpression;
    if (str_equal(name, "UnaryExpression")) return NT_UnaryExpression;
    if (str_equal(name, "AssignmentExpression")) return NT_AssignmentExpression;
    if (str_equal(name, "ArrowFunctionExpression")) return NT_ArrowFunctionExpression;
    if (str_equal(name, "FunctionDeclaration")) return NT_FunctionDeclaration;
    if (str_equal(name, "ClassDeclaration")) return NT_ClassDeclaration;
    if (str_equal(name, "ClassBody")) return NT_ClassBody;
    if (str_equal(name, "ClassMethod")) return NT_ClassMethod;
    if (str_equal(name, "VariableDeclaration")) return NT_VariableDeclaration;
    if (str_equal(name, "ReturnStatement")) return NT_ReturnStatement;
    if (str_equal(name, "ExpressionStatement")) return NT_ExpressionStatement;
    if (str_equal(name, "BlockStatement")) return NT_BlockStatement;
    if (str_equal(name, "IfStatement")) return NT_IfStatement;
    if (str_equal(name, "ForOfStatement")) return NT_ForOfStatement;
    if (str_equal(name, "ExportNamedDeclaration")) return NT_ExportNamedDeclaration;
    if (str_equal(name, "ExportSpecifier")) return NT_ExportSpecifier;
    return NT_OTHER;
}

static Node *parse_value(void);
static void skip_value(void);

static void skip_value(void) {
    skip_ws();
    if (json_src[json_at] == '"') { read_string(); return; }
    if (json_src[json_at] == '{' || json_src[json_at] == '[') {
        char open = json_src[json_at];
        char close = open == '{' ? '}' : ']';
        int depth = 0;
        for (;;) {
            char c = json_src[json_at];
            if (c == '"') { read_string(); continue; }
            if (c == open) depth++;
            else if (c == close) { depth--; json_at++; if (depth == 0) return; continue; }
            json_at++;
        }
    }
    while (json_src[json_at] != ',' && json_src[json_at] != '}' &&
           json_src[json_at] != ']' && json_src[json_at] != 0) json_at++;
}

/* Parses a JSON array of nodes into a slice of the shared child pool.
   The elements are collected locally first: a nested list inside one of them
   would otherwise interleave its own children into this slice. */
static Node **parse_node_list(int *out_len) {
    Node *local[MAX_PARSE_LIST];
    Node **items;
    int n = 0;
    int i;
    json_at++; /* '[' */
    skip_ws();
    if (json_src[json_at] == ']') {
        json_at++;
        *out_len = 0;
        return &child_pool[child_count];
    }
    for (;;) {
        local[n++] = parse_value();
        skip_ws();
        if (json_src[json_at] == ',') { json_at++; skip_ws(); continue; }
        json_at++; /* ']' */
        break;
    }
    items = &child_pool[child_count];
    for (i = 0; i < n; i++) child_pool[child_count++] = local[i];
    *out_len = n;
    return items;
}

/* Parses one JSON object into a Node; non-object values yield a null child. */
static Node *parse_value(void) {
    Node *node;
    skip_ws();
    if (json_src[json_at] != '{') { skip_value(); return 0; }
    node = &node_pool[node_count++];
    json_at++;
    skip_ws();
    if (json_src[json_at] == '}') { json_at++; return node; }
    for (;;) {
        const char *key = read_string();
        skip_ws();
        json_at++; /* ':' */
        skip_ws();
        if (str_equal(key, "type")) node->type = node_type_id(read_string());
        else if (str_equal(key, "name")) node->name = read_string();
        else if (str_equal(key, "pattern")) node->pattern = read_string();
        else if (str_equal(key, "flags")) node->flags = read_string();
        else if (str_equal(key, "operator")) node->operator_text = read_string();
        else if (str_equal(key, "kind")) node->kind = read_string();
        else if (str_equal(key, "computed")) { node->computed = json_src[json_at] == 't'; skip_value(); }
        else if (str_equal(key, "shorthand")) { node->shorthand = json_src[json_at] == 't'; skip_value(); }
        else if (str_equal(key, "static")) { node->is_static = json_src[json_at] == 't'; skip_value(); }
        else if (str_equal(key, "async")) { node->is_async = json_src[json_at] == 't'; skip_value(); }
        else if (str_equal(key, "generator")) { node->generator = json_src[json_at] == 't'; skip_value(); }
        else if (str_equal(key, "value")) {
            char c = json_src[json_at];
            if (c == '"') node->text_value = read_string();
            else if (c == '{') node->value = parse_value();
            else if (c == 't' || c == 'f') { node->bool_value = c == 't'; skip_value(); }
            else if (c == 'n') skip_value();
            else { node->num = read_number(); node->has_num = 1; }
        }
        else if (str_equal(key, "key")) node->key = parse_value();
        else if (str_equal(key, "argument")) node->argument = parse_value();
        else if (str_equal(key, "left")) node->left = parse_value();
        else if (str_equal(key, "right")) node->right = parse_value();
        else if (str_equal(key, "object")) node->object = parse_value();
        else if (str_equal(key, "property")) node->property = parse_value();
        else if (str_equal(key, "callee")) node->callee = parse_value();
        else if (str_equal(key, "id")) node->id = parse_value();
        else if (str_equal(key, "init")) node->init = parse_value();
        else if (str_equal(key, "expression")) {
            /* Program-level "expression" flags are booleans; node bodies are objects. */
            if (json_src[json_at] == '{') node->expression = parse_value();
            else skip_value();
        }
        else if (str_equal(key, "test")) node->test = parse_value();
        else if (str_equal(key, "consequent")) node->consequent = parse_value();
        else if (str_equal(key, "alternate")) node->alternate = parse_value();
        else if (str_equal(key, "declaration")) node->declaration = parse_value();
        else if (str_equal(key, "local")) node->local = parse_value();
        else if (str_equal(key, "exported")) node->exported = parse_value();
        else if (str_equal(key, "elements")) node->elements = parse_node_list(&node->elements_len);
        else if (str_equal(key, "properties")) node->properties = parse_node_list(&node->properties_len);
        else if (str_equal(key, "arguments")) node->arguments = parse_node_list(&node->arguments_len);
        else if (str_equal(key, "params")) node->params = parse_node_list(&node->params_len);
        else if (str_equal(key, "declarations")) node->declarations = parse_node_list(&node->declarations_len);
        else if (str_equal(key, "specifiers")) node->specifiers = parse_node_list(&node->specifiers_len);
        else if (str_equal(key, "body")) {
            if (json_src[json_at] == '[') node->body_list = parse_node_list(&node->body_list_len);
            else node->body_node = parse_value();
        }
        else skip_value();
        skip_ws();
        if (json_src[json_at] == ',') { json_at++; skip_ws(); continue; }
        json_at++; /* '}' */
        break;
    }
    return node;
}

/* ----------------------------------------------------------- document arena */

enum { D_TEXT, D_CONCAT, D_INDENT, D_GROUP, D_IF_BREAK, D_LINE, D_SOFTLINE, D_HARDLINE };

typedef struct Doc Doc;
struct Doc {
    int kind;
    const char *text;
    Doc **parts; int parts_len;
    Doc *contents;
    long break_threshold;
    Doc *broken, *flat;
};

static Doc doc_arena[DOC_ARENA];
static int doc_used = 0;
static Doc *parts_arena[DOC_PARTS_ARENA];
static int parts_used = 0;

static Doc line_doc_v, softline_doc_v, hardline_doc_v;

static Doc *doc_new(int kind) {
    Doc *d = &doc_arena[doc_used++];
    d->kind = kind;
    d->text = 0;
    d->parts = 0;
    d->parts_len = 0;
    d->contents = 0;
    d->break_threshold = LARGE_LENGTH;
    d->broken = 0;
    d->flat = 0;
    return d;
}

static Doc *text_doc(const char *value) {
    Doc *d = doc_new(D_TEXT);
    d->text = value;
    return d;
}

static Doc *concat_docs(Doc **parts, int len) {
    Doc *d = doc_new(D_CONCAT);
    d->parts = parts;
    d->parts_len = len;
    return d;
}

static Doc *indent_doc(Doc *contents) {
    Doc *d = doc_new(D_INDENT);
    d->contents = contents;
    return d;
}

static Doc *group_doc(Doc *contents, long break_threshold) {
    Doc *d = doc_new(D_GROUP);
    d->contents = contents;
    d->break_threshold = break_threshold;
    return d;
}

static Doc *if_break_doc(Doc *broken) {
    Doc *d = doc_new(D_IF_BREAK);
    d->broken = broken;
    d->flat = text_doc("");
    return d;
}

/* --------------------------------------------------------------- text building */

static const char *concat_text(const char *a, const char *b) {
    int la = str_length(a);
    int lb = str_length(b);
    char *out = text_alloc(la + lb + 1);
    int i;
    for (i = 0; i < la; i++) out[i] = a[i];
    for (i = 0; i < lb; i++) out[la + i] = b[i];
    out[la + lb] = 0;
    return out;
}

/* Shortest decimal form for the fixture's literal values, matching Lambda's string(). */
static const char *number_text(double value) {
    char *out = text_alloc(32);
    int at = 0;
    long whole;
    double fraction;
    if (value < 0) { out[at++] = '-'; value = -value; }
    whole = (long) value;
    fraction = value - (double) whole;
    {
        char digits[24];
        int n = 0;
        if (whole == 0) digits[n++] = '0';
        while (whole > 0) { digits[n++] = (char) ('0' + whole % 10); whole /= 10; }
        while (n > 0) out[at++] = digits[--n];
    }
    if (fraction > 1e-9) {
        int k;
        int end;
        out[at++] = '.';
        end = at;
        for (k = 0; k < 9; k++) {
            int digit;
            fraction *= 10;
            digit = (int) (fraction + 1e-6);
            if (digit > 9) digit = 9;
            fraction -= (double) digit;
            out[at++] = (char) ('0' + digit);
            if (digit != 0) end = at;
            if (fraction < 1e-6) break;
        }
        at = end;
    }
    out[at] = 0;
    return out;
}

static const char *json_quote(const char *value) {
    int n = str_length(value);
    char *out = text_alloc(n * 2 + 3);
    int at = 0;
    int i;
    out[at++] = '"';
    for (i = 0; i < n; i++) {
        char c = value[i];
        if (c == '\\') { out[at++] = '\\'; out[at++] = '\\'; }
        else if (c == '"') { out[at++] = '\\'; out[at++] = '"'; }
        else if (c == '\n') { out[at++] = '\\'; out[at++] = 'n'; }
        else if (c == '\r') { out[at++] = '\\'; out[at++] = 'r'; }
        else if (c == '\t') { out[at++] = '\\'; out[at++] = 't'; }
        else out[at++] = c;
    }
    out[at++] = '"';
    out[at] = 0;
    return out;
}

/* --------------------------------------------------------------- doc measuring */

static long flat_length(const Doc *doc) {
    int i;
    long total = 0;
    switch (doc->kind) {
    case D_TEXT: return (long) str_length(doc->text);
    case D_CONCAT:
        for (i = 0; i < doc->parts_len; i++) total += flat_length(doc->parts[i]);
        return total;
    case D_INDENT: return flat_length(doc->contents);
    case D_GROUP: return flat_length(doc->contents);
    case D_IF_BREAK: return flat_length(doc->flat);
    case D_LINE: return 1;
    case D_SOFTLINE: return 0;
    default: return LARGE_LENGTH;
    }
}

static int fits(const Doc *doc, long remaining) {
    return flat_length(doc) <= remaining;
}

/* --------------------------------------------------------------- rendering */

static char out_buffer[OUT_SIZE];
static int out_at = 0;

static void emit(const char *text) {
    int i = 0;
    while (text[i] != 0) out_buffer[out_at++] = text[i++];
}

static void emit_indent(long level) {
    long i;
    out_buffer[out_at++] = '\n';
    for (i = 0; i < level; i++) { out_buffer[out_at++] = ' '; out_buffer[out_at++] = ' '; }
}

static long render_doc(const Doc *doc, long column, long indent, int flat_mode) {
    int i;
    switch (doc->kind) {
    case D_TEXT:
        emit(doc->text);
        return column + (long) str_length(doc->text);
    case D_CONCAT:
        for (i = 0; i < doc->parts_len; i++) column = render_doc(doc->parts[i], column, indent, flat_mode);
        return column;
    case D_INDENT:
        return render_doc(doc->contents, column, indent + 1, flat_mode);
    case D_GROUP: {
        int is_flat = flat_mode ||
            (doc->break_threshold >= flat_length(doc->contents) &&
             fits(doc->contents, (long) PRINT_WIDTH - column));
        return render_doc(doc->contents, column, indent, is_flat);
    }
    case D_IF_BREAK:
        return render_doc(flat_mode ? doc->flat : doc->broken, column, indent, flat_mode);
    case D_LINE:
        if (flat_mode) { emit(" "); return column + 1; }
        emit_indent(indent);
        return indent * 2;
    case D_SOFTLINE:
        if (flat_mode) return column;
        emit_indent(indent);
        return indent * 2;
    default:
        emit_indent(indent);
        return indent * 2;
    }
}

/* --------------------------------------------------------------- AST printer */

#define MAX_LIST 128

static Doc *print_node(const Node *node);
static Doc *print_statement(const Node *node);
static Doc *print_expression(const Node *node, long parent_precedence);

/* Commits an already-built item array into the shared parts arena. */
static Doc *concat_of(Doc **items, int n) {
    Doc **slot = &parts_arena[parts_used];
    int i;
    for (i = 0; i < n; i++) parts_arena[parts_used++] = items[i];
    return concat_docs(slot, n);
}

static Doc *join_docs(Doc *separator, Doc **printed, int count) {
    Doc *items[MAX_LIST];
    int n = 0;
    int i;
    for (i = 0; i < count; i++) {
        if (i > 0) items[n++] = separator;
        items[n++] = printed[i];
    }
    return concat_of(items, n);
}

static Doc *comma_line(void) {
    Doc *items[2];
    items[0] = text_doc(",");
    items[1] = &line_doc_v;
    return concat_of(items, 2);
}

static int print_nodes(Node **nodes, int count, Doc **out) {
    int i;
    for (i = 0; i < count; i++) out[i] = print_node(nodes[i]);
    return count;
}

static int print_statements(Node **nodes, int count, Doc **out) {
    int i;
    for (i = 0; i < count; i++) out[i] = print_statement(nodes[i]);
    return count;
}

/* group("(" indent(softline join(", " | "," line)) if_break(",") softline ")") */
static Doc *bracketed_group(const char *open, const char *close, Doc **printed, int count) {
    Doc *inner[2];
    Doc *items[5];
    inner[0] = &softline_doc_v;
    inner[1] = join_docs(comma_line(), printed, count);
    items[0] = text_doc(open);
    items[1] = indent_doc(concat_of(inner, 2));
    items[2] = if_break_doc(text_doc(","));
    items[3] = &softline_doc_v;
    items[4] = text_doc(close);
    return group_doc(concat_of(items, 5), LARGE_LENGTH);
}

static Doc *parameter_list(Node **params, int count) {
    Doc *printed[MAX_LIST];
    print_nodes(params, count, printed);
    return bracketed_group("(", ")", printed, count);
}

static int has_object_argument(Node **args, int count) {
    int i;
    for (i = 0; i < count; i++) if (args[i] && args[i]->type == NT_ObjectExpression) return 1;
    return 0;
}

static Doc *argument_list(Node **args, int count) {
    Doc *printed[MAX_LIST];
    if (count == 0) return text_doc("()");
    print_nodes(args, count, printed);
    if (has_object_argument(args, count)) {
        Doc *items[3];
        items[0] = text_doc("(");
        items[1] = join_docs(text_doc(", "), printed, count);
        items[2] = text_doc(")");
        return concat_of(items, 3);
    }
    return bracketed_group("(", ")", printed, count);
}

static Doc *array_doc(Node **elements, int count) {
    Doc *printed[MAX_LIST];
    if (count == 0) return text_doc("[]");
    print_nodes(elements, count, printed);
    return bracketed_group("[", "]", printed, count);
}

static Doc *object_doc(Node **properties, int count) {
    Doc *printed[MAX_LIST];
    Doc *inner[3];
    Doc *items[4];
    if (count == 0) return text_doc("{}");
    print_nodes(properties, count, printed);
    inner[0] = &line_doc_v;
    inner[1] = join_docs(comma_line(), printed, count);
    inner[2] = if_break_doc(text_doc(","));
    items[0] = text_doc("{");
    items[1] = indent_doc(concat_of(inner, 3));
    items[2] = &line_doc_v;
    items[3] = text_doc("}");
    return group_doc(concat_of(items, 4), LARGE_LENGTH);
}

static Doc *hardline_block(const char *open, const char *close, Doc **printed, int count) {
    Doc *inner[2];
    Doc *items[4];
    inner[0] = &hardline_doc_v;
    inner[1] = join_docs(&hardline_doc_v, printed, count);
    items[0] = text_doc(open);
    items[1] = indent_doc(concat_of(inner, 2));
    items[2] = &hardline_doc_v;
    items[3] = text_doc(close);
    return concat_of(items, 4);
}

static Doc *block_doc(Node **body, int count) {
    Doc *printed[MAX_LIST];
    if (count == 0) return text_doc("{}");
    print_statements(body, count, printed);
    return hardline_block("{", "}", printed, count);
}

static Doc *variable_doc(const Node *node, int terminator) {
    Doc *printed[MAX_LIST];
    Doc *head[2];
    Doc *items[2];
    Doc *declaration;
    print_nodes(node->declarations, node->declarations_len, printed);
    head[0] = text_doc(concat_text(node->kind, " "));
    head[1] = join_docs(comma_line(), printed, node->declarations_len);
    declaration = concat_of(head, 2);
    if (!terminator) return declaration;
    items[0] = declaration;
    items[1] = text_doc(";");
    return concat_of(items, 2);
}

static Doc *node_key(const Node *node) {
    Doc *items[3];
    if (!node->computed) return print_node(node->key);
    items[0] = text_doc("[");
    items[1] = print_node(node->key);
    items[2] = text_doc("]");
    return concat_of(items, 3);
}

static Doc *print_property(const Node *node) {
    Doc *items[3];
    if (node->type == NT_SpreadElement) {
        items[0] = text_doc("...");
        items[1] = print_node(node->argument);
        return concat_of(items, 2);
    }
    items[0] = node_key(node);
    if (node->shorthand) return items[0];
    items[1] = text_doc(": ");
    items[2] = print_expression(node->value, 0);
    return concat_of(items, 3);
}

static Doc *literal_doc(const Node *node) {
    if (node->type == NT_StringLiteral) return text_doc(json_quote(node->text_value));
    if (node->type == NT_NumericLiteral) return text_doc(number_text(node->num));
    if (node->type == NT_BooleanLiteral) return text_doc(node->bool_value ? "true" : "false");
    if (node->type == NT_NullLiteral) return text_doc("null");
    return text_doc(concat_text(concat_text(concat_text("/", node->pattern), "/"), node->flags));
}

static long expression_precedence(const Node *node) {
    const char *op;
    if (node == 0) return 100;
    if (node->type == NT_AssignmentExpression || node->type == NT_ArrowFunctionExpression) return 1;
    if (node->type == NT_LogicalExpression) return str_equal(node->operator_text, "&&") ? 3 : 2;
    if (node->type == NT_BinaryExpression) {
        op = node->operator_text;
        if (str_equal(op, "*") || str_equal(op, "/") || str_equal(op, "%")) return 12;
        if (str_equal(op, "+") || str_equal(op, "-")) return 11;
        if (str_equal(op, "<") || str_equal(op, "<=") || str_equal(op, ">") ||
            str_equal(op, ">=") || str_equal(op, "in") || str_equal(op, "instanceof")) return 9;
        if (str_equal(op, "==") || str_equal(op, "!=") ||
            str_equal(op, "===") || str_equal(op, "!==")) return 8;
        return 7;
    }
    return 20;
}

static Doc *print_expression(const Node *node, long parent_precedence) {
    Doc *doc = print_node(node);
    Doc *items[3];
    if (expression_precedence(node) >= parent_precedence) return doc;
    items[0] = text_doc("(");
    items[1] = doc;
    items[2] = text_doc(")");
    return concat_of(items, 3);
}

static int flatten_additive(const Node *node, const Node **out, int n) {
    if (node && node->type == NT_BinaryExpression && str_equal(node->operator_text, "+")) {
        n = flatten_additive(node->left, out, n);
        out[n++] = node->right;
        return n;
    }
    out[n++] = node;
    return n;
}

static Doc *additive_chain_doc(const Node *node) {
    const Node *operands[MAX_LIST];
    Doc *tail[MAX_LIST];
    Doc *items[2];
    int count = flatten_additive(node, operands, 0);
    int n = 0;
    int index;
    for (index = 1; index < count; index++) {
        tail[n++] = text_doc(" +");
        tail[n++] = &line_doc_v;
        tail[n++] = print_expression(operands[index], 12);
    }
    items[0] = print_expression(operands[0], 11);
    items[1] = indent_doc(concat_of(tail, n));
    return group_doc(concat_of(items, 2), 60);
}

static Doc *print_node(const Node *node) {
    Doc *items[8];
    Doc *printed[MAX_LIST];
    if (node == 0) return text_doc("");
    switch (node->type) {
    case NT_Identifier: return text_doc(node->name);
    case NT_ThisExpression: return text_doc("this");
    case NT_StringLiteral: case NT_NumericLiteral: case NT_BooleanLiteral:
    case NT_NullLiteral: case NT_RegExpLiteral:
        return literal_doc(node);
    case NT_ArrayExpression: return array_doc(node->elements, node->elements_len);
    case NT_ObjectExpression: return object_doc(node->properties, node->properties_len);
    case NT_ObjectProperty: return print_property(node);
    case NT_VariableDeclarator:
        if (node->init == 0) return print_node(node->id);
        items[0] = print_node(node->id);
        items[1] = text_doc(" = ");
        items[2] = print_expression(node->init, 0);
        return concat_of(items, 3);
    case NT_SpreadElement:
        items[0] = text_doc("...");
        items[1] = print_node(node->argument);
        return concat_of(items, 2);
    case NT_AssignmentPattern:
        items[0] = print_node(node->left);
        items[1] = text_doc(" = ");
        items[2] = print_node(node->right);
        return concat_of(items, 3);
    case NT_ArrayPattern: return array_doc(node->elements, node->elements_len);
    case NT_MemberExpression:
        items[0] = print_expression(node->object, 20);
        if (node->computed) {
            items[1] = text_doc("[");
            items[2] = print_expression(node->property, 0);
            items[3] = text_doc("]");
        } else {
            items[1] = text_doc(".");
            items[2] = print_node(node->property);
            return concat_of(items, 3);
        }
        return concat_of(items, 4);
    case NT_CallExpression:
        if (node->arguments_len == 1 &&
            node->arguments[0] && node->arguments[0]->type == NT_ArrowFunctionExpression) {
            const Node *arrow = node->arguments[0];
            Doc *inner[2];
            items[0] = print_expression(node->callee, 20);
            items[1] = text_doc("(");
            items[2] = parameter_list(arrow->params, arrow->params_len);
            items[3] = text_doc(" =>");
            inner[0] = &line_doc_v;
            inner[1] = print_expression(arrow->body_node, 0);
            items[4] = indent_doc(concat_of(inner, 2));
            items[5] = if_break_doc(text_doc(","));
            items[6] = &softline_doc_v;
            items[7] = text_doc(")");
            return group_doc(concat_of(items, 8), LARGE_LENGTH);
        }
        items[0] = print_expression(node->callee, 20);
        items[1] = argument_list(node->arguments, node->arguments_len);
        return concat_of(items, 2);
    case NT_NewExpression:
        items[0] = text_doc("new ");
        items[1] = print_expression(node->callee, 20);
        items[2] = argument_list(node->arguments, node->arguments_len);
        return concat_of(items, 3);
    case NT_BinaryExpression: case NT_LogicalExpression: {
        long precedence = expression_precedence(node);
        long bump = 0;
        Doc *inner[2];
        if (node->type == NT_BinaryExpression && str_equal(node->operator_text, "+") &&
            node->left && node->left->type == NT_BinaryExpression &&
            str_equal(node->left->operator_text, "+")) {
            return additive_chain_doc(node);
        }
        if (str_equal(node->operator_text, "&&") || str_equal(node->operator_text, "||")) bump = 1;
        items[0] = print_expression(node->left, precedence);
        items[1] = text_doc(concat_text(" ", node->operator_text));
        inner[0] = &line_doc_v;
        inner[1] = print_expression(node->right, precedence + bump);
        items[2] = indent_doc(concat_of(inner, 2));
        return group_doc(concat_of(items, 3), LARGE_LENGTH);
    }
    case NT_UnaryExpression:
        if (str_equal(node->operator_text, "!")) items[0] = text_doc("!");
        else items[0] = text_doc(concat_text(node->operator_text, " "));
        items[1] = print_expression(node->argument, 20);
        return concat_of(items, 2);
    case NT_AssignmentExpression:
        items[0] = print_expression(node->left, 2);
        items[1] = text_doc(concat_text(concat_text(" ", node->operator_text), " "));
        items[2] = print_expression(node->right, 1);
        return concat_of(items, 3);
    case NT_ArrowFunctionExpression:
        items[0] = parameter_list(node->params, node->params_len);
        items[1] = text_doc(" => ");
        items[2] = print_expression(node->body_node, 1);
        return concat_of(items, 3);
    case NT_FunctionDeclaration:
        items[0] = text_doc(node->is_async ? "async function " : "function ");
        items[1] = text_doc(node->generator ? "*" : "");
        items[2] = print_node(node->id);
        items[3] = parameter_list(node->params, node->params_len);
        items[4] = text_doc(" ");
        items[5] = block_doc(node->body_node->body_list, node->body_node->body_list_len);
        return concat_of(items, 6);
    case NT_ClassDeclaration:
        items[0] = text_doc("class ");
        items[1] = print_node(node->id);
        items[2] = text_doc(" ");
        items[3] = print_node(node->body_node);
        return concat_of(items, 4);
    case NT_ClassBody:
        if (node->body_list_len == 0) return text_doc("{}");
        print_nodes(node->body_list, node->body_list_len, printed);
        return hardline_block("{", "}", printed, node->body_list_len);
    case NT_ClassMethod:
        items[0] = text_doc(node->is_static ? "static " : "");
        items[1] = text_doc(node->is_async ? "async " : "");
        items[2] = text_doc(node->generator ? "*" : "");
        items[3] = node_key(node);
        items[4] = parameter_list(node->params, node->params_len);
        items[5] = text_doc(" ");
        items[6] = block_doc(node->body_node->body_list, node->body_node->body_list_len);
        return concat_of(items, 7);
    default:
        return print_statement(node);
    }
}

static Doc *print_statement(const Node *node) {
    Doc *items[6];
    Doc *printed[MAX_LIST];
    switch (node->type) {
    case NT_VariableDeclaration: return variable_doc(node, 1);
    case NT_ReturnStatement:
        items[0] = text_doc("return");
        if (node->argument == 0) items[1] = text_doc("");
        else {
            Doc *inner[2];
            inner[0] = text_doc(" ");
            inner[1] = print_node(node->argument);
            items[1] = concat_of(inner, 2);
        }
        items[2] = text_doc(";");
        return concat_of(items, 3);
    case NT_ExpressionStatement:
        items[0] = print_node(node->expression);
        items[1] = text_doc(";");
        return concat_of(items, 2);
    case NT_BlockStatement: return block_doc(node->body_list, node->body_list_len);
    case NT_IfStatement:
        if (node->consequent && node->consequent->type == NT_BlockStatement) {
            items[0] = text_doc("if (");
            items[1] = print_expression(node->test, 0);
            items[2] = text_doc(") ");
            items[3] = print_statement(node->consequent);
            if (node->alternate == 0) items[4] = text_doc("");
            else {
                Doc *inner[2];
                inner[0] = text_doc(" else ");
                inner[1] = print_statement(node->alternate);
                items[4] = concat_of(inner, 2);
            }
            return concat_of(items, 5);
        } else {
            Doc *inner[2];
            Doc *alt[4];
            items[0] = text_doc("if (");
            items[1] = print_expression(node->test, 0);
            items[2] = text_doc(")");
            inner[0] = &line_doc_v;
            inner[1] = print_statement(node->consequent);
            items[3] = indent_doc(concat_of(inner, 2));
            if (node->alternate == 0) items[4] = text_doc("");
            else {
                alt[0] = &line_doc_v;
                alt[1] = text_doc("else");
                alt[2] = &line_doc_v;
                alt[3] = print_statement(node->alternate);
                items[4] = indent_doc(concat_of(alt, 4));
            }
            return group_doc(concat_of(items, 5), LARGE_LENGTH);
        }
    case NT_ForOfStatement:
        items[0] = text_doc("for (");
        items[1] = variable_doc(node->left, 0);
        items[2] = text_doc(" of ");
        items[3] = print_node(node->right);
        items[4] = text_doc(") ");
        items[5] = print_statement(node->body_node);
        return concat_of(items, 6);
    case NT_FunctionDeclaration: case NT_ClassDeclaration:
        return print_node(node);
    case NT_ExportNamedDeclaration:
        if (node->declaration != 0) {
            items[0] = text_doc("export ");
            items[1] = print_statement(node->declaration);
            return concat_of(items, 2);
        }
        print_nodes(node->specifiers, node->specifiers_len, printed);
        items[0] = text_doc("export { ");
        items[1] = join_docs(text_doc(", "), printed, node->specifiers_len);
        items[2] = text_doc(" };");
        return concat_of(items, 3);
    case NT_ExportSpecifier:
        if (str_equal(node->local->name, node->exported->name)) return print_node(node->local);
        items[0] = print_node(node->local);
        items[1] = text_doc(" as ");
        items[2] = print_node(node->exported);
        return concat_of(items, 3);
    default:
        return text_doc("");
    }
}

static void print_program(const Node *program) {
    Doc *printed[MAX_LIST];
    Doc *items[2];
    Doc *document;
    print_statements(program->body_list, program->body_list_len, printed);
    items[0] = join_docs(&hardline_doc_v, printed, program->body_list_len);
    items[1] = &hardline_doc_v;
    document = concat_of(items, 2);
    out_at = 0;
    render_doc(document, 0, 0, 0);
    out_buffer[out_at] = 0;
}

int main(void) {
    const Node *ast;
    long checksum = 0;
    int iteration;
    int index;
    int ast_text_used;

    line_doc_v.kind = D_LINE;
    softline_doc_v.kind = D_SOFTLINE;
    hardline_doc_v.kind = D_HARDLINE;

    json_src = AST_JSON;
    json_at = 0;
    ast = parse_value();
    ast_text_used = text_used;

    for (iteration = 0; iteration < ITERATIONS; iteration++) {
        /* the document IR is rebuilt from scratch every iteration, as in the
           Lambda port; only the parsed AST and its strings survive across them */
        doc_used = 0;
        parts_used = 0;
        text_used = ast_text_used;
        print_program(ast);
    }

    for (index = 0; index < out_at; index++) {
        checksum = (checksum * 31 + (long) (unsigned char) out_buffer[index]) % 1000000007L;
    }
    printf("%s", out_buffer);
    printf("prettier_ast: CHECKSUM:%ld\n", checksum);
    return checksum != 56483873;
}
