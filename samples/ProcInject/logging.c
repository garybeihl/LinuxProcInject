#include "logging.h"
#include <Library/BaseLib.h>

//
// External serial output function from drvmain.c
//
extern VOID EFIAPI SerialOutString(IN UINT8* Str);

//
// Global logging configuration
//
LOG_CONFIG gLogConfig = {
    .CurrentLevel = LOG_LEVEL_INFO,  // Default to INFO level
    .Enabled = TRUE,
    .TimestampEnabled = TRUE,
    .MessageCounter = 0
};

//
// Buffer for formatting log messages
//
STATIC CHAR8 gLogBuffer[512];

//
// Log level prefix strings
//
STATIC CONST CHAR8* gLogLevelPrefixes[] = {
    "[ERROR]  ",
    "[WARN ]  ",
    "[INFO ]  ",
    "[DEBUG]  ",
    "[TRACE]  "
};

//
// Error code description table
//
typedef struct _ERROR_CODE_ENTRY {
    INJECT_ERROR_CODE Code;
    CONST CHAR8* Description;
} ERROR_CODE_ENTRY;

STATIC CONST ERROR_CODE_ENTRY gErrorCodeTable[] = {
    // Stack scanning errors
    {INJECT_ERROR_STACK_SCAN_FAILED, "Stack scan failed"},
    {INJECT_ERROR_EEVM_NOT_FOUND, "efi_enter_virtual_mode return address not found"},
    {INJECT_ERROR_EEVM_PATTERN_MISMATCH, "EEVM pattern mismatch"},
    {INJECT_ERROR_EEVM_STRING_MISMATCH, "EEVM error string mismatch"},

    // Address calculation errors
    {INJECT_ERROR_PRINTK_CALC_FAILED, "printk address calculation failed"},
    {INJECT_ERROR_KMALLOC_CALC_FAILED, "__kmalloc address calculation failed"},
    {INJECT_ERROR_MSLEEP_CALC_FAILED, "msleep address calculation failed"},
    {INJECT_ERROR_KTHREAD_CALC_FAILED, "kthread_create_on_node address calculation failed"},

    // Patch 1 errors
    {INJECT_ERROR_PATCH1_INSTALL_FAILED, "Patch 1 installation failed"},
    {INJECT_ERROR_PATCH1_INVALID_ADDR, "Patch 1 invalid address"},
    {INJECT_ERROR_PATCH1_FIXUP_FAILED, "Patch 1 address fixup failed"},

    // start_kernel search errors
    {INJECT_ERROR_START_KERNEL_NOT_FOUND, "start_kernel return address not found"},
    {INJECT_ERROR_CALL_PATTERN_MISMATCH, "Call instruction pattern mismatch"},
    {INJECT_ERROR_MFENCE_NOT_FOUND, "mfence instruction not found"},
    {INJECT_ERROR_ARCH_CALL_REST_INIT_INVALID, "arch_call_rest_init address invalid"},

    // rest_init errors
    {INJECT_ERROR_REST_INIT_NOT_FOUND, "rest_init not found"},
    {INJECT_ERROR_REST_INIT_PROLOGUE_INVALID, "rest_init prologue invalid"},
    {INJECT_ERROR_COMPLETE_NOT_FOUND, "complete() call not found"},
    {INJECT_ERROR_COMPLETE_INVALID_INSN, "complete() location has invalid instruction"},

    // Patch 2 errors
    {INJECT_ERROR_PATCH2_INSTALL_FAILED, "Patch 2 installation failed"},
    {INJECT_ERROR_PATCH2_INVALID_ADDR, "Patch 2 invalid address"},
    {INJECT_ERROR_PATCH2_FIXUP_FAILED, "Patch 2 address fixup failed"},

    // Configuration errors
    {INJECT_ERROR_CONFIG_INVALID, "Configuration invalid"},
    {INJECT_ERROR_CONFIG_VERSION_MISMATCH, "Kernel version mismatch"},
    {INJECT_ERROR_CONFIG_OFFSET_INVALID, "Configuration offset invalid"},

    // General errors
    {INJECT_ERROR_INVALID_PARAMETER, "Invalid parameter"},
    {INJECT_ERROR_OUT_OF_RESOURCES, "Out of resources"},
    {INJECT_ERROR_STACK_INDEX_OUT_OF_RANGE, "Stack index out of valid range"},
    {INJECT_ERROR_ADDRESS_OUT_OF_RANGE, "Address out of kernel range"},
    {INJECT_ERROR_POINTER_OVERFLOW, "Pointer arithmetic overflow"},
    {INJECT_ERROR_MEMORY_NOT_WRITABLE, "Memory region not writable"},
    {INJECT_ERROR_UNKNOWN, "Unknown error"}
};

