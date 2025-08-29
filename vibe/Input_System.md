# Enhanced Input System Design Plan

## Overview

This document outlines the design and implementation plan for enhancing the Lambda Script input system with directory listing, HTTP/HTTPS support, and comprehensive caching capabilities. The enhanced system will provide efficient, cached access to local files, directories, and remote resources.

## Current State Analysis

### Existing Architecture
- **Core Function**: `input_from_url()` handles file:// URLs only
- **Parser Integration**: 13+ format parsers (JSON, XML, HTML, Markdown, PDF, etc.)
- **Memory Management**: Variable memory pools with reference counting
- **URL System**: WHATWG-compliant URL parser supporting multiple schemes
- **Type System**: Strong typing with automatic format detection via MIME types

### Limitations
1. Only supports local file:// URLs
2. No directory listing capability
3. No HTTP/HTTPS network support
4. No caching mechanism
5. Each file read creates new Input object

## Enhanced System Design

### 1. Directory Listing Support

#### Design Approach
When a file:// URL points to a directory, return a structured representation using Lambda Script's element system:

```lambda
<directory name:"src" path:"/Users/project/src" modified:t'2025-01-01T10:30:00'>
  <file name:"main.cpp" size:1024 modified:t'2025-01-01T09:15:00'>
  <file name:"header.h" size:256 modified:t'2025-01-01T08:45:00'>
  <dir name:"lib" entries:15 modified:t'2025-01-01T10:00:00'>
</directory>
```

#### Implementation Strategy
- Extend `input_from_url()` to detect directory paths using `stat()`
- Use `readdir()` (POSIX) / `FindFirstFile()` (Windows) for directory traversal
- Create Element objects for each directory entry
- Include metadata: size, modification time, permissions, type
- Support recursive traversal via optional depth parameter

### 2. HTTP/HTTPS Network Support

#### libcurl Integration
- **Dependency**: Add libcurl as static dependency
- **Platforms**: Cross-platform support (macOS, Linux, Windows)
- **Features**: HTTP/HTTPS, automatic redirects, compression, SSL/TLS
- **Configuration**: Connection pooling, timeout settings, user-agent

#### Network Request Flow
1. Parse HTTP/HTTPS URL using existing URL parser
2. Check memory cache first
3. Check filesystem cache second
4. If miss, download via libcurl to temp cache
5. Parse content and store in memory cache
6. Return parsed Input object

#### Error Handling
- Network timeouts and connection failures
- HTTP error codes (404, 500, etc.)
- SSL certificate validation
- Graceful fallback for offline scenarios

### 3. Two-Level Caching System

#### Memory Cache (Level 1)
**Design Pattern**: LRU (Least Recently Used) with size limits
- **Key**: Normalized URL string
- **Value**: Parsed Input object with metadata
- **Eviction**: Time-based (TTL) + size-based (max entries/memory)
- **Thread Safety**: Read-write locks for concurrent access

**Cache Entry Structure**:
```c
typedef struct CacheEntry {
    String* url_key;           // normalized URL
    Input* input;             // parsed input object
    time_t created_at;        // creation timestamp
    time_t last_accessed;     // last access timestamp
    size_t memory_size;       // estimated memory usage
    struct CacheEntry* next;  // LRU linked list
    struct CacheEntry* prev;
} CacheEntry;
```

#### Filesystem Cache (Level 2)
**Design Pattern**: Content-addressable storage with metadata
- **Location**: `./temp/cache/` directory
- **Naming**: SHA-256 hash of URL + content-type extension
- **Metadata**: JSON sidecar files with headers, timestamps, ETags
- **Structure**: Hierarchical by first 2 chars of hash (like Git objects)

**Cache Directory Layout**:
```
./temp/cache/
├── metadata.json          # cache configuration and stats
├── ab/
│   ├── ab123...789.html   # cached content
│   └── ab123...789.meta   # metadata JSON
└── cd/
    ├── cd456...012.json
    └── cd456...012.meta
```

#### Cache Coherency & Aging
**Time-based Expiration**:
- Default TTL: 1 hour for remote content, indefinite for local files
- HTTP Cache-Control header respect
- Conditional requests (If-Modified-Since, ETag)

**Size-based Eviction**:
- Memory cache: Max 64MB or 1000 entries (configurable)
- Filesystem cache: Max 1GB total size (configurable)
- LRU eviction when limits exceeded

**Cache Validation**:
- Local files: stat() mtime comparison
- Remote files: HTTP HEAD requests with conditional headers

### 4. System Architecture

#### Core Components

**InputCacheManager**:
```c
typedef struct InputCacheManager {
    // Memory cache
    CacheEntry* lru_head;
    CacheEntry* lru_tail;
    HashMap* memory_cache;      // URL -> CacheEntry mapping
    
    // Filesystem cache
    char* cache_directory;
    size_t max_memory_size;
    size_t max_filesystem_size;
    size_t current_memory_size;
    
    // Configuration
    int default_ttl_seconds;
    int max_entries;
    bool enable_compression;
    bool validate_ssl;
    
    // Statistics
    size_t hit_count;
    size_t miss_count;
    size_t network_requests;
} InputCacheManager;
```

