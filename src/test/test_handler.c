/******************************************************************************
* djinterp [test]                                                test_handler.c
*
*   Implementation of test handler functionality including DSL helpers.
*
* path:      \src\test\test_handler.c
* link:      TBA
* author(s): Samuel 'teer' Neal-Blim                           date: 2025.01.30
******************************************************************************/

#include "..\..\inc\test\test_handler.h"
#include "..\..\inc\test\test_module.h"
#include "..\..\inc\test\test_tree.h"
#include "..\..\inc\test\test_session.h"
#include <string.h>
#include <time.h>


/******************************************************************************
 * INTERNAL HELPERS
 *****************************************************************************/

static double d_test_handler_get_time_ms(void)
{
    return (double)clock() / CLOCKS_PER_SEC * 1000.0;
}


/******************************************************************************
 * CREATION AND DESTRUCTION
 *****************************************************************************/

struct d_test_handler* 
d_test_handler_new(struct d_test_config* _default_config)
{
    return d_test_handler_new_full(_default_config, 0, 0, D_TEST_HANDLER_FLAG_NONE);
}

struct d_test_handler* 
d_test_handler_new_with_events(struct d_test_config* _default_config, size_t _event_capacity)
{
    return d_test_handler_new_full(_default_config, _event_capacity, 0, D_TEST_HANDLER_FLAG_EMIT_EVENTS);
}

struct d_test_handler*
d_test_handler_new_full(struct d_test_config* _default_config, size_t _event_capacity,
                         size_t _stack_capacity, uint32_t _flags)
{
    struct d_test_handler* handler = malloc(sizeof(struct d_test_handler));
    if (!handler) return NULL;

    memset(handler, 0, sizeof(struct d_test_handler));

    if (_event_capacity > 0)
    {
        handler->event_handler = d_event_handler_new(_event_capacity, 16);
        if (!handler->event_handler) { free(handler); return NULL; }
    }

    handler->default_config = _default_config;

    if ((_stack_capacity > 0) || (_flags & D_TEST_HANDLER_FLAG_TRACK_STACK))
    {
        size_t cap = _stack_capacity > 0 ? _stack_capacity : 32;
        handler->result_stack = d_min_stack_new(cap, sizeof(struct d_test_result));
        handler->context_stack = d_min_stack_new(cap, sizeof(struct d_test_context));
        if (!handler->result_stack || !handler->context_stack)
        {
            if (handler->result_stack) d_min_stack_free(handler->result_stack);
            if (handler->context_stack) d_min_stack_free(handler->context_stack);
            if (handler->event_handler) d_event_handler_free(handler->event_handler);
            free(handler);
            return NULL;
        }
    }

    handler->flags = _flags;
    handler->abort_on_failure = (_flags & D_TEST_HANDLER_FLAG_ABORT_ON_FAIL) != 0;
    handler->output_stream = stdout;

    return handler;
}

void d_test_handler_free(struct d_test_handler* _handler)
{
    if (!_handler) return;
    if (_handler->event_handler) d_event_handler_free(_handler->event_handler);
    if (_handler->result_stack) d_min_stack_free(_handler->result_stack);
    if (_handler->context_stack) d_min_stack_free(_handler->context_stack);
    if (_handler->output_buffer) free(_handler->output_buffer);
    free(_handler);
}


/******************************************************************************
 * FLAG MANAGEMENT
 *****************************************************************************/

void d_test_handler_set_flag(struct d_test_handler* _handler, enum DTestHandlerFlag _flag)
{
    if (_handler)
    {
        _handler->flags |= _flag;
        if (_flag == D_TEST_HANDLER_FLAG_ABORT_ON_FAIL) _handler->abort_on_failure = true;
    }
}

void d_test_handler_clear_flag(struct d_test_handler* _handler, enum DTestHandlerFlag _flag)
{
    if (_handler)
    {
        _handler->flags &= ~_flag;
        if (_flag == D_TEST_HANDLER_FLAG_ABORT_ON_FAIL) _handler->abort_on_failure = false;
    }
}

bool d_test_handler_has_flag(const struct d_test_handler* _handler, enum DTestHandlerFlag _flag)
{
    return _handler ? ((_handler->flags & _flag) == _flag) : false;
}


