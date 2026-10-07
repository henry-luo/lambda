// Reusable fictional strategy deck; authoring and animation policy stay in Lambda.
let navy = "#102738"
let ink = "#183747"
let muted = "#617884"
let paper = "#f3f6f5"
let teal = "#087f78"
let mint = "#7de0c0"
let blue = "#356cce"
let coral = "#e98870"
let line = "#dce5e5"

fn txt(id, x, y, w, h, value, size = 22, color = ink, weight = 400) =>
    <text id: id, x: x, y: y, width: w, height: h, font_size: size, color: color,
        style: "font-weight:" ++ string(weight) ++ ";line-height:1.2;", value>

fn rect(id, x, y, w, h, fill, radius = 0) =>
    <shape id: id, x: x, y: y, width: w, height: h, kind: 'rect', fill: fill, radius: radius>

fn box(id, x, y, w, h, body, background = "#ffffff", border = line) =>
    <content id: id, x: x, y: y, width: w, height: h,
        <div style: "width:100%;height:" ++ string(h) ++ "px;box-sizing:border-box;padding:26px;border:1px solid " ++ border ++
            ";border-radius:14px;background:" ++ background ++ ";color:" ++ ink ++ ";font-size:20px;line-height:1.4;", *body>>

fn small(value, color = muted) =>
    <div style: "font-size:15px;font-weight:700;letter-spacing:1.5px;color:" ++ color ++ ";margin-bottom:16px;", value>

fn big(value, color = ink, size = 48) =>
    <div style: "font-size:" ++ string(size) ++ "px;font-weight:700;line-height:1.1;color:" ++ color ++ ";margin-bottom:14px;", value>

fn copy(value, color = muted) =>
    <div style: "font-size:20px;line-height:1.4;color:" ++ color ++ ";", value>

fn card(id, x, y, w, h, kicker, title, body, accent = teal) =>
    box(id, x, y, w, h, [small(kicker, accent), big(title, ink, 30), copy(body)])

fn metric(id, x, y, w, value, title, detail, accent = teal) =>
    box(id, x, y, w, 176, [small(title), big(value, accent, 48), copy(detail)])

fn footer(n, dark = false) => [
    rect("footer-rule", 72, 654, 1136, 1, if (dark) "#34505c" else line),
    txt("brand", 72, 673, 650, 20, "NORTHSTAR  /  STRATEGY 2027  /  ILLUSTRATIVE DATA", 12,
        if (dark) "#acc2ca" else muted, 700),
    txt("page-number", 1134, 669, 74, 26, (if (n < 10) "0" else "") ++ string(n) ++ " / 18", 16,
        if (dark) "#acc2ca" else muted, 700)
]

fn entrance(target, kind = 'fade-in', delay = 0) =>
    <effect target: target, kind: kind, duration: 480.0, delay: delay, easing: 'ease-out-cubic'>

fn frame(n, section, title, subtitle, objects, notes, builds = [], reveals = []) =>
    <slide id: "chapter-" ++ string(n), title: title, background: paper,
        transition: if (n == 3 or n == 7 or n == 11) 'push' else 'fade', transition_duration: 360.0,
        *[
            rect("accent", 72, 47, 34, 5, teal),
            txt("section", 120, 39, 1000, 26, section, 15, teal, 700),
            txt("headline", 72, 94, 1136, 70, title, 44, ink, 700),
            txt("subtitle", 72, 169, 1136, 56, subtitle, 21, muted),
            *objects, *footer(n),
            <cue start: 'entry', <parallel *[entrance("headline"), entrance("hero", 'fade-in', 80), *reveals]>>,
            *builds, <notes notes>
        ]>

fn svg_text(x, y, value, size = 18, color = muted, weight = 400) =>
    <text x: x, y: y, fill: color, ["font-family"]: "Arial", ["font-size"]: size,
        ["font-weight"]: weight, value>

// Chart marks are separate slide targets: axes stay still while Lambda reveals data.
fn chart_layer(id, w, h, body) =>
    <content id: id, x: 0, y: 0, width: w, height: h,
        <svg width: w, height: h, viewBox: "0 0 " ++ string(w) ++ " " ++ string(h), *body>>

fn chart_group(x, y, w, h, layers, effects) =>
    {node: <group id: "hero", x: x, y: y, width: w, height: h, *layers>, effects: effects}

fn wipe(target, from, delay = 160, duration = 760) =>
    <effect target: target, kind: 'wipe-in', from: from, delay: delay, duration: duration, easing: 'ease-out-cubic'>

