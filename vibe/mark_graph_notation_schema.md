# Mark Graph Notation (MGN) Schema

A comprehensive graph markup language based on **Mark Notation**, designed to express structure, layout, styling, and semantics across major graph formats — Graphviz, Mermaid, PlantUML, D2, Cytoscape.js, BPMN, UML, etc.

---

## 1. Top‑Level Structure

```mark
<graph
  id: myGraph
  directed: true
  layout: "dot"           // e.g. dot, neato, fdp, force, radial, grid, dagre
  rankdir: "LR"           // LR, TB, RL, BT (for hierarchical layouts)
  background: "#fff"
  font: "Inter"
  theme: "light"
  comment: "Demonstration graph"
>
  <meta>
    <author>Henry</author>
    <version>1.0</version>
    <description>"Demonstration graph with multiple features"</description>
  >

  <style>
    <node fill:"#E3F2FD" stroke:"#1E88E5" font:"Inter" shape:"rect" />
    <edge color:"#555" width:2 style:"solid" />
    <graph margin:20 padding:10 />
  >

  <defs>
    <nodeStyle id:highlight fill:"#FFF59D" stroke:"#F9A825" />
    <edgeStyle id:dashed style:"dashed" color:"#8E24AA" />
  >

  <nodes>
    <node id:A label:"Start" tooltip:"Entry point" icon:"🚀" style:highlight />
    <node id:B label:"Process" shape:"ellipse" fill:"#C8E6C9" />
    <node id:C label:"Decision" shape:"diamond" fill:"#FFE0B2" />
    <node id:D label:"End" shape:"rect" fill:"#FFCDD2" />
  >

  <edges>
    <edge from:A to:B label:"begin" />
    <edge from:B to:C label:"evaluate" />
    <edge from:C to:D label:"yes" style:dashed />
    <edge from:C to:B label:"no" color:"#F57C00" />
  >

  <subgraph id:cluster1 label:"Loop section" rank:"same" color:"#90CAF9">
    <nodes>
      <node id:E label:"Check" />
      <node id:F label:"Retry" />
    >
    <edges>
      <edge from:E to:F label:"retry" />
      <edge from:F to:E label:"loop" />
    >
  >
>
```

---

## 2. Elements and Attributes

### 2.1 `<graph>`
| Attribute | Type | Description |
|------------|------|-------------|
| id | string | Unique identifier |
| directed | bool | `true` for digraphs |
| layout | string | Layout engine or algorithm |
| rankdir | string | Direction for hierarchical layouts |
| background | color | Background color |
| font | string | Default font |
| theme | string | “light” or “dark” |
| margin, padding | number | Space around the graph |
| scale | number | Global zoom scaling |
| width, height | number | Fixed graph dimensions |

### 2.2 `<nodes>` / `<node>`
| Attribute | Type | Description |
|------------|------|-------------|
| id | string | Unique node identifier |
| label | string | Text label |
| shape | string | `rect`, `ellipse`, `diamond`, etc. |
| fill, stroke | color | Colors |
| width, height | number | Size override |
| icon | string | Emoji, Unicode, or icon name |
| image | URL | Embedded image |
| style | ref | Reference to a style in `<defs>` |
| tooltip | string | Hover text |
| href | URL | Hyperlink |
| group | string | Logical grouping |
| pos | x,y | Fixed position |
| font, fontsize, fontcolor | string/number | Text styling |
| opacity | float | Transparency |

### 2.3 `<edges>` / `<edge>`
| Attribute | Type | Description |
|------------|------|-------------|
| from, to | node id | Endpoints |
| label | string | Edge label |
| style | ref/string | e.g. `solid`, `dashed`, etc. |
| color | color | Line color |
| width | number | Stroke width |
| arrowhead, arrowtail | string | Arrow style |
| weight | number | Layout ranking weight |
| constraint | bool | Affects layout |
| curve | string | `smooth`, `poly`, etc. |
| tooltip | string | Hover text |
| href | URL | Hyperlink |
| labelpos | string | `mid`, `head`, `tail` |
| font, fontsize, fontcolor | string/number | Label styling |
| opacity | float | Transparency |

