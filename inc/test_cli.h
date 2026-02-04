/*******************************************************************************
* djinterp [test]                                                    test_cli.h
*
* 
* 
*
* link:      TBA
* file:      \inc\test\test_cli.h 
* author(s): Samuel 'teer' Neal-Blim                           date: 2025.09.24
*******************************************************************************/

#ifndef DJINTERP_TEST_CLI_H_
#define	DJINTERP_TEST_CLI_H_ 1

#include <stddef.h>
#include "..\djinterp.h"
#include ".\test_common.h"


// forward declaration
struct d_test_counter;

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================

#define D_TEST_MAX_MODULES        64
#define D_TEST_MAX_MODULE_NAME    32
#define D_TEST_MAX_DEPENDENCIES   8
#define D_TEST_MAX_INCLUDES       16
#define D_TEST_MAX_EXCLUDES       16

// ============================================================================
// COMMAND TYPES
// ============================================================================

typedef enum
{
    D_TEST_CMD_NONE,
    D_TEST_CMD_RUN_ALL,
    D_TEST_CMD_RUN_MODULE,
    D_TEST_CMD_HELP,
    D_TEST_CMD_LIST
} d_test_command_t;

// ============================================================================
// MODULE REGISTRY
// ============================================================================

// Test module registration info
typedef struct
{
    char name[D_TEST_MAX_MODULE_NAME];
    char dependencies[D_TEST_MAX_DEPENDENCIES][D_TEST_MAX_MODULE_NAME];
    int  dependency_count;
    bool available;
    const struct d_test_module_metadata* metadata;
} d_test_module_info_t;

// Global module registry
struct d_test_registry_t
{
    d_test_module_info_t modules[D_TEST_MAX_MODULES];
    int count;
    bool enabled[D_TEST_MAX_MODULES];  // Bitfield for which modules to run
};

// ============================================================================
// PARSED ARGUMENTS STRUCTURE
// ============================================================================

typedef struct
{
    d_test_command_t command;
    char target_module[D_TEST_MAX_MODULE_NAME];
    char includes[D_TEST_MAX_INCLUDES][D_TEST_MAX_MODULE_NAME];
    int  include_count;
    char excludes[D_TEST_MAX_EXCLUDES][D_TEST_MAX_MODULE_NAME];
    int  exclude_count;
    bool verbose;
    bool dry_run;  // Show what would run without running it
} d_test_args_t;

// ============================================================================
// MODULE REGISTRY FUNCTIONS
// ============================================================================

// Initialize the test registry
static struct d_test_registry_t g_test_registry = {0};

// Register a test module
bool 
d_test_register_module
(
    const char*                          name, 
    const struct d_test_module_metadata* metadata,
    const char*                          dependencies[], 
    int                                  dep_count
)
{
    if (g_test_registry.count >= D_TEST_MAX_MODULES)
    {
        return D_FAILURE;
    }

    int idx = g_test_registry.count++;
    d_test_module_info_t* module = &g_test_registry.modules[idx];
    
    // Copy name
    strncpy(module->name, name, D_TEST_MAX_MODULE_NAME - 1);
    module->name[D_TEST_MAX_MODULE_NAME - 1] = '\0';
    
    // Copy dependencies
    module->dependency_count = (dep_count > D_TEST_MAX_DEPENDENCIES) ? 
                              D_TEST_MAX_DEPENDENCIES : dep_count;
    
    for (int i = 0; i < module->dependency_count; i++)
    {
        strncpy(module->dependencies[i], dependencies[i], D_TEST_MAX_MODULE_NAME - 1);
        module->dependencies[i][D_TEST_MAX_MODULE_NAME - 1] = '\0';
    }
    
    module->available = D_TRUE;
    module->metadata = metadata;
    
    return D_SUCCESS;
}

