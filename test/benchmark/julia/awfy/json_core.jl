# port of test/benchmark/awfy/python/json.py; algorithms retain their original control flow.
# This code is derived from the SOM benchmarks, see AUTHORS.md file.
# This benchmark is based on the minimal-json Java library maintained at:
# https://github.com/ralfstx/minimal-json
#
# Copyright (c) 2015-2021 Stefan Marr <gitself._stefan-marr.de>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the 'Software'), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
const _RAP_BENCHMARK_MINIFIED = "{\"head\":{\"requestCounter\":4},\"operations\":[[\"destroy\",\"w54\"],[\"set\",\"w2\",{\"activeControl\":\"w99\"}],[\"set\",\"w21\",{\"customVariant\":\"variant_navigation\"}],[\"set\",\"w28\",{\"customVariant\":\"variant_selected\"}],[\"set\",\"w53\",{\"children\":[\"w95\"]}],[\"create\",\"w95\",\"rwt.widgets.Composite\",{\"parent\":\"w53\",\"style\":[\"NONE\"],\"bounds\":[0,0,1008,586],\"children\":[\"w96\",\"w97\"],\"tabIndex\":-1,\"clientArea\":[0,0,1008,586]}],[\"create\",\"w96\",\"rwt.widgets.Label\",{\"parent\":\"w95\",\"style\":[\"NONE\"],\"bounds\":[10,30,112,26],\"tabIndex\":-1,\"customVariant\":\"variant_pageHeadline\",\"text\":\"TableViewer\"}],[\"create\",\"w97\",\"rwt.widgets.Composite\",{\"parent\":\"w95\",\"style\":[\"NONE\"],\"bounds\":[0,61,1008,525],\"children\":[\"w98\",\"w99\",\"w226\",\"w228\"],\"tabIndex\":-1,\"clientArea\":[0,0,1008,525]}],[\"create\",\"w98\",\"rwt.widgets.Text\",{\"parent\":\"w97\",\"style\":[\"LEFT\",\"SINGLE\",\"BORDER\"],\"bounds\":[10,10,988,32],\"tabIndex\":22,\"activeKeys\":[\"#13\",\"#27\",\"#40\"]}],[\"listen\",\"w98\",{\"KeyDown\":true,\"Modify\":true}],[\"create\",\"w99\",\"rwt.widgets.Grid\",{\"parent\":\"w97\",\"style\":[\"SINGLE\",\"BORDER\"],\"appearance\":\"table\",\"indentionWidth\":0,\"treeColumn\":-1,\"markupEnabled\":false}],[\"create\",\"w100\",\"rwt.widgets.ScrollBar\",{\"parent\":\"w99\",\"style\":[\"HORIZONTAL\"]}],[\"create\",\"w101\",\"rwt.widgets.ScrollBar\",{\"parent\":\"w99\",\"style\":[\"VERTICAL\"]}],[\"set\",\"w99\",{\"bounds\":[10,52,988,402],\"children\":[],\"tabIndex\":23,\"activeKeys\":[\"CTRL+#70\",\"CTRL+#78\",\"CTRL+#82\",\"CTRL+#89\",\"CTRL+#83\",\"CTRL+#71\",\"CTRL+#69\"],\"cancelKeys\":[\"CTRL+#70\",\"CTRL+#78\",\"CTRL+#82\",\"CTRL+#89\",\"CTRL+#83\",\"CTRL+#71\",\"CTRL+#69\"]}],[\"listen\",\"w99\",{\"MouseDown\":true,\"MouseUp\":true,\"MouseDoubleClick\":true,\"KeyDown\":true}],[\"set\",\"w99\",{\"itemCount\":118,\"itemHeight\":28,\"itemMetrics\":[[0,0,50,3,0,3,44],[1,50,50,53,0,53,44],[2,100,140,103,0,103,134],[3,240,180,243,0,243,174],[4,420,50,423,0,423,44],[5,470,50,473,0,473,44]],\"columnCount\":6,\"headerHeight\":35,\"headerVisible\":true,\"linesVisible\":true,\"focusItem\":\"w108\",\"selection\":[\"w108\"]}],[\"listen\",\"w99\",{\"Selection\":true,\"DefaultSelection\":true}],[\"set\",\"w99\",{\"enableCellToolTip\":true}],[\"listen\",\"w100\",{\"Selection\":true}],[\"set\",\"w101\",{\"visibility\":true}],[\"listen\",\"w101\",{\"Selection\":true}],[\"create\",\"w102\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Nr.\",\"width\":50,\"moveable\":true}],[\"listen\",\"w102\",{\"Selection\":true}],[\"create\",\"w103\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Sym.\",\"index\":1,\"left\":50,\"width\":50,\"moveable\":true}],[\"listen\",\"w103\",{\"Selection\":true}],[\"create\",\"w104\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Name\",\"index\":2,\"left\":100,\"width\":140,\"moveable\":true}],[\"listen\",\"w104\",{\"Selection\":true}],[\"create\",\"w105\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Series\",\"index\":3,\"left\":240,\"width\":180,\"moveable\":true}],[\"listen\",\"w105\",{\"Selection\":true}],[\"create\",\"w106\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Group\",\"index\":4,\"left\":420,\"width\":50,\"moveable\":true}],[\"listen\",\"w106\",{\"Selection\":true}],[\"create\",\"w107\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Period\",\"index\":5,\"left\":470,\"width\":50,\"moveable\":true}],[\"listen\",\"w107\",{\"Selection\":true}],[\"create\",\"w108\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":0,\"texts\":[\"1\",\"H\",\"Hydrogen\",\"Nonmetal\",\"1\",\"1\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w109\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":1,\"texts\":[\"2\",\"He\",\"Helium\",\"Noble gas\",\"18\",\"1\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w110\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":2,\"texts\":[\"3\",\"Li\",\"Lithium\",\"Alkali metal\",\"1\",\"2\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w111\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":3,\"texts\":[\"4\",\"Be\",\"Beryllium\",\"Alkaline earth metal\",\"2\",\"2\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w112\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":4,\"texts\":[\"5\",\"B\",\"Boron\",\"Metalloid\",\"13\",\"2\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w113\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":5,\"texts\":[\"6\",\"C\",\"Carbon\",\"Nonmetal\",\"14\",\"2\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w114\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":6,\"texts\":[\"7\",\"N\",\"Nitrogen\",\"Nonmetal\",\"15\",\"2\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w115\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":7,\"texts\":[\"8\",\"O\",\"Oxygen\",\"Nonmetal\",\"16\",\"2\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w116\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":8,\"texts\":[\"9\",\"F\",\"Fluorine\",\"Halogen\",\"17\",\"2\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w117\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":9,\"texts\":[\"10\",\"Ne\",\"Neon\",\"Noble gas\",\"18\",\"2\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w118\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":10,\"texts\":[\"11\",\"Na\",\"Sodium\",\"Alkali metal\",\"1\",\"3\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w119\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":11,\"texts\":[\"12\",\"Mg\",\"Magnesium\",\"Alkaline earth metal\",\"2\",\"3\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w120\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":12,\"texts\":[\"13\",\"Al\",\"Aluminium\",\"Poor metal\",\"13\",\"3\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w121\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":13,\"texts\":[\"14\",\"Si\",\"Silicon\",\"Metalloid\",\"14\",\"3\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w122\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":14,\"texts\":[\"15\",\"P\",\"Phosphorus\",\"Nonmetal\",\"15\",\"3\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w123\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":15,\"texts\":[\"16\",\"S\",\"Sulfur\",\"Nonmetal\",\"16\",\"3\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w124\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":16,\"texts\":[\"17\",\"Cl\",\"Chlorine\",\"Halogen\",\"17\",\"3\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w125\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":17,\"texts\":[\"18\",\"Ar\",\"Argon\",\"Noble gas\",\"18\",\"3\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w126\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":18,\"texts\":[\"19\",\"K\",\"Potassium\",\"Alkali metal\",\"1\",\"4\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w127\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":19,\"texts\":[\"20\",\"Ca\",\"Calcium\",\"Alkaline earth metal\",\"2\",\"4\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w128\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":20,\"texts\":[\"21\",\"Sc\",\"Scandium\",\"Transition metal\",\"3\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w129\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":21,\"texts\":[\"22\",\"Ti\",\"Titanium\",\"Transition metal\",\"4\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w130\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":22,\"texts\":[\"23\",\"V\",\"Vanadium\",\"Transition metal\",\"5\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w131\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":23,\"texts\":[\"24\",\"Cr\",\"Chromium\",\"Transition metal\",\"6\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w132\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":24,\"texts\":[\"25\",\"Mn\",\"Manganese\",\"Transition metal\",\"7\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w133\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":25,\"texts\":[\"26\",\"Fe\",\"Iron\",\"Transition metal\",\"8\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w134\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":26,\"texts\":[\"27\",\"Co\",\"Cobalt\",\"Transition metal\",\"9\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w135\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":27,\"texts\":[\"28\",\"Ni\",\"Nickel\",\"Transition metal\",\"10\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w136\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":28,\"texts\":[\"29\",\"Cu\",\"Copper\",\"Transition metal\",\"11\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w137\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":29,\"texts\":[\"30\",\"Zn\",\"Zinc\",\"Transition metal\",\"12\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w138\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":30,\"texts\":[\"31\",\"Ga\",\"Gallium\",\"Poor metal\",\"13\",\"4\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w139\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":31,\"texts\":[\"32\",\"Ge\",\"Germanium\",\"Metalloid\",\"14\",\"4\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w140\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":32,\"texts\":[\"33\",\"As\",\"Arsenic\",\"Metalloid\",\"15\",\"4\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w141\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":33,\"texts\":[\"34\",\"Se\",\"Selenium\",\"Nonmetal\",\"16\",\"4\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w142\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":34,\"texts\":[\"35\",\"Br\",\"Bromine\",\"Halogen\",\"17\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w143\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":35,\"texts\":[\"36\",\"Kr\",\"Krypton\",\"Noble gas\",\"18\",\"4\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w144\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":36,\"texts\":[\"37\",\"Rb\",\"Rubidium\",\"Alkali metal\",\"1\",\"5\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w145\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":37,\"texts\":[\"38\",\"Sr\",\"Strontium\",\"Alkaline earth metal\",\"2\",\"5\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w146\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":38,\"texts\":[\"39\",\"Y\",\"Yttrium\",\"Transition metal\",\"3\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w147\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":39,\"texts\":[\"40\",\"Zr\",\"Zirconium\",\"Transition metal\",\"4\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w148\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":40,\"texts\":[\"41\",\"Nb\",\"Niobium\",\"Transition metal\",\"5\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w149\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":41,\"texts\":[\"42\",\"Mo\",\"Molybdenum\",\"Transition metal\",\"6\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w150\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":42,\"texts\":[\"43\",\"Tc\",\"Technetium\",\"Transition metal\",\"7\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w151\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":43,\"texts\":[\"44\",\"Ru\",\"Ruthenium\",\"Transition metal\",\"8\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w152\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":44,\"texts\":[\"45\",\"Rh\",\"Rhodium\",\"Transition metal\",\"9\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w153\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":45,\"texts\":[\"46\",\"Pd\",\"Palladium\",\"Transition metal\",\"10\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w154\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":46,\"texts\":[\"47\",\"Ag\",\"Silver\",\"Transition metal\",\"11\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w155\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":47,\"texts\":[\"48\",\"Cd\",\"Cadmium\",\"Transition metal\",\"12\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w156\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":48,\"texts\":[\"49\",\"In\",\"Indium\",\"Poor metal\",\"13\",\"5\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w157\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":49,\"texts\":[\"50\",\"Sn\",\"Tin\",\"Poor metal\",\"14\",\"5\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w158\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":50,\"texts\":[\"51\",\"Sb\",\"Antimony\",\"Metalloid\",\"15\",\"5\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w159\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":51,\"texts\":[\"52\",\"Te\",\"Tellurium\",\"Metalloid\",\"16\",\"5\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w160\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":52,\"texts\":[\"53\",\"I\",\"Iodine\",\"Halogen\",\"17\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w161\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":53,\"texts\":[\"54\",\"Xe\",\"Xenon\",\"Noble gas\",\"18\",\"5\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w162\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":54,\"texts\":[\"55\",\"Cs\",\"Caesium\",\"Alkali metal\",\"1\",\"6\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w163\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":55,\"texts\":[\"56\",\"Ba\",\"Barium\",\"Alkaline earth metal\",\"2\",\"6\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w164\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":56,\"texts\":[\"57\",\"La\",\"Lanthanum\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w165\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":57,\"texts\":[\"58\",\"Ce\",\"Cerium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w166\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":58,\"texts\":[\"59\",\"Pr\",\"Praseodymium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w167\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":59,\"texts\":[\"60\",\"Nd\",\"Neodymium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w168\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":60,\"texts\":[\"61\",\"Pm\",\"Promethium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w169\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":61,\"texts\":[\"62\",\"Sm\",\"Samarium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w170\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":62,\"texts\":[\"63\",\"Eu\",\"Europium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w171\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":63,\"texts\":[\"64\",\"Gd\",\"Gadolinium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w172\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":64,\"texts\":[\"65\",\"Tb\",\"Terbium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w173\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":65,\"texts\":[\"66\",\"Dy\",\"Dysprosium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w174\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":66,\"texts\":[\"67\",\"Ho\",\"Holmium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w175\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":67,\"texts\":[\"68\",\"Er\",\"Erbium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w176\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":68,\"texts\":[\"69\",\"Tm\",\"Thulium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w177\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":69,\"texts\":[\"70\",\"Yb\",\"Ytterbium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w178\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":70,\"texts\":[\"71\",\"Lu\",\"Lutetium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w179\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":71,\"texts\":[\"72\",\"Hf\",\"Hafnium\",\"Transition metal\",\"4\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w180\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":72,\"texts\":[\"73\",\"Ta\",\"Tantalum\",\"Transition metal\",\"5\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w181\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":73,\"texts\":[\"74\",\"W\",\"Tungsten\",\"Transition metal\",\"6\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w182\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":74,\"texts\":[\"75\",\"Re\",\"Rhenium\",\"Transition metal\",\"7\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w183\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":75,\"texts\":[\"76\",\"Os\",\"Osmium\",\"Transition metal\",\"8\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w184\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":76,\"texts\":[\"77\",\"Ir\",\"Iridium\",\"Transition metal\",\"9\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w185\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":77,\"texts\":[\"78\",\"Pt\",\"Platinum\",\"Transition metal\",\"10\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w186\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":78,\"texts\":[\"79\",\"Au\",\"Gold\",\"Transition metal\",\"11\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w187\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":79,\"texts\":[\"80\",\"Hg\",\"Mercury\",\"Transition metal\",\"12\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w188\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":80,\"texts\":[\"81\",\"Tl\",\"Thallium\",\"Poor metal\",\"13\",\"6\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w189\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":81,\"texts\":[\"82\",\"Pb\",\"Lead\",\"Poor metal\",\"14\",\"6\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w190\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":82,\"texts\":[\"83\",\"Bi\",\"Bismuth\",\"Poor metal\",\"15\",\"6\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w191\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":83,\"texts\":[\"84\",\"Po\",\"Polonium\",\"Metalloid\",\"16\",\"6\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w192\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":84,\"texts\":[\"85\",\"At\",\"Astatine\",\"Halogen\",\"17\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w193\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":85,\"texts\":[\"86\",\"Rn\",\"Radon\",\"Noble gas\",\"18\",\"6\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w194\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":86,\"texts\":[\"87\",\"Fr\",\"Francium\",\"Alkali metal\",\"1\",\"7\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w195\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":87,\"texts\":[\"88\",\"Ra\",\"Radium\",\"Alkaline earth metal\",\"2\",\"7\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w196\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":88,\"texts\":[\"89\",\"Ac\",\"Actinium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w197\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":89,\"texts\":[\"90\",\"Th\",\"Thorium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w198\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":90,\"texts\":[\"91\",\"Pa\",\"Protactinium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w199\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":91,\"texts\":[\"92\",\"U\",\"Uranium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w200\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":92,\"texts\":[\"93\",\"Np\",\"Neptunium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w201\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":93,\"texts\":[\"94\",\"Pu\",\"Plutonium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w202\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":94,\"texts\":[\"95\",\"Am\",\"Americium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w203\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":95,\"texts\":[\"96\",\"Cm\",\"Curium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w204\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":96,\"texts\":[\"97\",\"Bk\",\"Berkelium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w205\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":97,\"texts\":[\"98\",\"Cf\",\"Californium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w206\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":98,\"texts\":[\"99\",\"Es\",\"Einsteinium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w207\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":99,\"texts\":[\"100\",\"Fm\",\"Fermium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w208\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":100,\"texts\":[\"101\",\"Md\",\"Mendelevium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w209\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":101,\"texts\":[\"102\",\"No\",\"Nobelium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w210\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":102,\"texts\":[\"103\",\"Lr\",\"Lawrencium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w211\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":103,\"texts\":[\"104\",\"Rf\",\"Rutherfordium\",\"Transition metal\",\"4\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w212\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":104,\"texts\":[\"105\",\"Db\",\"Dubnium\",\"Transition metal\",\"5\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w213\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":105,\"texts\":[\"106\",\"Sg\",\"Seaborgium\",\"Transition metal\",\"6\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w214\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":106,\"texts\":[\"107\",\"Bh\",\"Bohrium\",\"Transition metal\",\"7\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w215\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":107,\"texts\":[\"108\",\"Hs\",\"Hassium\",\"Transition metal\",\"8\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w216\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":108,\"texts\":[\"109\",\"Mt\",\"Meitnerium\",\"Transition metal\",\"9\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w217\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":109,\"texts\":[\"110\",\"Ds\",\"Darmstadtium\",\"Transition metal\",\"10\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w218\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":110,\"texts\":[\"111\",\"Rg\",\"Roentgenium\",\"Transition metal\",\"11\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w219\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":111,\"texts\":[\"112\",\"Uub\",\"Ununbium\",\"Transition metal\",\"12\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w220\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":112,\"texts\":[\"113\",\"Uut\",\"Ununtrium\",\"Poor metal\",\"13\",\"7\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w221\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":113,\"texts\":[\"114\",\"Uuq\",\"Ununquadium\",\"Poor metal\",\"14\",\"7\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w222\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":114,\"texts\":[\"115\",\"Uup\",\"Ununpentium\",\"Poor metal\",\"15\",\"7\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w223\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":115,\"texts\":[\"116\",\"Uuh\",\"Ununhexium\",\"Poor metal\",\"16\",\"7\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w224\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":116,\"texts\":[\"117\",\"Uus\",\"Ununseptium\",\"Halogen\",\"17\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w225\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":117,\"texts\":[\"118\",\"Uuo\",\"Ununoctium\",\"Noble gas\",\"18\",\"7\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w226\",\"rwt.widgets.Composite\",{\"parent\":\"w97\",\"style\":[\"BORDER\"],\"bounds\":[10,464,988,25],\"children\":[\"w227\"],\"tabIndex\":-1,\"clientArea\":[0,0,986,23]}],[\"create\",\"w227\",\"rwt.widgets.Label\",{\"parent\":\"w226\",\"style\":[\"NONE\"],\"bounds\":[10,10,966,3],\"tabIndex\":-1,\"text\":\"Hydrogen (H)\"}],[\"create\",\"w228\",\"rwt.widgets.Label\",{\"parent\":\"w97\",\"style\":[\"WRAP\"],\"bounds\":[10,499,988,16],\"tabIndex\":-1,\"foreground\":[150,150,150,255],\"font\":[[\"Verdana\",\"Lucida Sans\",\"Arial\",\"Helvetica\",\"sans-serif\"],10,false,false],\"text\":\"Shortcuts: [CTRL+F] - Filter | Sort by: [CTRL+R] - Number, [CTRL+Y] - Symbol, [CTRL+N] - Name, [CTRL+S] - Series, [CTRL+G] - Group, [CTRL+E] - Period\"}],[\"set\",\"w1\",{\"focusControl\":\"w99\"}],[\"call\",\"rwt.client.BrowserNavigation\",\"addToHistory\",{\"entries\":[[\"tableviewer\",\"TableViewer\"]]}]]}"
abstract type A_Json <: A_Benchmark end
mutable struct C_Json <: A_Json
    C_Json(::Val{:raw}) = new()
