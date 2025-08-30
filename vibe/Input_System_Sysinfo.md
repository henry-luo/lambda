# Lambda Input System: sys:// Scheme Design

## Overview

This document outlines the design for a new "sys://" URL scheme in the Lambda Input System that provides cross-platform system information access. The scheme enables Lambda scripts to query system resources, hardware information, and runtime metrics in a unified, structured format across Windows, macOS, and Linux platforms.

## Design Philosophy

### Core Principles
1. **Cross-Platform Consistency**: Identical API across all supported platforms
2. **Structured Data**: Return system information as structured Lambda elements
3. **Performance Oriented**: Minimal overhead with intelligent caching
4. **Security Conscious**: Safe read-only access to system information
5. **Extensible Architecture**: Easy to add new information sources

### Inspiration from Existing Systems
- **Linux /proc filesystem**: Hierarchical organization of system information
- **Windows WMI**: Comprehensive system management interface
- **macOS System Profiler**: Detailed hardware and software inventory
- **SIGAR Library**: Cross-platform system information gathering
- **Modern APIs**: JSON-structured responses like /api/sysinfo endpoints

## URL Scheme Structure

### Base Syntax
```
sys://[category]/[subcategory]/[item]?[parameters]
```

### Supported Categories

#### 1. System Overview
```
sys://system/info          # Basic system information
sys://system/uptime         # System uptime and boot time
sys://system/version        # OS version and kernel information
sys://system/hostname       # System hostname and domain
```

#### 2. Hardware Information
```
sys://hardware/cpu          # CPU information and specifications
sys://hardware/memory       # Physical memory information
sys://hardware/storage      # Storage devices and filesystems
sys://hardware/network      # Network interfaces and configuration
sys://hardware/gpu          # Graphics hardware information
sys://hardware/sensors      # Temperature, voltage, fan sensors
```

#### 3. Process Information
```
sys://process/list          # Running processes list
sys://process/[pid]         # Specific process information
sys://process/tree          # Process hierarchy tree
sys://process/stats         # Process statistics summary
```

#### 4. Performance Metrics
```
sys://perf/cpu              # CPU usage and load averages
sys://perf/memory           # Memory usage statistics
sys://perf/disk             # Disk I/O statistics
sys://perf/network          # Network I/O statistics
sys://perf/load             # System load metrics
```

#### 5. Filesystem Information
```
sys://fs/mounts             # Mounted filesystems
sys://fs/usage              # Filesystem usage statistics
sys://fs/[path]             # Specific path information
```

#### 6. Network Information
```
sys://net/interfaces        # Network interface details
sys://net/connections       # Active network connections
sys://net/routes            # Routing table information
sys://net/stats             # Network statistics
```

## Data Structure Design

### Lambda Element Schema

All sys:// responses return structured Lambda elements following consistent patterns:

#### System Information Example
```lambda
<system timestamp:t'2025-01-15T10:30:00Z'>
  <os name:"macOS" version:"14.2.1" kernel:"Darwin 23.2.0">
  <hostname value:"lambda-dev.local">
  <uptime seconds:86400 boot_time:t'2025-01-14T10:30:00Z'>
  <architecture value:"arm64">
  <platform value:"darwin">
</system>
```

#### CPU Information Example
```lambda
<cpu>
  <processor model:"Apple M2 Pro" cores:12 threads:12 frequency:3504000000>
    <cache l1_instruction:131072 l1_data:65536 l2:4194304 l3:0>
    <features>
      <feature name:"sse4_2" supported:true>
      <feature name:"avx2" supported:true>
      <feature name:"virtualization" supported:true>
    </features>
  </processor>
  <usage user:15.2 system:8.1 idle:76.7 iowait:0.0>
  <load_average one_min:1.23 five_min:1.45 fifteen_min:1.12>
</cpu>
```

#### Memory Information Example
```lambda
<memory>
  <physical total:17179869184 available:8589934592 used:8589934592 free:8589934592>
  <swap total:2147483648 used:0 free:2147483648>
  <virtual total:281474976710656 used:17179869184 available:264295107641472>
  <buffers size:1073741824>
  <cached size:2147483648>
</memory>
```

#### Process List Example
```lambda
<process_list timestamp:t'2025-01-15T10:30:00Z' count:247>
  <process pid:1 ppid:0 name:"kernel_task" user:"root" cpu:0.1 memory:8388608>
  <process pid:123 ppid:1 name:"lambda.exe" user:"henry" cpu:2.3 memory:67108864>
  <process pid:456 ppid:123 name:"child_process" user:"henry" cpu:0.5 memory:16777216>
</process_list>
```

