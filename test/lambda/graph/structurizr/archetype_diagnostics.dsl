workspace {
  !impliedRelationships com.example.CustomStrategy
  model {
    archetypes {
      missing = unknownBase {}
      first = second {}
      second = first {}
    }
    a = person "A"
    b = softwareSystem "B"
    unknown = absent "Unknown"
    a --absentRelation-> b "Calls"
  }
}