end
function C_Json(args...)
    self = C_Json(Val(:raw))
    return self
end

function m_benchmark(self::A_Json)
    return m_parse(C__Parser(_RAP_BENCHMARK_MINIFIED))
end

function m_verify_result(self::A_Json, result)
    if truth0(!truth0(m_is_object(result)))
        return false
    end
    if truth0(!truth0(m_is_object(m_get(m_as_object(result), "head"))))
        return false
    end
    if truth0(!truth0(m_is_array(m_get(m_as_object(result), "operations"))))
        return false
    end
    return ((m_size(m_as_array(m_get(m_as_object(result), "operations"))) == 156))
end

abstract type A__JsonValue end
mutable struct C__JsonValue <: A__JsonValue
    C__JsonValue(::Val{:raw}) = new()
end
function C__JsonValue(args...)
    self = C__JsonValue(Val(:raw))
    return self
end

function m_is_object(self::A__JsonValue)
    return false
end

function m_is_array(self::A__JsonValue)
    return false
end

function m_is_number(self::A__JsonValue)
    return false
end

function m_is_string(self::A__JsonValue)
    return false
end

function m_is_boolean(self::A__JsonValue)
    return false
end

function m_is_true(self::A__JsonValue)
    return false
end

function m_is_false(self::A__JsonValue)
    return false
