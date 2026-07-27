#ifndef LAMBDA_MODULE_NODE_CORE_MODULE_HPP
#define LAMBDA_MODULE_NODE_CORE_MODULE_HPP

// The full static Lambda executable links node-core directly and registers it
// after selecting its Jube module profile.
extern "C" void node_core_jube_register_static(void);

#endif
