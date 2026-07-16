workspace "Source contract" {
  model {
    user = person "User"
  }
  model {
    system = softwareSystem "Ignored duplicate model"
  }
  misplaced = person "Misplaced"
  views {
  }
}
