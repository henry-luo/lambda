// Graph visual defaults kept separate from geometry and SVG construction.

fn contains(items, value) => len([for (item in items where item == value) item]) > 0

pub fn palette(name) {
  let theme = lower(string(name));
  let dark = contains([
    "dark", "tokyo-night", "nord", "dracula", "catppuccin-mocha",
    "one-dark", "github-dark", "zinc-dark"
  ], theme);
  if (dark) {
    {
      graph_background: "#111827",
      node_background: "#1f2937",
      node_border: "#64748b",
      node_text: "#f8fafc",
      edge: "#94a3b8"
    }
  } else {
    {
      graph_background: "transparent",
      node_background: "#ffffff",
      node_border: "#64748b",
      node_text: "#172033",
      edge: "#59636e"
    }
  }
}
