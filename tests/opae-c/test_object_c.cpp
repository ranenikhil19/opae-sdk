// Copyright(c) 2018-2022, Intel Corporation
//
// Redistribution  and  use  in source  and  binary  forms,  with  or  without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of  source code  must retain the  above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name  of Intel Corporation  nor the names of its contributors
//   may be used to  endorse or promote  products derived  from this  software
//   without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,  BUT NOT LIMITED TO,  THE
// IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT  SHALL THE COPYRIGHT OWNER  OR CONTRIBUTORS BE
// LIABLE  FOR  ANY  DIRECT,  INDIRECT,  INCIDENTAL,  SPECIAL,  EXEMPLARY,  OR
// CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT LIMITED  TO,  PROCUREMENT  OF
// SUBSTITUTE GOODS OR SERVICES;  LOSS OF USE,  DATA, OR PROFITS;  OR BUSINESS
// INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY  OF LIABILITY,  WHETHER IN
// CONTRACT,  STRICT LIABILITY,  OR TORT  (INCLUDING NEGLIGENCE  OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif // HAVE_CONFIG_H

#include "mock/opae_fixtures.h"

using namespace opae::testing;

class object_c_p : public opae_p<> {
 protected:
  object_c_p() :
    token_obj_(nullptr),
    handle_obj_(nullptr)
  {}

  virtual void SetUp() override
  {
    opae_p<>::SetUp();

    EXPECT_EQ(fpgaTokenGetObject(device_token_,
                                 "ports_num",
                                 &token_obj_,
                                 0), FPGA_OK);

    EXPECT_EQ(fpgaHandleGetObject(accel_,
                                  "power_state",
                                  &handle_obj_,
                                  0), FPGA_OK);
  }

  virtual void TearDown() override
  {
    EXPECT_EQ(fpgaDestroyObject(&handle_obj_), FPGA_OK);
    handle_obj_ = nullptr;

    EXPECT_EQ(fpgaDestroyObject(&token_obj_), FPGA_OK);
    token_obj_ = nullptr;

    opae_p<>::TearDown();
  }

  fpga_object token_obj_;
  fpga_object handle_obj_;
};

/**
 * @test       obj_read
 * @brief      Test: fpgaObjectRead
 * @details    When fpgaObjectRead is called with valid params,<br>
 *             the fn retrieves the value of the targeted object<br>
 *             and returns FPGA_OK.<br>
 */
TEST_P(object_c_p, obj_read) {
  char power_state[32] = { 0, };
  EXPECT_EQ(fpgaObjectRead(handle_obj_, (uint8_t *) power_state, 0,
                           4, 0), FPGA_OK);
  power_state[4] = 0;
  EXPECT_STREQ(power_state, "0x0\n");
}

/**
 * @test       obj_read64
 * @brief      Test: fpgaObjectRead64
 * @details    When fpgaObjectRead64 is called with valid params,<br>
 *             the fn retrieves the value of the targeted object<br>
 *             and returns FPGA_OK.<br>
 */
TEST_P(object_c_p, obj_read64) {
  uint64_t val = 0;
  EXPECT_EQ(fpgaObjectRead64(token_obj_, &val, 0), FPGA_OK);
  EXPECT_EQ(val, 1ul);
}

/**
 * @test       obj_write64
 * @brief      Test: fpgaObjectWrite64
 * @details    When fpgaObjectWrite64 is called with valid params,<br>
 *             the fn sets the value of the targeted object<br>
 *             and returns FPGA_OK.<br>
 */
TEST_P(object_c_p, obj_write64) {
  uint64_t errors = 0xbaddecaf;
  fpga_object obj = nullptr;

  test_device device = platform_.devices[0];

  if (device.has_afu) {
    // read the port errors
    ASSERT_EQ(fpgaHandleGetObject(accel_, "errors/errors", &obj, 0), FPGA_OK);
    ASSERT_EQ(fpgaObjectRead64(obj, &errors, 0), FPGA_OK);
    EXPECT_EQ(fpgaDestroyObject(&obj), FPGA_OK);

    // clear the port errors
    ASSERT_EQ(fpgaHandleGetObject(accel_, "errors/errors", &obj, 0), FPGA_OK);
    ASSERT_EQ(fpgaObjectWrite64(obj, errors, 0), FPGA_OK);
    EXPECT_EQ(fpgaDestroyObject(&obj), FPGA_OK);
  }
}

