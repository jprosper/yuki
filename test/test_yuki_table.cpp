#include <gtest/gtest.h>

#include "yuki.h"
#define YUKI_CFG_FILE "./test/yuki.config"

class YukiTableTest : public ::testing::Test {
protected:
    virtual void SetUp()
    {
        yuki_init(YUKI_CFG_FILE);
    }

    virtual void TearDown()
    {
        yuki_shutdown();
    }
};

static char string_need_escape[] = "abc\n \" ' asd cs\r quo't cas\" \\";

TEST_F(YukiTableTest, InsertOne) {
    yvar_t field1 = YVAR_EMPTY();
    yvar_t field2 = YVAR_EMPTY();
    yvar_t field3 = YVAR_EMPTY();
    yvar_t field4 = YVAR_EMPTY();
    yvar_t field5 = YVAR_EMPTY();
    yvar_t value1 = YVAR_EMPTY();
    yvar_t value2 = YVAR_EMPTY();
    yvar_t value3 = YVAR_EMPTY();
    yvar_t value4 = YVAR_EMPTY();
    yvar_t value5 = YVAR_EMPTY();
    yvar_t cond_key1 = YVAR_EMPTY();
    yvar_t cond_value1 = YVAR_EMPTY();
    yvar_cstr(field1, "uid");
    yvar_cstr(field2, "diamond");
    yvar_cstr(field3, "cash");
    yvar_cstr(field4, "content");
    yvar_cstr(field5, "created_at");
    yvar_cstr(value1, "1234567890");
    yvar_int64(value2, 21);
    yvar_int64(value3, 12);
    yvar_cstr(value4, string_need_escape);
    yvar_cstr(value5, "2010-08-13 01:23:45");
    yvar_cstr(cond_key1, "uid");
    yvar_cstr(cond_value1, "1234567890");

    yvar_map_kv_t raw_fields = {
        {field1, value1},
        {field2, value2},
        {field3, value3},
        {field4, value4},
        {field5, value5},
    };

    yvar_t * insert_map;
    ASSERT_TRUE(yvar_map_smart_clone(insert_map, raw_fields));

    yvar_map_kv_t raw_cond = {
        {cond_key1, cond_value1},
    };
    yvar_t * cond;
    ASSERT_TRUE(yvar_map_smart_clone(cond, raw_cond));

    ytable_t * ytable = ytable_instance("mytest");
    ASSERT_TRUE(ytable);

    yvar_t * result;
    ASSERT_EQ(ytable_insert(ytable, *insert_map), ytable);
    ASSERT_EQ(ytable_last_error(ytable), YTABLE_ERROR_SUCCESS);
    ASSERT_EQ(ytable_where(ytable, *cond), ytable); // cannot set where conf for INSERT
    ASSERT_EQ(ytable_last_error(ytable), YTABLE_ERROR_INVALID_VERB);
    ASSERT_TRUE(ytable_fetch_one(ytable, result));

    // the result should be a bool true:
    ASSERT_TRUE(yvar_is_bool(*result));
    yvar_t bool_true = YVAR_EMPTY();
    yvar_bool(bool_true, ytrue);
    ASSERT_TRUE(yvar_equal(*result, bool_true));

    yvar_t insert_id = YVAR_EMPTY();
    ASSERT_TRUE(ytable_fetch_insert_id(ytable, insert_id));
    yint64_t insert_id_value;
    ASSERT_TRUE(yvar_get_int64(insert_id, insert_id_value));
    ASSERT_TRUE(insert_id_value > 0);
}