// Find module index by name
int d_test_find_module_index(const char* name)
{
    for (int i = 0; i < g_test_registry.count; i++)
    {
        if (strcmp(g_test_registry.modules[i].name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

// Enable a module and all its dependencies recursively
void d_test_enable_module_with_deps(int module_idx)
{
    if (module_idx < 0 || module_idx >= g_test_registry.count)
        return;
        
    if (g_test_registry.enabled[module_idx])
        return;  // Already enabled
    
    g_test_registry.enabled[module_idx] = D_TRUE;
    
    // Enable all dependencies
    d_test_module_info_t* module = &g_test_registry.modules[module_idx];
    for (int i = 0; i < module->dependency_count; i++)
    {
        int dep_idx = d_test_find_module_index(module->dependencies[i]);
        if (dep_idx != -1)
        {
            d_test_enable_module_with_deps(dep_idx);
        }
    }
}

// Disable a module (but don't touch dependencies)
void d_test_disable_module(int module_idx)
{
    if (module_idx >= 0 && module_idx < g_test_registry.count)
    {
        g_test_registry.enabled[module_idx] = D_FALSE;
    }
}

// Enable all available modules
void d_test_enable_all_modules(void)
{
    for (int i = 0; i < g_test_registry.count; i++)
    {
        if (g_test_registry.modules[i].available)
        {
            d_test_enable_module_with_deps(i);
        }
    }
}

// Reset all modules to disabled
void d_test_reset_modules(void)
{
    for (int i = 0; i < g_test_registry.count; i++)
    {
        g_test_registry.enabled[i] = D_FALSE;
    }
}

// ============================================================================
// ARGUMENT PARSING FUNCTIONS
// ============================================================================

// Print usage information
void d_test_print_usage(const char* program_name)
{
    printf("Usage: %s <command> [options]\n\n", program_name);
    printf("Commands:\n");
    printf("  run test all          Run all available tests\n");
    printf("  run test <module>     Run tests for specific module (with dependencies)\n");
    printf("  list                  List all available modules\n");
    printf("  help                  Show this help\n\n");
    printf("Options:\n");
    printf("  -include <module>     Force include module (with dependencies)\n");
    printf("  -exclude <module>     Exclude specific module\n");
    printf("  -verbose              Show detailed output\n");
    printf("  -dry-run              Show what would run without running\n\n");
    printf("Available modules:\n");
    
    for (int i = 0; i < g_test_registry.count; i++)
    {
        d_test_module_info_t* module = &g_test_registry.modules[i];
        printf("  %-16s", module->name);
        
        if (module->dependency_count > 0)
        {
            printf(" (depends on:");
            for (int j = 0; j < module->dependency_count; j++)
            {
                printf(" %s", module->dependencies[j]);
                if (j < module->dependency_count - 1) printf(",");
            }
            printf(")");
        }
        printf("\n");
    }
}

// Parse command line arguments
d_test_args_t
d_test_parse_args
(
    int    _argc, 
    char** _argv
)
{
    d_test_args_t args = {0};
    args.command = D_TEST_CMD_NONE;
    
    if (_argc < 2)
    {
        args.command = D_TEST_CMD_HELP;
        return args;
    }
    
    int i = 1;
    
    // Parse main command
    if (strcmp(_argv[i], "run") == 0 && i + 1 < _argc)
    {
        i++;
        if (strcmp(_argv[i], "test") == 0 && i + 1 < _argc)
        {
            i++;
            if (strcmp(_argv[i], "all") == 0)
            {
                args.command = D_TEST_CMD_RUN_ALL;
            }
            else
            {
                args.command = D_TEST_CMD_RUN_MODULE;
                strncpy(args.target_module, _argv[i], D_TEST_MAX_MODULE_NAME - 1);
                args.target_module[D_TEST_MAX_MODULE_NAME - 1] = '\0';
            }
        }
        i++;
    }
    else if (strcmp(_argv[i], "list") == 0)
    {
        args.command = D_TEST_CMD_LIST;
        i++;
    }
    else if (strcmp(_argv[i], "help") == 0)
    {
        args.command = D_TEST_CMD_HELP;
        return args;
    }
    else
    {
        args.command = D_TEST_CMD_HELP;
        return args;
    }
    
    // Parse options
    while (i < _argc)
    {
        if (strcmp(_argv[i], "-include") == 0 && i + 1 < _argc)
        {
            i++;
            if (args.include_count < D_TEST_MAX_INCLUDES)
            {
                strncpy(args.includes[args.include_count], _argv[i], D_TEST_MAX_MODULE_NAME - 1);
                args.includes[args.include_count][D_TEST_MAX_MODULE_NAME - 1] = '\0';
                args.include_count++;
            }
        }
        else if (strcmp(_argv[i], "-exclude") == 0 && i + 1 < _argc)
        {
            i++;
            if (args.exclude_count < D_TEST_MAX_EXCLUDES)
            {
                strncpy(args.excludes[args.exclude_count], _argv[i], D_TEST_MAX_MODULE_NAME - 1);
                args.excludes[args.exclude_count][D_TEST_MAX_MODULE_NAME - 1] = '\0';
                args.exclude_count++;
            }
        }
        else if (strcmp(_argv[i], "-verbose") == 0)
        {
            args.verbose = D_TRUE;
        }
        else if (strcmp(_argv[i], "-dry-run") == 0)
        {
            args.dry_run = D_TRUE;
        }

        i++;
    }
    
    return args;
}

// ============================================================================
// COMMAND EXECUTION
// ============================================================================

// Execute the parsed command
int d_test_execute_command(d_test_args_t args)
{
    // Reset module states
    d_test_reset_modules();
    
    switch (args.command)
    {
        case D_TEST_CMD_RUN_ALL:
            if (args.verbose) printf("Running all tests...\n");
            d_test_enable_all_modules();
            break;
            
        case D_TEST_CMD_RUN_MODULE:
        {
            int target_idx = d_test_find_module_index(args.target_module);
            if (target_idx == -1)
            {
                printf("Error: Module '%s' not found\n", args.target_module);
                return 1;
            }
            if (args.verbose) printf("Running tests for module: %s\n", args.target_module);
            d_test_enable_module_with_deps(target_idx);
            break;
        }
        
        case D_TEST_CMD_LIST:
            d_test_print_usage("test_runner");
            return 0;
            
        case D_TEST_CMD_HELP:
        case D_TEST_CMD_NONE:
        default:
            d_test_print_usage("test_runner");
            return args.command == D_TEST_CMD_HELP ? 0 : 1;
    }
    
    // Process includes (force enable modules)
    for (int i = 0; i < args.include_count; i++)
    {
        int idx = d_test_find_module_index(args.includes[i]);
        if (idx != -1)
        {
            if (args.verbose) printf("Force including: %s\n", args.includes[i]);
            d_test_enable_module_with_deps(idx);
        }
        else
        {
            printf("Warning: Include module '%s' not found\n", args.includes[i]);
        }
    }
    
    // Process excludes (disable modules)
    for (int i = 0; i < args.exclude_count; i++)
    {
        int idx = d_test_find_module_index(args.excludes[i]);
        if (idx != -1)
        {
            if (args.verbose) printf("Excluding: %s\n", args.excludes[i]);
            d_test_disable_module(idx);
        }
        else
        {
            printf("Warning: Exclude module '%s' not found\n", args.excludes[i]);
        }
    }
    
    // Show what will run (dry run or verbose)
    if (args.dry_run || args.verbose)
    {
        printf("\nModules to run:\n");
        for (int i = 0; i < g_test_registry.count; i++)
        {
            if (g_test_registry.enabled[i])
            {
                printf("  %s\n", g_test_registry.modules[i].name);
            }
        }
        printf("\n");
    }
    
    if (args.dry_run)
    {
        return 0;  // Don't actually run tests
    }
    
    // Run enabled tests
    int total_run = 0;
    int total_passed = 0;
    
    printf("Executing tests:\n");
    for (int i = 0; i < g_test_registry.count; i++)
    {
        if (g_test_registry.enabled[i])
        {
            d_test_module_info_t* module = &g_test_registry.modules[i];
            printf("--- Testing %s ---\n", module->name);
            
            if (module->metadata && module->metadata->fn_test_all)
            {
                struct d_test_counter test_info = {0};
                bool result = module->metadata->fn_test_all(&test_info);
                total_run++;
                
                if (result == D_SUCCESS)
                {
                    total_passed++;
                    printf("PASS: %s\n", module->name);
                }
                else
                {
                    printf("FAIL: %s\n", module->name);
                }
            }
            else
            {
                printf("SKIP: %s (no test function)\n", module->name);
            }
        }
    }
    
    printf("\n=== Results ===\n");
    printf("Tests run: %d\n", total_run);
    printf("Passed: %d\n", total_passed);
    printf("Failed: %d\n", total_run - total_passed);
    
    return (total_run - total_passed) > 0 ? 1 : 0;
}

// ============================================================================
// CONVENIENCE MACRO FOR REGISTRATION
// ============================================================================

#define D_TEST_REGISTER_MODULE(name, metadata, ...)     \
    do                                                              \
    {                                                               \ 
                                                                    \
        const char* deps[] = { __VA_ARGS__ };                       \
        int dep_count = sizeof(deps) / sizeof(deps[0]);             \
        if (sizeof(#__VA_ARGS__) <= 1) dep_count = 0;               \
        d_test_register_module(name, metadata, deps, dep_count);    \
    } while(0)


#endif	// DJINTERP_TEST_CLI_H_