/**
 * @test       obj_get_obj_at0
 * @brief      Test: fpgaObjectGetObjectAt
 * @details    When fpgaObjectGetObjectAt is called with valid parameters,<br>
 *             the fn opens the underlying object<br>
 *             and returns FPGA_OK.<br>
 */
TEST_P(object_c_p, obj_get_obj_at0) {
  fpga_object obj = nullptr;
  fpga_object child_obj = nullptr;

  ASSERT_EQ(fpgaHandleGetObject(accel_,
                                "power",
                                &obj,
                                FPGA_OBJECT_RECURSE_ONE), FPGA_OK);

  EXPECT_EQ(fpgaObjectGetObjectAt(obj, 0, &child_obj), FPGA_OK);

  EXPECT_EQ(fpgaDestroyObject(&child_obj), FPGA_OK);
  EXPECT_EQ(fpgaDestroyObject(&obj), FPGA_OK);
}

/**
 * @test       obj_get_type0
 * @brief      Test: fpgaObjectGetType
 * @details    When fpgaObjectGetType is called with valid parameters,<br>
 *             the fn opens the underlying object<br>
 *             and returns FPGA_OK.<br>
 */
TEST_P(object_c_p, obj_get_type0) {
  fpga_object obj = nullptr;
  fpga_object ctrl_obj = nullptr;
  enum fpga_sysobject_type type;

  ASSERT_EQ(fpgaHandleGetObject(accel_, "power", &obj, 0), FPGA_OK);

  EXPECT_EQ(fpgaObjectGetType(obj, &type), FPGA_OK);
  EXPECT_EQ(type, FPGA_OBJECT_CONTAINER);

  ASSERT_EQ(fpgaObjectGetObject(obj, "control", &ctrl_obj, 0), FPGA_OK);

  EXPECT_EQ(fpgaObjectGetType(ctrl_obj, &type), FPGA_OK);
  EXPECT_EQ(type, FPGA_OBJECT_ATTRIBUTE);

  EXPECT_EQ(fpgaDestroyObject(&ctrl_obj), FPGA_OK);
  EXPECT_EQ(fpgaDestroyObject(&obj), FPGA_OK);
}

/**
 * @test       obj_get_obj0
 * @brief      Test: fpgaObjectGetObject
 * @details    When fpgaObjectGetObject is called with valid parameters,<br>
 *             the fn opens the underlying object<br>
 *             and returns FPGA_OK.<br>
 */
TEST_P(object_c_p, obj_get_obj0) {
  fpga_object errors_obj = nullptr;
  fpga_object clear_obj = nullptr;

  test_device device = platform_.devices[0];

  if (device.has_afu) {
    ASSERT_EQ(fpgaHandleGetObject(accel_, "errors", &errors_obj, 0), FPGA_OK);
    ASSERT_EQ(fpgaObjectGetObject(errors_obj, "errors",
                                  &clear_obj, 0), FPGA_OK);
    ASSERT_EQ(fpgaObjectWrite64(clear_obj, 0, 0), FPGA_OK);
    EXPECT_EQ(fpgaDestroyObject(&clear_obj), FPGA_OK);
    EXPECT_EQ(fpgaDestroyObject(&errors_obj), FPGA_OK);
  }
}

/**
 * @test       obj_get_obj1
 * @brief      Test: fpgaObjectGetObject
 * @details    When fpgaObjectGetObject is called with a name that has a null
 *             byte, the function returns FPGA_NOT_FOUND. <br>
 *             and returns FPGA_OK.<br>
 */
TEST_P(object_c_p, obj_get_obj1) {
  fpga_object errors_obj = nullptr;
  fpga_object obj = nullptr;
  const char *bad_name = "err\0rs";

  test_device device = platform_.devices[0];

  if (device.has_afu) {
    ASSERT_EQ(fpgaHandleGetObject(accel_, "errors", &errors_obj, 0), FPGA_OK);
    EXPECT_EQ(fpgaObjectGetObject(errors_obj, bad_name, &obj, 0),FPGA_NOT_FOUND);

    ASSERT_NE(fpgaDestroyObject(&obj), FPGA_OK);
    ASSERT_EQ(fpgaDestroyObject(&errors_obj), FPGA_OK);
  }
}

