#ifndef VALIDATION_EXEC_H
#define VALIDATION_EXEC_H

#ifdef __cplusplus
extern "C" {
#endif

// Function to execute validation that can be called directly by tests
// This replaces the need to spawn the CLI process for validation testing
int exec_validation(int argc, char* argv[]);

#ifdef __cplusplus
}
#endif

#endif // VALIDATION_EXEC_H