TEST_F(YukiTableTest, SelectOne) {
    yvar_t field1 = YVAR_EMPTY();
    yvar_t field2 = YVAR_EMPTY();
    yvar_t field3 = YVAR_EMPTY();
    yvar_t field4 = YVAR_EMPTY();
    yvar_t field5 = YVAR_EMPTY();
    yvar_t cond_key1 = YVAR_EMPTY();
    yvar_t cond_value1 = YVAR_EMPTY();
    yvar_cstr(field1, "uid");
    yvar_cstr(field2, "diamond");
    yvar_cstr(field3, "cash");
    yvar_cstr(field4, "content");
    yvar_cstr(field5, "created_at");
    yvar_cstr(cond_key1, "uid");
    yvar_cstr(cond_value1, "1234567890");

    yvar_t field_wildcard = YVAR_EMPTY();
    yvar_cstr(field_wildcard, "*");

    yvar_t raw_fields[] = {
        field_wildcard
    };

    yvar_t fields = YVAR_EMPTY();
    yvar_array(fields, raw_fields);

    yvar_t op = YVAR_EMPTY();
    yvar_cstr(op, "=");

    yvar_triple_array_t raw_cond = {
        {cond_key1, op, cond_value1},
    };
    yvar_t * cond;
    ASSERT_TRUE(yvar_triple_array_smart_clone(cond, raw_cond));

    ytable_t * ytable = ytable_instance("mytest");
    ASSERT_TRUE(ytable);

    yvar_t * result;
    ASSERT_EQ(ytable_select(ytable, fields), ytable);
    ASSERT_EQ(ytable_last_error(ytable), YTABLE_ERROR_SUCCESS);
    ASSERT_EQ(ytable_where(ytable, *cond), ytable);
    ASSERT_EQ(ytable_last_error(ytable), YTABLE_ERROR_SUCCESS);
    ASSERT_TRUE(ytable_fetch_one(ytable, result));

    // the result should be an array:
    // row #1 (a map):
    // - "uid" => "1234567890"
    // - "diamond" => "21"
    // - "cash" => "12"
    // - "content" => string_need_escape
    // - "created_at" => "2010-08-13 01:23:45"
    ASSERT_EQ(yvar_count(*result), 1u);

    yvar_t row1 = YVAR_EMPTY();
    ASSERT_TRUE(yvar_array_get(*result, 0, row1));
    ASSERT_TRUE(yvar_is_map(row1));

    yvar_t uid_var = YVAR_EMPTY();
    yvar_t diamond_var = YVAR_EMPTY();
    yvar_t cash_var = YVAR_EMPTY();
    yvar_t content_var = YVAR_EMPTY();
    yvar_t created_at_var = YVAR_EMPTY();

    ASSERT_TRUE(yvar_map_get(row1, field1, uid_var));
    ASSERT_TRUE(yvar_map_get(row1, field2, diamond_var));
    ASSERT_TRUE(yvar_map_get(row1, field3, cash_var));
    ASSERT_TRUE(yvar_map_get(row1, field4, content_var));
    ASSERT_TRUE(yvar_map_get(row1, field5, created_at_var));

    yvar_t expected_uid_var = YVAR_EMPTY();
    yvar_t expected_diamond_var = YVAR_EMPTY();
    yvar_t expected_cash_var = YVAR_EMPTY();
    yvar_t expected_content = YVAR_EMPTY();
    yvar_t expected_created_at = YVAR_EMPTY();
    yvar_cstr(expected_uid_var, "1234567890");
    yvar_int64(expected_diamond_var, 21);
    yvar_int64(expected_cash_var, 12);
    yvar_cstr(expected_content, string_need_escape);
    yvar_cstr(expected_created_at, "2010-08-13 01:23:45");

    ASSERT_TRUE(yvar_equal(uid_var, expected_uid_var));
    ASSERT_TRUE(yvar_equal(diamond_var, expected_diamond_var));
    ASSERT_TRUE(yvar_equal(cash_var, expected_cash_var));
    ASSERT_TRUE(yvar_equal(content_var, expected_content));
    ASSERT_TRUE(yvar_equal(created_at_var, expected_created_at));
}