end

function m_is_null(self::A__JsonValue)
    return false
end

function m_as_object(self::A__JsonValue)
    throw(ErrorException(add0("Unsupported operation, not an object: ", string(self))))
end

function m_as_array(self::A__JsonValue)
    throw(ErrorException(add0("Unsupported operation, not an array: ", string(self))))
end

abstract type A__JsonLiteral <: A__JsonValue end
mutable struct C__JsonLiteral <: A__JsonLiteral
    _value
    _is_null
    _is_true
    _is_false
    C__JsonLiteral(::Val{:raw}) = new(nothing, nothing, nothing, nothing)
end
function C__JsonLiteral(args...)
    self = C__JsonLiteral(Val(:raw))
    init__JsonLiteral(self, args...)
    return self
end

function init__JsonLiteral(self, value)
    self._value = value
    self._is_null = (("null" == value))
    self._is_true = (("true" == value))
    self._is_false = (("false" == value))
    return nothing
end

function m_is_null(self::A__JsonLiteral)
    return self._is_null
end

function m_is_true(self::A__JsonLiteral)
    return self._is_true
end

function m_is_false(self::A__JsonLiteral)
    return self._is_false
end

function m_as_string(self::A__JsonLiteral)
    return self._value
end

function m_is_boolean(self::A__JsonLiteral)
    return (let _bool_value = self._is_true; truth0(_bool_value) ? _bool_value : self._is_false end)