### JSON Compatibility

For interoperability, all Lambda elements can be automatically converted to JSON:

```json
{
  "system": {
    "timestamp": "2025-01-15T10:30:00Z",
    "os": {
      "name": "macOS",
      "version": "14.2.1",
      "kernel": "Darwin 23.2.0"
    },
    "hostname": {
      "value": "lambda-dev.local"
    },
    "uptime": {
      "seconds": 86400,
      "boot_time": "2025-01-14T10:30:00Z"
    },
    "architecture": {
      "value": "arm64"
    },
    "platform": {
      "value": "darwin"
    }
  }
}
```

## Cross-Platform Implementation Strategy

### Platform-Specific Data Sources

#### Linux Implementation
- **System Info**: `/proc/version`, `/proc/uptime`, `/etc/os-release`
- **CPU Info**: `/proc/cpuinfo`, `/proc/stat`, `/proc/loadavg`
- **Memory Info**: `/proc/meminfo`, `/proc/vmstat`
- **Process Info**: `/proc/[pid]/`, `/proc/[pid]/stat`, `/proc/[pid]/status`
- **Network Info**: `/proc/net/dev`, `/proc/net/tcp`, `/proc/net/route`
- **Filesystem**: `/proc/mounts`, `/proc/diskstats`, `statvfs()`

#### macOS Implementation
- **System Info**: `uname()`, `sysctl()`, `host_info()`
- **CPU Info**: `sysctl()` with `hw.ncpu`, `hw.cpufrequency`
- **Memory Info**: `vm_statistics64()`, `host_statistics64()`
- **Process Info**: `proc_listpids()`, `proc_pidinfo()`
- **Network Info**: `getifaddrs()`, `sysctl()` with networking MIBs
- **Filesystem**: `getmntent()`, `statvfs()`

#### Windows Implementation
- **System Info**: `GetSystemInfo()`, `GetVersionEx()`, Registry
- **CPU Info**: WMI `Win32_Processor`, Performance Counters
- **Memory Info**: `GlobalMemoryStatusEx()`, Performance Counters
- **Process Info**: `EnumProcesses()`, `GetProcessMemoryInfo()`
- **Network Info**: `GetAdaptersInfo()`, `GetTcpTable()`
- **Filesystem**: `GetDiskFreeSpace()`, `GetVolumeInformation()`

### SIGAR C Library Integration

#### Library Selection Rationale
- **Proven Cross-Platform Support**: Works on Linux, macOS, Windows, Solaris, AIX
- **Comprehensive API**: Covers all major system information categories
- **Mature Codebase**: Battle-tested in enterprise environments
- **C Interface**: Direct integration with Lambda's C codebase
- **Apache License**: Compatible with Lambda's licensing

#### Integration Architecture
```c
// Core SIGAR wrapper structure
typedef struct SysInfoManager {
    sigar_t* sigar_handle;
    time_t last_update;
    int cache_ttl_seconds;
    HashMap* cached_results;
    bool initialized;
} SysInfoManager;

// Main entry points
SysInfoManager* sysinfo_manager_create(void);
void sysinfo_manager_destroy(SysInfoManager* manager);
Input* sysinfo_from_url(Url* url, SysInfoManager* manager);

// Category-specific functions
Input* sysinfo_get_system(SysInfoManager* manager);
Input* sysinfo_get_cpu(SysInfoManager* manager);
Input* sysinfo_get_memory(SysInfoManager* manager);
Input* sysinfo_get_processes(SysInfoManager* manager);
Input* sysinfo_get_network(SysInfoManager* manager);
```

#### Build Configuration
```json
{
  "dependencies": {
    "sigar": {
      "version": "1.6.4",
      "type": "static",
      "platforms": ["darwin", "linux", "windows"],
      "source": "https://github.com/hyperic/sigar",
      "build_flags": ["-DSIGAR_SHARED=OFF"]
    }
  }
}
```

## Caching and Performance

### Multi-Level Caching Strategy

#### 1. In-Memory Cache
- **TTL-Based**: Different cache times for different data types
  - Static info (CPU model, memory size): 1 hour
  - Semi-static info (process list): 5 seconds
  - Dynamic info (CPU usage, memory usage): 1 second
- **LRU Eviction**: Automatic cleanup of old entries
- **Thread-Safe**: Concurrent access support

#### 2. Query Optimization
- **Batch Queries**: Collect multiple metrics in single SIGAR call
- **Lazy Loading**: Only query requested information
- **Delta Calculations**: Compute usage percentages efficiently

