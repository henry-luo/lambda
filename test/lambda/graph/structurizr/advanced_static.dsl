workspace "Advanced static" {
  model {
    properties {
      "structurizr.groupSeparator" "/"
    }
    group "Platform" {
      group "Payments" {
        api = softwareSystem "API" {
          tags Core
        }
        worker = softwareSystem "Worker" {
          tags "Core,Hidden"
        }
      }
    }
    user = person "Customer" {
      tags External
    }
    curved = api -> worker "Dispatches" {
      tags Curved
    }
    direct = user -> api "Uses" {
      tags Direct
    }
  }
  views {
    systemLandscape "All" {
      include *
      autoLayout lr
    }
    systemLandscape "Expression" {
      include "(element.tag==Core || element.tag==External) && element.tag!=Hidden"
    }
    terminology {
      person "Actor"
      softwareSystem "Service"
      relationship "Flow"
      metadata curly
    }
    styles {
      element Core {
        shape Hexagon
        border dotted
        opacity 80
      }
      relationship Curved {
        routing Curved
        style dotted
        opacity 60
      }
      relationship Direct {
        routing Direct
      }
    }
  }
}
