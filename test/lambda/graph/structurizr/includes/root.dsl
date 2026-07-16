workspace "Include Demo" {
    !impliedRelationships false
    model {
        !include fragments/model
    }
    views {
        !include fragments/views.dsl
    }
}
