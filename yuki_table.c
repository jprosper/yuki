#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <pthread.h>
#include <mysql.h>
#include <assert.h>

#include "yuki.h"

typedef struct _ytable_connection_t {
    MYSQL mysql;
    ybool_t connected;
} ytable_connection_t;

typedef struct _ytable_mysql_res_t {
    MYSQL_RES res;
} ytable_mysql_res_t;

static ytable_config_t *g_ytable_configs = NULL;
static ytable_db_config_t *g_ytable_db_configs = NULL;
static yuint32_t g_ytable_db_configs_count = 0;

static ybool_t g_ytable_inited = yfalse;
static pthread_key_t g_ytable_connection_thread_key;

static yvar_t g_ytable_result_true = YVAR_BOOL(ytrue);
static yvar_t g_ytable_result_false = YVAR_BOOL(yfalse);

static ybool_t _ytable_sql_select_validator(const ytable_t * ytable);
static ybool_t _ytable_sql_update_validator(const ytable_t * ytable);
static ybool_t _ytable_sql_insert_validator(const ytable_t * ytable);
static ybool_t _ytable_sql_delete_validator(const ytable_t * ytable);

static ybool_t _ytable_sql_select_builder(ytable_t * ytable);
static ybool_t _ytable_sql_update_builder(ytable_t * ytable);
static ybool_t _ytable_sql_insert_builder(ytable_t * ytable);
static ybool_t _ytable_sql_delete_builder(ytable_t * ytable);

static ybool_t _ytable_sql_select_result_parser(const ytable_t * ytable, ytable_mysql_res_t * mysql_res, yvar_t ** result);
static ybool_t _ytable_sql_update_result_parser(const ytable_t * ytable, ytable_mysql_res_t * mysql_res, yvar_t ** result);
static ybool_t _ytable_sql_insert_result_parser(const ytable_t * ytable, ytable_mysql_res_t * mysql_res, yvar_t ** result);
static ybool_t _ytable_sql_delete_result_parser(const ytable_t * ytable, ytable_mysql_res_t * mysql_res, yvar_t ** result);

static const ytable_sql_builder_t g_ytable_sql_builder[] = {
    {"<NULL>", NULL, NULL}, // YTABLE_VERB_NULL
    {"SELECT", // YTABLE_VERB_SELECT
     &_ytable_sql_select_validator, &_ytable_sql_select_builder, &_ytable_sql_select_result_parser},
    {"UPDATE", // YTABLE_VERB_UPDATE
     &_ytable_sql_update_validator, &_ytable_sql_update_builder, &_ytable_sql_update_result_parser},
    {"INSERT", // YTABLE_VERB_INSERT
     &_ytable_sql_insert_validator, &_ytable_sql_insert_builder, &_ytable_sql_insert_result_parser},
    {"DELETE", // YTABLE_VERB_DELETE
     &_ytable_sql_delete_validator, &_ytable_sql_delete_builder, &_ytable_sql_delete_result_parser},
};

static inline ybool_t _ytable_inited()
{
    return g_ytable_inited;
}

static inline ybool_t _ytable_sql_is_valid_verb(ytable_verb_t verb)
{
    return verb > YTABLE_VERB_NULL && verb < YTABLE_VERB_MAX;
}

static inline const char * _ytable_sql_get_verb(ytable_verb_t verb)
{
    YUKI_ASSERT(_ytable_sql_is_valid_verb(verb) || YTABLE_VERB_NULL == verb);

    return g_ytable_sql_builder[verb].verb;
}

static ybool_t _ytable_sql_do_validate(const ytable_t * ytable)
{
    YUKI_ASSERT(ytable);
    YUKI_ASSERT(_ytable_sql_is_valid_verb(ytable->verb));

    return g_ytable_sql_builder[ytable->verb].validator(ytable);
}

static ybool_t _ytable_sql_do_build(ytable_t * ytable)
{
    YUKI_ASSERT(ytable);
    YUKI_ASSERT(_ytable_sql_is_valid_verb(ytable->verb));

    return g_ytable_sql_builder[ytable->verb].builder(ytable);
}

static ybool_t _ytable_sql_do_result_parse(const ytable_t * ytable, ytable_mysql_res_t * mysql_res, yvar_t ** result)
{
    YUKI_ASSERT(ytable);
    YUKI_ASSERT(_ytable_sql_is_valid_verb(ytable->verb));

    return g_ytable_sql_builder[ytable->verb].parser(ytable, mysql_res, result);
}

static inline ybool_t _ytable_check_verb(const ytable_t * ytable)
{
    YUKI_ASSERT(ytable);

    if (YTABLE_VERB_NULL != ytable->verb) {
        YUKI_LOG_DEBUG("verb can only be set once. [verb: %s]", _ytable_sql_get_verb(ytable->verb));
        return yfalse;
    }

    return ytrue;
}

static ybool_t _ytable_sql_select_validator(const ytable_t * ytable)
{
    YUKI_ASSERT(ytable);
    YUKI_ASSERT(ytable->verb == YTABLE_VERB_SELECT);

    if (!ytable->fields || !yvar_is_array(*ytable->fields) || !yvar_count(*ytable->fields)) {
        YUKI_LOG_DEBUG("fields is invalid");
        return yfalse;
    }

    // FIXME: condition can be YVAR_BOOL(ytrue)
    if (!ytable->conditions || !yvar_is_map(*ytable->conditions) || !yvar_count(*ytable->conditions)) {
        YUKI_LOG_DEBUG("conditions is invalid");
        return yfalse;
    }

    return ytrue;
}