#### 3. Platform-Specific Optimizations
- **Linux**: Memory-map /proc files for faster access
- **macOS**: Cache sysctl results and use batch queries
- **Windows**: Use WMI connection pooling and async queries

### Performance Targets
- **Cold Query**: <100ms for any sys:// request
- **Cached Query**: <10ms for cached responses
- **Memory Usage**: <10MB for full system information cache
- **CPU Overhead**: <1% CPU usage during normal operation

## Security Considerations

### Access Control
- **Read-Only Access**: No system modification capabilities
- **Safe Information**: Only expose non-sensitive system information
- **Process Filtering**: Hide sensitive processes (security software, etc.)
- **Network Sanitization**: Mask sensitive network configuration

### Information Disclosure Prevention
- **User Filtering**: Only show processes owned by current user (optional)
- **Path Sanitization**: Avoid exposing sensitive file paths
- **Credential Protection**: Never expose passwords or keys
- **Network Security**: Don't expose internal network topology details

### Platform Security Integration
- **Linux**: Respect user permissions and capabilities
- **macOS**: Honor System Integrity Protection (SIP) restrictions
- **Windows**: Work within User Account Control (UAC) limitations

## Error Handling

### Error Categories
1. **Platform Errors**: OS-specific API failures
2. **Permission Errors**: Insufficient access rights
3. **Resource Errors**: System resource unavailable
4. **Parse Errors**: Invalid URL format or parameters

### Error Response Format
```lambda
<error type:"permission_denied" category:"sys" url:"sys://process/1">
  <message value:"Access denied to process information">
  <code value:403>
  <timestamp value:t'2025-01-15T10:30:00Z'>
</error>
```

### Graceful Degradation
- **Partial Results**: Return available information even if some queries fail
- **Fallback Methods**: Use alternative APIs when primary method fails
- **Default Values**: Provide sensible defaults for unavailable metrics

## Usage Examples

### Basic System Information
```lambda
// Get system overview
let system = input("sys://system/info", 'auto')
system

// Get CPU information
let cpu = input("sys://hardware/cpu", 'auto')
cpu.processor.model
cpu.usage.user

// Get memory usage
let memory = input("sys://perf/memory", 'auto')
memory.physical.used / memory.physical.total * 100
```

### Process Monitoring
```lambda
// List all processes
let processes = input("sys://process/list", 'auto')
processes.process

// Get specific process info
let lambda_proc = input("sys://process/list?name=lambda.exe", 'auto')
lambda_proc.process[0].memory

// Process tree view
let proc_tree = input("sys://process/tree", 'auto')
proc_tree
```

### Performance Monitoring
```lambda
// CPU performance over time
for i in range(10) {
    let cpu = input("sys://perf/cpu", 'auto')
    print(cpu.usage.user, cpu.usage.system)
    sleep(1000)  // 1 second
}

// Memory usage analysis
let mem = input("sys://perf/memory", 'auto')
let usage_percent = mem.physical.used / mem.physical.total * 100
if usage_percent > 80 {
    print("High memory usage: ", usage_percent, "%")
}
```

### Network Monitoring
```lambda
// Network interface status
let interfaces = input("sys://net/interfaces", 'auto')
for iface in interfaces.interface {
    if iface.status == "up" {
        print(iface.name, iface.ip_address)
    }
}

// Active connections
let connections = input("sys://net/connections", 'auto')
connections.connection.filter(c => c.state == "ESTABLISHED")
```

## Implementation Phases

### Phase 1: Foundation (Week 1-2)
**Objective**: Set up SIGAR integration and basic infrastructure

**Tasks**:
1. **SIGAR Library Integration**
   - Add SIGAR as dependency to build system
   - Create cross-platform build scripts
   - Test basic SIGAR functionality

2. **Core Infrastructure**
   - Implement `SysInfoManager` structure
   - Create URL parsing for sys:// scheme
   - Add basic caching framework

3. **System Information Module**
   - Implement `sys://system/info` endpoint
   - Add basic system identification
   - Create Lambda element generation

**Deliverables**:
- [ ] SIGAR library successfully integrated
- [ ] Basic sys:// URL parsing working
- [ ] System info endpoint functional
- [ ] Cross-platform compatibility verified

### Phase 2: Hardware Information (Week 3)
**Objective**: Implement hardware information endpoints

**Tasks**:
1. **CPU Information**
   - Implement `sys://hardware/cpu` endpoint
   - Add CPU specifications and features
   - Include real-time usage statistics

2. **Memory Information**
   - Implement `sys://hardware/memory` endpoint
   - Add physical and virtual memory details
   - Include memory usage statistics