**Enhanced Input Functions**:
```c
// Enhanced main entry point
Input* input_from_url_cached(String* url, String* type, String* flavor, Url* cwd, InputCacheManager* cache);

// Directory listing
Input* input_from_directory(const char* directory_path, bool recursive, int max_depth);

// Network operations
Input* input_from_http(Url* url, InputCacheManager* cache);
char* download_to_cache(Url* url, InputCacheManager* cache);

// Cache management
InputCacheManager* cache_manager_create(const char* cache_dir);
void cache_manager_destroy(InputCacheManager* cache);
void cache_cleanup_expired(InputCacheManager* cache);
void cache_cleanup_by_size(InputCacheManager* cache);
```

#### Best Practices Integration

**From Other Languages/Systems**:
1. **HTTP Client**: libcurl's proven robustness (used by Git, PHP, Python)
2. **Cache Design**: Inspired by browser caches and CDN architectures
3. **Memory Management**: Reference counting with automatic cleanup
4. **Directory Traversal**: POSIX-compliant with Windows compatibility
5. **Content-Addressable Storage**: Git-like object storage for deduplication

**Performance Optimizations**:
- Lazy loading of directory contents
- Streaming parser integration
- Memory-mapped file access for large files
- Connection pooling for HTTP requests
- Compression support (gzip, deflate)

## Implementation Plan

### Phase 1: Foundation (Week 1-2)
**Objective**: Set up basic infrastructure and dependencies

**Tasks**:
1. **Add libcurl dependency**
   - Update `build_lambda_config.json` with libcurl static linking
   - Modify setup scripts for all platforms (`setup-mac-deps.sh`, `setup-linux-deps.sh`, `setup-windows-deps.sh`)
   - Test cross-compilation compatibility

2. **Create cache infrastructure**
   - Implement `InputCacheManager` structure
   - Create cache directory management functions
   - Add configuration loading from JSON/environment

3. **Enhance URL handling**
   - Extend URL parser to better handle directory detection
   - Add URL normalization functions
   - Implement cache key generation

**Deliverables**:
- [ ] libcurl successfully integrated and linking
- [ ] Cache manager structure implemented
- [ ] Basic cache directory creation/cleanup
- [ ] Unit tests for cache manager initialization

### Phase 2: Directory Listing (Week 3)
**Objective**: Implement directory traversal and element generation

**Tasks**:
1. **Directory detection and traversal**
   - Implement `is_directory()` helper using `stat()`
   - Create cross-platform directory reading (`readdir()` / `FindFirstFile()`)
   - Add metadata extraction (size, mtime, permissions)

2. **Element structure generation**
   - Design `<file>` and `<dir>` element schemas
   - Implement directory-to-elements conversion
   - Add support for recursive traversal with depth limits

3. **Integration with input system**
   - Modify `input_from_url()` to detect and handle directories
   - Add directory-specific type detection
   - Ensure proper memory management for large directories

**Deliverables**:
- [ ] Directory listing functionality working
- [ ] Recursive traversal with configurable depth
- [ ] Cross-platform compatibility tested
- [ ] Integration tests with existing input system

### Phase 3: HTTP/HTTPS Support (Week 4-5)
**Objective**: Add network download capabilities with libcurl

**Tasks**:
1. **HTTP client implementation**
   - Create libcurl wrapper functions
   - Implement download with progress callbacks
   - Add proper error handling and timeouts
   - Support HTTP redirects and compression

2. **Network integration**
   - Extend `input_from_url()` to handle HTTP/HTTPS schemes
   - Add network-specific error handling
   - Implement user-agent and header customization
   - Add SSL certificate validation options

3. **File system cache implementation**
   - Implement cache file naming strategy (SHA-256)
   - Create metadata sidecar files
   - Add cache file validation and cleanup
   - Implement atomic write operations

**Deliverables**:
- [ ] HTTP/HTTPS downloads working reliably
- [ ] Filesystem cache storing downloads correctly
- [ ] Network error handling comprehensive
- [ ] SSL/TLS support verified

### Phase 4: Memory Caching (Week 6)
**Objective**: Implement in-memory LRU cache for parsed objects

**Tasks**:
1. **LRU cache implementation**
   - Create doubly-linked list for LRU ordering
   - Implement HashMap integration for O(1) lookup
   - Add thread-safe access with read-write locks
   - Implement size-based and time-based eviction

2. **Cache integration**
   - Modify all input functions to check cache first
   - Add cache population on successful parsing
   - Implement cache invalidation for modified files
   - Add cache statistics and monitoring

3. **Memory management**
   - Ensure proper reference counting for cached objects
   - Implement memory usage estimation
   - Add graceful handling of memory pressure
   - Optimize cache entry allocation