static ybool_t _ytable_sql_update_validator(const ytable_t * ytable)
{
    YUKI_ASSERT(ytable);
    YUKI_ASSERT(ytable->verb == YTABLE_VERB_UPDATE);

    if (!ytable->fields || !yvar_is_map(*ytable->fields) || !yvar_count(*ytable->fields)) {
        YUKI_LOG_DEBUG("fields is invalid");
        return yfalse;
    }

    // FIXME: condition can be YVAR_BOOL(ytrue)
    if (!ytable->conditions || !yvar_is_map(*ytable->conditions) || !yvar_count(*ytable->conditions)) {
        YUKI_LOG_DEBUG("conditions is invalid");
        return yfalse;
    }

    return ytrue;
}

static ybool_t _ytable_sql_insert_validator(const ytable_t * ytable)
{
    YUKI_ASSERT(ytable);
    YUKI_ASSERT(ytable->verb == YTABLE_VERB_INSERT);

    if (!ytable->fields || !yvar_is_map(*ytable->fields) || !yvar_count(*ytable->fields)) {
        YUKI_LOG_DEBUG("fields is invalid");
        return yfalse;
    }

    return ytrue;
}

static ybool_t _ytable_sql_delete_validator(const ytable_t * ytable)
{
    YUKI_ASSERT(ytable);
    YUKI_ASSERT(ytable->verb == YTABLE_VERB_DELETE);

    // FIXME: condition can be YVAR_BOOL(ytrue)
    if (!ytable->conditions || !yvar_is_map(*ytable->conditions) || !yvar_count(*ytable->conditions)) {
        YUKI_LOG_DEBUG("conditions is invalid");
        return yfalse;
    }

    return ytrue;
}

static ybool_t _ytable_sql_estimate_where(const ytable_t * ytable, ysize_t * result)
{
    YUKI_ASSERT(ytable && result);
    YUKI_ASSERT(ytable->conditions);

    ysize_t size = 0;

    if (yvar_is_map(*ytable->conditions)) {
        size += _YTABLE_SQL_STRLEN(_YTABLE_SQL_KEYWORD_WHERE);

        FOREACH_YVAR_MAP(*ytable->conditions, key, value) {
            if (!yvar_like_string(*key)) {
                YUKI_LOG_DEBUG("key in condition must be string");
                return yfalse;
            }

            size += yvar_cstr_strlen(*key) + 2 * _YTABLE_SQL_STRLEN(_YTABLE_SQL_QUOT_FIELD);

            // FIXME: currently, only support op ==
            size += _YTABLE_SQL_STRLEN(_YTABLE_SQL_OP_EQ);

            if (yvar_like_int(*value)) {
                size += _YTABLE_SQL_INT_MAXLEN + 2 * _YTABLE_SQL_STRLEN(_YTABLE_SQL_QUOT_VALUE);
            } else if (yvar_like_string(*value)) {
                size += yvar_cstr_strlen(*value) + 2 * _YTABLE_SQL_STRLEN(_YTABLE_SQL_QUOT_VALUE);
            } else {
                // TODO: support other types
                YUKI_LOG_DEBUG("condition value can only be int and string");
                return yfalse;
            }

            // FIXME: currently, only support AND
            size += _YTABLE_SQL_STRLEN(_YTABLE_SQL_KEYWORD_AND);
        }
    }

    *result += size;
    return ytrue;
}

static ybool_t _ytable_sql_estimate_table(const ytable_t * ytable, ysize_t * result)
{
    YUKI_ASSERT(ytable && result);

    ytable_config_t * config = &g_ytable_configs[ytable->ytable_index];

    // TODO: implement hash
    ysize_t size = config->name_len + 1; // 1 is for space at the end

    *result += size;
    return ytrue;
}

static ybool_t _ytable_sql_do_build_where(const ytable_t * ytable, char * buffer, ysize_t size, ysize_t * offset)
{
    YUKI_ASSERT(ytable && buffer && size && offset);
    YUKI_ASSERT(ytable->conditions);

    if (yvar_is_map(*ytable->conditions)) {
        char int_buffer[_YTABLE_SQL_INT_MAXLEN + 1];
        *offset += snprintf(buffer + *offset, size - *offset, "%s", _YTABLE_SQL_KEYWORD_WHERE);

        FOREACH_YVAR_MAP(*ytable->conditions, key, value) {
            // FIXME: currently, only support AND
            const char * value_buffer;

            // TODO: do real escaple on value
            if (yvar_like_string(*value)) {
                value_buffer = yvar_cstr_buffer(*value);
            } else {
                // TODO: finish it
                ybool_t ret = yvar_get_str(*value, int_buffer, sizeof(int_buffer));
                YUKI_ASSERT(ret);
                value_buffer = buffer;
            }

            *offset += snprintf(buffer + *offset, size - *offset,
                _YTABLE_SQL_QUOT_FIELD "%s" _YTABLE_SQL_QUOT_FIELD _YTABLE_SQL_OP_EQ
                _YTABLE_SQL_QUOT_VALUE "%s" _YTABLE_SQL_QUOT_VALUE _YTABLE_SQL_KEYWORD_AND,
                yvar_cstr_buffer(*key), value_buffer);
        }

        // remove tailing AND
        *offset -= _YTABLE_SQL_STRLEN(_YTABLE_SQL_KEYWORD_AND);
    }

    return ytrue;
}

static ybool_t _ytable_sql_do_build_table(const ytable_t * ytable, char * buffer, ysize_t size, ysize_t * offset)
{
    YUKI_ASSERT(ytable && buffer && size && offset);

    ytable_config_t * config = &g_ytable_configs[ytable->ytable_index];

    // TODO: implement hash
    *offset += snprintf(buffer + *offset, size - *offset,
        _YTABLE_SQL_QUOT_FIELD "%s" _YTABLE_SQL_QUOT_FIELD,
        config->table_name);

    return ytrue;
}

