// Public Structurizr/C4 facade.

import normalize_module: .normalize
import schema_module: .schema
import view_module: .views
import graph_transform: lambda.graph.transform

pub fn normalize(source) any^ => normalize_module.normalize(source)

pub fn validate_source(source) any^ => schema_module.validate_source(source)

pub fn validate(workspace) any^ => schema_module.validate(workspace)

pub fn view_keys(workspace) any^ => view_module.view_keys(workspace)

fn selected_key(workspace, key) any^ {
  let keys = view_keys(workspace);
  if ((key == null or string(key) == "") and len(keys) > 0) keys[0] else key
}

pub fn project(workspace, key = null) any^ =>
  view_module.project(workspace, selected_key(workspace, key))

pub fn project_all(workspace) any^ => view_module.project_all(workspace)

pub fn to_html(workspace, key = null, opts = null) any^ =>
  graph_transform.to_html(project(workspace, key), opts)