3. **Storage Information**
   - Implement `sys://hardware/storage` endpoint
   - Add disk and filesystem information
   - Include storage usage statistics

**Deliverables**:
- [ ] CPU information endpoint complete
- [ ] Memory information endpoint complete
- [ ] Storage information endpoint complete
- [ ] Hardware detection working across platforms

### Phase 3: Process and Performance (Week 4)
**Objective**: Add process monitoring and performance metrics

**Tasks**:
1. **Process Information**
   - Implement `sys://process/list` endpoint
   - Add process details and statistics
   - Include process tree functionality

2. **Performance Metrics**
   - Implement `sys://perf/*` endpoints
   - Add real-time performance monitoring
   - Include load average and system metrics

3. **Caching Optimization**
   - Implement intelligent caching for dynamic data
   - Add cache invalidation strategies
   - Optimize query performance

**Deliverables**:
- [ ] Process monitoring fully functional
- [ ] Performance metrics accurate
- [ ] Caching system optimized
- [ ] Real-time updates working

### Phase 4: Network and Filesystem (Week 5)
**Objective**: Complete network and filesystem information

**Tasks**:
1. **Network Information**
   - Implement `sys://net/*` endpoints
   - Add network interface details
   - Include connection monitoring

2. **Filesystem Information**
   - Implement `sys://fs/*` endpoints
   - Add mount point information
   - Include filesystem usage statistics

3. **Advanced Features**
   - Add query parameters and filtering
   - Implement batch queries
   - Add export formats (JSON, CSV)

**Deliverables**:
- [ ] Network monitoring complete
- [ ] Filesystem information complete
- [ ] Advanced query features working
- [ ] Multiple export formats supported

### Phase 5: Testing and Documentation (Week 6)
**Objective**: Comprehensive testing and documentation

**Tasks**:
1. **Cross-Platform Testing**
   - Test on macOS, Linux, Windows
   - Verify data accuracy and consistency
   - Performance testing and optimization

2. **Security Testing**
   - Verify access control mechanisms
   - Test error handling and edge cases
   - Security audit of exposed information

3. **Documentation and Examples**
   - Complete API documentation
   - Create usage examples and tutorials
   - Performance benchmarking

**Deliverables**:
- [ ] Full cross-platform compatibility
- [ ] Security audit complete
- [ ] Comprehensive documentation
- [ ] Performance benchmarks available

## Configuration

### System Configuration
```json
{
  "sysinfo": {
    "cache": {
      "ttl_static": 3600,
      "ttl_semi_static": 5,
      "ttl_dynamic": 1,
      "max_entries": 1000
    },
    "security": {
      "hide_sensitive_processes": true,
      "user_processes_only": false,
      "sanitize_paths": true
    },
    "performance": {
      "batch_queries": true,
      "lazy_loading": true,
      "async_updates": false
    }
  }
}
```

### Environment Variables
```bash
LAMBDA_SYSINFO_CACHE_TTL="5"
LAMBDA_SYSINFO_SECURITY_MODE="strict"
LAMBDA_SYSINFO_BATCH_SIZE="10"
```

## Success Metrics

### Functionality Targets
- **Coverage**: 100% of planned endpoints implemented
- **Accuracy**: >99% data accuracy compared to system tools
- **Consistency**: Identical data format across all platforms
- **Completeness**: All major system information categories covered

### Performance Targets
- **Response Time**: <100ms for any sys:// query
- **Memory Usage**: <10MB total memory footprint
- **CPU Overhead**: <1% CPU usage during operation
- **Cache Hit Rate**: >90% for repeated queries

### Reliability Targets
- **Cross-Platform**: 100% compatibility on supported platforms
- **Error Handling**: Graceful degradation for all error conditions
- **Security**: No information disclosure vulnerabilities
- **Stability**: No crashes or memory leaks under normal operation

## Future Enhancements

### Advanced Features
- **Historical Data**: Time-series data collection and storage
- **Alerting**: Threshold-based system monitoring
- **Clustering**: Multi-system information aggregation
- **Streaming**: Real-time system information updates
- **Machine Learning**: Anomaly detection and prediction

### Protocol Extensions
- **Remote Systems**: Query remote system information via SSH/WinRM
- **Container Support**: Docker and Kubernetes container information
- **Cloud Integration**: AWS/Azure/GCP instance metadata
- **IoT Devices**: Embedded system information gathering
- **Custom Metrics**: User-defined system metrics

This sys:// scheme design provides Lambda Script with comprehensive, cross-platform system information access while maintaining the language's focus on performance, simplicity, and structured data handling.