fn fade(target, delay) =>
    <effect target: target, kind: 'fade-in', delay: delay, duration: 200, easing: 'ease-out'>

fn trend_chart() => chart_group(72, 249, 730, 350, [
    chart_layer("trend-axes", 730, 350, [
        <rect width: 730, height: 350, rx: 14, fill: "#ffffff">,
        *[for (i in 0 to 3) <g *[
            <line x1: 64, x2: 690, y1: 52 + i * 70, y2: 52 + i * 70, stroke: line>,
            svg_text(18, 58 + i * 70, string(12 - i * 4), 14)]>],
        *[for (i, label in ["Q1 25","Q2 25","Q3 25","Q4 25","Q1 26","Q2 26"])
            svg_text(42 + i * 122, 301, label, 14)],
        svg_text(24, 331, "Annual recurring revenue, USD millions", 14)]),
    chart_layer("trend-series", 730, 350, [
        <path d: "M64 262 L185 241 L306 209 L427 165 L548 105 L674 55 L674 262 Z", fill: "#ddf3eb">,
        <path d: "M64 262 L185 241 L306 209 L427 165 L548 105 L674 55", fill: "none", stroke: teal, ["stroke-width"]: 4>,
        *[for (p in [{x:64,y:262},{x:185,y:241},{x:306,y:209},{x:427,y:165},{x:548,y:105},{x:674,y:55}])
            <circle cx: p.x, cy: p.y, r: 6, fill: teal, stroke: "#ffffff", ["stroke-width"]: 3>]]),
    chart_layer("trend-value", 730, 350, [svg_text(608, 34, "$11.8m", 22, teal, 700)])
], [wipe("trend-series", 'right', 180, 960), fade("trend-value", 980)])

let revenue_data = [{year: "2026", revenue: 18, profit: 2},
    {year: "2027", revenue: 27, profit: 5}, {year: "2028", revenue: 39, profit: 10}]
let revenue_marks = [for (i, d in revenue_data) for (j, series in [
    {value: d.revenue, color: teal}, {value: d.profit, color: blue}])
    {id: "revenue-bar-" ++ string(i * 2 + j), x: 126 + i * 200 + j * 66,
        value: series.value, color: series.color, delay: 140 + i * 110 + j * 55}]

fn revenue_chart() => chart_group(72, 250, 740, 340, [
    chart_layer("revenue-axes", 740, 340, [
        <rect width: 740, height: 340, rx: 14, fill: "#ffffff">,
        *[for (i in 0 to 4) <g *[
            <line x1: 58, x2: 704, y1: 36 + i * 56, y2: 36 + i * 56, stroke: line>,
            svg_text(15, 42 + i * 56, string(40 - i * 10), 14)]>],
        *[for (i, d in revenue_data) svg_text(147 + i * 200, 290, d.year, 18, ink, 700)],
        <rect x: 170, y: 316, width: 12, height: 12, fill: teal>, svg_text(191, 328, "Revenue", 15),
        <rect x: 356, y: 316, width: 12, height: 12, fill: blue>, svg_text(377, 328, "Operating profit", 15)]),
    *[for (bar in revenue_marks) rect(bar.id, bar.x, 260 - bar.value * 5.6, 58, bar.value * 5.6, bar.color, 5)],
    chart_layer("revenue-values", 740, 340, [for (i, d in revenue_data)
        svg_text(133 + i * 200, 251 - d.revenue * 5.6, "$" ++ string(d.revenue) ++ "m", 18, teal, 700)])
], [*[for (bar in revenue_marks) wipe(bar.id, 'top', bar.delay, 680)], fade("revenue-values", 980)])

fn polar(radius, angle) => string(141 + radius * math.cos(angle)) ++ " " ++ string(141 + radius * math.sin(angle))
fn ring_segment(begin, end) => "M" ++ polar(141, begin) ++ " A141 141 0 0 1 " ++ polar(141, end) ++
    " L" ++ polar(99, end) ++ " A99 99 0 0 0 " ++ polar(99, begin) ++ " Z"

fn market_chart() => chart_group(72, 245, 550, 378, [
    chart_layer("market-base", 550, 378, [
        <rect width: 550, height: 378, rx: 14, fill: "#ffffff">,
        <circle cx: 235, cy: 186, r: 120, fill: "none", stroke: "#e5ecec", ["stroke-width"]: 42>,
        svg_text(164, 181, "$1.2bn", 39, ink, 700), svg_text(169, 212, "SERVICEABLE", 14, muted, 700),
        svg_text(49, 346, "25% of a $4.8bn illustrative addressable market", 17)]),
    *[for (i in 0 to 23) <content id: "market-sector-" ++ string(i), x: 94, y: 45, width: 282, height: 282,
        <svg width: 282, height: 282, viewBox: "0 0 282 282",
            <path d: ring_segment(-math.pi / 2 + i * math.pi / 48, -math.pi / 2 + (i + 1) * math.pi / 48),
                fill: teal, stroke: teal, ["stroke-width"]: 0.5>>>]
], [for (i in 0 to 23) <effect target: "market-sector-" ++ string(i), kind: 'fade-in',
    duration: 90, delay: 180 + i * 26, easing: 'linear'>])