static ybool_t _ytable_sql_select_builder(ytable_t * ytable)
{
    YUKI_ASSERT(ytable);
    YUKI_ASSERT(ytable->verb == YTABLE_VERB_SELECT);
    YUKI_ASSERT(ytable->fields && ytable->conditions);

    if (yvar_like_string(ytable->sql)) {
        YUKI_LOG_DEBUG("sql was built. sql: %s", yvar_cstr_buffer(ytable->sql));
        return ytrue;
    }

    ysize_t size = 1; // space for tailing '\0'
    size += _YTABLE_SQL_STRLEN(_YTABLE_SQL_VERB_SELECT);

    // estimate buffer length for fields
    FOREACH_YVAR_ARRAY(*ytable->fields, field) {
        // FIXME: field can be an array with 2 elements
        if (!yvar_like_string(*field)) {
            YUKI_LOG_DEBUG("field can only be str/cstr or array with 2 element");
            return yfalse;
        }

        size += yvar_cstr_strlen(*field) + _YTABLE_SQL_STRLEN(_YTABLE_SQL_COMMA);
    }

    size += _YTABLE_SQL_STRLEN(_YTABLE_SQL_KEYWORD_FROM);

    if (!_ytable_sql_estimate_table(ytable, &size)) {
        YUKI_LOG_DEBUG("cannot estimate table name size");
        return yfalse;
    }

    if (!_ytable_sql_estimate_where(ytable, &size)) {
        YUKI_LOG_DEBUG("cannot estimate where condition size");
        return yfalse;
    }

    char * buffer = ybuffer_simple_alloc(size);

    if (!buffer) {
        YUKI_LOG_WARNING("out of memory");
        return yfalse;
    }

    ysize_t offset = 0;
    offset += snprintf(buffer + offset, size - offset, "%s", _YTABLE_SQL_VERB_SELECT);

    // FIXME: field can be an array. assume it's a string right now.
    FOREACH_YVAR_ARRAY(*ytable->fields, field) {
        YUKI_ASSERT(yvar_like_string(*field));

        offset += snprintf(buffer + offset, size - offset,
            "%s" _YTABLE_SQL_COMMA,
            yvar_cstr_buffer(*field));
    }

    // remove tailing comma
    offset -= _YTABLE_SQL_STRLEN(_YTABLE_SQL_COMMA);

    offset += snprintf(buffer + offset, size - offset, "%s", _YTABLE_SQL_KEYWORD_FROM);

    if (!_ytable_sql_do_build_table(ytable, buffer, size, &offset)) {
        YUKI_LOG_FATAL("cannot build table name");
        return yfalse;
    }

    if (!_ytable_sql_do_build_where(ytable, buffer, size, &offset)) {
        YUKI_LOG_FATAL("cannot build where condition");
        return yfalse;
    }

    buffer[offset] = '\0';

    YUKI_LOG_DEBUG("built sql: %s", buffer);
    yvar_cstr_with_size(ytable->sql, buffer, offset);
    return ytrue;
}

static ybool_t _ytable_sql_update_builder(ytable_t * ytable)
{
    YUKI_ASSERT(ytable);
    YUKI_ASSERT(ytable->verb == YTABLE_VERB_UPDATE);
    YUKI_ASSERT(ytable->fields && ytable->conditions);
    YUKI_ASSERT(yvar_is_map(*ytable->fields));

    if (yvar_like_string(ytable->sql)) {
        YUKI_LOG_DEBUG("sql was built. sql: %s", yvar_cstr_buffer(ytable->sql));
        return ytrue;
    }

    ysize_t size = 1; // space for tailing '\0'
    size += _YTABLE_SQL_STRLEN(_YTABLE_SQL_VERB_UPDATE);

    if (!_ytable_sql_estimate_table(ytable, &size)) {
        YUKI_LOG_DEBUG("cannot estimate table name size");
        return yfalse;
    }

    size += _YTABLE_SQL_STRLEN(_YTABLE_SQL_KEYWORD_SET);

    // estimate buffer length for fields
    FOREACH_YVAR_MAP(*ytable->fields, key1, value1) {
        if (!yvar_like_string(*key1)) {
            YUKI_LOG_DEBUG("field key can only be str/cstr");
            return yfalse;
        }

        // FIXME: field can be an array
        if (!yvar_like_string(*value1) && !yvar_like_int(*value1)) {
            YUKI_LOG_DEBUG("field value can only be str/cstr/int");
            return yfalse;
        }

        if (yvar_like_int(*value1)) {
            size += _YTABLE_SQL_INT_MAXLEN + 2 * _YTABLE_SQL_STRLEN(_YTABLE_SQL_QUOT_VALUE);
        } else if (yvar_like_string(*value1)) {
            size += yvar_cstr_strlen(*value1) + 2 * _YTABLE_SQL_STRLEN(_YTABLE_SQL_QUOT_VALUE);
        } else {
            // TODO: support other types
            YUKI_LOG_DEBUG("condition value can only be int and string");
            return yfalse;
        }

        size += yvar_cstr_strlen(*key1) + _YTABLE_SQL_STRLEN(_YTABLE_SQL_OP_EQ)
            + 2 * _YTABLE_SQL_STRLEN(_YTABLE_SQL_QUOT_FIELD);
    }

    if (!_ytable_sql_estimate_where(ytable, &size)) {
        YUKI_LOG_DEBUG("cannot estimate where condition size");
        return yfalse;
    }

    char * buffer = ybuffer_simple_alloc(size);

    if (!buffer) {
        YUKI_LOG_WARNING("out of memory");
        return yfalse;
    }

    ysize_t offset = 0;
    offset += snprintf(buffer + offset, size - offset, "%s", _YTABLE_SQL_VERB_UPDATE);

    if (!_ytable_sql_do_build_table(ytable, buffer, size, &offset)) {
        YUKI_LOG_FATAL("cannot build table name");
        return yfalse;
    }

    offset += snprintf(buffer + offset, size - offset, "%s", _YTABLE_SQL_KEYWORD_SET);

    char int_buffer[_YTABLE_SQL_INT_MAXLEN + 1];
    const char * value_buffer;

    FOREACH_YVAR_MAP(*ytable->fields, key2, value2) {
        // FIXME: support more types
        YUKI_ASSERT(yvar_like_string(*key2));
        YUKI_ASSERT(yvar_like_string(*value2) || yvar_like_int(*value2));

        if (yvar_like_string(*value2)) {
            value_buffer = yvar_cstr_buffer(*value2);
        } else {
            ybool_t ret = yvar_get_str(*value2, int_buffer, sizeof(int_buffer));
            YUKI_ASSERT(ret);
            value_buffer = int_buffer;
        }

        offset += snprintf(buffer + offset, size - offset,
            _YTABLE_SQL_QUOT_FIELD "%s" _YTABLE_SQL_QUOT_FIELD _YTABLE_SQL_OP_EQ
            _YTABLE_SQL_QUOT_VALUE "%s" _YTABLE_SQL_QUOT_VALUE _YTABLE_SQL_COMMA,
            yvar_cstr_buffer(*key2), value_buffer);
    }

    // remove tailing comma
    offset -= _YTABLE_SQL_STRLEN(_YTABLE_SQL_COMMA);

    if (!_ytable_sql_do_build_where(ytable, buffer, size, &offset)) {
        YUKI_LOG_FATAL("cannot build where condition");
        return yfalse;
    }

    buffer[offset] = '\0';

    YUKI_LOG_DEBUG("built sql: %s", buffer);
    yvar_cstr_with_size(ytable->sql, buffer, offset);
    return ytrue;
}

