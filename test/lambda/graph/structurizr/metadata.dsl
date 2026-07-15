workspace "Metadata" {
  model {
    properties {
      "structurizr.groupSeparator" "/"
    }
    group "Sales" {
      user = person "User" {
        properties {
          owner "Retail"
          criticality "high"
        }
        perspectives {
          Security "Public actor" "high"
          perspective Reliability {
            description "User availability"
            value "medium"
            url "https://example.test/reliability"
          }
        }
      }
      app = softwareSystem "Application" {
        properties {
          owner "Retail"
        }
      }
      uses = user -> app "Uses" {
        properties {
          protocol "HTTPS"
        }
        perspectives {
          Security "Encrypted"
        }
      }
    }
  }
  views {
    systemLandscape "Retail" {
      include "element.properties[owner]==Retail"
    }
    systemLandscape "Sales" {
      include "element.group==Sales"
    }
    systemLandscape "WithoutHttps" {
      include *
      exclude "relationship.properties[protocol]==HTTPS"
    }
  }
}
