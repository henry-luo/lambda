#ifndef LAMBDA_MODULE_NODE_FS_MODULE_HPP
#define LAMBDA_MODULE_NODE_FS_MODULE_HPP

// The full executable links the first extracted fs slice directly and only
// registers it after the selected Jube profile includes node-fs.
extern "C" void node_fs_jube_register_static(void);

#endif
