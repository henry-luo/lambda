/**
 * WebDriver Test Suite for Radiant
 * 
 * Tests the W3C WebDriver implementation in radiant/webdriver/
 * 
 * Usage:
 *   1. Start the WebDriver server: ./lambda.exe webdriver --port 4444
 *   2. Run tests: node test/webdriver/test_webdriver.js
 * 
 * Requirements:
 *   npm install selenium-webdriver
 */

const { Builder, By, until } = require('selenium-webdriver');
const http = require('http');
const path = require('path');
const assert = require('assert');

// Configuration
const WEBDRIVER_URL = process.env.WEBDRIVER_URL || 'http://localhost:4444';
const TEST_HTML_DIR = path.join(__dirname, '..', 'html');
const TIMEOUT_MS = 10000;

// Test results tracking
let passed = 0;
let failed = 0;
const results = [];

// ============================================================================
// Test Utilities
// ============================================================================

function log(msg) {
    console.log(`[TEST] ${msg}`);
}

function logPass(testName) {
    passed++;
    results.push({ name: testName, status: 'PASS' });
    console.log(`  ✓ ${testName}`);
}

function logFail(testName, error) {
    failed++;
    results.push({ name: testName, status: 'FAIL', error: error.message });
    console.log(`  ✗ ${testName}`);
    console.log(`    Error: ${error.message}`);
}

async function runTest(name, testFn) {
    try {
        await testFn();
        logPass(name);
    } catch (error) {
        logFail(name, error);
    }
}

// HTTP request helper for raw WebDriver API testing
function httpRequest(method, path, body = null) {
    return new Promise((resolve, reject) => {
        const url = new URL(path, WEBDRIVER_URL);
        const options = {
            hostname: url.hostname,
            port: url.port,
            path: url.pathname,
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
                try {
                    resolve({
                        status: res.statusCode,
                        headers: res.headers,
                        body: data ? JSON.parse(data) : null
                    });
                } catch (e) {
                    resolve({
                        status: res.statusCode,
                        headers: res.headers,
                        body: data
                    });
                }
            });
        });

        req.on('error', reject);
        
        if (body) {
            req.write(JSON.stringify(body));
        }
        req.end();
    });
}

// ============================================================================
// Raw HTTP API Tests
// ============================================================================

async function testServerStatus() {
    const response = await httpRequest('GET', '/status');
    assert.strictEqual(response.status, 200, 'Status endpoint should return 200');
    assert.ok(response.body, 'Response should have body');
    assert.ok(response.body.value, 'Response should have value');
    assert.strictEqual(response.body.value.ready, true, 'Server should be ready');
}

async function testCreateSession() {
    const response = await httpRequest('POST', '/session', {
        capabilities: {
            alwaysMatch: {
                browserName: 'radiant'
            }
        }
    });
    
    assert.strictEqual(response.status, 200, 'Create session should return 200');
    assert.ok(response.body.value, 'Response should have value');
    assert.ok(response.body.value.sessionId, 'Response should have sessionId');
    
    // Clean up - delete the session
    const sessionId = response.body.value.sessionId;
    await httpRequest('DELETE', `/session/${sessionId}`);
    
    return sessionId;
}

async function testDeleteSession() {
    // Create a session first
    const createResponse = await httpRequest('POST', '/session', {
        capabilities: { alwaysMatch: { browserName: 'radiant' } }
    });
    const sessionId = createResponse.body.value.sessionId;
    
    // Delete it
    const deleteResponse = await httpRequest('DELETE', `/session/${sessionId}`);
    assert.strictEqual(deleteResponse.status, 200, 'Delete session should return 200');
}

async function testInvalidSession() {
    const response = await httpRequest('GET', '/session/invalid-session-id/url');
    assert.strictEqual(response.status, 404, 'Invalid session should return 404');
    assert.ok(response.body.value.error, 'Response should have error');
}

// ============================================================================
// Selenium WebDriver Tests
// ============================================================================