/**
 * Initialize the logging system
 */
VOID
EFIAPI
LogInitialize(
    IN LOG_LEVEL Level
)
{
    gLogConfig.CurrentLevel = Level;
    gLogConfig.Enabled = TRUE;
    gLogConfig.TimestampEnabled = TRUE;
    gLogConfig.MessageCounter = 0;

    // Log initialization message
    LogMessage(LOG_LEVEL_INFO, "Logging initialized at level %d", Level);
}

/**
 * Set the current log level
 */
VOID
EFIAPI
LogSetLevel(
    IN LOG_LEVEL Level
)
{
    if (Level <= LOG_LEVEL_VERBOSE) {
        gLogConfig.CurrentLevel = Level;
        LogMessage(LOG_LEVEL_INFO, "Log level changed to %d", Level);
    }
}

/**
 * Log a message at the specified level
 */
VOID
EFIAPI
LogMessage(
    IN LOG_LEVEL Level,
    IN CONST CHAR8* Format,
    ...
)
{
    VA_LIST Marker;
    UINTN PrefixLen;
    UINTN MessageLen;

    //
    // Check if logging is enabled and level is appropriate
    //
    if (!gLogConfig.Enabled || Level > gLogConfig.CurrentLevel) {
        return;
    }

    //
    // Build the log message
    //
    gLogConfig.MessageCounter++;

    //
    // Add prefix with optional timestamp
    //
    if (gLogConfig.TimestampEnabled) {
        PrefixLen = AsciiSPrint(gLogBuffer, sizeof(gLogBuffer),
                                "[%04d] %a ",
                                gLogConfig.MessageCounter,
                                gLogLevelPrefixes[Level]);
    } else {
        PrefixLen = AsciiSPrint(gLogBuffer, sizeof(gLogBuffer),
                                "%a ",
                                gLogLevelPrefixes[Level]);
    }

    //
    // Add the formatted message
    // Note: AsciiVSPrint may truncate if buffer is insufficient
    //
    VA_START(Marker, Format);
    MessageLen = AsciiVSPrint(gLogBuffer + PrefixLen,
                              sizeof(gLogBuffer) - PrefixLen,
                              Format,
                              Marker);
    VA_END(Marker);

    //
    // Ensure newline at end and proper null termination
    // Handle truncation cases
    //
    if (MessageLen >= sizeof(gLogBuffer) - PrefixLen) {
        // Message was truncated - ensure null termination and add newline if space
        if (sizeof(gLogBuffer) >= 2) {
            gLogBuffer[sizeof(gLogBuffer) - 2] = '\n';
            gLogBuffer[sizeof(gLogBuffer) - 1] = '\0';
        } else {
            gLogBuffer[sizeof(gLogBuffer) - 1] = '\0';
        }
    } else if (PrefixLen + MessageLen < sizeof(gLogBuffer) - 2) {
        // Normal case - room for newline and null terminator
        gLogBuffer[PrefixLen + MessageLen] = '\n';
        gLogBuffer[PrefixLen + MessageLen + 1] = '\0';
    } else if (PrefixLen + MessageLen < sizeof(gLogBuffer) - 1) {
        // Edge case - only room for newline OR null (choose null for safety)
        gLogBuffer[PrefixLen + MessageLen] = '\0';
    } else {
        // Safety - ensure null termination
        gLogBuffer[sizeof(gLogBuffer) - 1] = '\0';
    }

    //
    // Output to serial
    //
    SerialOutString((UINT8*)gLogBuffer);
}

/**
 * Log an error with error code
 */
