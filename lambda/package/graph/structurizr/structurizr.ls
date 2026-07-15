// Public Structurizr/C4 facade.

import normalize_module: .normalize
import view_module: .views
import graph_transform: lambda.package.graph.transform

pub fn normalize(source) => normalize_module.normalize(source)

pub fn view_keys(workspace) => view_module.view_keys(workspace)

pub fn project(workspace, key) => view_module.project(workspace, key)

pub fn project_all(workspace) => view_module.project_all(workspace)

pub fn to_html(workspace, key, opts = null) =>
  graph_transform.to_html(project(workspace, key), opts)