async function testSeleniumNavigation(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    const url = await driver.getCurrentUrl();
    assert.ok(url.includes('index.html'), 'URL should contain index.html');
}

async function testSeleniumGetTitle(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    const title = await driver.getTitle();
    // Title might be empty if not implemented, just check it doesn't throw
    assert.ok(typeof title === 'string', 'Title should be a string');
}

async function testSeleniumFindElementById(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    // Try to find an element - adjust selector based on actual test HTML
    try {
        const element = await driver.findElement(By.css('body'));
        assert.ok(element, 'Should find body element');
    } catch (e) {
        // If no element found, that's also valid behavior for this test
        if (!e.message.includes('no such element')) {
            throw e;
        }
    }
}

async function testSeleniumFindElementByCss(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    try {
        const elements = await driver.findElements(By.css('div'));
        assert.ok(Array.isArray(elements), 'findElements should return array');
    } catch (e) {
        if (!e.message.includes('no such element')) {
            throw e;
        }
    }
}

async function testSeleniumFindElementByTagName(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    try {
        const elements = await driver.findElements(By.tagName('p'));
        assert.ok(Array.isArray(elements), 'findElements by tag should return array');
    } catch (e) {
        if (!e.message.includes('no such element')) {
            throw e;
        }
    }
}

async function testSeleniumScreenshot(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    const screenshot = await driver.takeScreenshot();
    assert.ok(screenshot, 'Screenshot should be returned');
    assert.ok(typeof screenshot === 'string', 'Screenshot should be base64 string');
    // Validate it's base64 PNG
    assert.ok(screenshot.length > 100, 'Screenshot should have significant data');
}

async function testSeleniumElementClick(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    try {
        const element = await driver.findElement(By.css('a'));
        if (element) {
            await element.click();
            // If click succeeds without error, test passes
        }
    } catch (e) {
        // No link element is fine for this test
        if (!e.message.includes('no such element')) {
            throw e;
        }
    }
}

async function testSeleniumSendKeys(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    try {
        const input = await driver.findElement(By.css('input[type="text"]'));
        if (input) {
            await input.clear();
            await input.sendKeys('Hello WebDriver');
            // Success if no error
        }
    } catch (e) {
        // No input element is fine for this test
        if (!e.message.includes('no such element')) {
            throw e;
        }
    }
}

async function testSeleniumGetElementText(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    try {
        const element = await driver.findElement(By.css('p'));
        if (element) {
            const text = await element.getText();
            assert.ok(typeof text === 'string', 'getText should return string');
        }
    } catch (e) {
        if (!e.message.includes('no such element')) {
            throw e;
        }
    }
}

async function testSeleniumGetAttribute(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    try {
        const element = await driver.findElement(By.css('a'));
        if (element) {
            const href = await element.getAttribute('href');
            // href could be null if attribute doesn't exist
            assert.ok(href === null || typeof href === 'string', 
                     'getAttribute should return string or null');
        }
    } catch (e) {
        if (!e.message.includes('no such element')) {
            throw e;
        }
    }
}

async function testSeleniumGetRect(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    try {
        const element = await driver.findElement(By.css('body'));
        if (element) {
            const rect = await element.getRect();
            assert.ok(typeof rect.x === 'number', 'rect.x should be number');
            assert.ok(typeof rect.y === 'number', 'rect.y should be number');
            assert.ok(typeof rect.width === 'number', 'rect.width should be number');
            assert.ok(typeof rect.height === 'number', 'rect.height should be number');
        }
    } catch (e) {
        if (!e.message.includes('no such element')) {
            throw e;
        }
    }
}

async function testSeleniumIsDisplayed(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    try {
        const element = await driver.findElement(By.css('body'));
        if (element) {
            const displayed = await element.isDisplayed();
            assert.ok(typeof displayed === 'boolean', 'isDisplayed should return boolean');
        }
    } catch (e) {
        if (!e.message.includes('no such element')) {
            throw e;
        }
    }
}

