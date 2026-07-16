workspace "Scoped relationships" {
  !identifiers hierarchical
  !impliedRelationships false
  model {
    first = softwareSystem "First" {
      api = container "API" {
        outbound = -> second.worker "Calls"
        inbound = second.worker -> this "Returns"
      }
    }
    second = softwareSystem "Second" {
      worker = container "Worker"
    }
  }
}
