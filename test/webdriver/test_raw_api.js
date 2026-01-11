/**
 * Raw WebDriver API Test
 * 
 * Tests the WebDriver HTTP API directly without Selenium client.
 * Useful for debugging the server implementation.
 * 
 * Usage:
 *   1. Start the WebDriver server: ./lambda.exe webdriver --port 4444
 *   2. Run: node test/webdriver/test_raw_api.js
 */

const http = require('http');
const path = require('path');

const WEBDRIVER_URL = process.env.WEBDRIVER_URL || 'http://localhost:4444';

// HTTP request helper
function request(method, urlPath, body = null) {
    return new Promise((resolve, reject) => {
        const url = new URL(urlPath, WEBDRIVER_URL);
        const options = {
            hostname: url.hostname,
            port: url.port,
            path: url.pathname + url.search,
            method: method,
            headers: {
                'Content-Type': 'application/json',
                'Accept': 'application/json'
            }
        };

        const req = http.request(options, (res) => {
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => {
                let parsed = null;
                try {
                    parsed = data ? JSON.parse(data) : null;
                } catch (e) {
                    parsed = data;
                }
                resolve({
                    status: res.statusCode,
                    headers: res.headers,
                    body: parsed,
                    raw: data
                });
            });
        });

        req.on('error', reject);
        req.setTimeout(5000, () => {
            req.destroy();
            reject(new Error('Request timeout'));
        });
        
        if (body) {
            req.write(JSON.stringify(body));
        }
        req.end();
    });
}

function log(msg) {
    console.log(msg);
}

function logRequest(method, path) {
    console.log(`\n>>> ${method} ${path}`);
}

function logResponse(response) {
    console.log(`<<< ${response.status}`);
    if (response.body) {
        console.log(JSON.stringify(response.body, null, 2));
    }
}

// ============================================================================
// Test Cases
// ============================================================================

async function testStatus() {
    log('\n=== Test: GET /status ===');
    logRequest('GET', '/status');
    
    const response = await request('GET', '/status');
    logResponse(response);
    
    if (response.status !== 200) {
        throw new Error(`Expected 200, got ${response.status}`);
    }
    if (!response.body?.value?.ready) {
        throw new Error('Expected server to be ready');
    }
    
    log('✓ Status check passed');
}

async function testCreateSession() {
    log('\n=== Test: POST /session ===');
    
    const capabilities = {
        capabilities: {
            alwaysMatch: {
                browserName: 'radiant',
                'radiant:options': {
                    headless: true,
                    windowSize: { width: 1200, height: 800 }
                }
            }
        }
    };
    
    logRequest('POST', '/session');
    console.log('Body:', JSON.stringify(capabilities, null, 2));
    
    const response = await request('POST', '/session', capabilities);
    logResponse(response);
    
    if (response.status !== 200) {
        throw new Error(`Expected 200, got ${response.status}`);
    }
    
    const sessionId = response.body?.value?.sessionId;
    if (!sessionId) {
        throw new Error('Expected sessionId in response');
    }
    
    log(`✓ Session created: ${sessionId}`);
    return sessionId;
}

async function testNavigate(sessionId) {
    log('\n=== Test: POST /session/:id/url ===');
    
    const testFile = `file://${path.resolve(__dirname, '..', 'html', 'index.html')}`;
    
    logRequest('POST', `/session/${sessionId}/url`);
    console.log('Body:', JSON.stringify({ url: testFile }));
    
    const response = await request('POST', `/session/${sessionId}/url`, {
        url: testFile
    });
    logResponse(response);
    
    if (response.status !== 200) {
        throw new Error(`Expected 200, got ${response.status}`);
    }
    
    log('✓ Navigation successful');
}

async function testGetUrl(sessionId) {
    log('\n=== Test: GET /session/:id/url ===');
    
    logRequest('GET', `/session/${sessionId}/url`);
    
    const response = await request('GET', `/session/${sessionId}/url`);
    logResponse(response);
    
    if (response.status !== 200) {
        throw new Error(`Expected 200, got ${response.status}`);
    }
    
    log(`✓ Current URL: ${response.body?.value}`);
}

async function testGetTitle(sessionId) {
    log('\n=== Test: GET /session/:id/title ===');
    
    logRequest('GET', `/session/${sessionId}/title`);
    
    const response = await request('GET', `/session/${sessionId}/title`);
    logResponse(response);
    
    if (response.status !== 200) {
        throw new Error(`Expected 200, got ${response.status}`);
    }
    
    log(`✓ Page title: ${response.body?.value}`);
}

async function testFindElement(sessionId) {
    log('\n=== Test: POST /session/:id/element ===');
    
    const locator = {
        using: 'css selector',
        value: 'body'
    };
    
    logRequest('POST', `/session/${sessionId}/element`);
    console.log('Body:', JSON.stringify(locator));
    
    const response = await request('POST', `/session/${sessionId}/element`, locator);
    logResponse(response);
    
    if (response.status !== 200) {
        // 404 is acceptable if no element found
        if (response.status === 404) {
            log('⚠ No element found (expected for some test cases)');
            return null;
        }
        throw new Error(`Expected 200, got ${response.status}`);
    }
    
    const elementId = response.body?.value?.['element-6066-11e4-a52e-4f735466cecf'];
    log(`✓ Found element: ${elementId}`);
    return elementId;
}

