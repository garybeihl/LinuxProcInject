//
// Logging Infrastructure
//
// Provides structured logging with multiple severity levels for
// debugging and production diagnostics.
//
#ifndef _LOGGING_H_
#define _LOGGING_H_

#include <Uefi.h>
#include <Library/PrintLib.h>

//
// Log levels
//
typedef enum _LOG_LEVEL {
    LOG_LEVEL_ERROR = 0,    // Critical errors that prevent operation
    LOG_LEVEL_WARNING = 1,  // Warnings about potential issues
    LOG_LEVEL_INFO = 2,     // Informational messages about progress
    LOG_LEVEL_DEBUG = 3,    // Detailed debugging information
    LOG_LEVEL_VERBOSE = 4   // Very detailed tracing information
} LOG_LEVEL;

//
// Error codes for structured error reporting
//
typedef enum _INJECT_ERROR_CODE {
    INJECT_ERROR_NONE = 0,

    // Stack scanning errors (0x1000 - 0x1FFF)
    INJECT_ERROR_STACK_SCAN_FAILED = 0x1000,
    INJECT_ERROR_EEVM_NOT_FOUND = 0x1001,
    INJECT_ERROR_EEVM_PATTERN_MISMATCH = 0x1002,
    INJECT_ERROR_EEVM_STRING_MISMATCH = 0x1003,

    // Address calculation errors (0x2000 - 0x2FFF)
    INJECT_ERROR_PRINTK_CALC_FAILED = 0x2000,
    INJECT_ERROR_KMALLOC_CALC_FAILED = 0x2001,
    INJECT_ERROR_MSLEEP_CALC_FAILED = 0x2002,
    INJECT_ERROR_KTHREAD_CALC_FAILED = 0x2003,

    // Patch 1 errors (0x3000 - 0x3FFF)
    INJECT_ERROR_PATCH1_INSTALL_FAILED = 0x3000,
    INJECT_ERROR_PATCH1_INVALID_ADDR = 0x3001,
    INJECT_ERROR_PATCH1_FIXUP_FAILED = 0x3002,

    // start_kernel search errors (0x4000 - 0x4FFF)
    INJECT_ERROR_START_KERNEL_NOT_FOUND = 0x4000,
    INJECT_ERROR_CALL_PATTERN_MISMATCH = 0x4001,
    INJECT_ERROR_MFENCE_NOT_FOUND = 0x4002,
    INJECT_ERROR_ARCH_CALL_REST_INIT_INVALID = 0x4003,

    // rest_init errors (0x5000 - 0x5FFF)
    INJECT_ERROR_REST_INIT_NOT_FOUND = 0x5000,
    INJECT_ERROR_REST_INIT_PROLOGUE_INVALID = 0x5001,
    INJECT_ERROR_COMPLETE_NOT_FOUND = 0x5002,
    INJECT_ERROR_COMPLETE_INVALID_INSN = 0x5003,

    // Patch 2 errors (0x6000 - 0x6FFF)
    INJECT_ERROR_PATCH2_INSTALL_FAILED = 0x6000,
    INJECT_ERROR_PATCH2_INVALID_ADDR = 0x6001,
    INJECT_ERROR_PATCH2_FIXUP_FAILED = 0x6002,

    // Configuration errors (0x7000 - 0x7FFF)
    INJECT_ERROR_CONFIG_INVALID = 0x7000,
    INJECT_ERROR_CONFIG_VERSION_MISMATCH = 0x7001,
    INJECT_ERROR_CONFIG_OFFSET_INVALID = 0x7002,

    // General errors (0xF000 - 0xFFFF)
    INJECT_ERROR_INVALID_PARAMETER = 0xF000,
    INJECT_ERROR_OUT_OF_RESOURCES = 0xF001,
    INJECT_ERROR_UNKNOWN = 0xFFFF
} INJECT_ERROR_CODE;

//
// Logging configuration
//
typedef struct _LOG_CONFIG {
    LOG_LEVEL CurrentLevel;     // Current log level threshold
    BOOLEAN Enabled;            // Master enable/disable
    BOOLEAN TimestampEnabled;   // Include simple counter in logs
    UINT32 MessageCounter;      // Incremental message counter
} LOG_CONFIG;

//
// Global logging configuration
//
extern LOG_CONFIG gLogConfig;

//
// Logging functions
//

/**
 * Initialize the logging system
 *
 * @param Level     Initial log level
 */
VOID
EFIAPI
LogInitialize(
    IN LOG_LEVEL Level
);

/**
 * Set the current log level
 *
 * @param Level     New log level
 */
VOID
EFIAPI
LogSetLevel(
    IN LOG_LEVEL Level
);

/**
 * Log a message at the specified level
 *
 * @param Level     Log level for this message
 * @param Format    Printf-style format string
 * @param ...       Variable arguments
 */
VOID
EFIAPI
LogMessage(
    IN LOG_LEVEL Level,
    IN CONST CHAR8* Format,
    ...
);

/**
 * Log an error with error code
 *
 * @param ErrorCode     Structured error code
 * @param Format        Printf-style format string
 * @param ...           Variable arguments
 */
VOID
EFIAPI
LogError(
    IN INJECT_ERROR_CODE ErrorCode,
    IN CONST CHAR8* Format,
    ...
);

/**
 * Log function entry (for detailed tracing)
 *
 * @param FunctionName  Name of the function being entered
 */
VOID
EFIAPI
LogFunctionEntry(
    IN CONST CHAR8* FunctionName
);

/**
 * Log function exit with status (for detailed tracing)
 *
 * @param FunctionName  Name of the function being exited
 * @param Status        EFI_STATUS return value
 */
VOID
EFIAPI
LogFunctionExit(
    IN CONST CHAR8* FunctionName,
    IN EFI_STATUS Status
);

/**
 * Get error code description string
 *
 * @param ErrorCode     Error code to describe
 * @return Description string (never NULL)
 */
CONST CHAR8*
EFIAPI
GetErrorCodeDescription(
    IN INJECT_ERROR_CODE ErrorCode
);

//
// Convenience macros for common log levels
//

#define LOG_ERROR(ErrorCode, ...) \
    LogError((ErrorCode), __VA_ARGS__)

#define LOG_WARNING(...) \
    LogMessage(LOG_LEVEL_WARNING, __VA_ARGS__)

#define LOG_INFO(...) \
    LogMessage(LOG_LEVEL_INFO, __VA_ARGS__)

#define LOG_DEBUG(...) \
    LogMessage(LOG_LEVEL_DEBUG, __VA_ARGS__)

#define LOG_VERBOSE(...) \
    LogMessage(LOG_LEVEL_VERBOSE, __VA_ARGS__)

//
// Function tracing macros (only active in VERBOSE mode)
//

#define LOG_FUNCTION_ENTRY() \
    LogFunctionEntry(__FUNCTION__)

#define LOG_FUNCTION_EXIT(Status) \
    LogFunctionExit(__FUNCTION__, (Status))

//
// Helper macro for address logging
//

#define LOG_ADDRESS(Level, Name, Addr) \
    LogMessage((Level), "%a = 0x%llx", (Name), (UINT64)(Addr))

#endif // _LOGGING_H_