end

const _LITERAL_NULL = C__JsonLiteral("null")
const _LITERAL_TRUE = C__JsonLiteral("true")
const _LITERAL_FALSE = C__JsonLiteral("false")
abstract type A__Parser end
mutable struct C__Parser <: A__Parser
    _input::Union{String,AsciiText}
    _index::Int
    _line::Int
    _capture_start::Int
    _column::Int
    _current::Union{Nothing,String}
    _capture_buffer::String
    C__Parser(::Val{:raw}) = new("", -1, 0, -1, 0, nothing, "")
end
function C__Parser(args...)
    self = C__Parser(Val(:raw))
    init__Parser(self, args...)
    return self
end

function init__Parser(self, string)
    self._input = isascii(string) ? AsciiText(string) : string
    self._index = -(1)
    self._line = 1
    self._capture_start = -(1)
    self._column = 0
    self._current = nothing
    self._capture_buffer = ""
    return nothing
end

function m_parse(self::A__Parser)
    local result
    m__read(self)
    m__skip_white_space(self)
    result = m__read_value(self)
    m__skip_white_space(self)
    if truth0(!truth0(m__is_end_of_text(self)))
        throw(m__error(self, "Unexpected character"))
    end
    return result
end

function m__read_value(self::A__Parser)
    if truth0(((self._current == "n")))
        return m__read_null(self)
    end
    if truth0(((self._current == "t")))
        return m__read_true(self)
    end
    if truth0(((self._current == "f")))
        return m__read_false(self)
    end
    if truth0(((self._current == "\"")))
        return m__read_string(self)
    end
    if truth0(((self._current == "[")))
        return m__read_array(self)
    end
    if truth0(((self._current == "{")))
        return m__read_object(self)
    end
    if truth0((let _bool_value = ((self._current == "-")); truth0(_bool_value) ? _bool_value : (let _bool_value = ((self._current == "0")); truth0(_bool_value) ? _bool_value : (let _bool_value = ((self._current == "1")); truth0(_bool_value) ? _bool_value : (let _bool_value = ((self._current == "2")); truth0(_bool_value) ? _bool_value : (let _bool_value = ((self._current == "3")); truth0(_bool_value) ? _bool_value : (let _bool_value = ((self._current == "4")); truth0(_bool_value) ? _bool_value : (let _bool_value = ((self._current == "5")); truth0(_bool_value) ? _bool_value : (let _bool_value = ((self._current == "6")); truth0(_bool_value) ? _bool_value : (let _bool_value = ((self._current == "7")); truth0(_bool_value) ? _bool_value : (let _bool_value = ((self._current == "8")); truth0(_bool_value) ? _bool_value : ((self._current == "9")) end) end) end) end) end) end) end) end) end) end))
        return m__read_number(self)
    end
    throw(m__expected(self, "value"))