async function testFindElements(sessionId) {
    log('\n=== Test: POST /session/:id/elements ===');
    
    const locator = {
        using: 'css selector',
        value: 'div'
    };
    
    logRequest('POST', `/session/${sessionId}/elements`);
    console.log('Body:', JSON.stringify(locator));
    
    const response = await request('POST', `/session/${sessionId}/elements`, locator);
    logResponse(response);
    
    if (response.status !== 200) {
        throw new Error(`Expected 200, got ${response.status}`);
    }
    
    const elements = response.body?.value || [];
    log(`✓ Found ${elements.length} elements`);
    return elements;
}

async function testElementClick(sessionId, elementId) {
    if (!elementId) {
        log('\n=== Test: POST /session/:id/element/:id/click (SKIPPED) ===');
        return;
    }
    
    log('\n=== Test: POST /session/:id/element/:id/click ===');
    
    logRequest('POST', `/session/${sessionId}/element/${elementId}/click`);
    
    const response = await request('POST', `/session/${sessionId}/element/${elementId}/click`, {});
    logResponse(response);
    
    if (response.status !== 200) {
        throw new Error(`Expected 200, got ${response.status}`);
    }
    
    log('✓ Click successful');
}

async function testGetElementText(sessionId, elementId) {
    if (!elementId) {
        log('\n=== Test: GET /session/:id/element/:id/text (SKIPPED) ===');
        return;
    }
    
    log('\n=== Test: GET /session/:id/element/:id/text ===');
    
    logRequest('GET', `/session/${sessionId}/element/${elementId}/text`);
    
    const response = await request('GET', `/session/${sessionId}/element/${elementId}/text`);
    logResponse(response);
    
    if (response.status !== 200) {
        throw new Error(`Expected 200, got ${response.status}`);
    }
    
    log(`✓ Element text: "${response.body?.value}"`);
}

async function testGetElementRect(sessionId, elementId) {
    if (!elementId) {
        log('\n=== Test: GET /session/:id/element/:id/rect (SKIPPED) ===');
        return;
    }
    
    log('\n=== Test: GET /session/:id/element/:id/rect ===');
    
    logRequest('GET', `/session/${sessionId}/element/${elementId}/rect`);
    
    const response = await request('GET', `/session/${sessionId}/element/${elementId}/rect`);
    logResponse(response);
    
    if (response.status !== 200) {
        throw new Error(`Expected 200, got ${response.status}`);
    }
    
    const rect = response.body?.value;
    log(`✓ Element rect: x=${rect?.x}, y=${rect?.y}, w=${rect?.width}, h=${rect?.height}`);
}

async function testScreenshot(sessionId) {
    log('\n=== Test: GET /session/:id/screenshot ===');
    
    logRequest('GET', `/session/${sessionId}/screenshot`);
    
    const response = await request('GET', `/session/${sessionId}/screenshot`);
    
    // Don't log the full base64 data
    console.log(`<<< ${response.status}`);
    if (response.body?.value) {
        const data = response.body.value;
        console.log(`{ "value": "<base64 PNG, ${data.length} chars>" }`);
    }
    
    if (response.status !== 200) {
        throw new Error(`Expected 200, got ${response.status}`);
    }
    
    const screenshot = response.body?.value;
    if (!screenshot || screenshot.length < 100) {
        throw new Error('Expected base64 screenshot data');
    }
    
    log(`✓ Screenshot captured (${screenshot.length} bytes base64)`);
}

async function testDeleteSession(sessionId) {
    log('\n=== Test: DELETE /session/:id ===');
    
    logRequest('DELETE', `/session/${sessionId}`);
    
    const response = await request('DELETE', `/session/${sessionId}`);
    logResponse(response);
    
    if (response.status !== 200) {
        throw new Error(`Expected 200, got ${response.status}`);
    }
    
    log('✓ Session deleted');
}

async function testInvalidSession() {
    log('\n=== Test: Invalid Session ID ===');
    
    logRequest('GET', '/session/invalid-session-12345/url');
    
    const response = await request('GET', '/session/invalid-session-12345/url');
    logResponse(response);
    
    if (response.status !== 404) {
        throw new Error(`Expected 404, got ${response.status}`);
    }
    
    log('✓ Invalid session correctly returns 404');
}

// ============================================================================
// Main
// ============================================================================

async function main() {
    console.log('='.repeat(60));
    console.log('Radiant WebDriver Raw API Test');
    console.log('='.repeat(60));
    console.log(`Server: ${WEBDRIVER_URL}`);
    
    try {
        // Test server connectivity
        await testStatus();
        
        // Test session lifecycle
        const sessionId = await testCreateSession();
        
        // Test navigation
        await testNavigate(sessionId);
        await testGetUrl(sessionId);
        await testGetTitle(sessionId);
        
        // Test element finding
        const elementId = await testFindElement(sessionId);
        await testFindElements(sessionId);
        
        // Test element operations
        await testElementClick(sessionId, elementId);
        await testGetElementText(sessionId, elementId);
        await testGetElementRect(sessionId, elementId);
        
        // Test screenshot
        await testScreenshot(sessionId);
        
        // Test session deletion
        await testDeleteSession(sessionId);
        
        // Test error handling
        await testInvalidSession();
        
        console.log('\n' + '='.repeat(60));
        console.log('All tests passed!');
        console.log('='.repeat(60));
        
    } catch (error) {
        console.error('\n❌ Test failed:', error.message);
        if (error.code === 'ECONNREFUSED') {
            console.error('\nMake sure the WebDriver server is running:');
            console.error('  ./lambda.exe webdriver --port 4444');
        }
        process.exit(1);
    }
}

main();