static ybool_t _ytable_sql_insert_builder(ytable_t * ytable)
{
    YUKI_ASSERT(ytable);
    YUKI_ASSERT(ytable->verb == YTABLE_VERB_INSERT);
    YUKI_ASSERT(ytable->fields);
    YUKI_ASSERT(yvar_is_map(*ytable->fields));

    if (yvar_like_string(ytable->sql)) {
        YUKI_LOG_DEBUG("sql was built. sql: %s", yvar_cstr_buffer(ytable->sql));
        return ytrue;
    }

    ysize_t size = 1; // space for tailing '\0'
    size += _YTABLE_SQL_STRLEN(_YTABLE_SQL_VERB_INSERT);

    if (!_ytable_sql_estimate_table(ytable, &size)) {
        YUKI_LOG_DEBUG("cannot estimate table name size");
        return yfalse;
    }

    size += 2 * _YTABLE_SQL_STRLEN(_YTABLE_SQL_BRACKET_LEFT) + 2 * _YTABLE_SQL_STRLEN(_YTABLE_SQL_BRACKET_RIGHT)
        + _YTABLE_SQL_STRLEN(_YTABLE_SQL_KEYWORD_VALUES) + 1 /*space for a blank*/;

    // estimate buffer length for fields
    FOREACH_YVAR_MAP(*ytable->fields, key1, value1) {
        if (!yvar_like_string(*key1)) {
            YUKI_LOG_DEBUG("field key can only be str/cstr");
            return yfalse;
        }

        if (!yvar_like_string(*value1) && !yvar_like_int(*value1)) {
            YUKI_LOG_DEBUG("field value can only be str/cstr/int");
            return yfalse;
        }

        if (yvar_like_int(*value1)) {
            size += _YTABLE_SQL_INT_MAXLEN + 2 * _YTABLE_SQL_STRLEN(_YTABLE_SQL_QUOT_VALUE);
        } else if (yvar_like_string(*value1)) {
            size += yvar_cstr_strlen(*value1) + 2 * _YTABLE_SQL_STRLEN(_YTABLE_SQL_QUOT_VALUE);
        } else {
            // TODO: support other types
            YUKI_LOG_DEBUG("condition value can only be int and string");
            return yfalse;
        }

        size += yvar_cstr_strlen(*key1)
            + 2 * _YTABLE_SQL_STRLEN(_YTABLE_SQL_COMMA)
            + 2 * _YTABLE_SQL_STRLEN(_YTABLE_SQL_QUOT_FIELD);
    }

    char * buffer = ybuffer_simple_alloc(size);

    if (!buffer) {
        YUKI_LOG_WARNING("out of memory");
        return yfalse;
    }

    ysize_t offset = 0;
    offset += snprintf(buffer + offset, size - offset, "%s", _YTABLE_SQL_VERB_INSERT);

    if (!_ytable_sql_do_build_table(ytable, buffer, size, &offset)) {
        YUKI_LOG_FATAL("cannot build table name");
        return yfalse;
    }

    offset += snprintf(buffer + offset, size - offset, " %s",
        _YTABLE_SQL_BRACKET_LEFT);

    FOREACH_YVAR_MAP(*ytable->fields, key2, value2) {
        YUKI_ASSERT(yvar_like_string(*key2));

        offset += snprintf(buffer + offset, size - offset,
            _YTABLE_SQL_QUOT_FIELD "%s" _YTABLE_SQL_QUOT_FIELD _YTABLE_SQL_COMMA,
            yvar_cstr_buffer(*key2));
    }

    // remove tailing comma
    offset -= _YTABLE_SQL_STRLEN(_YTABLE_SQL_COMMA);

    offset += snprintf(buffer + offset, size - offset, "%s",
        _YTABLE_SQL_BRACKET_RIGHT _YTABLE_SQL_KEYWORD_VALUES _YTABLE_SQL_BRACKET_LEFT);

    char int_buffer[_YTABLE_SQL_INT_MAXLEN + 1];
    const char * value_buffer;

    FOREACH_YVAR_MAP(*ytable->fields, key3, value3) {
        YUKI_ASSERT(yvar_like_string(*value3) || yvar_like_int(*value3));

        if (yvar_like_string(*value3)) {
            value_buffer = yvar_cstr_buffer(*value3);
        } else {
            ybool_t ret = yvar_get_str(*value3, int_buffer, sizeof(int_buffer));
            YUKI_ASSERT(ret);
            value_buffer = int_buffer;
        }

        offset += snprintf(buffer + offset, size - offset,
            _YTABLE_SQL_QUOT_VALUE "%s" _YTABLE_SQL_QUOT_VALUE _YTABLE_SQL_COMMA,
            value_buffer);
    }

    // remove tailing comma
    offset -= _YTABLE_SQL_STRLEN(_YTABLE_SQL_COMMA);

    offset += snprintf(buffer + offset, size - offset, "%s",
        _YTABLE_SQL_BRACKET_RIGHT);

    buffer[offset] = '\0';

    YUKI_LOG_DEBUG("built sql: %s", buffer);
    yvar_cstr_with_size(ytable->sql, buffer, offset);
    return ytrue;
}