### 2.4 `<subgraph>`
| Attribute | Type | Description |
|------------|------|-------------|
| id | string | Cluster name |
| label | string | Display title |
| rank | string | “same”, “min”, “max” |
| color, fill, stroke | color | Cluster border styling |
| style | string | `dashed`, `rounded`, etc. |

### 2.5 `<style>` and `<defs>`
Used for global and reusable style definitions.

```mark
<style>
  <node fill:"#f0f0f0" stroke:"#000" />
  <edge color:"#555" width:1.5 />
  <graph margin:10 />
>
<defs>
  <nodeStyle id:warning fill:"#FFF59D" stroke:"#F9A825" />
  <edgeStyle id:highlight color:"#E91E63" style:"dashed" />
>
```

### 2.6 `<meta>`
Metadata for authorship, versioning, provenance, etc.

```mark
<meta>
  <author>Henry</author>
  <version>2.0</version>
  <license>MIT</license>
  <date>2025‑10‑25</date>
  <source>"https://example.com/data"</source>
>
```

---

## 3. Optional Extensions

### 3.1 Data attributes
```mark
<node id:X type:"function" data:"relu" />
<edge from:X to:Y weight:0.85 />
```

### 3.2 Clustering
```mark
<cluster id:GroupA color:"#E0F7FA">
  <members>[A,B,C]</members>
>
```

### 3.3 Events and animations
```mark
<edge from:A to:B on:hover color:"#FF0000" />
```

### 3.4 Layering and constraints
```mark
<node id:X layer:2 />
<constraints>
  <sameRank>[A,B,C]</sameRank>
  <order>[A,B,C,D]</order>
>
```

---

## 4. Example

```mark
<graph directed:true layout:"dot" theme:"light">
  <style>
    <node fill:"#E3F2FD" stroke:"#1E88E5" />
    <edge color:"#757575" />
  >
  <nodes>
    <node id:Login label:"User Login" shape:"rect" icon:"🔐" tooltip:"Start of flow" />
    <node id:Validate label:"Validate Credentials" shape:"diamond" />
    <node id:Dashboard label:"Dashboard" shape:"ellipse" />
    <node id:Error label:"Error Page" shape:"rect" fill:"#FFCDD2" />
  >
  <edges>
    <edge from:Login to:Validate label:"submit" />
    <edge from:Validate to:Dashboard label:"success" color:"#43A047" />
    <edge from:Validate to:Error label:"fail" color:"#E53935" style:"dashed" />
  >
  <subgraph id:authFlow label:"Authentication Flow" color:"#BBDEFB" />
>
```

---

## 5. Compatibility Mappings

| Target | Mapping | Notes |
|---------|----------|-------|
| Graphviz DOT | `graph.directed` → digraph/graph; attributes map 1:1 | Full coverage |
| Mermaid | Layout → graph direction (`LR`, `TD`); `<subgraph>` → `subgraph` | Supported |
| PlantUML | `<edge>` → `A --> B : label`; styles → skinparam | Good coverage |
| D2 | Direct mapping to D2 syntax | |
| Cytoscape.js | Export to JSON `{ data, style }` | |
| BPMN/UML | Extend node `type` (e.g., “task”, “event”) | |

---

## 6. Implementation Roadmap

1. Parser → Reuse Mark parser (recursive descent).
2. Builder → Intermediate representation (nodes, edges, clusters, styles).
3. Exporters → DOT, Mermaid, PlantUML, D2, JSON.
4. Renderer → via D3.js, Dagre, or Graphviz.
5. Validator → check references, style inheritance.

---

© 2025 Mark‑Graph Notation Schema Specification v1.0
