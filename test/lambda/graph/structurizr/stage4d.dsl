workspace "Order platform" {
  !identifiers hierarchical
  model {
    customer = person "Customer"
    store = softwareSystem "Order platform" {
      api = container "API" "Accepts orders" "Lambda"
      database = container "Database" "Stores orders" "SQLite"
      audit = container "Audit" "Records activity" "Lambda"
    }

    customer -> store.api "Places order"
    store.api -> store.database "Writes order" "SQL"
    store.api -> store.audit "Records activity" "HTTPS"
    store.api -> customer "Returns receipt" "HTTPS"

    production = deploymentEnvironment "Production" {
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
      1: customer -> store.api "Starts checkout"
      {
        {
          store.api -> store.database "Persists order"
        }
        {
          store.api -> store.audit "Records audit"
        }
      }
      3: store.api -> customer "Returns receipt"
      autoLayout lr
    }
    deployment * production "Production" {
      include *
      autoLayout lr
    }
  }
}