/**
 * @test       handle_get_obj
 * @brief      Test: fpgaHandleGetObject
 * @details    When fpgaHandleGetObject is called with a name that has a null
 *             byte, the function returns FPGA_NOT_FOUND. <br>
 */
TEST_P(object_c_p, handle_get_obj) {
  fpga_object obj = nullptr;
  const char *bad_name = "err\0rs";

  EXPECT_EQ(fpgaHandleGetObject(accel_, bad_name, &obj, 0), FPGA_NOT_FOUND);
  ASSERT_NE(fpgaDestroyObject(&obj), FPGA_OK);
}

/**
 * @test       handle_get_obj
 * @brief      Test: fpgaHandleGetObject
 * @details    When fpgaHandleGetObject is called with too short name,
 *             the function returns FPGA_NOT_FOUND. <br>
 */
TEST_P(object_c_p, handle_get_obj_too_short) {
  fpga_object obj = nullptr;
  const char *too_short_name = "a";

  EXPECT_EQ(fpgaHandleGetObject(accel_, too_short_name, &obj, 0), FPGA_NOT_FOUND);
  ASSERT_NE(fpgaDestroyObject(&obj), FPGA_OK);
}

/**
 * @test       handle_get_obj
 * @brief      Test: fpgaHandleGetObject
 * @details    When fpgaHandleGetObject is called with too long name,
 *             the function returns FPGA_NOT_FOUND. <br>
 */
TEST_P(object_c_p, handle_get_obj_too_long) {
  fpga_object obj = nullptr;
  const char *too_long_name = "This/is/invalid/path/with/maximim/255/\
			       characterssssssssssssssssssssssssssssss\
			       ssssssssssssssssssssssa/lengthhhhhhhhhhh\
			       hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh\
			       /so/opaeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\
			       eeeeeeeeeeeeeeeeeee/api/should/return/with/\
			       errorrrrrrrrrrrrrrrrr/for/SDL testing/";

  EXPECT_EQ(fpgaHandleGetObject(accel_, too_long_name, &obj, 0), FPGA_NOT_FOUND);
  ASSERT_NE(fpgaDestroyObject(&obj), FPGA_OK);
}

/**
 * @test       token_get_obj
 * @brief      Test: fpgaTokenGetObject
 * @details    When fpgaTokenGetObject is called with a name that has a null
 *             byte, the function returns FPGA_NOT_FOUND. <br>
 */
TEST_P(object_c_p, token_get_obj) {
  fpga_object obj = nullptr;
  const char *bad_name = "err\0rs";

  EXPECT_EQ(fpgaTokenGetObject(device_token_, bad_name, &obj, 0), 
                                FPGA_NOT_FOUND);
  ASSERT_NE(fpgaDestroyObject(&obj), FPGA_OK);
}

/**
 * @test       token_get_obj
 * @brief      Test: fpgaTokenGetObject
 * @details    When fpgaTokenGetObject is called with too short path name,
 *             the function returns FPGA_NOT_FOUND. <br>
 */
TEST_P(object_c_p, token_get_obj_too_short) {
  fpga_object obj = nullptr;
  const char *too_short_path = "a";

  EXPECT_EQ(fpgaTokenGetObject(device_token_, too_short_path, &obj, 0),
                                FPGA_NOT_FOUND);
  ASSERT_NE(fpgaDestroyObject(&obj), FPGA_OK);
}

/**
 * @test       token_get_obj
 * @brief      Test: fpgaTokenGetObject
 * @details    When fpgaTokenGetObject is called with too long path name,
 *             the function returns FPGA_NOT_FOUND. <br>
 */
TEST_P(object_c_p, token_get_obj_too_long) {
  fpga_object obj = nullptr;
  const char *too_long_path = "This/is/invalid/path/with/maximim/255/\
			       characterssssssssssssssssssssssssssssss\
			       ssssssssssssssssssssssa/lengthhhhhhhhhhh\
			       hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh\
			       /so/opaeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\
			       eeeeeeeeeeeeeeeeeee/api/should/return/with/\
			       errorrrrrrrrrrrrrrrrr/for/SDL testing/";

  EXPECT_EQ(fpgaTokenGetObject(device_token_, too_long_path, &obj, 0),
                                FPGA_NOT_FOUND);
  ASSERT_NE(fpgaDestroyObject(&obj), FPGA_OK);
}


