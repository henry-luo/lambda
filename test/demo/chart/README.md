# Chart collection

From the checkout or unpacked release root:

```sh
./lambda.exe demo chart
./lambda.exe view test/demo/chart/index.html
```

The index links to three pages, each with a breadcrumb back to the collection:

| Page | Examples and controls |
| --- | --- |
| `normal.ls` | The existing dashboard specifications, with two live animation examples, point/legend selection, pan/zoom, linked brushing, and a range input. Playback supports replay, pause, resume, halfway seeking, and reverse. Gallery marks support hover/focus and selection; Shift-click adds marks and double-click clears. |
| `scientific.ls` | The existing six scientific examples: saddle surface, contour bands, streamlines, spectrum, spectrogram, and voxels. Drag the surface to orbit, Shift-drag to pan, and scroll to zoom. |
| `graphs.ls` | An illustrative interactive timeline, Mermaid flowchart and sequence diagram, TikZ/PGFPlots, Graphviz, word cloud, Sankey, chord, force graph, trees, circle packing, links, polygons, and paths. |

The normal and graph galleries read `test/lambda/chart/dashboard_data.json`,
the same source used by `test/lambda/chart/chart_dashboard.ls`. The release
preparation script bundles that file. Shared page layout lives in
`mod_gallery.ls` and `chart.css`; chart conversion and classification live in
`mod_catalog.ls`. The normal gallery mounts eight interactive examples per page;
Previous/Next controls expose the entire dashboard without mounting every live
view at once. Packages use the shipped `lambda.*` hierarchy (D7.2.4v3).
Live state belongs to each applied view (S12.1.3). Animations play once on load
and can be replayed. Mermaid, Graphviz, and TikZ examples are rendered diagrams;
the timeline and chart marks provide the interactive examples.

The existing animation sampler can log caught color-parser diagnostics while
interpolating SVG paint attributes in the morph example. Playback and rendering
continue; this demo does not change that package behavior. Scientific 3D output
needs the native graphics host, so the navigation replay uses the visible viewer.

The existing standalone `./lambda.exe demo wordcloud` remains available.

To replay the collection checks:

```sh
mkdir -p temp/chart-demo
./lambda.exe demo chart \
  --event-file test/demo/chart/tests/navigation.json \
  --event-result temp/chart-demo/navigation.json
./lambda.exe view test/demo/chart/normal.ls --headless \
  --event-file test/demo/chart/tests/interaction.json \
  --event-result temp/chart-demo/interaction.json
```
