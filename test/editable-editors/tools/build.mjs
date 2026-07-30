import { build } from "esbuild";

const entries = ["codemirror", "prosemirror", "editorjs"];

await Promise.all(entries.map((name) => build({
  entryPoints: [`src/${name}-entry.js`],
  bundle: true,
  format: "iife",
  platform: "browser",
  target: ["es2020"],
  outfile: `build/${name}.js`,
  legalComments: "none"
})));