/**
 * @test       obj_get_size
 * @brief      Test: fpgaObjectGetSize
 * @details    Given an object created using name power_state<br>
 *             When fpgaObjectGetSize is called with that object<br>
 *             Then the size retrieved equals the length of the power_state
 *             string + one for the new line character<br>
 */
TEST_P(object_c_p, obj_get_size) {
  uint32_t value = 0;
  EXPECT_EQ(fpgaObjectGetSize(handle_obj_, &value, FPGA_OBJECT_SYNC), FPGA_OK);
  EXPECT_EQ(value, /* 0x0\n */ 4);
}

/**
 * @test       fpgaClose
 * @brief      Test: fpgaClose
 * @details    
 *             When fpgaClose is called with NULL object<br>
 *             the function returns FPGA_INVALID_PARAM.<br>
 */
TEST_P(object_c_p, obj_close) {
  EXPECT_EQ(fpgaClose(nullptr), FPGA_INVALID_PARAM);
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(object_c_p);
INSTANTIATE_TEST_SUITE_P(object_c, object_c_p,
                         ::testing::ValuesIn(test_platform::platforms({
                                                                        "dfl-d5005",
                                                                        "dfl-n3000",
                                                                        "dfl-n6000-sku0",
                                                                        "dfl-n6000-sku1"
                                                                      })));

class object_c_mock_p : public object_c_p {};

/**
 * @test       tok_get_err
 * @brief      Test: fpgaTokenGetObject
 * @details    When the call to opae_allocate_wrapped_object fails,<br>
 *             fpgaTokenGetObject destroys the underlying object<br>
 *             and returns FPGA_NO_MEMORY.<br>
 */
TEST_P(object_c_mock_p, tok_get_err) {
  fpga_object obj = nullptr;
  system_->invalidate_malloc(0, "opae_allocate_wrapped_object");
  EXPECT_EQ(fpgaTokenGetObject(device_token_, "ports_num",
                               &obj, 0), FPGA_NO_MEMORY);
}

/**
 * @test       handle_get_err
 * @brief      Test: fpgaHandleGetObject
 * @details    When the call to opae_allocate_wrapped_object fails,<br>
 *             fpgaHandleGetObject destroys the underlying object<br>
 *             and returns FPGA_NO_MEMORY.<br>
 */
TEST_P(object_c_mock_p, handle_get_err) {
  fpga_object obj = nullptr;
  system_->invalidate_malloc(0, "opae_allocate_wrapped_object");
  EXPECT_EQ(fpgaHandleGetObject(accel_, "id",
                               &obj, 0), FPGA_NO_MEMORY);
}

/**
 * @test       obj_get_obj_err
 * @brief      Test: fpgaObjectGetObject
 * @details    When opae_allocate_wrapped_object fails,<br>
 *             fpgaObjectGetObject frees the underlying object<br>
 *             and returns FPGA_NO_MEMORY.<br>
 */
TEST_P(object_c_mock_p, obj_get_obj_err) {
  fpga_object errors_obj = nullptr;
  fpga_object clear_obj = nullptr;

  test_device device = platform_.devices[0];

  if (device.has_afu) {
    ASSERT_EQ(fpgaHandleGetObject(accel_, "errors", &errors_obj, 0), FPGA_OK);

    system_->invalidate_malloc(0, "opae_allocate_wrapped_object");
    ASSERT_EQ(fpgaObjectGetObject(errors_obj, "errors",
                                  &clear_obj, 0), FPGA_NO_MEMORY);

    EXPECT_EQ(fpgaDestroyObject(&errors_obj), FPGA_OK);
  }
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(object_c_mock_p);
INSTANTIATE_TEST_SUITE_P(object_c, object_c_mock_p,
                         ::testing::ValuesIn(test_platform::mock_platforms({
                                                                             "dfl-d5005",
                                                                             "dfl-n3000",
                                                                             "dfl-n6000-sku0",
                                                                             "dfl-n6000-sku1"
                                                                           })));
