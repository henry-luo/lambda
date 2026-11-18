~user.name is string
let user = ~user
user.name == ~user.name
user.home is element
~user.home is element
;(\.~user.name)# == ~user.name
;(\.~user.home)# is element
;(&user.home) == (&~home)
;(&~user.home) == (&~home)
;(\.~user['name'])# == ~user.name
;(\.~user ++ \.name)# == ~user.name
