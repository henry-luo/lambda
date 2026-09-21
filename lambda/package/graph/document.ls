// Native document-loader entry point for all supported graph source flavors.

import graph_transform: .transform
import structurizr: .structurizr.structurizr

fn option(options, key, fallback = null) =>
  if (options != null and options[key] != null) options[key] else fallback

pub fn to_html(source, options) {
  let installed = graph_transform.install()
  if (source.flavor == "structurizr") {
    let workspace = structurizr.normalize(source)
    structurizr.to_html(workspace, option(options, "view_key"), options)
  } else {
    graph_transform.to_html(source, options)
  }
}
