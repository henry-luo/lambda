// Error Handling Test

// Basic try/catch
function safeDivide(a, b) {
    try {
        if (b === 0) {
            throw new Error("Division by zero");
        }
        return a / b;
    } catch (error) {
        return "Error: " + error.message;
    }
}

// Try/catch/finally
function processData(data) {
    var result = null;
    var processed = false;
    
    try {
        if (!data) {
            throw new Error("No data provided");
        }
        
        result = data * 2;
        processed = true;
        return result;
    } catch (error) {
        result = "Error: " + error.message;
        return result;
    } finally {
        // This always runs
        if (processed) {
            console.log("Data processed successfully");
        } else {
            console.log("Data processing failed");
        }
    }
}

// Nested try/catch
function nestedErrorHandling() {
    try {
        try {
            throw new Error("Inner error");
        } catch (innerError) {
            throw new Error("Outer error: " + innerError.message);
        }
    } catch (outerError) {
        return outerError.message;
    }
}

// Error propagation
function functionThatThrows() {
    throw new Error("Something went wrong");
}

function callerFunction() {
    try {
        functionThatThrows();
        return "Success";
    } catch (error) {
        return "Caught: " + error.message;
    }
}

// Custom error types
function CustomError(message) {
    this.name = "CustomError";
    this.message = message;
}

function throwCustomError() {
    throw new CustomError("This is a custom error");
}

function handleCustomError() {
    try {
        throwCustomError();
    } catch (error) {
        if (error.name === "CustomError") {
            return "Custom error handled: " + error.message;
        } else {
            return "Unknown error: " + error.message;
        }
    }
}

// Test all error handling scenarios
var test1 = safeDivide(10, 2);      // Should return 5
var test2 = safeDivide(10, 0);      // Should return error message
var test3 = processData(5);         // Should return 10
var test4 = processData(null);      // Should return error message
var test5 = nestedErrorHandling();  // Should return nested error message
var test6 = callerFunction();       // Should return caught error
var test7 = handleCustomError();    // Should return custom error message

// Return test results
{
    safeDivideSuccess: test1,
    safeDivideError: test2,
    processDataSuccess: test3,
    processDataError: test4,
    nestedError: test5,
    propagatedError: test6,
    customError: test7
};