end

function m__read_array_element(self::A__Parser, array)
    m__skip_white_space(self)
    m_add(array, m__read_value(self))
    m__skip_white_space(self)
    return nothing
end

function m__read_array(self::A__Parser)
    local array
    m__read(self)
    array = C__JsonArray()
    m__skip_white_space(self)
    if truth0(m__read_char(self, "]"))
        return array
    end
    m__read_array_element(self, array)
    while truth0(m__read_char(self, ","))
        m__read_array_element(self, array)
    end
    if truth0(!truth0(m__read_char(self, "]")))
        throw(m__expected(self, "',' or ']'"))
    end
    return array
end

function m__read_object_key_value_pair(self::A__Parser, obj)
    local name
    m__skip_white_space(self)
    name = m__read_name(self)
    m__skip_white_space(self)
    if truth0(!truth0(m__read_char(self, ":")))
        throw(m__expected(self, "':'"))
    end
    m__skip_white_space(self)
    m_add(obj, name, m__read_value(self))
    m__skip_white_space(self)
    return nothing
end

function m__read_object(self::A__Parser)
    local obj
    m__read(self)
    obj = C__JsonObject()
    m__skip_white_space(self)
    if truth0(m__read_char(self, "}"))
        return obj
    end
    m__read_object_key_value_pair(self, obj)
    while truth0(m__read_char(self, ","))
        m__read_object_key_value_pair(self, obj)
    end
    if truth0(!truth0(m__read_char(self, "}")))
        throw(m__expected(self, "',' or '}'"))
    end
    return obj