static ybool_t _ytable_sql_delete_builder(ytable_t * ytable)
{
    YUKI_ASSERT(ytable);
    YUKI_ASSERT(ytable->verb == YTABLE_VERB_DELETE);
    YUKI_ASSERT(ytable->conditions);

    if (yvar_like_string(ytable->sql)) {
        YUKI_LOG_DEBUG("sql was built. sql: %s", yvar_cstr_buffer(ytable->sql));
        return ytrue;
    }

    ysize_t size = 1; // space for tailing '\0'
    size += _YTABLE_SQL_STRLEN(_YTABLE_SQL_VERB_DELETE);

    if (!_ytable_sql_estimate_table(ytable, &size)) {
        YUKI_LOG_DEBUG("cannot estimate table name size");
        return yfalse;
    }

    if (!_ytable_sql_estimate_where(ytable, &size)) {
        YUKI_LOG_DEBUG("cannot estimate where condition size");
        return yfalse;
    }

    char * buffer = ybuffer_simple_alloc(size);

    if (!buffer) {
        YUKI_LOG_WARNING("out of memory");
        return yfalse;
    }

    ysize_t offset = 0;
    offset += snprintf(buffer + offset, size - offset, "%s",
        _YTABLE_SQL_VERB_DELETE);

    if (!_ytable_sql_do_build_table(ytable, buffer, size, &offset)) {
        YUKI_LOG_FATAL("cannot build table name");
        return yfalse;
    }

    if (!_ytable_sql_do_build_where(ytable, buffer, size, &offset)) {
        YUKI_LOG_FATAL("cannot build where condition");
        return yfalse;
    }

    buffer[offset] = '\0';

    YUKI_LOG_DEBUG("built sql: %s", buffer);
    yvar_cstr_with_size(ytable->sql, buffer, offset);
    return ytrue;
}

