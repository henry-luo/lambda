// shared breadcrumb structure for the Lambda pages in this demo collection.
pub fn breadcrumbs(title, root = "index.html") element =>
    <nav class:"scene3d-breadcrumbs",'aria-label':"Breadcrumb",
        <a href:root,"Scene3D">
        <span class:"scene3d-separator",'aria-hidden':"true","/">
        <span 'aria-current':"page",title>>