VOID
EFIAPI
LogError(
    IN INJECT_ERROR_CODE ErrorCode,
    IN CONST CHAR8* Format,
    ...
)
{
    VA_LIST Marker;
    UINTN PrefixLen;
    UINTN MessageLen;
    CONST CHAR8* ErrorDesc;

    //
    // Check if logging is enabled
    //
    if (!gLogConfig.Enabled) {
        return;
    }

    gLogConfig.MessageCounter++;

    //
    // Get error description
    //
    ErrorDesc = GetErrorCodeDescription(ErrorCode);

    //
    // Build the error message with error code
    //
    if (gLogConfig.TimestampEnabled) {
        PrefixLen = AsciiSPrint(gLogBuffer, sizeof(gLogBuffer),
                                "[%04d] [ERROR]   [0x%04x] %a: ",
                                gLogConfig.MessageCounter,
                                ErrorCode,
                                ErrorDesc);
    } else {
        PrefixLen = AsciiSPrint(gLogBuffer, sizeof(gLogBuffer),
                                "[ERROR]   [0x%04x] %a: ",
                                ErrorCode,
                                ErrorDesc);
    }

    //
    // Add the formatted message
    // Note: AsciiVSPrint may truncate if buffer is insufficient
    //
    VA_START(Marker, Format);
    MessageLen = AsciiVSPrint(gLogBuffer + PrefixLen,
                              sizeof(gLogBuffer) - PrefixLen,
                              Format,
                              Marker);
    VA_END(Marker);

    //
    // Ensure newline at end and proper null termination
    // Handle truncation cases
    //
    if (MessageLen >= sizeof(gLogBuffer) - PrefixLen) {
        // Message was truncated - ensure null termination and add newline if space
        if (sizeof(gLogBuffer) >= 2) {
            gLogBuffer[sizeof(gLogBuffer) - 2] = '\n';
            gLogBuffer[sizeof(gLogBuffer) - 1] = '\0';
        } else {
            gLogBuffer[sizeof(gLogBuffer) - 1] = '\0';
        }
    } else if (PrefixLen + MessageLen < sizeof(gLogBuffer) - 2) {
        // Normal case - room for newline and null terminator
        gLogBuffer[PrefixLen + MessageLen] = '\n';
        gLogBuffer[PrefixLen + MessageLen + 1] = '\0';
    } else if (PrefixLen + MessageLen < sizeof(gLogBuffer) - 1) {
        // Edge case - only room for newline OR null (choose null for safety)
        gLogBuffer[PrefixLen + MessageLen] = '\0';
    } else {
        // Safety - ensure null termination
        gLogBuffer[sizeof(gLogBuffer) - 1] = '\0';
    }

    //
    // Output to serial
    //
    SerialOutString((UINT8*)gLogBuffer);
}

/**
 * Log function entry
 */
VOID
EFIAPI
LogFunctionEntry(
    IN CONST CHAR8* FunctionName
)
{
    if (!gLogConfig.Enabled || gLogConfig.CurrentLevel < LOG_LEVEL_VERBOSE) {
        return;
    }

    gLogConfig.MessageCounter++;

    AsciiSPrint(gLogBuffer, sizeof(gLogBuffer),
                "[%04d] [TRACE]  --> %a()\n",
                gLogConfig.MessageCounter,
                FunctionName);

    SerialOutString((UINT8*)gLogBuffer);
}

/**
 * Log function exit with status
 */
VOID
EFIAPI
LogFunctionExit(
    IN CONST CHAR8* FunctionName,
    IN EFI_STATUS Status
)
{
    if (!gLogConfig.Enabled || gLogConfig.CurrentLevel < LOG_LEVEL_VERBOSE) {
        return;
    }

    gLogConfig.MessageCounter++;

    if (EFI_ERROR(Status)) {
        AsciiSPrint(gLogBuffer, sizeof(gLogBuffer),
                    "[%04d] [TRACE]  <-- %a() = %r [FAILED]\n",
                    gLogConfig.MessageCounter,
                    FunctionName,
                    Status);
    } else {
        AsciiSPrint(gLogBuffer, sizeof(gLogBuffer),
                    "[%04d] [TRACE]  <-- %a() = %r\n",
                    gLogConfig.MessageCounter,
                    FunctionName,
                    Status);
    }

    SerialOutString((UINT8*)gLogBuffer);
}

/**
 * Get error code description string
 */
CONST CHAR8*
EFIAPI
GetErrorCodeDescription(
    IN INJECT_ERROR_CODE ErrorCode
)
{
    UINTN i;
    UINTN TableSize;

    TableSize = sizeof(gErrorCodeTable) / sizeof(gErrorCodeTable[0]);

    for (i = 0; i < TableSize; i++) {
        if (gErrorCodeTable[i].Code == ErrorCode) {
            return gErrorCodeTable[i].Description;
        }
    }

    return "Unknown error code";
}