static ybool_t _ytable_sql_select_result_parser(const ytable_t * ytable, ytable_mysql_res_t * mysql_res, yvar_t ** result)
{
    // TODO: finish it
    YUKI_ASSERT(ytable && mysql_res && result);
    YUKI_ASSERT(ytable->fields && yvar_is_array(*ytable->fields));
    MYSQL_RES * res = (MYSQL_RES*)mysql_res;

    if (!ytable->affected_rows) {
        YUKI_LOG_DEBUG("empty result set");
        *result = &g_ytable_result_false;
        return ytrue;
    }

    ysize_t field_cnt = yvar_count(*ytable->fields);
    ysize_t num_fields = mysql_num_fields(res);

    if (field_cnt != num_fields) {
        YUKI_LOG_FATAL("actual query field count doesn't match original query. what's wrong? [old: %lu] [actual: %lu]",
            field_cnt, num_fields);
        return yfalse;
    }

    yvar_t local_result[ytable->affected_rows * field_cnt];
    enum enum_field_types field_types[field_cnt];
    yuint64_t field_flags[field_cnt];
    MYSQL_ROW row;
    MYSQL_FIELD * field = NULL;
    uint64_t * lengths = NULL;
    ysize_t affected_rows = ytable->affected_rows;
    ysize_t cnt;
    ysize_t i;

    yint64_t temp_signed = 0;
    yuint64_t temp_unsigned = 0;
    ybool_t is_unsigned = yfalse;

    for (cnt = 0; (field = mysql_fetch_field(res)) != NULL; cnt++) {
        YUKI_ASSERT(cnt < field_cnt);

        field_types[cnt] = field->type;
        field_flags[cnt] = field->flags;
    }

    for (cnt = 0; (row = mysql_fetch_row(res)) != NULL; cnt++) {
        YUKI_ASSERT(field_cnt == mysql_num_fields(res));
        YUKI_ASSERT(cnt < affected_rows);
        lengths = mysql_fetch_lengths(res);

        for (i = 0; i < field_cnt; i++) {
            if (NULL == row[i]) {
                YUKI_LOG_DEBUG("got a NULL value");
                yvar_undefined(local_result[cnt * field_cnt + i]);
                continue;
            }

            is_unsigned = field_flags[i] & UNSIGNED_FLAG;

            if (IS_NUM(field_types[i])) {
                errno = 0;

                if (is_unsigned) {
                    temp_unsigned = strtoull(row[i], NULL, 10);
                } else {
                    temp_signed = strtoll(row[i], NULL, 10);
                }

                if (errno) {
                    YUKI_LOG_WARNING("cannot convert numeric field to int. [errno: %d]", errno);
                    return yfalse;
                }
            }

            switch (field_types[i]) {
                case MYSQL_TYPE_TINY:
                    if (is_unsigned) {
                        yvar_uint8(local_result[cnt * field_cnt + i], temp_unsigned);
                    } else {
                        yvar_int8(local_result[cnt * field_cnt + i], temp_signed);
                    }

                    break;
                case MYSQL_TYPE_SHORT:
                    if (is_unsigned) {
                        yvar_uint16(local_result[cnt * field_cnt + i], temp_unsigned);
                    } else {
                        yvar_int16(local_result[cnt * field_cnt + i], temp_signed);
                    }

                    break;
                case MYSQL_TYPE_LONG:
                case MYSQL_TYPE_INT24:
                    if (is_unsigned) {
                        yvar_uint32(local_result[cnt * field_cnt + i], temp_unsigned);
                    } else {
                        yvar_int32(local_result[cnt * field_cnt + i],temp_signed);
                    }

                    break;
                case MYSQL_TYPE_LONGLONG:
                    if (is_unsigned) {
                        yvar_uint64(local_result[cnt * field_cnt + i], temp_unsigned);
                    } else {
                        yvar_int64(local_result[cnt * field_cnt + i], temp_signed);
                    }

                    break;
                case MYSQL_TYPE_STRING:
                case MYSQL_TYPE_VAR_STRING:
                case MYSQL_TYPE_BLOB:
                    yvar_cstr_with_size(local_result[cnt * field_cnt + i], row[i], lengths[i]);
                    break;
                case MYSQL_TYPE_NULL:
                    YUKI_LOG_DEBUG("NULL type value");
                    yvar_undefined(local_result[cnt * field_cnt + i]);
                    
                    break;
                default:
                    YUKI_LOG_WARNING("unsupported type. [type: %lu]", field_types[i]);
                    yvar_undefined(local_result[cnt * field_cnt + i]);
            }
        }
    }

    yvar_t value_array[affected_rows];
    yvar_t map_array[affected_rows];

    for (i = 0; i < affected_rows; i++) {
        yvar_array_with_size(value_array[i], local_result + i * field_cnt, field_cnt);
        yvar_map(map_array[i], *ytable->fields, value_array[i]);
    }

    yvar_t result_var = YVAR_ARRAY_WITH_SIZE(map_array, affected_rows);
    return yvar_clone(*result, result_var);
}

static ybool_t _ytable_sql_update_result_parser(const ytable_t * ytable, ytable_mysql_res_t * mysql_res, yvar_t ** result)
{
    YUKI_ASSERT(ytable && result);
    (void)mysql_res;

    if (ytable->affected_rows) {
        *result = &g_ytable_result_true;
    } else {
        *result = &g_ytable_result_false;
    }

    return ytrue;
}

static ybool_t _ytable_sql_insert_result_parser(const ytable_t * ytable, ytable_mysql_res_t * mysql_res, yvar_t ** result)
{
    YUKI_ASSERT(ytable && result);
    (void)mysql_res;

    *result = &g_ytable_result_true;
    return ytrue;
}

static ybool_t _ytable_sql_delete_result_parser(const ytable_t * ytable, ytable_mysql_res_t * mysql_res, yvar_t ** result)
{
    YUKI_ASSERT(ytable && result);
    (void)mysql_res;

    if (ytable->affected_rows) {
        *result = &g_ytable_result_true;
    } else {
        *result = &g_ytable_result_false;
    }

    return ytrue;
}

static ybool_t _ytable_build_sql(ytable_t * ytable)
{
    YUKI_ASSERT(ytable);

    if (!_ytable_sql_is_valid_verb(ytable->verb)) {
        YUKI_LOG_DEBUG("verb is not set");
        return yfalse;
    }

    if (!_ytable_sql_do_validate(ytable)) {
        YUKI_LOG_DEBUG("invalid/incomplete ytable instance");
        return yfalse;
    }

    const char * verb = _ytable_sql_get_verb(ytable->verb);
    YUKI_LOG_TRACE("start to build sql for verb %s", verb);

    if (!_ytable_sql_do_build(ytable)) {
        YUKI_LOG_DEBUG("fail to build sql");
        return yfalse;
    }

    YUKI_LOG_DEBUG("finish building sql");
    return ytrue;
}

static ytable_connection_thread_data_t * _ytable_thread_get_connection()
{
    ytable_connection_thread_data_t * thread_data = pthread_getspecific(g_ytable_connection_thread_key);

    if (!thread_data) {
        ybuffer_t * buffer = ybuffer_create_global(sizeof(ytable_connection_thread_data_t));

        if (!buffer) {
            YUKI_LOG_WARNING("out of memory");
            return NULL;
        }

        thread_data = ybuffer_smart_alloc(buffer, ytable_connection_thread_data_t);

        if (!thread_data) {
            YUKI_LOG_FATAL("cannot alloc memory for thread data");
            return NULL;
        }

        thread_data->size = g_ytable_db_configs_count;

        ysize_t conn_size = sizeof(ytable_connection_t) * g_ytable_db_configs_count;
        buffer = ybuffer_create_global(conn_size);

        if (!buffer) {
            YUKI_LOG_WARNING("out of memory");
            return NULL;
        }

        ytable_connection_t * connections = (ytable_connection_t*)ybuffer_alloc(buffer, conn_size);

        if (!connections) {
            YUKI_LOG_FATAL("cannot alloc memory for connections");
            return NULL;
        }

        memset(connections, 0, conn_size);

        ysize_t cnt = 0;
        for (; cnt < thread_data->size; cnt++) {
            if (!mysql_init(&connections->mysql)) {
                YUKI_LOG_FATAL("fail to init mysql struct");
                return NULL;
            }

            connections->connected = yfalse;
        }

        thread_data->connections = connections;

        pthread_setspecific(g_ytable_connection_thread_key, thread_data);
    }

    return thread_data;
}