end

function m__read_name(self::A__Parser)
    if truth0(((self._current != "\"")))
        throw(m__expected(self, "name"))
    end
    return m__read_string_internal(self)
end

function m__read_null(self::A__Parser)
    m__read(self)
    m__read_required_char(self, "u")
    m__read_required_char(self, "l")
    m__read_required_char(self, "l")
    return _LITERAL_NULL
end

function m__read_true(self::A__Parser)
    m__read(self)
    m__read_required_char(self, "r")
    m__read_required_char(self, "u")
    m__read_required_char(self, "e")
    return _LITERAL_TRUE
end

function m__read_false(self::A__Parser)
    m__read(self)
    m__read_required_char(self, "a")
    m__read_required_char(self, "l")
    m__read_required_char(self, "s")
    m__read_required_char(self, "e")
    return _LITERAL_FALSE
end

function m__read_required_char(self::A__Parser, ch)
    if truth0(!truth0(m__read_char(self, ch)))
        throw(m__expected(self, add0(add0("'", ch), "'")))
    end
    return nothing
end

function m__read_string(self::A__Parser)
    return C__JsonString(m__read_string_internal(self))
end

function m__read_string_internal(self::A__Parser)
    local string
    m__read(self)
    m__start_capture(self)
    while truth0(((self._current != "\"")))
        if truth0(((self._current == "\\")))
            m__pause_capture(self)
            m__read_escape(self)
            m__start_capture(self)
        else
            m__read(self)
        end
    end
    string = m__end_capture(self)
    m__read(self)
    return string
end

function m__read_escape_char(self::A__Parser)
    if truth0(((self._current == "\"")))
        return "\""
    end
    if truth0(((self._current == "/")))
        return "/"
    end
    if truth0(((self._current == "\\")))
        return "\\"
    end
    if truth0(((self._current == "b")))
        return "\b"
    end
    if truth0(((self._current == "f")))
        return "\f"
    end
    if truth0(((self._current == "n")))
        return "\n"
    end
    if truth0(((self._current == "r")))
        return "\r"
    end
    if truth0(((self._current == "t")))
        return "\t"
    end
    throw(m__expected(self, "valid escape sequence"))
end

function m__read_escape(self::A__Parser)
    m__read(self)
    self._capture_buffer = add0(self._capture_buffer, m__read_escape_char(self))
    m__read(self)
    return nothing
end