// Both horizontal charts share mark geometry and reveal timing from their row data.
fn progress_chart(config) map^ => chart_group(config.x, config.y, config.w, config.h, [
    chart_layer("progress-base", config.w, config.h, [
        *(if (config.background) [<rect width: config.w, height: config.h, rx: 14, fill: "#ffffff">,
            svg_text(29, 49, "$6m investment envelope", 30, ink, 700)] else []),
        *[for (row in config.rows) <g *[
            svg_text(config.row_x, config.row_y + row.y + 17, row.label, 19, ink, 700),
            <rect x: config.row_x + 210, y: config.row_y + row.y, width: 420, height: 26, rx: 6, fill: "#e5ecec">]>],
        svg_text(config.foot_x, config.foot_y, config.footnote, config.foot_size, if (config.background) muted else ink)]),
    *[for (i, row in config.rows) rect("progress-bar-" ++ string(i), config.row_x + 210, config.row_y + row.y,
        row.amount * 4.2, 26, row.color, 6)],
    *[for (i, row in config.rows) chart_layer("progress-value-" ++ string(i), config.w, config.h,
        [svg_text(config.row_x + 653, config.row_y + row.y + 20, row.value, 20, row.color, 700)])]
], [for (i, row in config.rows) for (effect in [wipe("progress-bar-" ++ string(i), 'right', 160 + i * 130, 720),
    fade("progress-value-" ++ string(i), 760 + i * 130)]) effect])^

let market = market_chart()
let trend = trend_chart()
let revenue = revenue_chart()
let economics = progress_chart({x: 99, y: 456, w: 1082, h: 159, background: false, row_x: 0, row_y: 0,
    foot_x: 0, foot_y: 140, foot_size: 20,
    footnote: "Levers: a repeatable launch, stronger adoption and disciplined cost-to-serve.", rows: [
        {label: "Activation rate", amount: 72, value: "72% → 85%", y: 9, color: teal},
        {label: "Gross retention", amount: 94, value: "94% → 97%", y: 61, color: blue}]})^
let allocation = progress_chart({x: 72, y: 251, w: 745, h: 366, background: true, row_x: 29, row_y: 93,
    foot_x: 29, foot_y: 324, foot_size: 17,
    footnote: "Product $2.4m  /  Customer value $2.1m  /  Distribution $1.5m", rows: [
        {label: "Product & data", amount: 40, value: "40%", y: 0, color: teal},
        {label: "Customer value", amount: 35, value: "35%", y: 72, color: blue},
        {label: "Distribution", amount: 25, value: "25%", y: 144, color: "#b86c47"}]})^

fn th(value, w = "auto") => <th style: "width:" ++ w ++ ";padding:17px 20px;text-align:left;font-size:15px;letter-spacing:0.5px;color:" ++ muted ++ ";background:#eaf0ef;", value>
fn td(value, strong = false, color = ink) => <td style: "padding:18px 20px;border-bottom:1px solid " ++ line ++
    ";font-size:19px;line-height:1.25;color:" ++ color ++ ";font-weight:" ++ (if (strong) "700" else "400") ++ ";", value>

