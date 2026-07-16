workspace "Source contract" {
  model {
    user = person "User"
    views {
    }
  }
  model {
    system = softwareSystem "Ignored duplicate model"
  }
  misplaced = person "Misplaced"
  views {
    illegal = person "Illegal view child"
  }
}
