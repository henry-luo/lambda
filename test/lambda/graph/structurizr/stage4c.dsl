workspace "Static views" {
  !identifiers hierarchical
  model {
    visitor = person "Visitor" "Uses the platform" {
      tags External
    }
    store = softwareSystem "Store" "Sells products" {
      tags Core
      web = container "Web" "Serves requests" "Lambda" {
        tags Core
        api = component "API" "Handles requests" "Lambda" {
          tags API
        }
        repo = component "Repository" "Stores data" "SQLite" {
          tags Internal
        }
        hidden = component "Legacy" "Being retired" "C" {
          tags Hidden
        }
      }
      worker = container "Worker" "Runs jobs" "Lambda" {
        tags Core
      }
    }
    billing = softwareSystem "Billing" "Collects payments" {
      tags Core
    }
    queue = element "Queue" "Message Bus" "Carries commands"
    archive = element "Archive" "Object Store" "Stores messages"

    visitor -> store "Uses"
    store -> billing "Charges"
    visitor -> billing "Pays"
    store.web -> store.worker "Dispatches"
    store.web.api -> store.web.repo "Writes" "SQL" {
      tags Critical
    }
    store.web.repo -> store.web.hidden "Migrates" {
      tags Internal
    }
    queue -> archive "Forwards"
  }
  views {
    systemLandscape "Landscape" {
      include *
      autoLayout lr 140 90
    }
    systemContext store "ContextAll" {
      include *
    }
    systemContext store "ContextReluctant" {
      include *?
    }
    component store.web "Components" {
      include "element.type==Component && element.parent==store.web"
      exclude "element.tag==Hidden"
      exclude "relationship.tag==Critical"
    }
    component store.web "Coupled" {
      include "->store.web.api->"
    }
    custom "Integration" {
      include *
    }
    filtered "Landscape" include "Core,Relationship" "CoreOnly"
    filtered "Landscape" exclude "External" "NoExternal"
    styles {
      element API {
        background #eef6ff
        color #17365d
        stroke #3978a8
        strokeWidth 2
        fontSize 15
      }
      relationship Critical {
        color #b42318
        thickness 3
        dashed true
      }
    }
  }
}
