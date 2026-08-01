import { build } from "esbuild";

const entries = ["raphael", "maxgraph", "loop-closure"];
const jointDependencyEntries = {
  "deps/backbone": "backbone",
  "deps/jquery": "jquery"
};

const jointDependencyPaths = {
  backbone: "./deps/backbone.js",
  jquery: "./deps/jquery.js"
};

const jointExternalDependencies = {
  name: "joint-external-dependencies",
  setup(build) {
    build.onResolve({ filter: /^[^./]/ }, (args) => {
      const path = jointDependencyPaths[args.path];
      return path ? { path, external: true } : null;
    });
  }
};

await Promise.all(entries.map((name) => build({
  entryPoints: [`src/${name}-entry.js`],
  bundle: true,
  format: "iife",
  platform: "browser",
  target: ["es2020"],
  outfile: `build/${name}.js`,
  // Preserve source expressions while shortening private bundle identifiers.
  // This keeps the local browser program semantically identical while making
  // the JointJS core practical to parse in a debug runtime.
  minifyIdentifiers: name === "raphael",
  minifyWhitespace: true,
  legalComments: "none"
}))); 

await build({
  entryPoints: jointDependencyEntries,
  bundle: true,
  format: "esm",
  platform: "browser",
  target: ["es2022"],
  outdir: "build/jointjs",
  minifyIdentifiers: true,
  minifyWhitespace: true,
  legalComments: "none"
});

await build({
  entryPoints: ["src/jointjs-entry.js"],
  bundle: true,
  splitting: true,
  format: "esm",
  platform: "browser",
  target: ["es2022"],
  outdir: "build/jointjs",
  entryNames: "jointjs",
  chunkNames: "jointjs-chunk-[hash]",
  plugins: [jointExternalDependencies],
  minifyIdentifiers: false,
  minifySyntax: true,
  minifyWhitespace: true,
  legalComments: "none"
});