/******************************************************************************
 * EVENT LISTENER REGISTRATION
 *****************************************************************************/

bool d_test_handler_register_listener(struct d_test_handler* _handler, d_event_id _event_id,
                                       fn_callback _callback, bool _enabled)
{
    struct d_event_listener* listener;
    if (!_handler || !_handler->event_handler || !_callback) return false;

    listener = d_event_listener_new(_event_id, _callback, _enabled);
    if (!listener) return false;

    if (!d_event_handler_bind(_handler->event_handler, listener))
    {
        d_event_listener_free(listener);
        return false;
    }
    return true;
}

bool d_test_handler_unregister_listener(struct d_test_handler* _handler, d_event_id _event_id)
{
    if (!_handler || !_handler->event_handler) return false;
    return d_event_handler_unbind(_handler->event_handler, _event_id);
}

bool d_test_handler_enable_listener(struct d_test_handler* _handler, d_event_id _id)
{
    if (!_handler || !_handler->event_handler) return false;
    return d_event_handler_enable_listener(_handler->event_handler, _id);
}

bool d_test_handler_disable_listener(struct d_test_handler* _handler, d_event_id _id)
{
    if (!_handler || !_handler->event_handler) return false;
    return d_event_handler_disable_listener(_handler->event_handler, _id);
}


/******************************************************************************
 * EVENT EMISSION
 *****************************************************************************/

void d_test_handler_emit_event(struct d_test_handler* _handler, enum DTestEvent _event_type,
                                struct d_test_context* _context)
{
    void* args[1];
    struct d_event* event;

    if (!_handler || !_handler->event_handler) return;
    if (!d_test_handler_has_flag(_handler, D_TEST_HANDLER_FLAG_EMIT_EVENTS)) return;

    args[0] = _context;
    event = d_event_new_args((d_event_id)_event_type, args, 1);
    if (event)
    {
        d_event_handler_fire_event(_handler->event_handler, event);
        d_event_free(event);
    }
}


/******************************************************************************
 * TEST EXECUTION
 *****************************************************************************/

bool d_test_handler_run_test(struct d_test_handler* _handler, struct d_test* _test,
                              struct d_test_config* _run_config)
{
    bool result;
    struct d_test_context context;

    if (!_handler || !_test || !_test->test) return false;

    d_test_context_init(&context, _handler);
    context.current_test = _test;
    context.depth = _handler->current_depth;
    context.start_time_ms = d_test_handler_get_time_ms();

    context.event_type = D_TEST_EVENT_SETUP;
    d_test_handler_emit_event(_handler, D_TEST_EVENT_SETUP, &context);

    context.event_type = D_TEST_EVENT_START;
    d_test_handler_emit_event(_handler, D_TEST_EVENT_START, &context);

    result = _test->test(_test);
    context.result = result;

    _handler->results.tests_run++;
    if (result)
    {
        _handler->results.tests_passed++;
        context.event_type = D_TEST_EVENT_SUCCESS;
        d_test_handler_emit_event(_handler, D_TEST_EVENT_SUCCESS, &context);
    }
    else
    {
        _handler->results.tests_failed++;
        context.event_type = D_TEST_EVENT_FAILURE;
        d_test_handler_emit_event(_handler, D_TEST_EVENT_FAILURE, &context);
    }

    context.end_time_ms = d_test_handler_get_time_ms();
    context.event_type = D_TEST_EVENT_END;
    d_test_handler_emit_event(_handler, D_TEST_EVENT_END, &context);

    context.event_type = D_TEST_EVENT_TEAR_DOWN;
    d_test_handler_emit_event(_handler, D_TEST_EVENT_TEAR_DOWN, &context);

    if (d_test_handler_has_flag(_handler, D_TEST_HANDLER_FLAG_TRACK_STACK))
        d_test_handler_push_context(_handler, &context);

    if (!result && _handler->abort_on_failure) return false;
    return result;
}

bool d_test_handler_run_block(struct d_test_handler* _handler, struct d_test_block* _block,
                               struct d_test_config* _run_config)
{
    size_t i;
    bool all_passed = true;
    struct d_test_context context;

    if (!_handler || !_block) return false;

    d_test_context_init(&context, _handler);
    context.current_block = _block;
    context.depth = _handler->current_depth;
    context.start_time_ms = d_test_handler_get_time_ms();