function m__read_number(self::A__Parser)
    local first_digit
    m__start_capture(self)
    m__read_char(self, "-")
    first_digit = self._current
    if truth0(!truth0(m__read_digit(self)))
        throw(m__expected(self, "digit"))
    end
    if truth0(((first_digit != "0")))
        while truth0(m__read_digit(self))
            nothing
        end
    end
    m__read_fraction(self)
    m__read_exponent(self)
    return C__JsonNumber(m__end_capture(self))
end

function m__read_fraction(self::A__Parser)
    if truth0(!truth0(m__read_char(self, ".")))
        return false
    end
    if truth0(!truth0(m__read_digit(self)))
        throw(m__expected(self, "digit"))
    end
    while truth0(m__read_digit(self))
        nothing
    end
    return true
end

function m__read_exponent(self::A__Parser)
    if truth0((let _bool_value = !truth0(m__read_char(self, "e")); truth0(_bool_value) ? !truth0(m__read_char(self, "E")) : _bool_value end))
        return false
    end
    if truth0(!truth0(m__read_char(self, "+")))
        return m__read_char(self, "-")
    end
    if truth0(m__read_digit(self))
        throw(m__expected(self, "digit"))
    end
    while truth0(m__read_digit(self))
        nothing
    end
    return true
end

function m__read_char(self::A__Parser, ch)
    if truth0(((self._current != ch)))
        return false
    end
    m__read(self)
    return true
end

function m__read_digit(self::A__Parser)
    if truth0(!truth0(m__is_digit(self)))
        return false
    end
    m__read(self)
    return true
end

function m__skip_white_space(self::A__Parser)
    while truth0(m__is_white_space(self))
        m__read(self)
    end
    return nothing
end

function m__read(self::A__Parser)
    if truth0((("\n" == self._current)))
        self._line = add0(self._line, 1)
        self._column = 0
    end
    self._index = add0(self._index, 1)
    if truth0(((self._index < length(self._input))))
        self._current = get0(self._input, self._index)
    else
        self._current = nothing
    end
    return nothing
end

function m__start_capture(self::A__Parser)
    self._capture_start = self._index
    return nothing
end

function m__pause_capture(self::A__Parser)
    local j_end
    j_end = (truth0(((self._current === nothing))) ? self._index : (self._index - 1))
    self._capture_buffer = add0(self._capture_buffer, slice0(self._input, self._capture_start, add0(j_end, 1), nothing))
    self._capture_start = -(1)
    return nothing
end

function m__end_capture(self::A__Parser)
    local captured, j_end
    j_end = (truth0(((self._current === nothing))) ? self._index : (self._index - 1))
    if truth0((("" == self._capture_buffer)))
        captured = slice0(self._input, self._capture_start, add0(j_end, 1), nothing)
    else
        self._capture_buffer = add0(self._capture_buffer, slice0(self._input, self._capture_start, add0(j_end, 1), nothing))
        captured = self._capture_buffer
        self._capture_buffer = ""
    end
    self._capture_start = -(1)
    return captured
end

function m__expected(self::A__Parser, expected)
    if truth0(m__is_end_of_text(self))
        return m__error(self, "Unexpected end of input")
    end
    return m__error(self, add0("Expected ", expected))
end

function m__error(self::A__Parser, message)
    return C__ParseException(message, self._index, self._line, (self._column - 1))
end

function m__is_white_space(self::A__Parser)
    return (let _bool_value = ((" " == self._current)); truth0(_bool_value) ? _bool_value : (let _bool_value = (("\t" == self._current)); truth0(_bool_value) ? _bool_value : (let _bool_value = (("\n" == self._current)); truth0(_bool_value) ? _bool_value : (("\r" == self._current)) end) end) end)
end

function m__is_digit(self::A__Parser)
    return (let _bool_value = (("0" == self._current)); truth0(_bool_value) ? _bool_value : (let _bool_value = (("1" == self._current)); truth0(_bool_value) ? _bool_value : (let _bool_value = (("2" == self._current)); truth0(_bool_value) ? _bool_value : (let _bool_value = (("3" == self._current)); truth0(_bool_value) ? _bool_value : (let _bool_value = (("4" == self._current)); truth0(_bool_value) ? _bool_value : (let _bool_value = (("5" == self._current)); truth0(_bool_value) ? _bool_value : (let _bool_value = (("6" == self._current)); truth0(_bool_value) ? _bool_value : (let _bool_value = (("7" == self._current)); truth0(_bool_value) ? _bool_value : (let _bool_value = (("8" == self._current)); truth0(_bool_value) ? _bool_value : (("9" == self._current)) end) end) end) end) end) end) end) end) end)
end

function m__is_end_of_text(self::A__Parser)
    return ((self._current === nothing))
end

abstract type A__HashIndexTable end
mutable struct C__HashIndexTable <: A__HashIndexTable
    _hash_table
    C__HashIndexTable(::Val{:raw}) = new(nothing)
end
function C__HashIndexTable(args...)
    self = C__HashIndexTable(Val(:raw))
    init__HashIndexTable(self, args...)
    return self
end

function init__HashIndexTable(self)
    self._hash_table = mul0(Any[0], 32)
    return nothing
end

