// D7.2.4v3: public value API; native mounting lives in lambda.editor.draw.view.
import core: .draw.core
import model: .draw.model
import scene: .draw.scene
pub fn new_document(options=null) => core.new_document(options)
pub fn vertex(id,descriptor,geometry,properties=null) any^ => core.vertex(id,descriptor,geometry,properties)^
pub fn edge(id,source,target,properties=null) any^ => core.edge(id,source,target,properties)^
pub fn open_session(document,options=null) any^ => core.open_session(document,options)^
pub fn exec(session,command) any^ => core.exec(session,command)^
pub fn transaction(document,context,command) any^ => core.transaction(document,context,command)^
pub fn validate(document,options=null) => core.validate(document,options)
pub fn vertices(document) => core.vertices(document)
pub fn edges(document) => core.edges(document)
pub fn lookup(document,id) => model.lookup(document,id)
pub fn incident_edges(document,id) => core.incident_edges(document,id)
pub fn incoming_edges(document,id) => core.incoming_edges(document,id)
pub fn outgoing_edges(document,id) => core.outgoing_edges(document,id)
pub fn neighbors(document,id) => core.neighbors(document,id)
pub fn resolve_endpoint(document,edge,side) => model.endpoints(document,edge)[side]
pub fn route(document,edge) => {points:model.visible_route(document,edge),path:model.path_data(document,edge),
    bounds:model.bounds(model.visible_route(document,edge)),handles:model.edge_handles(document,edge)}
pub fn hit(document,point,tolerance=6.0) => model.hit(document,point,tolerance)
pub fn to_svg(document,options=null) => scene.svg(document,"","",false,true,[],1.0,null,if (options.prefix == null) "" else options.prefix)
import codec: .draw.codec
pub fn encode(document,options=null) any^ => codec.encode(document,options)^
pub fn decode(value,options=null) any^ => codec.decode(value,options)^
import interaction: .draw.interaction
pub fn update(session,input) => interaction.update(session,input)
import schemas: .draw.schema
pub let schema = schemas.draw_schema
