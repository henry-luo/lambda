#pragma once

#include "../jube/jube.h"

// Host-owned platform filesystem provider consumed by the node-fs Jube module.
bool js_node_fs_read_write(JubeNodeFilesystemReadWrite* operation);
void js_node_fs_read_write_release(JubeNodeFilesystemReadWrite* operation);
bool js_node_fs_copy_file(JubeNodeFilesystemCopy* operation);
bool js_node_fs_path_operation(JubeNodeFilesystemPathOperation* operation);
bool js_node_fs_string_operation(JubeNodeFilesystemStringOperation* operation);
void js_node_fs_string_operation_release(JubeNodeFilesystemStringOperation* operation);
bool js_node_fs_directory_read(JubeNodeFilesystemDirectoryOperation* operation);
void js_node_fs_directory_read_release(JubeNodeFilesystemDirectoryOperation* operation);
bool js_node_fs_descriptor_operation(JubeNodeFilesystemDescriptorOperation* operation);
bool js_node_fs_metadata_operation(JubeNodeFilesystemMetadataOperation* operation);
bool js_node_fs_statfs_operation(JubeNodeFilesystemStatfsOperation* operation);
Item js_node_fs_read_stream_new(Item path, Item options);
Item js_node_fs_write_stream_new(Item path, Item options);