function m_add(self::A__HashIndexTable, name, index)
    local slot
    slot = m__hash_slot_for(self, name)
    if truth0(((index < 255)))
        set0!(self._hash_table, slot, (add0(index, 1) & 255))
    else
        set0!(self._hash_table, slot, 0)
    end
    return nothing
end

function m_get(self::A__HashIndexTable, name)
    local slot
    slot = m__hash_slot_for(self, name)
    return ((get0(self._hash_table, slot) & 255) - 1)
end

function C__HashIndexTable___string_hash(s)
    return mul0(length(s), 1402589)
end

function m__hash_slot_for(self::A__HashIndexTable, element)
    return (C__HashIndexTable___string_hash(element) & (length(self._hash_table) - 1))
end

abstract type A__ParseException end
mutable struct C__ParseException <: A__ParseException
    _message
    _offset
    _line
    _column
    C__ParseException(::Val{:raw}) = new(nothing, nothing, nothing, nothing)
end
function C__ParseException(args...)
    self = C__ParseException(Val(:raw))
    init__ParseException(self, args...)
    return self
end

function init__ParseException(self, message, offset, line, column)
    nothing
    self._message = message
    self._offset = offset
    self._line = line
    self._column = column
    return nothing
end

function m_get_message(self::A__ParseException)
    return self._message
end

function m_get_offset(self::A__ParseException)
    return self._offset
end

function m_get_line(self::A__ParseException)
    return self._line
end

function m_get_column(self::A__ParseException)
    return self._column
end

abstract type A__JsonArray <: A__JsonValue end
mutable struct C__JsonArray <: A__JsonArray
    _values
    C__JsonArray(::Val{:raw}) = new(nothing)
end
function C__JsonArray(args...)
    self = C__JsonArray(Val(:raw))
    init__JsonArray(self, args...)
    return self
end

function init__JsonArray(self)
    self._values = C_Vector()
    return nothing
end

function m_add(self::A__JsonArray, value)
    if truth0(((value === nothing)))
        throw(ErrorException("value is null"))
    end
    m_append(self._values, value)
    return self
end

function m_size(self::A__JsonArray)
    return m_size(self._values)
end

function m_get(self::A__JsonArray, index)
    return m_at(self._values, index)
end

function m_is_array(self::A__JsonArray)
    return true
end

function m_as_array(self::A__JsonArray)
    return self
end

abstract type A__JsonNumber <: A__JsonValue end
mutable struct C__JsonNumber <: A__JsonNumber
    _string::String
    C__JsonNumber(::Val{:raw}) = new("")
end
function C__JsonNumber(args...)
    self = C__JsonNumber(Val(:raw))
    init__JsonNumber(self, args...)
    return self
end

function init__JsonNumber(self, string)
    self._string = string
    if truth0(((string === nothing)))
        throw(ErrorException("string is null"))
    end
    return nothing
end

function m_as_string(self::A__JsonNumber)
    return self._string
end

function m_is_number(self::A__JsonNumber)
    return true
end

abstract type A__JsonObject <: A__JsonValue end
mutable struct C__JsonObject <: A__JsonObject
    _names
    _values
    _table
    C__JsonObject(::Val{:raw}) = new(nothing, nothing, nothing)
end
function C__JsonObject(args...)
    self = C__JsonObject(Val(:raw))
    init__JsonObject(self, args...)
    return self
end

function init__JsonObject(self)
    self._names = C_Vector()
    self._values = C_Vector()
    self._table = C__HashIndexTable()
    return nothing
end

function m_add(self::A__JsonObject, name, value)
    if truth0(((name === nothing)))
        throw(ErrorException("name is null"))
    end
    if truth0(((value === nothing)))
        throw(ErrorException("value is null"))
    end
    m_add(self._table, name, m_size(self._names))
    m_append(self._names, name)
    m_append(self._values, value)
    return self
end

function m_get(self::A__JsonObject, name)
    local index
    if truth0(((name === nothing)))
        throw(ErrorException("name is null"))
    end
    index = m_index_of(self, name)
    return (truth0(((index == -(1)))) ? nothing : m_at(self._values, index))
end

function m_size(self::A__JsonObject)
    return m_size(self._names)
end

function m_is_empty(self::A__JsonObject)
    return m_is_empty(self._names)
end

function m_is_object(self::A__JsonObject)
    return true
end

function m_as_object(self::A__JsonObject)
    return self
end

function m_index_of(self::A__JsonObject, name)
    local index
    index = m_get(self._table, name)
    if truth0((let _bool_value = ((index != -(1))); truth0(_bool_value) ? ((name == m_at(self._names, index))) : _bool_value end))
        return index
    end
    throw(ErrorException("NotImplemented"))
end

abstract type A__JsonString <: A__JsonValue end
mutable struct C__JsonString <: A__JsonString
    _string::String
    C__JsonString(::Val{:raw}) = new("")
end
function C__JsonString(args...)
    self = C__JsonString(Val(:raw))
    init__JsonString(self, args...)
    return self
end

function init__JsonString(self, string)
    self._string = string
    return nothing
end

function m_is_string(self::A__JsonString)
    return true
end
