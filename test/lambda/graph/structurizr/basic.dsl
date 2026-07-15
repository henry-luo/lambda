workspace "Online shop" "C4 workspace" {
  !identifiers hierarchical
  model {
    user = person "Customer" "Shops online"
    shop = softwareSystem "Online shop" "Sells books" {
      web = container "Web application" "Serves the storefront" "Lambda"
      db = container "Database" "Stores orders" "SQLite" {
        tags "Database"
      }
    }
    user -> shop "Uses"
    user -> shop.web "Uses" "HTTPS"
    shop.web -> shop.db "Reads from and writes to" "SQL"
    development = deploymentEnvironment "Development" {
      laptop = deploymentNode "Developer laptop" {
        webInstance = containerInstance shop.web
        dbInstance = containerInstance shop.db
      }
    }
  }
  views {
    systemContext shop "Context" {
      include *
      autoLayout lr 120 80
    }
    container shop "Containers" {
      include element.type==container
      autoLayout lr
    }
    styles {
      element "Software System" {
        background #1168bd
        color #ffffff
      }
      relationship "Relationship" {
        routing Orthogonal
      }
    }
  }
}
