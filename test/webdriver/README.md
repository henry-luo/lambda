# WebDriver Tests

This directory contains tests for the Radiant WebDriver implementation.

## Setup

```bash
cd test/webdriver
npm install
```

## Running Tests

### 1. Start the WebDriver Server

```bash
# From project root
./lambda.exe webdriver --port 4444
```

### 2. Run Tests

**Full test suite (requires selenium-webdriver):**
```bash
npm test
# or
node test_webdriver.js
```

**Raw API test (no dependencies):**
```bash
npm run test:raw
# or
node test_raw_api.js
```

## Test Files

| File | Description |
|------|-------------|
| `test_webdriver.js` | Full test suite using Selenium WebDriver client |
| `test_raw_api.js` | Direct HTTP API tests (no dependencies) |
| `package.json` | npm dependencies |

## Environment Variables

- `WEBDRIVER_URL` - WebDriver server URL (default: `http://localhost:4444`)

## Test Coverage

### Raw API Tests (`test_raw_api.js`)
- GET /status - Server readiness
- POST /session - Create session
- DELETE /session/:id - Delete session
- POST /session/:id/url - Navigate
- GET /session/:id/url - Get current URL
- GET /session/:id/title - Get page title
- POST /session/:id/element - Find element
- POST /session/:id/elements - Find elements
- POST /session/:id/element/:id/click - Click element
- GET /session/:id/element/:id/text - Get element text
- GET /session/:id/element/:id/rect - Get element rectangle
- GET /session/:id/screenshot - Take screenshot
- Invalid session handling (404 response)

### Selenium Tests (`test_webdriver.js`)
- Navigation to file URLs
- Page title retrieval
- Element finding (CSS, tag name)
- Element click
- Send keys
- Get element text/attributes
- Get element rect
- Is displayed/enabled checks
- Screenshots

## Expected Output

```
============================================================
Radiant WebDriver Raw API Test
============================================================
Server: http://localhost:4444

=== Test: GET /status ===
>>> GET /status
<<< 200
{
  "value": {
    "ready": true,
    "message": "Radiant WebDriver Server"
  }
}
✓ Status check passed

=== Test: POST /session ===
>>> POST /session
...

============================================================
All tests passed!
============================================================
```