**Deliverables**:
- [ ] LRU memory cache fully functional
- [ ] Cache hit/miss statistics accurate
- [ ] Memory usage tracking working
- [ ] Thread safety verified

### Phase 5: Cache Management & Optimization (Week 7)
**Objective**: Implement advanced cache features and optimization

**Tasks**:
1. **Cache aging and cleanup**
   - Implement TTL-based expiration
   - Add background cleanup threads
   - Create cache size monitoring and limits
   - Implement intelligent prefetching

2. **HTTP cache optimization**
   - Support HTTP cache headers (Cache-Control, ETag)
   - Implement conditional requests (If-Modified-Since)
   - Add compression support (gzip, deflate)
   - Optimize connection reuse

3. **Configuration and monitoring**
   - Add comprehensive configuration options
   - Implement cache statistics reporting
   - Create cache debugging and inspection tools
   - Add performance profiling hooks

**Deliverables**:
- [ ] Advanced cache management working
- [ ] HTTP cache optimization complete
- [ ] Configuration system flexible
- [ ] Performance monitoring available

### Phase 6: Testing & Documentation (Week 8)
**Objective**: Comprehensive testing and documentation

**Tasks**:
1. **Comprehensive testing**
   - Unit tests for all cache components
   - Integration tests with existing parsers
   - Performance tests with large datasets
   - Network failure simulation tests

2. **Cross-platform validation**
   - Test on macOS, Linux, Windows
   - Verify cross-compilation works
   - Test with various network conditions
   - Validate cache consistency across platforms

3. **Documentation and examples**
   - Update API documentation
   - Create usage examples and tutorials
   - Document configuration options
   - Add troubleshooting guide

**Deliverables**:
- [ ] Full test suite passing
- [ ] Cross-platform compatibility verified
- [ ] Complete documentation available
- [ ] Performance benchmarks documented

## Configuration

### Cache Configuration (JSON)
```json
{
  "input_cache": {
    "memory": {
      "max_size_mb": 64,
      "max_entries": 1000,
      "default_ttl_seconds": 3600
    },
    "filesystem": {
      "cache_directory": "./temp/cache",
      "max_size_gb": 1,
      "cleanup_threshold": 0.9
    },
    "network": {
      "timeout_seconds": 30,
      "max_redirects": 5,
      "user_agent": "Lambda-Script/1.0",
      "validate_ssl": true,
      "enable_compression": true
    }
  }
}
```

### Environment Variables
```bash
LAMBDA_CACHE_DIR="./temp/cache"
LAMBDA_CACHE_SIZE="1G"
LAMBDA_NETWORK_TIMEOUT="30"
LAMBDA_CACHE_TTL="3600"
```

## Success Metrics

### Performance Targets
- **Cache Hit Rate**: >80% for repeated accesses
- **Memory Usage**: <100MB for typical workloads
- **Network Latency**: <5s for typical HTTP requests
- **Directory Listing**: <1s for directories with <1000 entries

### Reliability Targets
- **Cross-platform**: 100% compatibility on macOS, Linux, Windows
- **Network Resilience**: Graceful handling of timeouts, 404s, network failures
- **Memory Safety**: No memory leaks under normal operation
- **Cache Consistency**: No stale data served when source changes

## Risk Mitigation

### Technical Risks
1. **libcurl Integration**: Static linking complexity across platforms
   - *Mitigation*: Comprehensive testing, fallback to system curl if needed

2. **Memory Leaks**: Cache holding references too long
   - *Mitigation*: Rigorous reference counting, automated leak testing

3. **Cache Corruption**: Filesystem cache inconsistency
   - *Mitigation*: Atomic writes, checksums, automatic recovery

4. **Network Security**: SSL/TLS and certificate validation
   - *Mitigation*: Use libcurl's proven security features, regular updates

### Operational Risks
1. **Disk Space**: Cache growing unbounded
   - *Mitigation*: Size limits, automatic cleanup, monitoring

2. **Network Abuse**: Too many requests to servers
   - *Mitigation*: Rate limiting, respect robots.txt, HTTP caching

3. **Performance Regression**: New features slowing down existing code
   - *Mitigation*: Performance benchmarks, optional caching, profiling

## Future Enhancements

### Advanced Features
- **Parallel Downloads**: Concurrent HTTP requests
- **Content Deduplication**: Share identical content across cache entries
- **Distributed Caching**: Network-shared cache for multi-user environments
- **Advanced Prefetching**: Predictive content loading
- **Cache Warming**: Pre-populate cache with common resources

### Protocol Extensions
- **FTP/FTPS Support**: File transfer protocol integration
- **SSH/SFTP Support**: Secure file transfer
- **Cloud Storage**: S3, Google Cloud, Azure Blob support
- **Version Control**: Git repository integration
- **Database Connections**: Direct SQL query support

This enhanced input system will provide Lambda Script with robust, efficient, and reliable access to both local and remote resources while maintaining the language's focus on performance and simplicity.