TEST_F(YukiTableTest, UpdateOne) {
    yvar_t field1 = YVAR_EMPTY();
    yvar_t field2 = YVAR_EMPTY();
    yvar_t field3 = YVAR_EMPTY();
    yvar_t field4 = YVAR_EMPTY();
    yvar_t value1 = YVAR_EMPTY();
    yvar_t value2 = YVAR_EMPTY();
    yvar_t value3 = YVAR_EMPTY();
    yvar_t value4 = YVAR_EMPTY();
    yvar_t cond_key1 = YVAR_EMPTY();
    yvar_t cond_value1 = YVAR_EMPTY();
    yvar_t op = YVAR_EMPTY();
    yvar_t plus_eq_op = YVAR_EMPTY();
    yvar_t minus_eq_op = YVAR_EMPTY();
    yvar_cstr(field1, "uid");
    yvar_cstr(field2, "diamond");
    yvar_cstr(field3, "cash");
    yvar_cstr(field4, "content");
    yvar_cstr(value1, "1234567890");
    yvar_int64(value2, 44444);
    yvar_int64(value3, 55555);
    yvar_int64(value4, 66666);
    yvar_cstr(cond_key1, "uid");
    yvar_cstr(cond_value1, "1234567890");
    yvar_cstr(op, "=");
    yvar_cstr(plus_eq_op, "+=");
    yvar_cstr(minus_eq_op, "-=");

    yvar_triple_array_t raw_fields = {
        {field2, plus_eq_op, value2},
        {field3, minus_eq_op, value3},
        {field4, op, value4},
    };

    yvar_t * update_triple_array;
    ASSERT_TRUE(yvar_triple_array_smart_clone(update_triple_array, raw_fields));

    yvar_triple_array_t raw_cond = {
        {cond_key1, op, cond_value1},
    };
    yvar_t * cond;
    ASSERT_TRUE(yvar_triple_array_smart_clone(cond, raw_cond));

    ytable_t * ytable = ytable_instance("mytest");
    ASSERT_TRUE(ytable);

    yvar_t * result;
    ASSERT_EQ(ytable_update(ytable, *update_triple_array), ytable);
    ASSERT_EQ(ytable_last_error(ytable), YTABLE_ERROR_SUCCESS);
    ASSERT_EQ(ytable_where(ytable, *cond), ytable);
    ASSERT_EQ(ytable_last_error(ytable), YTABLE_ERROR_SUCCESS);
    ASSERT_TRUE(ytable_fetch_one(ytable, result));

    // the result should be a bool true:
    ASSERT_TRUE(yvar_is_bool(*result));
    yvar_t bool_true = YVAR_EMPTY();
    yvar_bool(bool_true, ytrue);
    ASSERT_TRUE(yvar_equal(*result, bool_true));
}


TEST_F(YukiTableTest, DeleteOne) {
    yvar_t cond_key1 = YVAR_EMPTY();
    yvar_t cond_value1 = YVAR_EMPTY();
    yvar_t op = YVAR_EMPTY();
    yvar_cstr(cond_key1, "uid");
    yvar_cstr(cond_value1, "1234567890");
    yvar_cstr(op, "=");

    yvar_triple_array_t raw_cond = {
        {cond_key1, op, cond_value1},
    };
    yvar_t * cond;
    ASSERT_TRUE(yvar_triple_array_smart_clone(cond, raw_cond));

    ytable_t * ytable = ytable_instance("mytest");
    ASSERT_TRUE(ytable);

    yvar_t * result;
    ASSERT_EQ(ytable_delete(ytable), ytable);
    ASSERT_EQ(ytable_last_error(ytable), YTABLE_ERROR_SUCCESS);
    ASSERT_EQ(ytable_where(ytable, *cond), ytable);
    ASSERT_EQ(ytable_last_error(ytable), YTABLE_ERROR_SUCCESS);
    ASSERT_TRUE(ytable_fetch_one(ytable, result));

    // the result should be a bool true:
    ASSERT_TRUE(yvar_is_bool(*result));
    yvar_t bool_true = YVAR_EMPTY();
    yvar_bool(bool_true, ytrue);
    ASSERT_TRUE(yvar_equal(*result, bool_true));
}

