workspace "Grouped deployment" {
  !identifiers hierarchical
  model {
    service = softwareSystem "Service" {
      api = container "API"
      database = container "Database"
      api -> database "Reads and writes"
    }

    production = deploymentEnvironment "Production" {
      blue = deploymentGroup "Blue"
      green = deploymentGroup "Green"

      blueServer = deploymentNode "Blue server" {
        deploymentGroup blue
        apiBlue = containerInstance service.api {
          healthCheck "API" "https://blue.example.test/health"
        }
        blueDatabase = deploymentNode "Database server" {
          databaseBlue = containerInstance service.database
        }
      }
      greenServer = deploymentNode "Green server" {
        deploymentGroup green
        apiGreen = containerInstance service.api
        greenDatabase = deploymentNode "Database server" {
          databaseGreen = containerInstance service.database {
            healthCheck "Database" "https://green.example.test/health" 30 500
          }
        }
      }
    }
  }

  views {
    deployment * production "Production" {
      include *
      autoLayout lr
    }
  }
}