    _handler->current_depth++;
    if (_handler->current_depth > _handler->results.max_depth)
        _handler->results.max_depth = _handler->current_depth;
    _handler->results.blocks_run++;

    context.event_type = D_TEST_EVENT_SETUP;
    d_test_handler_emit_event(_handler, D_TEST_EVENT_SETUP, &context);

    for (i = 0; i < _block->count; i++)
    {
        struct d_test_config* config = _run_config ? _run_config :
            (_block->override_block ? _block->block_settings : _handler->default_config);

        if (!d_test_handler_run_test_type(_handler, &_block->tests[i], config))
        {
            all_passed = false;
            if (_handler->abort_on_failure) break;
        }
    }

    if (all_passed) _handler->results.blocks_passed++;
    else _handler->results.blocks_failed++;

    context.end_time_ms = d_test_handler_get_time_ms();
    context.result = all_passed;
    context.event_type = D_TEST_EVENT_TEAR_DOWN;
    d_test_handler_emit_event(_handler, D_TEST_EVENT_TEAR_DOWN, &context);

    _handler->current_depth--;
    return all_passed;
}

bool d_test_handler_run_test_type(struct d_test_handler* _handler, struct d_test_type* _test_type,
                                   struct d_test_config* _run_config)
{
    if (!_handler || !_test_type) return false;

    switch (_test_type->type)
    {
        case D_TEST_TYPE_TEST:
            return d_test_handler_run_test(_handler, _test_type->test, _run_config);
        case D_TEST_TYPE_TEST_BLOCK:
            return d_test_handler_run_block(_handler, _test_type->block, _run_config);
        case D_TEST_TYPE_MODULE:
            return d_test_handler_run_module(_handler, _test_type->module, _run_config);
        default:
            return false;
    }
}

bool d_test_handler_run_module(struct d_test_handler* _handler, struct d_test_module* _module,
                                struct d_test_config* _run_config)
{
    size_t i;
    bool all_passed = true;
    struct d_test_context context;

    if (!_handler || !_module) return false;
    if (!d_test_module_is_enabled(_module)) { _handler->results.modules_skipped++; return true; }

    d_test_context_init(&context, _handler);
    context.current_module = _module;
    context.depth = _handler->current_depth;
    context.start_time_ms = d_test_handler_get_time_ms();

    _handler->current_depth++;
    _handler->results.modules_run++;

    context.event_type = D_TEST_EVENT_SETUP;
    d_test_handler_emit_event(_handler, D_TEST_EVENT_SETUP, &context);

    struct d_test_config* config = _run_config ? _run_config : _module->module_config;

    for (i = 0; i < _module->block_count; i++)
    {
        if (_module->blocks[i] && !d_test_handler_run_block(_handler, _module->blocks[i], config))
        {
            all_passed = false;
            if (_handler->abort_on_failure) break;
        }
    }

    if (all_passed) _handler->results.modules_passed++;
    else _handler->results.modules_failed++;

    context.end_time_ms = d_test_handler_get_time_ms();
    context.result = all_passed;
    context.event_type = D_TEST_EVENT_TEAR_DOWN;
    d_test_handler_emit_event(_handler, D_TEST_EVENT_TEAR_DOWN, &context);

    _handler->current_depth--;
    return all_passed;
}

bool d_test_handler_run_tree(struct d_test_handler* _handler, struct d_test_tree* _tree,
                              struct d_test_config* _run_config)
{
    struct d_test_context context;
    bool result;

    if (!_handler || !_tree) return false;

    d_test_context_init(&context, _handler);
    context.current_tree = _tree;
    context.depth = _handler->current_depth;
    context.start_time_ms = d_test_handler_get_time_ms();

    context.event_type = D_TEST_EVENT_SETUP;
    d_test_handler_emit_event(_handler, D_TEST_EVENT_SETUP, &context);

    result = d_test_tree_run(_tree, _run_config);
    context.result = result;

    context.end_time_ms = d_test_handler_get_time_ms();
    context.event_type = D_TEST_EVENT_TEAR_DOWN;
    d_test_handler_emit_event(_handler, D_TEST_EVENT_TEAR_DOWN, &context);

    return result;
}