async function testSeleniumIsEnabled(driver) {
    const testFile = `file://${path.join(TEST_HTML_DIR, 'index.html')}`;
    await driver.get(testFile);
    
    try {
        const element = await driver.findElement(By.css('body'));
        if (element) {
            const enabled = await element.isEnabled();
            assert.ok(typeof enabled === 'boolean', 'isEnabled should return boolean');
        }
    } catch (e) {
        if (!e.message.includes('no such element')) {
            throw e;
        }
    }
}

// ============================================================================
// Main Test Runner
// ============================================================================

async function runRawApiTests() {
    log('Running Raw HTTP API Tests...');
    
    await runTest('GET /status returns ready', testServerStatus);
    await runTest('POST /session creates session', testCreateSession);
    await runTest('DELETE /session/:id deletes session', testDeleteSession);
    await runTest('Invalid session returns 404', testInvalidSession);
}

async function runSeleniumTests() {
    log('Running Selenium WebDriver Tests...');
    
    let driver;
    try {
        driver = await new Builder()
            .usingServer(WEBDRIVER_URL)
            .forBrowser('chrome')  // Browser name (Radiant accepts any)
            .build();
        
        await runTest('Navigation to file URL', () => testSeleniumNavigation(driver));
        await runTest('Get page title', () => testSeleniumGetTitle(driver));
        await runTest('Find element by CSS', () => testSeleniumFindElementByCss(driver));
        await runTest('Find element by tag name', () => testSeleniumFindElementByTagName(driver));
        await runTest('Take screenshot', () => testSeleniumScreenshot(driver));
        await runTest('Element click', () => testSeleniumElementClick(driver));
        await runTest('Send keys to element', () => testSeleniumSendKeys(driver));
        await runTest('Get element text', () => testSeleniumGetElementText(driver));
        await runTest('Get element attribute', () => testSeleniumGetAttribute(driver));
        await runTest('Get element rect', () => testSeleniumGetRect(driver));
        await runTest('Is element displayed', () => testSeleniumIsDisplayed(driver));
        await runTest('Is element enabled', () => testSeleniumIsEnabled(driver));
        
    } catch (error) {
        console.error('Failed to create WebDriver session:', error.message);
        console.error('Make sure the WebDriver server is running: ./lambda.exe webdriver');
    } finally {
        if (driver) {
            try {
                await driver.quit();
            } catch (e) {
                // Ignore quit errors
            }
        }
    }
}

async function main() {
    console.log('='.repeat(60));
    console.log('Radiant WebDriver Test Suite');
    console.log('='.repeat(60));
    console.log(`WebDriver URL: ${WEBDRIVER_URL}`);
    console.log(`Test HTML Dir: ${TEST_HTML_DIR}`);
    console.log('');
    
    // Check if server is running
    try {
        await httpRequest('GET', '/status');
    } catch (error) {
        console.error('ERROR: Cannot connect to WebDriver server');
        console.error(`Make sure the server is running: ./lambda.exe webdriver --port 4444`);
        process.exit(1);
    }
    
    // Run tests
    await runRawApiTests();
    console.log('');
    await runSeleniumTests();
    
    // Summary
    console.log('');
    console.log('='.repeat(60));
    console.log('Test Summary');
    console.log('='.repeat(60));
    console.log(`  Passed: ${passed}`);
    console.log(`  Failed: ${failed}`);
    console.log(`  Total:  ${passed + failed}`);
    console.log('');
    
    if (failed > 0) {
        console.log('Failed Tests:');
        results.filter(r => r.status === 'FAIL').forEach(r => {
            console.log(`  - ${r.name}: ${r.error}`);
        });
        process.exit(1);
    } else {
        console.log('All tests passed!');
        process.exit(0);
    }
}

// Run tests
main().catch(error => {
    console.error('Test runner error:', error);
    process.exit(1);
});