pub let deck = <presentation id: "northstar-strategy", title: "Northstar / Strategy 2027", width: 1280.0, height: 720.0, *[
    <slide id: "opening", title: "A clearer path to durable growth", background: navy,
        *[
            rect("cover-accent", 72, 62, 42, 6, mint),
            txt("cover-brand", 132, 51, 700, 35, "NORTHSTAR  /  STRATEGY & OPERATIONS", 17, mint, 700),
            txt("headline", 72, 155, 720, 210, "A clearer path\nto durable growth.", 68, "#ffffff", 700),
            txt("cover-copy", 76, 402, 660, 90, "One operating system.\nA focused plan for the next stage.", 27, "#bed0d5"),
            txt("cover-date", 76, 560, 660, 35, "LEADERSHIP BRIEFING  /  2027", 16, mint, 700),
            <content id: "hero", x: 815, y: 132, width: 380, height: 430,
                <svg width: 380, height: 430, viewBox: "0 0 380 430", *[
                    *[for (r in [72, 122, 172]) <circle cx: 190, cy: 210, r: r, fill: "none", stroke: "#355160", ["stroke-width"]: 1>],
                    <path d: "M30 348 L121 281 L190 210 L274 132 L344 56", fill: "none", stroke: mint, ["stroke-width"]: 4>,
                    *[for (p in [{x:30,y:348},{x:121,y:281},{x:190,y:210},{x:274,y:132},{x:344,y:56}])
                        <circle cx: p.x, cy: p.y, r: 8, fill: mint>],
                    <circle cx: 190, cy: 210, r: 30, fill: navy, stroke: mint, ["stroke-width"]: 2>,
                    <path d: "M190 191 L195 205 L210 210 L195 215 L190 229 L185 215 L170 210 L185 205 Z", fill: mint>,
                    svg_text(25, 407, "FOCUS  /  MOMENTUM  /  SCALE", 14, "#bed0d5", 700)
                ]>>,
            *footer(1, true),
            <cue start: 'entry', <parallel *[entrance("headline", 'fly-in'), entrance("hero", 'zoom-in', 100)]>>,
            <notes "A fictional board-style briefing. All company names, metrics and forecasts are illustrative. Use Next for each build and slide.">
        ]>,

    frame(2, "01  /  THE THESIS", "Grow with focus. Scale with discipline.",
        "Three choices connect the customer promise to a repeatable operating model.", [
            <group id: "hero", x: 72, y: 253, width: 1136, height: 276, *[
                card("focus", 0, 0, 362, 276, "01 / WIN THE RIGHT CUSTOMER", "Focus the market", "Prioritize operations teams in complex, multi-site businesses. Build depth before expanding breadth."),
                card("value", 387, 0, 362, 276, "02 / PROVE THE VALUE", "Shorten time to impact", "Turn fragmented workflows into measurable outcomes in the first 30 days.", blue),
                card("scale", 774, 0, 362, 276, "03 / EARN THE RIGHT TO SCALE", "Compound the model", "Align product, distribution and service around one measurable customer journey.", "#b86c47")
            ]>,
            box("decision", 72, 551, 1136, 76, [<div style: "font-size:21px;font-weight:700;color:#087f78;", "THE DECISION  /  Invest in activation and expansion before adding new segments.">], "#e0f1eb")
        ], "Reveal the decision after walking through the three pillars.",
        [<cue entrance("decision", 'wipe-in')>]),

    frame(3, "02  /  MARKET CONTEXT", "The market is moving from tools to outcomes.",
        "Buyers want fewer handoffs, clearer accountability and a faster return on investment.", [
            <group id: "hero", x: 72, y: 248, width: 1136, height: 358,
                for (i, item in [
                    {n: "01", title: "Fragmented stacks", before: "A different tool for every team", after: "One connected execution layer", color: teal},
                    {n: "02", title: "Higher scrutiny", before: "Software purchased on potential", after: "Expansion earned through proof", color: blue},
                    {n: "03", title: "Lean operating teams", before: "More dashboards and manual checks", after: "Actionable exceptions and automation", color: "#b86c47"}
                ]) box("shift-" ++ string(i), 0, i * 120, 1136, 104, [
                    <div style: "display:flex;align-items:center;gap:28px;", *[
                        <div style: "width:45px;font-size:30px;font-weight:700;color:" ++ item.color ++ ";", item.n>,
                        <div style: "width:255px;font-size:25px;font-weight:700;", item.title>,
                        <div style: "width:325px;color:#617884;font-size:20px;", item.before>,
                        <div style: "color:#087f78;font-size:25px;", ">">,
                        <div style: "width:330px;font-weight:700;font-size:20px;", item.after>]>
                ])>
        ], "Position Northstar as an execution layer, with specific outcomes rather than a broad platform claim."),

    frame(4, "03  /  OPPORTUNITY", "A focused entry point into a large market.",
        "Start where operational complexity and willingness to pay intersect.", [
            market.node,
            metric("market-total", 646, 245, 562, "$4.8bn", "TOTAL ADDRESSABLE MARKET", "Global operations software in target industries"),
            box("market-focus", 646, 445, 562, 178, [small("INITIAL WEDGE", blue), big("2,400 accounts", ink, 34), copy("Multi-site services and logistics businesses with 200–2,000 employees.")])
        ], "Market sizing is a scenario, not external market research. The wedge is defined by customer need and sales repeatability.", [], market.effects),

    frame(5, "04  /  TRACTION", "Momentum is improving. Quality matters more.",
        "Illustrative revenue growth is supported by retention and faster activation.", [
            trend.node,
            metric("arr", 828, 249, 380, "$11.8m", "ANNUAL RECURRING REVENUE", "+48% year over year"),
            box("quality", 828, 446, 380, 153, [small("QUALITY OF GROWTH"), big("118% NRR", teal, 35), copy("24-day median activation")])
        ], "The chart is a six-quarter illustrative series. ARR rises from zero to 11.8 million; NRR and activation are separate operating measures.", [], trend.effects),

    frame(6, "05  /  CUSTOMER JOURNEY", "Make the first 30 days unmistakably valuable.",
        "Design one journey, with a clear customer outcome and accountable owner at every step.", [
            <group id: "hero", x: 72, y: 260, width: 1136, height: 263,
                for (i, step in [
                    {time: "DAY 01", title: "Connect", body: "Import core data and map the first workflow.", owner: "Solutions"},
                    {time: "DAY 07", title: "Activate", body: "Run a real process with a named team lead.", owner: "Customer success"},
                    {time: "DAY 14", title: "Prove", body: "Baseline time saved and exceptions resolved.", owner: "Value consultant"},
                    {time: "DAY 30", title: "Expand", body: "Agree the next workflow and success plan.", owner: "Account team"}
                ]) box("journey-" ++ string(i), i * 290, 0, 266, 263, [small(step.time, teal), big(step.title, ink, 30), copy(step.body),
                    <div style: "margin-top:22px;font-size:16px;font-weight:700;color:#356cce;", step.owner>])>,
            box("promise", 72, 552, 1136, 76, [<div style: "font-size:21px;font-weight:700;", "CUSTOMER PROMISE  /  Your first measurable operational improvement in 30 days.">], "#e0f1eb")
        ], "Reveal the promise after the journey. Each step needs one owner and one definition of done.", [<cue entrance("promise", 'fade-in')>]),

    frame(7, "06  /  PRODUCT SYSTEM", "A connected system, built around the work.",
        "A shared data model links front-line execution to management decisions.", [
            <group id: "hero", x: 72, y: 246, width: 745, height: 377,
                for (i, layer in [
                    {name: "EXPERIENCE", title: "Workspace  /  Mobile  /  Partner portal", body: "One task view for every role", bg: "#e0f1eb", accent: teal},
                    {name: "EXECUTION", title: "Workflows  /  Rules  /  Automation", body: "Turn intent into repeatable action", bg: "#e5edfc", accent: blue},
                    {name: "FOUNDATION", title: "Shared data  /  Integrations  /  Trust", body: "A reliable record of the operating business", bg: "#ffffff", accent: ink}
                ]) box("layer-" ++ string(i), 0, i * 128, 745, 117, [small(layer.name, layer.accent),
                    <div style: "font-size:23px;font-weight:700;margin-top:-8px;margin-bottom:4px;", layer.title>,
                    <div style: "font-size:17px;color:#617884;", layer.body>], layer.bg)>,
            box("product-principles", 844, 246, 364, 373, [small("DESIGN PRINCIPLES"), big("Built for adoption", ink, 31),
                copy("Clear defaults. Fewer decisions. Useful feedback."), <div style: "height:24px;">,
                small("ENTERPRISE READY", blue), copy("Role-based access, audit history and integration observability.")])
        ], "The architecture is conceptual. Each layer is ordinary Lambda-authored slide content, not a raster image."),

    frame(8, "07  /  OPERATING MODEL", "Every customer outcome feeds the next.",
        "A closed learning loop compounds value across product, service and distribution.", [
            <content id: "hero", x: 72, y: 237, width: 670, height: 395,
                <svg width: 670, height: 395, viewBox: "0 0 670 395",
                    <rect width: 670, height: 395, rx: 14, fill: "#ffffff">
                    <circle cx: 335, cy: 197, r: 133, fill: "none", stroke: "#c9e9df", ["stroke-width"]: 28>
                    <path d: "M317 46 L341 64 L317 82 M486 179 L468 203 L450 179 M353 348 L329 330 L353 312 M184 215 L202 191 L220 215",
                        fill: "none", stroke: teal, ["stroke-width"]: 5>
                    <circle cx: 335, cy: 197, r: 74, fill: navy>
                    svg_text(287, 189, "Customer", 20, "#ffffff", 700)
                    svg_text(292, 218, "outcome", 20, "#ffffff", 700)
                    svg_text(285, 35, "1. Activate", 19, teal, 700)
                    svg_text(495, 203, "2. Learn", 19, teal, 700)
                    svg_text(275, 381, "3. Improve", 19, teal, 700)
                    svg_text(24, 203, "4. Expand", 19, teal, 700)
                >>,
            card("loop-one", 767, 237, 441, 180, "WEEKLY SIGNAL", "Listen at the edge", "Activation friction and customer exceptions become product inputs."),
            card("loop-two", 767, 441, 441, 191, "MONTHLY DECISION", "Close the loop", "Ship improvements, measure adoption and update the playbook.", blue)
        ], "The flywheel connects feedback to a decision cadence. It is an operating discipline rather than a software feature."),

    frame(9, "08  /  UNIT ECONOMICS", "Better activation improves the whole model.",
        "Invest in retention and sales efficiency while protecting the customer experience.", [
            metric("payback", 72, 245, 362, "14 months", "CAC PAYBACK", "Target: 12 months", teal),
            metric("margin", 459, 245, 362, "78%", "GROSS MARGIN", "Target: 82%", blue),
            metric("ltv", 846, 245, 362, "4.2x", "LTV / CAC", "Target: greater than 5.0x", "#b86c47"),
            economics.node
        ], "Progress bars use a zero-to-100 percent domain. LTV/CAC and payback are illustrative modeled metrics, not audited financials.", [], economics.effects),

    frame(10, "09  /  FINANCIAL PLAN", "Build a growth engine that funds itself.",
        "A three-year planning scenario balances expansion with operating leverage.", [
            revenue.node,
            box("financial-assumptions", 838, 250, 370, 340, [small("PLANNING ASSUMPTIONS"), big("50% → 44%", teal, 37),
                copy("Annual revenue growth across the plan."), <div style: "height:24px;">,
                big("11% → 26%", blue, 37), copy("Operating margin improves as implementation and support scale.")]),
            txt("financial-note", 90, 609, 1110, 27, "Illustrative forecast  /  Revenue: $18m, $27m, $39m  /  Operating profit: $2m, $5m, $10m", 15, muted)
        ], "Click to reveal the planning assumptions after presenting the chart. These are scenario figures.",
        [<cue entrance("financial-assumptions", 'fly-in')>], revenue.effects),

    frame(11, "10  /  ROADMAP", "Sequence the work. Make the tradeoffs visible.",
        "Four quarters, each with a distinct customer promise and a measurable release gate.", [
            <group id: "hero", x: 72, y: 252, width: 1136, height: 281,
                for (i, quarter in [
                    {q: "Q1 / FOUNDATION", title: "Launch reliably", body: "Guided setup\nData quality checks\nLaunch playbooks", gate: "Activation ≥ 78%"},
                    {q: "Q2 / ADOPTION", title: "Make work flow", body: "Mobile workspace\nRole-based templates\nUsage health", gate: "Weekly active ≥ 65%"},
                    {q: "Q3 / EXPANSION", title: "Connect the teams", body: "Cross-site reporting\nPartner integrations\nExpansion signals", gate: "NRR ≥ 120%"},
                    {q: "Q4 / SCALE", title: "Automate the loop", body: "Exception routing\nAutomation library\nSelf-serve analytics", gate: "Gross margin ≥ 80%"}
                ]) box("quarter-" ++ string(i), i * 290, 0, 266, 281, [small(quarter.q, if (i == 0) teal else blue),
                    big(quarter.title, ink, 27), <div style: "white-space:pre-line;font-size:18px;line-height:1.6;color:#617884;", quarter.body>,
                    <div style: "margin-top:18px;font-size:16px;font-weight:700;color:#087f78;", quarter.gate>])>,
            box("roadmap-gate", 72, 557, 1136, 70, [<div style: "font-size:20px;font-weight:700;", "RELEASE RULE  /  Validate adoption before opening the next investment gate.">], "#e0f1eb")
        ], "Reveal the release rule after the four quarterly themes. Gates guide resource allocation, not just release dates.",
        [<cue entrance("roadmap-gate", 'wipe-in')>]),

    frame(12, "11  /  POSITIONING", "Win on the work that matters to the customer.",
        "An illustrative comparison of product archetypes, rather than named competitors.", [
            <content id: "hero", x: 72, y: 245, width: 1136, height: 326,
                <table style: "width:100%;border-collapse:collapse;background:#ffffff;",
                    <thead <tr *[th("BUYER PRIORITY", "32%"), th("POINT TOOLS"), th("BROAD SUITES"), th("NORTHSTAR")]>>
                    <tbody
                        for (row in [
                            ["Fast time to first value", "Strong", "Variable", "Core focus"],
                            ["Cross-team execution", "Limited", "Strong", "Core focus"],
                            ["Deployment simplicity", "Strong", "Complex", "Guided launch"],
                            ["Industry workflow depth", "Narrow", "Configurable", "Purpose-built"]
                        ]) <tr *[td(row[0], true), td(row[1]), td(row[2]), td(row[3], true, teal)]>>
                >>,
            txt("positioning-line", 91, 597, 1100, 36, "POSITIONING  /  The connected operations workspace for teams running complex, multi-site businesses.", 20, teal, 700)
        ], "Archetypes are deliberately generic. This is a positioning exercise, not a factual assessment of specific vendors."),

    frame(13, "12  /  GO TO MARKET", "Land with a use case. Expand with evidence.",
        "A focused funnel connects demand generation to customer-led expansion.", [
            <content id: "hero", x: 72, y: 244, width: 520, height: 380,
                <svg width: 520, height: 380, viewBox: "0 0 520 380",
                    <path d: "M0 0 H520 L469 102 H51 Z", fill: navy>
                    <path d: "M51 116 H469 L418 218 H102 Z", fill: teal>
                    <path d: "M102 232 H418 L367 334 H153 Z", fill: blue>
                    svg_text(149, 45, "2,400 target accounts", 22, "#ffffff", 700)
                    svg_text(170, 77, "Focused reach", 17, "#bed0d5")
                    svg_text(157, 162, "480 qualified teams", 22, "#ffffff", 700)
                    svg_text(178, 192, "Proven use case", 17, "#d9f3ec")
                    svg_text(186, 277, "120 new customers", 20, "#ffffff", 700)
                    svg_text(194, 307, "Repeatable launch", 16, "#e5edfc")
                    svg_text(21, 372, "Illustrative annual funnel  /  20% qualify  /  25% close", 17)
                >>,
            card("gtm-one", 618, 244, 590, 178, "ACQUIRE", "Industry expertise opens doors", "Use focused communities, partners and customer stories to reach the right operational buyer."),
            card("gtm-two", 618, 446, 590, 178, "EXPAND", "Measured outcomes create demand", "Use the first workflow's results to earn adjacent teams, sites and use cases.", blue)
        ], "The funnel describes an annual scenario. Qualification is 480 divided by 2400, and close rate is 120 divided by 480."),

    frame(14, "13  /  SCORECARD", "One scorecard. Clear owners. No surprises.",
        "Pair commercial outcomes with the leading indicators that teams can act on.", [
            <content id: "hero", x: 72, y: 244, width: 1136, height: 380,
                <table style: "width:100%;border-collapse:collapse;background:#ffffff;",
                    <thead <tr *[th("MEASURE", "34%"), th("CURRENT"), th("2027 TARGET"), th("ACCOUNTABLE OWNER")]>>
                    <tbody
                        for (row in [
                            ["Annual recurring revenue", "$18m", "$27m", "Revenue"],
                            ["Net revenue retention", "118%", "123%", "Customer success"],
                            ["30-day activation", "72%", "85%", "Product + solutions"],
                            ["Gross margin", "78%", "82%", "Operations"],
                            ["CAC payback", "14 months", "12 months", "Growth + finance"]
                        ]) <tr *[td(row[0], true), td(row[1]), td(row[2], true, teal), td(row[3])]>>
                >>
        ], "ARR current is the 2026 year-end plan, while the earlier traction slide reports Q2 2026. Separate leading indicators from lagging outcomes."),

    frame(15, "14  /  RISK & RESILIENCE", "Make the risks explicit. Build the response now.",
        "Prioritize the few uncertainties that could change the plan materially.", [
            <content id: "hero", x: 72, y: 244, width: 490, height: 378,
                <svg width: 490, height: 378, viewBox: "0 0 490 378", *[
                    *[for (row in 0 to 2) for (col in 0 to 2)
                        <rect x: 70 + col * 125, y: 12 + row * 95, width: 119, height: 89, rx: 4,
                            fill: if (col == 2 and row == 0) "#f4d8ce" else if (col > row) "#f1e7d8" else "#e0efea">],
                    svg_text(11, 34, "HIGH", 12), svg_text(15, 225, "LOW", 12),
                    svg_text(70, 327, "LOW", 13), svg_text(397, 327, "HIGH", 13),
                    svg_text(146, 363, "BUSINESS IMPACT", 15, muted, 700),
                    <circle cx: 380, cy: 59, r: 22, fill: navy>, svg_text(373, 66, "1", 20, "#ffffff", 700),
                    <circle cx: 255, cy: 154, r: 22, fill: teal>, svg_text(248, 161, "2", 20, "#ffffff", 700),
                    <circle cx: 380, cy: 249, r: 22, fill: blue>, svg_text(373, 256, "3", 20, "#ffffff", 700)
                ]>>,
            <group id: "risk-list", x: 588, y: 244, width: 620, height: 304,
                for (i, risk in [
                    {title: "1 / Adoption stalls", body: "Early health signals + a guided 30-day launch."},
                    {title: "2 / Sales cycles lengthen", body: "Tighter qualification + value-based pilots."},
                    {title: "3 / Integration incidents", body: "Observability + tested recovery playbooks."}
                ]) box("risk-" ++ string(i), 0, i * 104, 620, 94, [
                    <div style: "font-size:22px;font-weight:700;margin-bottom:6px;", risk.title>,
                    <div style: "font-size:18px;color:#617884;", risk.body>])>,
            txt("risk-response", 602, 579, 600, 48, "Review exposure monthly; escalate changes immediately.", 21, teal, 700)
        ], "The vertical axis indicates likelihood from low at the bottom to high at the top. Click to reveal the response cadence.",
        [<cue entrance("risk-response", 'fade-in')>]),

    frame(16, "15  /  RESOURCE ALLOCATION", "Fund the bottlenecks that unlock the next stage.",
        "An illustrative $6m incremental investment, allocated against measurable outcomes.", [
            allocation.node,
            box("allocation-rule", 843, 251, 365, 366, [small("CAPITAL DISCIPLINE"), big("Release in stages", ink, 33),
                copy("Commit the team, instrument the outcome, then unlock the next tranche."), <div style: "height:24px;">,
                small("REVIEW EVERY QUARTER", teal), copy("Rebalance when evidence changes. Stop work that does not improve the scorecard.")])
        ], "Allocation bars share a zero-to-100 percent domain. Dollar amounts sum to the six-million investment envelope.", [], allocation.effects),

    frame(17, "16  /  EXECUTION", "Turn the strategy into a management rhythm.",
        "A lightweight cadence keeps teams aligned and decisions close to the evidence.", [
            <group id: "hero", x: 72, y: 247, width: 1136, height: 239, *[
                card("weekly", 0, 0, 362, 239, "WEEKLY / TEAM", "Resolve the friction", "Review activation, delivery exceptions and actions. Make one owner accountable for each next step."),
                card("monthly", 387, 0, 362, 239, "MONTHLY / LEADERSHIP", "Review the system", "Check the scorecard, customer health and risk exposure. Resolve tradeoffs across functions.", blue),
                card("quarterly", 774, 0, 362, 239, "QUARTERLY / BOARD", "Reallocate with intent", "Review strategic assumptions and investment gates. Fund the next constraint, not last quarter's plan.", "#b86c47") ]>,
            box("next-thirty", 72, 514, 1136, 112, [small("NEXT 30 DAYS", teal),
                <div style: "font-size:21px;font-weight:700;", "01  Name the owners     /     02  Baseline the measures     /     03  Launch the first cohort">])
        ], "Close with the operating cadence and the concrete first thirty days. The plan needs named owners before new investment begins."),

    <slide id: "closing", title: "Focus creates momentum", background: navy, transition: 'fade', transition_duration: 420.0,
        *[
            rect("close-accent", 72, 62, 42, 6, mint),
            txt("close-kicker", 132, 51, 950, 35, "NORTHSTAR  /  THE NEXT CHAPTER", 17, mint, 700),
            txt("headline", 72, 144, 1120, 173, "Focus creates momentum.\nDiscipline makes it durable.", 61, "#ffffff", 700),
            <group id: "hero", x: 76, y: 372, width: 1128, height: 146,
                for (i, item in [
                    {n: "01", title: "Win the right customer"},
                    {n: "02", title: "Prove the value early"},
                    {n: "03", title: "Scale what works"}
                ]) <group id: "close-" ++ string(i), x: i * 382, y: 0, width: 350, height: 146, *[
                    txt("close-n-" ++ string(i), 0, 0, 350, 65, item.n, 48, mint, 700),
                    txt("close-t-" ++ string(i), 0, 80, 350, 62, item.title, 25, "#ffffff", 700)]>>,
            txt("close-question", 76, 567, 1120, 42, "DISCUSSION  /  What must we stop doing to make this plan possible?", 23, "#bed0d5"),
            *footer(18, true),
            <cue start: 'entry', <parallel *[entrance("headline"), entrance("hero", 'fly-in', 100)]>>,
            <notes "End with the tradeoff question. All metrics are fictional; the deck demonstrates Lambda/Radiant content, style and animation capabilities.">
        ]>
]>
