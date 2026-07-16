workspace "Invalid" {
  !identifiers flat
  model {
    duplicate = person "Person"
    duplicate = softwareSystem "System"
    orphan = container "Orphan"
    system = softwareSystem "Valid system" {
      misplaced = component "Misplaced component"
    }
    missing = containerInstance unknownContainer
    node = deploymentNode "Uncontained node"

    duplicate -> unknown "Missing endpoint"
    node -> duplicate "Invalid kinds"
  }
  views {
    systemContext orphan "BadScope"
    systemContext system "SameKey"
    systemLandscape "SameKey"
    filtered "MissingBase" include "Element" "BadFilter"
    styles {
      element Element {
        shape Hexagon
      }
    }
  }
}