bool d_test_handler_run_session(struct d_test_handler* _handler, struct d_test_session* _session)
{
    struct d_test_context context;
    bool result;

    if (!_handler || !_session) return false;

    d_test_context_init(&context, _handler);
    context.current_session = _session;
    context.depth = _handler->current_depth;
    context.start_time_ms = d_test_handler_get_time_ms();

    context.event_type = D_TEST_EVENT_SETUP;
    d_test_handler_emit_event(_handler, D_TEST_EVENT_SETUP, &context);

    result = d_test_session_run(_session);
    context.result = result;

    context.end_time_ms = d_test_handler_get_time_ms();
    context.event_type = D_TEST_EVENT_TEAR_DOWN;
    d_test_handler_emit_event(_handler, D_TEST_EVENT_TEAR_DOWN, &context);

    return result;
}


/******************************************************************************
 * ASSERTION TRACKING
 *****************************************************************************/

void d_test_handler_record_assertion(struct d_test_handler* _handler, struct d_assertion* _assertion)
{
    if (!_handler || !_assertion) return;
    _handler->results.assertions_run++;
    if (_assertion->result) _handler->results.assertions_passed++;
    else _handler->results.assertions_failed++;
}


/******************************************************************************
 * STACK OPERATIONS
 *****************************************************************************/

bool d_test_handler_push_result(struct d_test_handler* _handler, const struct d_test_result* _result)
{
    if (!_handler || !_handler->result_stack || !_result) return false;
    return d_min_stack_push(_handler->result_stack, _result);
}

bool d_test_handler_pop_result(struct d_test_handler* _handler, struct d_test_result* _out)
{
    if (!_handler || !_handler->result_stack) return false;
    return d_min_stack_pop(_handler->result_stack, _out);
}

bool d_test_handler_push_context(struct d_test_handler* _handler, const struct d_test_context* _context)
{
    if (!_handler || !_handler->context_stack || !_context) return false;
    return d_min_stack_push(_handler->context_stack, _context);
}

bool d_test_handler_pop_context(struct d_test_handler* _handler, struct d_test_context* _out)
{
    if (!_handler || !_handler->context_stack) return false;
    return d_min_stack_pop(_handler->context_stack, _out);
}


/******************************************************************************
 * RESULT QUERIES
 *****************************************************************************/

struct d_test_result d_test_handler_get_results(const struct d_test_handler* _handler)
{
    struct d_test_result empty = {0};
    return _handler ? _handler->results : empty;
}

void d_test_handler_reset_results(struct d_test_handler* _handler)
{
    if (!_handler) return;
    memset(&_handler->results, 0, sizeof(struct d_test_result));
    _handler->current_depth = 0;
}

void d_test_handler_print_results(const struct d_test_handler* _handler, const char* _label)
{
    const struct d_test_result* r;
    if (!_handler) return;
    r = &_handler->results;

    printf("\n========================================\n");
    printf("Test Results: %s\n", _label ? _label : "Unnamed");
    printf("========================================\n");
    printf("Tests:      %zu run, %zu passed, %zu failed, %zu skipped\n",
           r->tests_run, r->tests_passed, r->tests_failed, r->tests_skipped);
    printf("Assertions: %zu run, %zu passed, %zu failed\n",
           r->assertions_run, r->assertions_passed, r->assertions_failed);
    printf("Blocks:     %zu run, %zu passed, %zu failed\n",
           r->blocks_run, r->blocks_passed, r->blocks_failed);
    printf("Modules:    %zu run, %zu passed, %zu failed, %zu skipped\n",
           r->modules_run, r->modules_passed, r->modules_failed, r->modules_skipped);
    printf("Max depth:  %zu\n", r->max_depth);
    printf("Pass Rate:  %.2f%% (tests), %.2f%% (assertions)\n",
           d_test_handler_get_pass_rate(_handler),
           d_test_handler_get_assertion_rate(_handler));
    printf("========================================\n");
}

double d_test_handler_get_pass_rate(const struct d_test_handler* _handler)
{
    if (!_handler || _handler->results.tests_run == 0) return 0.0;
    return (double)_handler->results.tests_passed / (double)_handler->results.tests_run * 100.0;
}

