workspace "Expression diagnostics" {
  model {
    user = person "User"
    system = softwareSystem "System"
    user -> system "Uses" {
      tags Internal
    }
  }
  views {
    custom "Valid" {
      include "element.type==Person && element.tag==Element"
      exclude "relationship.tag!=Internal"
    }
    custom "Diagnostics" {
      include "element.owner==Team"
      include "element.type=="
      exclude "element.properties[owner]=Retail"
      exclude "(element.tag==Element)"
    }
  }
}
