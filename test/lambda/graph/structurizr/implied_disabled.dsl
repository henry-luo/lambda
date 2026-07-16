workspace {
  !impliedRelationships false
  model {
    user = person "User"
    shop = softwareSystem "Shop" {
      web = container "Web"
    }
    user -> web "Calls"
  }
}