double d_test_handler_get_assertion_rate(const struct d_test_handler* _handler)
{
    if (!_handler || _handler->results.assertions_run == 0) return 0.0;
    return (double)_handler->results.assertions_passed / (double)_handler->results.assertions_run * 100.0;
}


/******************************************************************************
 * CONTEXT HELPERS
 *****************************************************************************/

struct d_test_context* d_test_context_new(struct d_test_handler* _handler)
{
    struct d_test_context* context = malloc(sizeof(struct d_test_context));
    if (context) d_test_context_init(context, _handler);
    return context;
}

void d_test_context_init(struct d_test_context* _context, struct d_test_handler* _handler)
{
    if (!_context) return;
    memset(_context, 0, sizeof(struct d_test_context));
    _context->handler = _handler;
    _context->result = true;
}

void d_test_context_free(struct d_test_context* _context)
{
    if (_context) free(_context);
}


/******************************************************************************
 * DSL HELPER FUNCTIONS
 *****************************************************************************/

struct d_test_module* d_test_handler_create_module_with_metadata(
    const char* _name, const struct d_test_kv* _metadata, size_t _count)
{
    struct d_test_module* module = d_test_module_new(_name, NULL);
    if (!module) return NULL;

    for (size_t i = 0; i < _count; i++)
        d_test_module_set_metadata_str(module, _metadata[i].key, _metadata[i].value);

    return module;
}

bool d_test_module_set_metadata_str(struct d_test_module* _module, const char* _key, const char* _value)
{
    enum DTestConfigKey flag;
    if (!_module || !_key) return false;

    if (strcmp(_key, "name") == 0) flag = D_TEST_METADATA_NAME;
    else if (strcmp(_key, "author") == 0 || strcmp(_key, "authors") == 0) flag = D_TEST_METADATA_AUTHORS;
    else if (strcmp(_key, "version") == 0) flag = D_TEST_METADATA_VERSION_STRING;
    else if (strcmp(_key, "description") == 0) flag = D_TEST_METADATA_DESCRIPTION;
    else if (strcmp(_key, "category") == 0) flag = D_TEST_METADATA_CATEGORY;
    else if (strcmp(_key, "module") == 0) flag = D_TEST_METADATA_MODULE_NAME;
    else if (strcmp(_key, "tags") == 0) flag = D_TEST_METADATA_TAGS;
    else return false;

    return d_test_module_set_metadata(_module, flag, (void*)_value);
}

struct d_test* d_test_handler_create_test_from_nodes(
    const struct d_test_dsl_node* _nodes, size_t _count)
{
    if (!_nodes || _count == 0) return NULL;
    return d_test_new(NULL, NULL);
}

struct d_test_block* d_test_handler_create_block_from_nodes(
    const char* _name, const struct d_test_dsl_node* _nodes, size_t _count)
{
    struct d_test_block* block;
    if (!_nodes || _count == 0) return NULL;

    block = d_test_block_new(_name, NULL);
    if (!block) return NULL;

    for (size_t i = 0; i < _count; i++)
    {
        if (_nodes[i].type == D_TEST_DSL_NODE_TEST && _nodes[i].test)
            d_test_block_add_test(block, _nodes[i].test);
        else if (_nodes[i].type == D_TEST_DSL_NODE_BLOCK && _nodes[i].block)
            d_test_block_add_block(block, _nodes[i].block);
    }
    return block;
}

struct d_test_module* d_test_handler_create_module_from_decl(
    const struct d_test_kv* _metadata, size_t _metadata_count,
    const struct d_test_dsl_node* _children, size_t _children_count)
{
    const char* name = "Unnamed Module";
    struct d_test_module* module;

    for (size_t i = 0; i < _metadata_count; i++)
        if (_metadata[i].key && strcmp(_metadata[i].key, "name") == 0)
            { name = _metadata[i].value; break; }

    module = d_test_handler_create_module_with_metadata(name, _metadata, _metadata_count);
    if (!module) return NULL;

    for (size_t i = 0; i < _children_count; i++)
    {
        if (_children[i].type == D_TEST_DSL_NODE_TEST && _children[i].test)
            d_test_module_add_test(module, _children[i].test);
        else if (_children[i].type == D_TEST_DSL_NODE_BLOCK && _children[i].block)
            d_test_module_add_block(module, _children[i].block);
    }
    return module;
}
