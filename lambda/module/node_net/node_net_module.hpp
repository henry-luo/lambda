#ifndef LAMBDA_MODULE_NODE_NET_MODULE_HPP
#define LAMBDA_MODULE_NODE_NET_MODULE_HPP

// The full executable links the transitional node-net descriptor directly and
// registers it only when the selected Jube profile includes node-net.
extern "C" void node_net_jube_register_static(void);

#endif