static ytable_connection_t * _ytable_fetch_db_connection(ytable_t * ytable)
{
    YUKI_ASSERT(ytable);

    ytable_connection_thread_data_t * thread_data = _ytable_thread_get_connection();

    if (!thread_data) {
        YUKI_LOG_FATAL("cannot get connection thread data");
        return NULL;
    }

    // FIXME: validate ytable index
    ytable_config_t * config = g_ytable_configs + ytable->ytable_index;
    yuint32_t db_index;

    // FIXME: support array
    if (!yvar_get_uint32(*config->db_index, db_index)) {
        YUKI_LOG_WARNING("invalid db_index");
        return NULL;
    }

    YUKI_ASSERT(db_index < thread_data->size);

    ytable_connection_t * conn = thread_data->connections + db_index;

    if (conn->connected) {
        if (mysql_ping(&conn->mysql)) {
            YUKI_LOG_TRACE("mysql connection is gone.");
            mysql_close(&conn->mysql);
            
            if (!mysql_init(&conn->mysql)) {
                YUKI_LOG_FATAL("fail to init mysql struct");
                return NULL;
            }

            conn->connected = yfalse;
        }
    }

    if (!conn->connected) {
        ytable_db_config_t * db_config = g_ytable_db_configs + db_index;

        if (db_config->character_set) {
            mysql_options(&conn->mysql, MYSQL_SET_CHARSET_NAME, db_config->character_set);
            YUKI_LOG_TRACE("character set is set to '%s'", db_config->character_set);
        }

        if (!mysql_real_connect(&conn->mysql, db_config->host, db_config->user, db_config->password, db_config->database, db_config->port, NULL, 0)) {
            YUKI_LOG_FATAL("cannot connect to mysql. [err: %s] [host: %s] [user: %s] [database: %s] [port: %lu]", mysql_error(&conn->mysql),
                db_config->host, db_config->user, db_config->database, db_config->port);
            return NULL;
        }
    }

    YUKI_LOG_TRACE("mysql connection is ready. [thread_id: %lu]", mysql_thread_id(&conn->mysql));
    return conn;
}

static ybool_t _ytable_execute(ytable_t * ytable, ytable_connection_t * conn)
{
    YUKI_ASSERT(conn && ytable);

    if (mysql_real_query(&conn->mysql, yvar_cstr_buffer(ytable->sql), yvar_cstr_strlen(ytable->sql))) {
        YUKI_LOG_WARNING("fail to execute query. [error: %s]", mysql_error(&conn->mysql));
        return yfalse;
    }

    return ytrue;
}

static void _ytable_connection_thread_clean_up(void * thread_data)
{
    // TODO: implement it
    YUKI_LOG_FATAL("not implemented");
}

ybool_t _ytable_init()
{
    if (_ytable_inited()) {
        YUKI_LOG_DEBUG("ytable is inited");
        return ytrue;
    }

    yvar_set_option(g_ytable_result_true, YVAR_OPTION_READONLY);
    yvar_set_option(g_ytable_result_false, YVAR_OPTION_READONLY);

    // TODO: remove test code
    g_ytable_configs = ybuffer_simple_alloc(sizeof(ytable_config_t) * 2);
    memset(g_ytable_configs, 0, sizeof(ytable_config_t) * 2);
    g_ytable_configs[0].table_name = "mytest";
    g_ytable_configs[0].name_len = _YTABLE_SQL_STRLEN("mytest");
    g_ytable_configs[0].hash_key = "uid";
    yvar_t db_index = YVAR_UINT32(0);
    yvar_pin(g_ytable_configs[0].db_index, db_index);
    g_ytable_configs[0].hash_method = YTABLE_HASH_METHOD_DEFAULT;

    g_ytable_db_configs = ybuffer_simple_alloc(sizeof(ytable_db_config_t));
    g_ytable_db_configs[0].db_name = "162";
    g_ytable_db_configs[0].host = "127.0.0.1";
    g_ytable_db_configs[0].user = "test";
    g_ytable_db_configs[0].password = "test";
    g_ytable_db_configs[0].database = "test";
    g_ytable_db_configs[0].character_set = "utf8";
    g_ytable_db_configs[0].port = 3306;
    g_ytable_db_configs_count = 1;

    // init thread key
    if (pthread_key_create(&g_ytable_connection_thread_key, &_ytable_connection_thread_clean_up)) {
        YUKI_LOG_FATAL("cannot create thread key for ytable. [err: %d]", errno);
        return yfalse;
    }

    g_ytable_inited = ytrue;
    return ytrue;
}

void _ytable_clean_up()
{
    // do nothing
}

void _ytable_shutdown()
{
    // FIXME: once ytable is shut down, it cannot be turned on with different config.
    // FIXME: thread data cannot be updated automatically after init ytable again.
    g_ytable_configs = NULL;
    g_ytable_inited = yfalse;
}

