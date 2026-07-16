workspace "Order platform" {
  !identifiers hierarchical
  model {
    customer = person "Customer"
    store = softwareSystem "Order platform" {
      api = container "API" "Accepts orders" "Lambda"
      database = container "Database" "Stores orders" "SQLite"
      audit = container "Audit" "Records activity" "Lambda"
    }

    places = customer -> store.api "Places order"
    writes = store.api -> store.database "Writes order" "SQL"
    records = store.api -> store.audit "Records activity" "HTTPS"
    receipt = store.api -> customer "Returns receipt" "HTTPS"

    production = deploymentEnvironment "Production" {
      blue = deploymentGroup "Blue"
      region = deploymentNode "Region" {
        gateway = infrastructureNode "Gateway"
        application = deploymentNode "Application" {
          apiInstance = containerInstance store.api blue
          databaseInstance = containerInstance store.database blue
          auditInstance = containerInstance store.audit blue
        }
        gateway -> apiInstance "Forwards requests" "HTTPS"
      }
    }
  }

  views {
    dynamic store "PlaceOrder" {
      1: places "Starts checkout"
      {
        {
          writes "Persists order"
        }
        {
          records "Records audit"
        }
      }
      3: receipt
      autoLayout lr
    }
    deployment * production "Production" {
      include *
      autoLayout lr
    }
  }
}
