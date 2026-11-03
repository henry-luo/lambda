import launch: ~~.mod_launch
let pair = launch.users("user=maya,alex")^
let spaces = launch.users("user= maya, noah ")^
let empty = launch.users("user=") ^ { ^ }
let trailing = launch.users("user=alex,") ^ { ^ }
let duplicate = launch.users("user=maya,maya") ^ { ^ }
let unknown = launch.users("user=missing") ^ { ^ };
[pair, spaces, launch.peer(pair, "maya"), launch.peer(pair, "alex"),
 launch.peer(["maya"], "maya"), launch.client_args(["--event-file", "{user}.json"], "maya"),
 empty is error, trailing is error, duplicate is error, unknown is error]