ytable_t * ytable_instance(const char * table_name)
{
    if (!_ytable_inited()) {
        YUKI_LOG_FATAL("use ytable before init it");
        return NULL;
    }

    ysize_t index = 0;
    while (g_ytable_configs[index].table_name) {
        if (!strcmp(table_name, g_ytable_configs[index].table_name)) {
            YUKI_LOG_DEBUG("table is found. [name: %s] [index: %lu]", table_name, index);

            ybuffer_t * buffer = ybuffer_create_global(sizeof(ytable_t));

            if (!buffer) {
                YUKI_LOG_WARNING("out of memory");
                return NULL;
            }

            ytable_t * instance = ybuffer_smart_alloc(buffer, ytable_t);

            if (!instance) {
                YUKI_LOG_WARNING("out of memory");
                return NULL;
            }

            memset(instance, 0, sizeof(ytable_t));
            instance->ytable_index = index;
            instance->limit = YTABLE_DEFAULT_LIMIT;
            instance->offset = YTABLE_DEFAULT_OFFSET;

            return instance;
        }
    }

    return NULL;
}

ybool_t _ytable_select(ytable_t * ytable, const yvar_t * fields)
{
    if (!ytable || !fields) {
        YUKI_LOG_FATAL("invalid param");
        return yfalse;
    }

    if (!yvar_is_array(*fields)) {
        YUKI_LOG_DEBUG("fields must be array");
        return yfalse;
    }

    if (!_ytable_check_verb(ytable)) {
        YUKI_LOG_DEBUG("verb is set before");
        return yfalse;
    }

    // TODO: validate fields

    ytable->verb = YTABLE_VERB_SELECT;

    return yvar_clone(ytable->fields, *fields);
}

ybool_t _ytable_insert(ytable_t * ytable, const yvar_t * values)
{
    if (!ytable || !values) {
        YUKI_LOG_FATAL("invalid param");
        return yfalse;
    }

    if (!yvar_is_map(*values)) {
        YUKI_LOG_DEBUG("values must be map");
        return yfalse;
    }

    if (!_ytable_check_verb(ytable)) {
        YUKI_LOG_DEBUG("verb is set before");
        return yfalse;
    }

    // TODO: validate values

    ytable->verb = YTABLE_VERB_INSERT;

    return yvar_clone(ytable->fields, *values);
}

ybool_t _ytable_update(ytable_t * ytable, const yvar_t * values)
{
    if (!ytable || !values) {
        YUKI_LOG_FATAL("invalid param");
        return yfalse;
    }

    if (!yvar_is_map(*values)) {
        YUKI_LOG_DEBUG("values must be map");
        return yfalse;
    }

    if (!_ytable_check_verb(ytable)) {
        YUKI_LOG_DEBUG("verb is set before");
        return yfalse;
    }

    // TODO: validate values

    ytable->verb = YTABLE_VERB_UPDATE;

    return yvar_clone(ytable->fields, *values);
}

ybool_t _ytable_delete(ytable_t * ytable)
{
    if (!ytable) {
        YUKI_LOG_FATAL("invalid param");
        return yfalse;
    }

    if (!_ytable_check_verb(ytable)) {
        YUKI_LOG_DEBUG("verb is set before");
        return yfalse;
    }

    ytable->verb = YTABLE_VERB_DELETE;

    return ytrue;
}

ybool_t _ytable_where(ytable_t * ytable, const yvar_t * conditions)
{
    if (!ytable || !conditions) {
        YUKI_LOG_FATAL("invalid param");
        return yfalse;
    }

    if (!_ytable_sql_is_valid_verb(ytable->verb)) {
        YUKI_LOG_FATAL("verb must be set before using where");
        return yfalse;
    }

    if (YTABLE_VERB_INSERT == ytable->verb) {
        YUKI_LOG_DEBUG("%s verb does not support where condition",
            _ytable_sql_get_verb(ytable->verb));
        return yfalse;
    }

    // TODO: support multiple where conditions
    if (ytable->conditions) {
        YUKI_LOG_DEBUG("only one condition can be used currently");
        return yfalse;
    }

    // TODO: check conditions

    return yvar_clone(ytable->conditions, *conditions);
}

ybool_t _ytable_fetch_one(ytable_t * ytable, yvar_t ** result)
{
    if (!ytable || !result) {
        YUKI_LOG_FATAL("invalid param");
        return yfalse;
    }

    if (!_ytable_build_sql(ytable)) {
        YUKI_LOG_WARNING("unable to build sql");
        return yfalse;
    }

    ytable_connection_t * conn = _ytable_fetch_db_connection(ytable);

    if (!conn) {
        YUKI_LOG_FATAL("cannot fetch a valid connection");
        return yfalse;
    }

    if (!_ytable_execute(ytable, conn)) {
        YUKI_LOG_FATAL("fail to execute sql");
        return yfalse;
    }

    ytable_mysql_res_t * mysql_res = (ytable_mysql_res_t*)mysql_store_result(&conn->mysql);

    if (NULL == mysql_res) {
        if (mysql_field_count(&conn->mysql) == 0) {
            ytable->affected_rows = mysql_affected_rows(&conn->mysql);
        } else {
            YUKI_LOG_WARNING("cannot store query result. [error: %s]", mysql_error(&conn->mysql));
            return yfalse;
        }
    } else {
        ytable->affected_rows = mysql_affected_rows(&conn->mysql);
    }

    // must be affected one row
    if (ytable->affected_rows != 1UL) {
        YUKI_LOG_WARNING("affected row is not 1. [row: %lu]", ytable->affected_rows);
        mysql_free_result((MYSQL_RES*)mysql_res);
        return yfalse;
    }

    ybool_t ret = _ytable_sql_do_result_parse(ytable, mysql_res, result);

    if (!ret) {
        YUKI_LOG_WARNING("cannot parse result");
    }

    if (mysql_res) {
        mysql_free_result((MYSQL_RES*)mysql_res);
    }

    return ret;
}

ybool_t _ytable_fetch_insert_id(ytable_t * ytable, yvar_t * insert_id)
{
    // TODO: implement it
    YUKI_LOG_FATAL("not implemented");
    return yfalse;
}

