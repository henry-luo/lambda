workspace "Archetype workspace" {
  model {
    archetypes {
      application = container {
        description "Default application"
        technology "Spring"
        tags "Application"
        properties {
          owner "platform"
        }
        perspectives {
          Security "Default posture" "green"
        }
      }
      spring = application {
        technology "Spring Boot"
        tags "Backend"
      }
      sync = -> {
        technology "HTTPS"
        tags "Synchronous"
      }
      secure = --sync-> {
        tags "Encrypted"
        properties {
          protocol "TLS"
        }
      }
    }

    user = person "User"
    shop = softwareSystem "Shop" {
      web = spring "Web" "Explicit description" {
        tags "Public"
        properties {
          owner "team-web"
        }
      }
    }
    user --secure-> web "Calls"
  }
  views {
    systemContext shop "Context" {
      include *
    }
  }
}
