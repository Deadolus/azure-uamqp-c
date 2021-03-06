// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <cstdint>
#include <cstdbool>
#include <cstring>
#include "testrunnerswitcher.h"
#include "micromock.h"
#include "micromockcharstararenullterminatedstrings.h"
#include "azure_uamqp_c/sasl_frame_codec.h"
#include "azure_uamqp_c/frame_codec.h"
#include "azure_uamqp_c/amqpvalue.h"
#include "azure_uamqp_c/amqp_definitions.h"
#include "amqp_definitions_mocks.h"

#define TEST_FRAME_CODEC_HANDLE			(FRAME_CODEC_HANDLE)0x4242
#define TEST_DESCRIPTOR_AMQP_VALUE		(AMQP_VALUE)0x4243
#define TEST_DECODER_HANDLE				(AMQPVALUE_DECODER_HANDLE)0x4244
#define TEST_ENCODER_HANDLE				(ENCODER_HANDLE)0x4245
#define TEST_AMQP_VALUE					(AMQP_VALUE)0x4246
#define TEST_CONTEXT					(void*)0x4247

#define TEST_MIX_MAX_FRAME_SIZE			512

static const unsigned char default_test_encoded_bytes[2] = { 0x42, 0x43 };
static const unsigned char* test_encoded_bytes;
static size_t test_encoded_bytes_size;

static ON_FRAME_RECEIVED saved_on_frame_received;
static void* saved_callback_context;

static ON_VALUE_DECODED saved_value_decoded_callback;
static void* saved_value_decoded_callback_context;
static size_t total_bytes;

static unsigned char test_sasl_frame_value[] = { 0x42, 0x43, 0x44 };
static size_t test_sasl_frame_value_size;
static unsigned char* sasl_frame_value_decoded_bytes;
static size_t sasl_frame_value_decoded_byte_count;

static char expected_stringified_io[8192];
static char actual_stringified_io[8192];
static uint64_t sasl_frame_descriptor_ulong;

template<>
bool operator==<const PAYLOAD*>(const CMockValue<const PAYLOAD*>& lhs, const CMockValue<const PAYLOAD*>& rhs)
{
	return (lhs.GetValue() == rhs.GetValue()) ||
		(((lhs.GetValue()->length == rhs.GetValue()->length) &&
		(memcmp(lhs.GetValue()->bytes, rhs.GetValue()->bytes, lhs.GetValue()->length) == 0)));
}

void stringify_bytes(const unsigned char* bytes, size_t byte_count, char* output_string)
{
	size_t i;
	size_t pos = 0;

	output_string[pos++] = '[';
	for (i = 0; i < byte_count; i++)
	{
		(void)sprintf(&output_string[pos], "0x%02X", bytes[i]);
		if (i < byte_count - 1)
		{
			strcat(output_string, ",");
		}
		pos = strlen(output_string);
	}
	output_string[pos++] = ']';
	output_string[pos++] = '\0';
}

TYPED_MOCK_CLASS(sasl_frame_codec_mocks, CGlobalMock)
{
public:
	/* amqpalloc mocks */
	MOCK_STATIC_METHOD_1(, void*, amqpalloc_malloc, size_t, size)
	MOCK_METHOD_END(void*, malloc(size));
	MOCK_STATIC_METHOD_1(, void, amqpalloc_free, void*, ptr)
		free(ptr);
	MOCK_VOID_METHOD_END();

	/* amqpvalue mocks*/
	MOCK_STATIC_METHOD_1(, AMQP_VALUE, amqpvalue_create_ulong, uint64_t, value);
	MOCK_METHOD_END(AMQP_VALUE, TEST_AMQP_VALUE);
	MOCK_STATIC_METHOD_1(, AMQP_VALUE, amqpvalue_create_descriptor, AMQP_VALUE, value);
	MOCK_METHOD_END(AMQP_VALUE, TEST_AMQP_VALUE);
	MOCK_STATIC_METHOD_2(, int, amqpvalue_get_ulong, AMQP_VALUE, value, uint64_t*, ulong_value)
		*ulong_value = sasl_frame_descriptor_ulong;
	MOCK_METHOD_END(int, 0);
	MOCK_STATIC_METHOD_1(, AMQP_VALUE, amqpvalue_get_inplace_descriptor, AMQP_VALUE, value)
	MOCK_METHOD_END(AMQP_VALUE, TEST_DESCRIPTOR_AMQP_VALUE);
	MOCK_STATIC_METHOD_1(, void, amqpvalue_destroy, AMQP_VALUE, value)
	MOCK_VOID_METHOD_END();

	/* frame_codec mocks */
	MOCK_STATIC_METHOD_3(, FRAME_CODEC_HANDLE, frame_codec_create, ON_FRAME_CODEC_ERROR, frame_codec_error_callback, void*, frame_codec_error_callback_context, LOGGER_LOG, logger_log);
	MOCK_METHOD_END(FRAME_CODEC_HANDLE, TEST_FRAME_CODEC_HANDLE);
	MOCK_STATIC_METHOD_1(, void, frame_codec_destroy, FRAME_CODEC_HANDLE, frame_codec);
	MOCK_VOID_METHOD_END();
	MOCK_STATIC_METHOD_4(, int, frame_codec_subscribe, FRAME_CODEC_HANDLE, frame_codec, uint8_t, type, ON_FRAME_RECEIVED, on_frame_received, void*, callback_context);
		saved_on_frame_received = on_frame_received;
		saved_callback_context = callback_context;
	MOCK_METHOD_END(int, 0);
	MOCK_STATIC_METHOD_2(, int, frame_codec_unsubscribe, FRAME_CODEC_HANDLE, frame_codec, uint8_t, type);
	MOCK_METHOD_END(int, 0);
	MOCK_STATIC_METHOD_8(, int, frame_codec_encode_frame, FRAME_CODEC_HANDLE, frame_codec, uint8_t, type, const PAYLOAD*, payloads, size_t, payload_count, const unsigned char*, type_specific_bytes, uint32_t, type_specific_size, ON_BYTES_ENCODED, on_bytes_encoded, void*, callback_context);
	MOCK_METHOD_END(int, 0);

	/* decoder mocks */
	MOCK_STATIC_METHOD_2(, AMQPVALUE_DECODER_HANDLE, amqpvalue_decoder_create, ON_VALUE_DECODED, value_decoded_callback, void*, value_decoded_callback_context);
		saved_value_decoded_callback = value_decoded_callback;
		saved_value_decoded_callback_context = value_decoded_callback_context;
		total_bytes = 0;
	MOCK_METHOD_END(AMQPVALUE_DECODER_HANDLE, TEST_DECODER_HANDLE);
	MOCK_STATIC_METHOD_1(, void, amqpvalue_decoder_destroy, AMQPVALUE_DECODER_HANDLE, handle);
	MOCK_VOID_METHOD_END();
	MOCK_STATIC_METHOD_3(, int, amqpvalue_decode_bytes, AMQPVALUE_DECODER_HANDLE, handle, const unsigned char*, buffer, size_t, size);
		unsigned char* new_bytes = (unsigned char*)realloc(sasl_frame_value_decoded_bytes, sasl_frame_value_decoded_byte_count + size);
		int my_result = 0;
		if (new_bytes != NULL)
		{
			sasl_frame_value_decoded_bytes = new_bytes;
			(void)memcpy(sasl_frame_value_decoded_bytes + sasl_frame_value_decoded_byte_count, buffer, size);
			sasl_frame_value_decoded_byte_count += size;
		}
		total_bytes += size;
		if (total_bytes == test_sasl_frame_value_size)
		{
			saved_value_decoded_callback(saved_value_decoded_callback_context, TEST_AMQP_VALUE);
			total_bytes = 0;
		}
		MOCK_METHOD_END(int, 0);

	/* encoder mocks */
	MOCK_STATIC_METHOD_2(, int, amqpvalue_get_encoded_size, AMQP_VALUE, value, size_t*, encoded_size);
	MOCK_METHOD_END(int, 0);
	MOCK_STATIC_METHOD_3(, int, amqpvalue_encode, AMQP_VALUE, value, AMQPVALUE_ENCODER_OUTPUT, encoder_output, void*, context);
		encoder_output(context, test_encoded_bytes, test_encoded_bytes_size);
	MOCK_METHOD_END(int, 0);

	/* callbacks */
	MOCK_STATIC_METHOD_2(, void, test_on_sasl_frame_received, void*, context, AMQP_VALUE, sasl_frame_value);
	MOCK_VOID_METHOD_END();
	MOCK_STATIC_METHOD_1(, void, test_on_sasl_frame_codec_error, void*, context);
	MOCK_VOID_METHOD_END();
};

extern "C"
{
	DECLARE_GLOBAL_MOCK_METHOD_1(sasl_frame_codec_mocks, , void*, amqpalloc_malloc, size_t, size);
	DECLARE_GLOBAL_MOCK_METHOD_1(sasl_frame_codec_mocks, , void, amqpalloc_free, void*, ptr);

	DECLARE_GLOBAL_MOCK_METHOD_1(sasl_frame_codec_mocks, , AMQP_VALUE, amqpvalue_create_ulong, uint64_t, value);
	DECLARE_GLOBAL_MOCK_METHOD_1(sasl_frame_codec_mocks, , AMQP_VALUE, amqpvalue_create_descriptor, AMQP_VALUE, value);
	DECLARE_GLOBAL_MOCK_METHOD_2(sasl_frame_codec_mocks, , int, amqpvalue_get_ulong, AMQP_VALUE, value, uint64_t*, ulong_value);
	DECLARE_GLOBAL_MOCK_METHOD_1(sasl_frame_codec_mocks, , AMQP_VALUE, amqpvalue_get_inplace_descriptor, AMQP_VALUE, value);
	DECLARE_GLOBAL_MOCK_METHOD_1(sasl_frame_codec_mocks, , void, amqpvalue_destroy, AMQP_VALUE, value);

	DECLARE_GLOBAL_MOCK_METHOD_3(sasl_frame_codec_mocks, , FRAME_CODEC_HANDLE, frame_codec_create, ON_FRAME_CODEC_ERROR, frame_codec_error_callback, void*, frame_codec_error_callback_context, LOGGER_LOG, logger_log);
	DECLARE_GLOBAL_MOCK_METHOD_1(sasl_frame_codec_mocks, , void, frame_codec_destroy, FRAME_CODEC_HANDLE, frame_codec);
	DECLARE_GLOBAL_MOCK_METHOD_4(sasl_frame_codec_mocks, , int, frame_codec_subscribe, FRAME_CODEC_HANDLE, frame_codec, uint8_t, type, ON_FRAME_RECEIVED, frame_received_callback, void*, callback_context);
	DECLARE_GLOBAL_MOCK_METHOD_2(sasl_frame_codec_mocks, , int, frame_codec_unsubscribe, FRAME_CODEC_HANDLE, frame_codec, uint8_t, type);
	DECLARE_GLOBAL_MOCK_METHOD_8(sasl_frame_codec_mocks, , int, frame_codec_encode_frame, FRAME_CODEC_HANDLE, frame_codec, uint8_t, type, const PAYLOAD*, payloads, size_t, payload_count, const unsigned char*, type_specific_bytes, uint32_t, type_specific_size, ON_BYTES_ENCODED, on_bytes_encoded, void*, callback_context);

	DECLARE_GLOBAL_MOCK_METHOD_2(sasl_frame_codec_mocks, , AMQPVALUE_DECODER_HANDLE, amqpvalue_decoder_create, ON_VALUE_DECODED, value_decoded_callback, void*, value_decoded_callback_context);
	DECLARE_GLOBAL_MOCK_METHOD_1(sasl_frame_codec_mocks, , void, amqpvalue_decoder_destroy, AMQPVALUE_DECODER_HANDLE, handle);
	DECLARE_GLOBAL_MOCK_METHOD_3(sasl_frame_codec_mocks, , int, amqpvalue_decode_bytes, AMQPVALUE_DECODER_HANDLE, handle, const unsigned char*, buffer, size_t, size);

	DECLARE_GLOBAL_MOCK_METHOD_2(sasl_frame_codec_mocks, , int, amqpvalue_get_encoded_size, AMQP_VALUE, value, size_t*, encoded_size);
	DECLARE_GLOBAL_MOCK_METHOD_3(sasl_frame_codec_mocks, , int, amqpvalue_encode, AMQP_VALUE, value, AMQPVALUE_ENCODER_OUTPUT, encoder_output, void*, context);

	DECLARE_GLOBAL_MOCK_METHOD_2(sasl_frame_codec_mocks, , void, test_on_sasl_frame_received, void*, context, AMQP_VALUE, sasl_frame_value);
	DECLARE_GLOBAL_MOCK_METHOD_1(sasl_frame_codec_mocks, , void, test_on_sasl_frame_codec_error, void*, context);

	extern void consolelogger_log(char* format, ...)
	{
		(void)format;
	}

	extern void test_on_bytes_encoded(void* context, const unsigned char* bytes, size_t length, bool encode_complete)
	{
		(void)context, bytes, length, encode_complete;
	}
}

MICROMOCK_MUTEX_HANDLE test_serialize_mutex;

BEGIN_TEST_SUITE(sasl_frame_codec_unittests)

TEST_SUITE_INITIALIZE(suite_init)
{
	test_serialize_mutex = MicroMockCreateMutex();
	ASSERT_IS_NOT_NULL(test_serialize_mutex);
}

TEST_SUITE_CLEANUP(suite_cleanup)
{
	MicroMockDestroyMutex(test_serialize_mutex);
}

TEST_FUNCTION_INITIALIZE(method_init)
{
	if (!MicroMockAcquireMutex(test_serialize_mutex))
	{
		ASSERT_FAIL("Could not acquire test serialization mutex.");
	}

	test_encoded_bytes = default_test_encoded_bytes;
	test_encoded_bytes_size = sizeof(default_test_encoded_bytes);
	test_sasl_frame_value_size = sizeof(test_sasl_frame_value);
	sasl_frame_descriptor_ulong = SASL_MECHANISMS;
}

TEST_FUNCTION_CLEANUP(method_cleanup)
{
	if (sasl_frame_value_decoded_bytes != NULL)
	{
		free(sasl_frame_value_decoded_bytes);
		sasl_frame_value_decoded_bytes = NULL;
	}
	sasl_frame_value_decoded_byte_count = 0;
	if (!MicroMockReleaseMutex(test_serialize_mutex))
	{
		ASSERT_FAIL("Could not release test serialization mutex.");
	}
}

/* sasl_frame_codec_create */

/* Tests_SRS_SASL_FRAME_CODEC_01_018: [sasl_frame_codec_create shall create an instance of an sasl_frame_codec and return a non-NULL handle to it.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_020: [sasl_frame_codec_create shall subscribe for SASL frames with the given frame_codec.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_022: [sasl_frame_codec_create shall create a decoder to be used for decoding SASL values.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_001: [A SASL frame has a type code of 0x01.] */
TEST_FUNCTION(sasl_frame_codec_create_with_valid_args_succeeds)
{
	// arrange
	sasl_frame_codec_mocks mocks;

	EXPECTED_CALL(mocks, amqpalloc_malloc(IGNORED_NUM_ARG));
	EXPECTED_CALL(mocks, amqpvalue_decoder_create(IGNORED_PTR_ARG, IGNORED_PTR_ARG));
	STRICT_EXPECTED_CALL(mocks, frame_codec_subscribe(TEST_FRAME_CODEC_HANDLE, FRAME_TYPE_SASL, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.IgnoreArgument(3).IgnoreArgument(4);

	// act
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);

	// assert
	ASSERT_IS_NOT_NULL(sasl_frame_codec);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_018: [sasl_frame_codec_create shall create an instance of an sasl_frame_codec and return a non-NULL handle to it.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_020: [sasl_frame_codec_create shall subscribe for SASL frames with the given frame_codec.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_022: [sasl_frame_codec_create shall create a decoder to be used for decoding SASL values.] */
TEST_FUNCTION(sasl_frame_codec_create_with_valid_args_and_NULL_context_succeeds)
{
	// arrange
	sasl_frame_codec_mocks mocks;

	EXPECTED_CALL(mocks, amqpalloc_malloc(IGNORED_NUM_ARG));
	EXPECTED_CALL(mocks, amqpvalue_decoder_create(IGNORED_PTR_ARG, IGNORED_PTR_ARG));
	STRICT_EXPECTED_CALL(mocks, frame_codec_subscribe(TEST_FRAME_CODEC_HANDLE, FRAME_TYPE_SASL, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.IgnoreArgument(3).IgnoreArgument(4);

	// act
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, NULL);

	// assert
	ASSERT_IS_NOT_NULL(sasl_frame_codec);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_019: [If any of the arguments frame_codec, frame_received_callback or error_callback is NULL, sasl_frame_codec_create shall return NULL.] */
TEST_FUNCTION(sasl_frame_codec_create_with_NULL_frame_codec_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;

	// act
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(NULL, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);

	// assert
	ASSERT_IS_NULL(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_019: [If any of the arguments frame_codec, frame_received_callback or error_callback is NULL, sasl_frame_codec_create shall return NULL.] */
TEST_FUNCTION(sasl_frame_codec_create_with_NULL_frame_received_callback_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;

	// act
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, NULL, test_on_sasl_frame_codec_error, TEST_CONTEXT);

	// assert
	ASSERT_IS_NULL(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_019: [If any of the arguments frame_codec, frame_received_callback or error_callback is NULL, sasl_frame_codec_create shall return NULL.] */
TEST_FUNCTION(sasl_frame_codec_create_with_NULL_error_callback_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;

	// act
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, NULL, TEST_CONTEXT);

	// assert
	ASSERT_IS_NULL(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_021: [If subscribing for SASL frames fails, sasl_frame_codec_create shall fail and return NULL.] */
TEST_FUNCTION(when_frame_codec_subscribe_fails_then_sasl_frame_codec_create_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;

	EXPECTED_CALL(mocks, amqpalloc_malloc(IGNORED_NUM_ARG));
	EXPECTED_CALL(mocks, amqpvalue_decoder_create(IGNORED_PTR_ARG, IGNORED_PTR_ARG));
	STRICT_EXPECTED_CALL(mocks, frame_codec_subscribe(TEST_FRAME_CODEC_HANDLE, FRAME_TYPE_SASL, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.IgnoreArgument(3).IgnoreArgument(4)
		.SetReturn(1);

	STRICT_EXPECTED_CALL(mocks, amqpvalue_decoder_destroy(TEST_DECODER_HANDLE));
	EXPECTED_CALL(mocks, amqpalloc_free(IGNORED_PTR_ARG));

	// act
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);

	// assert
	ASSERT_IS_NULL(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_023: [If creating the decoder fails, sasl_frame_codec_create shall fail and return NULL.] */
TEST_FUNCTION(when_creating_the_decoder_fails_then_sasl_frame_codec_create_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;

	EXPECTED_CALL(mocks, amqpalloc_malloc(IGNORED_NUM_ARG));
	EXPECTED_CALL(mocks, amqpvalue_decoder_create(IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.SetReturn((AMQPVALUE_DECODER_HANDLE)NULL);

	EXPECTED_CALL(mocks, amqpalloc_free(IGNORED_PTR_ARG));

	// act
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);

	// assert
	ASSERT_IS_NULL(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_024: [If allocating memory for the new sasl_frame_codec fails, then sasl_frame_codec_create shall fail and return NULL.]  */
TEST_FUNCTION(when_allocating_memory_for_sasl_frame_codec_fails_then_sasl_frame_codec_create_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;

	EXPECTED_CALL(mocks, amqpalloc_malloc(IGNORED_NUM_ARG))
		.SetReturn((void*)NULL);

	// act
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);

	// assert
	ASSERT_IS_NULL(sasl_frame_codec);
}

/* sasl_frame_codec_destroy */

/* Tests_SRS_SASL_FRAME_CODEC_01_025: [sasl_frame_codec_destroy shall free all resources associated with the sasl_frame_codec instance.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_027: [sasl_frame_codec_destroy shall unsubscribe from receiving SASL frames from the frame_codec that was passed to sasl_frame_codec_create.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_028: [The decoder created in sasl_frame_codec_create shall be destroyed by sasl_frame_codec_destroy.] */
TEST_FUNCTION(sasl_frame_codec_destroy_frees_the_decoder_and_unsubscribes_from_AMQP_frames)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	STRICT_EXPECTED_CALL(mocks, frame_codec_unsubscribe(TEST_FRAME_CODEC_HANDLE, FRAME_TYPE_SASL));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_decoder_destroy(TEST_DECODER_HANDLE));
	EXPECTED_CALL(mocks, amqpalloc_free(IGNORED_PTR_ARG));

	// act
	sasl_frame_codec_destroy(sasl_frame_codec);

	// assert
	// uMock checks the calls
}

/* Tests_SRS_SASL_FRAME_CODEC_01_025: [sasl_frame_codec_destroy shall free all resources associated with the sasl_frame_codec instance.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_027: [sasl_frame_codec_destroy shall unsubscribe from receiving SASL frames from the frame_codec that was passed to sasl_frame_codec_create.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_028: [The decoder created in sasl_frame_codec_create shall be destroyed by sasl_frame_codec_destroy.] */
TEST_FUNCTION(when_unsubscribe_fails_sasl_frame_codec_destroy_still_frees_everything)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	STRICT_EXPECTED_CALL(mocks, frame_codec_unsubscribe(TEST_FRAME_CODEC_HANDLE, FRAME_TYPE_SASL))
		.SetReturn(1);
	STRICT_EXPECTED_CALL(mocks, amqpvalue_decoder_destroy(TEST_DECODER_HANDLE));
	EXPECTED_CALL(mocks, amqpalloc_free(IGNORED_PTR_ARG));

	// act
	sasl_frame_codec_destroy(sasl_frame_codec);

	// assert
	// uMock checks the calls
}

/* Tests_SRS_SASL_FRAME_CODEC_01_026: [If sasl_frame_codec is NULL, sasl_frame_codec_destroy shall do nothing.] */
TEST_FUNCTION(sasl_frame_codec_destroy_with_NULL_handle_does_nothing)
{
	// arrange
	sasl_frame_codec_mocks mocks;

	// act
	sasl_frame_codec_destroy(NULL);

	// assert
	// uMock checks the calls
}

/* sasl_frame_codec_encode_frame */

/* Tests_SRS_SASL_FRAME_CODEC_01_029: [sasl_frame_codec_encode_frame shall encode the frame header and sasl_frame_value AMQP value in a SASL frame and on success it shall return 0.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_031: [sasl_frame_codec_encode_frame shall encode the frame header and its contents by using frame_codec_encode_frame.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_032: [The payload frame size shall be computed based on the encoded size of the sasl_frame_value and its fields.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_033: [The encoded size of the sasl_frame_value and its fields shall be obtained by calling amqpvalue_get_encoded_size.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_035: [Encoding of the sasl_frame_value and its fields shall be done by calling amqpvalue_encode.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_012: [Bytes 6 and 7 of the header are ignored.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_013: [Implementations SHOULD set these to 0x00.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_014: [The extended header is ignored.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_015: [Implementations SHOULD therefore set DOFF to 0x02.] */
TEST_FUNCTION(encoding_a_sasl_frame_succeeds)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	PAYLOAD payload = { test_encoded_bytes, (uint32_t)test_encoded_bytes_size };
	size_t sasl_frame_value_size = 2;
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_ulong(TEST_DESCRIPTOR_AMQP_VALUE, IGNORED_PTR_ARG))
		.IgnoreArgument(2);
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_encoded_size(TEST_AMQP_VALUE, IGNORED_PTR_ARG))
		.CopyOutArgumentBuffer(2, &sasl_frame_value_size, sizeof(sasl_frame_value_size));
	EXPECTED_CALL(mocks, amqpalloc_malloc(IGNORED_NUM_ARG));
	EXPECTED_CALL(mocks, amqpvalue_encode(TEST_AMQP_VALUE, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.ValidateArgument(1);
	STRICT_EXPECTED_CALL(mocks, frame_codec_encode_frame(TEST_FRAME_CODEC_HANDLE, FRAME_TYPE_SASL, &payload, 1, NULL, 0, test_on_bytes_encoded, (void*)0x4242));
	EXPECTED_CALL(mocks, amqpalloc_free(IGNORED_PTR_ARG));

	// act
	int result = sasl_frame_codec_encode_frame(sasl_frame_codec, TEST_AMQP_VALUE, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_EQUAL(int, 0, result);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_030: [If sasl_frame_codec or sasl_frame_value is NULL, sasl_frame_codec_encode_frame shall fail and return a non-zero value.] */
TEST_FUNCTION(sasl_frame_codec_encode_frame_with_NULL_sasl_frame_codec_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;

	// act
	int result = sasl_frame_codec_encode_frame(NULL, TEST_AMQP_VALUE, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_030: [If sasl_frame_codec or sasl_frame_value is NULL, sasl_frame_codec_encode_frame shall fail and return a non-zero value.] */
TEST_FUNCTION(sasl_frame_codec_encode_frame_with_NULL_performative_value_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	// act
	int result = sasl_frame_codec_encode_frame(sasl_frame_codec, NULL, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_034: [If any error occurs during encoding, sasl_frame_codec_encode_frame shall fail and return a non-zero value.] */
TEST_FUNCTION(when_amqpvalue_get_inplace_descriptor_fails_then_sasl_frame_codec_encode_frame_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	size_t sasl_frame_value_size = 2;
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE))
		.SetReturn((AMQP_VALUE)NULL);

	// act
	int result = sasl_frame_codec_encode_frame(sasl_frame_codec, TEST_AMQP_VALUE, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_034: [If any error occurs during encoding, sasl_frame_codec_encode_frame shall fail and return a non-zero value.] */
TEST_FUNCTION(when_amqpvalue_get_ulong_fails_then_sasl_frame_codec_encode_frame_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	size_t sasl_frame_value_size = 2;
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_ulong(TEST_DESCRIPTOR_AMQP_VALUE, IGNORED_PTR_ARG))
		.IgnoreArgument(2)
		.SetReturn(1);

	// act
	int result = sasl_frame_codec_encode_frame(sasl_frame_codec, TEST_AMQP_VALUE, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_034: [If any error occurs during encoding, sasl_frame_codec_encode_frame shall fail and return a non-zero value.] */
TEST_FUNCTION(when_amqpvalue_get_encoded_size_fails_then_sasl_frame_codec_encode_frame_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	size_t sasl_frame_value_size = 2;
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_ulong(TEST_DESCRIPTOR_AMQP_VALUE, IGNORED_PTR_ARG))
		.IgnoreArgument(2);
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_encoded_size(TEST_AMQP_VALUE, IGNORED_PTR_ARG))
		.IgnoreArgument(2)
		.SetReturn(1);

	// act
	int result = sasl_frame_codec_encode_frame(sasl_frame_codec, TEST_AMQP_VALUE, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_034: [If any error occurs during encoding, sasl_frame_codec_encode_frame shall fail and return a non-zero value.] */
TEST_FUNCTION(when_amqpvalue_encode_fails_then_sasl_frame_codec_encode_frame_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	size_t sasl_frame_value_size = 2;
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_ulong(TEST_DESCRIPTOR_AMQP_VALUE, IGNORED_PTR_ARG))
		.IgnoreArgument(2);
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_encoded_size(TEST_AMQP_VALUE, IGNORED_PTR_ARG))
		.CopyOutArgumentBuffer(2, &sasl_frame_value_size, sizeof(sasl_frame_value_size));
	EXPECTED_CALL(mocks, amqpalloc_malloc(IGNORED_NUM_ARG));
	EXPECTED_CALL(mocks, amqpvalue_encode(TEST_AMQP_VALUE, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.ValidateArgument(1)
		.SetReturn(1);
	EXPECTED_CALL(mocks, amqpalloc_free(IGNORED_PTR_ARG));

	// act
	int result = sasl_frame_codec_encode_frame(sasl_frame_codec, TEST_AMQP_VALUE, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_034: [If any error occurs during encoding, sasl_frame_codec_encode_frame shall fail and return a non-zero value.] */
TEST_FUNCTION(when_frame_codec_encode_frame_fails_then_sasl_frame_codec_encode_frame_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	PAYLOAD payload = { test_encoded_bytes, (uint32_t)test_encoded_bytes_size };
	size_t sasl_frame_value_size = 2;
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_ulong(TEST_DESCRIPTOR_AMQP_VALUE, IGNORED_PTR_ARG))
		.IgnoreArgument(2);
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_encoded_size(TEST_AMQP_VALUE, IGNORED_PTR_ARG))
		.CopyOutArgumentBuffer(2, &sasl_frame_value_size, sizeof(sasl_frame_value_size));
	EXPECTED_CALL(mocks, amqpalloc_malloc(IGNORED_NUM_ARG));
	EXPECTED_CALL(mocks, amqpvalue_encode(TEST_AMQP_VALUE, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.ValidateArgument(1);
	STRICT_EXPECTED_CALL(mocks, frame_codec_encode_frame(TEST_FRAME_CODEC_HANDLE, FRAME_TYPE_SASL, &payload, 1, NULL, 0, test_on_bytes_encoded, (void*)0x4242))
		.SetReturn(1);
	EXPECTED_CALL(mocks, amqpalloc_free(IGNORED_PTR_ARG));

	// act
	int result = sasl_frame_codec_encode_frame(sasl_frame_codec, TEST_AMQP_VALUE, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_034: [If any error occurs during encoding, sasl_frame_codec_encode_frame shall fail and return a non-zero value.] */
TEST_FUNCTION(when_allocating_memory_for_the_encoded_sasl_value_fails_then_sasl_frame_codec_encode_frame_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	PAYLOAD payload = { test_encoded_bytes, (uint32_t)test_encoded_bytes_size };
	size_t sasl_frame_value_size = 2;
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_ulong(TEST_DESCRIPTOR_AMQP_VALUE, IGNORED_PTR_ARG))
		.IgnoreArgument(2);
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_encoded_size(TEST_AMQP_VALUE, IGNORED_PTR_ARG))
		.CopyOutArgumentBuffer(2, &sasl_frame_value_size, sizeof(sasl_frame_value_size));
	EXPECTED_CALL(mocks, amqpalloc_malloc(IGNORED_NUM_ARG))
		.SetReturn((void*)NULL);

	// act
	int result = sasl_frame_codec_encode_frame(sasl_frame_codec, TEST_AMQP_VALUE, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_011: [A SASL frame has a type code of 0x01.] */
TEST_FUNCTION(the_SASL_frame_type_is_according_to_ISO)
{
	// arrange
	// act

	// assert
	ASSERT_ARE_EQUAL(int, (int)1, FRAME_TYPE_SASL);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_016: [The maximum size of a SASL frame is defined by MIN-MAX-FRAME-SIZE.] */
TEST_FUNCTION(when_encoding_a_sasl_frame_value_that_makes_the_frame_be_the_max_size_sasl_frame_codec_encode_frame_succeeds)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	unsigned char encoded_bytes[TEST_MIX_MAX_FRAME_SIZE - 8] = { 0 };
	test_encoded_bytes = encoded_bytes;
	test_encoded_bytes_size = sizeof(encoded_bytes);
	size_t sasl_frame_value_size = test_encoded_bytes_size;
	PAYLOAD payload = { test_encoded_bytes, (uint32_t)test_encoded_bytes_size };
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_ulong(TEST_DESCRIPTOR_AMQP_VALUE, IGNORED_PTR_ARG))
		.IgnoreArgument(2);
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_encoded_size(TEST_AMQP_VALUE, IGNORED_PTR_ARG))
		.CopyOutArgumentBuffer(2, &sasl_frame_value_size, sizeof(sasl_frame_value_size));
	EXPECTED_CALL(mocks, amqpalloc_malloc(IGNORED_NUM_ARG));
	EXPECTED_CALL(mocks, amqpvalue_encode(TEST_AMQP_VALUE, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.ValidateArgument(1);
	STRICT_EXPECTED_CALL(mocks, frame_codec_encode_frame(TEST_FRAME_CODEC_HANDLE, FRAME_TYPE_SASL, &payload, 1, NULL, 0, test_on_bytes_encoded, (void*)0x4242));
	EXPECTED_CALL(mocks, amqpalloc_free(IGNORED_PTR_ARG));

	// act
	int result = sasl_frame_codec_encode_frame(sasl_frame_codec, TEST_AMQP_VALUE, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_EQUAL(int, 0, result);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_016: [The maximum size of a SASL frame is defined by MIN-MAX-FRAME-SIZE.] */
TEST_FUNCTION(when_encoding_a_sasl_frame_value_that_makes_the_frame_exceed_the_allowed_size_sasl_frame_codec_encode_frame_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	unsigned char encoded_bytes[TEST_MIX_MAX_FRAME_SIZE - 8 + 1] = { 0 };
	test_encoded_bytes = encoded_bytes;
	test_encoded_bytes_size = sizeof(encoded_bytes);
	PAYLOAD payload = { test_encoded_bytes, (uint32_t)test_encoded_bytes_size };
	size_t sasl_frame_value_size = test_encoded_bytes_size;
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_ulong(TEST_DESCRIPTOR_AMQP_VALUE, IGNORED_PTR_ARG))
		.IgnoreArgument(2);
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_encoded_size(TEST_AMQP_VALUE, IGNORED_PTR_ARG))
		.CopyOutArgumentBuffer(2, &sasl_frame_value_size, sizeof(sasl_frame_value_size));

	// act
	int result = sasl_frame_codec_encode_frame(sasl_frame_codec, TEST_AMQP_VALUE, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_034: [If any error occurs during encoding, sasl_frame_codec_encode_frame shall fail and return a non-zero value.] */
TEST_FUNCTION(when_the_sasl_frame_value_has_a_descriptor_ulong_lower_than_MECHANISMS_frame_codec_encode_frame_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	sasl_frame_descriptor_ulong = SASL_MECHANISMS - 1;
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_ulong(TEST_DESCRIPTOR_AMQP_VALUE, IGNORED_PTR_ARG))
		.CopyOutArgumentBuffer(2, &sasl_frame_descriptor_ulong, sizeof(sasl_frame_descriptor_ulong));

	// act
	int result = sasl_frame_codec_encode_frame(sasl_frame_codec, TEST_AMQP_VALUE, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_034: [If any error occurs during encoding, sasl_frame_codec_encode_frame shall fail and return a non-zero value.] */
TEST_FUNCTION(when_the_sasl_frame_value_has_a_descriptor_ulong_higher_than_OUTCOME_frame_codec_encode_frame_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	sasl_frame_descriptor_ulong = SASL_OUTCOME + 1;
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_ulong(TEST_DESCRIPTOR_AMQP_VALUE, IGNORED_PTR_ARG))
		.CopyOutArgumentBuffer(2, &sasl_frame_descriptor_ulong, sizeof(sasl_frame_descriptor_ulong));

	// act
	int result = sasl_frame_codec_encode_frame(sasl_frame_codec, TEST_AMQP_VALUE, test_on_bytes_encoded, (void*)0x4242);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Receive frames */

/* Tests_SRS_SASL_FRAME_CODEC_01_039: [sasl_frame_codec shall decode the sasl-frame value as a described type.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_040: [Decoding the sasl-frame type shall be done by feeding the bytes to the decoder create in sasl_frame_codec_create.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_041: [Once the sasl frame is decoded, the callback frame_received_callback shall be called.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_042: [The decoded sasl-frame value and the context passed in sasl_frame_codec_create shall be passed to frame_received_callback.] */
TEST_FUNCTION(when_sasl_frame_bytes_are_received_it_is_decoded_and_indicated_as_a_received_sasl_frame)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).ExpectedTimesExactly(sizeof(test_sasl_frame_value));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_mechanisms_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_received(TEST_CONTEXT, TEST_AMQP_VALUE));

	// act
	saved_on_frame_received(saved_callback_context, NULL, 0, test_sasl_frame_value,  sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_039: [sasl_frame_codec shall decode the sasl-frame value as a described type.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_040: [Decoding the sasl-frame type shall be done by feeding the bytes to the decoder create in sasl_frame_codec_create.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_041: [Once the sasl frame is decoded, the callback frame_received_callback shall be called.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_042: [The decoded sasl-frame value and the context passed in sasl_frame_codec_create shall be passed to frame_received_callback.] */
TEST_FUNCTION(when_context_is_NULL_decoding_a_sasl_frame_still_succeeds)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, NULL);
	mocks.ResetAllCalls();

	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).ExpectedTimesExactly(sizeof(test_sasl_frame_value));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_mechanisms_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_received(NULL, TEST_AMQP_VALUE));

	// act
	saved_on_frame_received(saved_callback_context, NULL, 0, test_sasl_frame_value, sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_046: [If any error occurs while decoding a frame, the decoder shall switch to an error state where decoding shall not be possible anymore.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_049: [If any error occurs while decoding a frame, the decoder shall call the error_callback and pass to it the callback_context, both of those being the ones given to sasl_frame_codec_create.] */
TEST_FUNCTION(when_amqpvalue_decode_bytes_fails_then_the_decoder_switches_to_an_error_state)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).SetReturn(1);

	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_codec_error(TEST_CONTEXT));

	// act
	saved_on_frame_received(saved_callback_context, NULL, 0, test_sasl_frame_value, sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_046: [If any error occurs while decoding a frame, the decoder shall switch to an error state where decoding shall not be possible anymore.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_049: [If any error occurs while decoding a frame, the decoder shall call the error_callback and pass to it the callback_context, both of those being the ones given to sasl_frame_codec_create.] */
TEST_FUNCTION(when_the_second_call_for_amqpvalue_decode_bytes_fails_then_the_decoder_switches_to_an_error_state)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1);
	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).SetReturn(1);

	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_codec_error(TEST_CONTEXT));

	// act
	saved_on_frame_received(saved_callback_context, NULL, 0, test_sasl_frame_value, sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_046: [If any error occurs while decoding a frame, the decoder shall switch to an error state where decoding shall not be possible anymore.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_049: [If any error occurs while decoding a frame, the decoder shall call the error_callback and pass to it the callback_context, both of those being the ones given to sasl_frame_codec_create.] */
TEST_FUNCTION(when_amqpvalue_get_inplace_descriptor_fails_then_the_decoder_switches_to_an_error_state)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).ExpectedTimesExactly(sizeof(test_sasl_frame_value));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE))
		.SetReturn((AMQP_VALUE)NULL);

	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_codec_error(TEST_CONTEXT));

	// act
	saved_on_frame_received(saved_callback_context, NULL, 0, test_sasl_frame_value, sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_006: [Bytes 6 and 7 of the header are ignored.] */
TEST_FUNCTION(when_some_extra_type_specific_bytes_are_passed_to_the_sasl_codec_they_are_ignored)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, NULL);
	mocks.ResetAllCalls();
	unsigned char test_extra_bytes[2] = { 0x42, 0x43 };

	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).ExpectedTimesExactly(sizeof(test_sasl_frame_value));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_mechanisms_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_received(NULL, TEST_AMQP_VALUE));

	// act
	saved_on_frame_received(saved_callback_context, test_extra_bytes, sizeof(test_extra_bytes), test_sasl_frame_value, sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_007: [The extended header is ignored.] */
TEST_FUNCTION(when_type_specific_byte_count_is_more_than_2_the_sasl_frame_codec_ignores_them_and_still_succeeds)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, NULL);
	mocks.ResetAllCalls();
	unsigned char test_extra_bytes[4] = { 0x42, 0x43 };

	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).ExpectedTimesExactly(sizeof(test_sasl_frame_value));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_mechanisms_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_received(NULL, TEST_AMQP_VALUE));

	// act
	saved_on_frame_received(saved_callback_context, test_extra_bytes, sizeof(test_extra_bytes), test_sasl_frame_value, sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_008: [The maximum size of a SASL frame is defined by MIN-MAX-FRAME-SIZE.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_049: [If any error occurs while decoding a frame, the decoder shall call the error_callback and pass to it the callback_context, both of those being the ones given to sasl_frame_codec_create.] */
TEST_FUNCTION(when_a_sasl_frame_of_513_bytes_is_received_decoding_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();
	unsigned char test_extra_bytes[2] = { 0x42, 0x43 };

	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_codec_error(TEST_CONTEXT));

	// act
	saved_on_frame_received(saved_callback_context, test_extra_bytes, sizeof(test_extra_bytes), test_sasl_frame_value, TEST_MIX_MAX_FRAME_SIZE - 8 + 1);

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_008: [The maximum size of a SASL frame is defined by MIN-MAX-FRAME-SIZE.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_049: [If any error occurs while decoding a frame, the decoder shall call the error_callback and pass to it the callback_context, both of those being the ones given to sasl_frame_codec_create.] */
TEST_FUNCTION(when_a_sasl_frame_of_513_bytes_with_4_type_specific_bytes_is_received_decoding_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();
	unsigned char test_extra_bytes[4] = { 0x42, 0x43 };

	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_codec_error(TEST_CONTEXT));

	// act
	saved_on_frame_received(saved_callback_context, test_extra_bytes, sizeof(test_extra_bytes), test_sasl_frame_value, TEST_MIX_MAX_FRAME_SIZE - 10 + 1);

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_008: [The maximum size of a SASL frame is defined by MIN-MAX-FRAME-SIZE.] */
TEST_FUNCTION(when_the_frame_size_is_exactly_MIN_MAX_FRAME_SIZE_decoding_succeeds)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, NULL);
	mocks.ResetAllCalls();
	unsigned char test_extra_bytes[2] = { 0x42, 0x43 };
	unsigned char big_frame[512 - 8] = { 0x42, 0x43 };

	test_sasl_frame_value_size = sizeof(big_frame);
	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).ExpectedTimesExactly(sizeof(big_frame));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_mechanisms_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_received(NULL, TEST_AMQP_VALUE));

	// act
	saved_on_frame_received(saved_callback_context, test_extra_bytes, sizeof(test_extra_bytes), big_frame, sizeof(big_frame));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_009: [The frame body of a SASL frame MUST contain exactly one AMQP type, whose type encoding MUST have provides=�sasl-frame�.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_049: [If any error occurs while decoding a frame, the decoder shall call the error_callback and pass to it the callback_context, both of those being the ones given to sasl_frame_codec_create.] */
TEST_FUNCTION(when_not_all_bytes_are_used_for_decoding_in_a_SASL_frame_then_decoding_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();
	unsigned char test_extra_bytes[2] = { 0x42, 0x43 };

	test_sasl_frame_value_size = sizeof(test_sasl_frame_value) - 1;
	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).ExpectedTimesExactly(sizeof(test_sasl_frame_value) - 1);
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE)).IgnoreAllCalls();
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_mechanisms_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE)).IgnoreAllCalls();

	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_codec_error(TEST_CONTEXT));

	// act
	saved_on_frame_received(saved_callback_context, test_extra_bytes, sizeof(test_extra_bytes), test_sasl_frame_value, sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_009: [The frame body of a SASL frame MUST contain exactly one AMQP type, whose type encoding MUST have provides=�sasl-frame�.] */
TEST_FUNCTION(when_a_sasl_init_frame_is_received_decoding_it_succeeds)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, NULL);
	mocks.ResetAllCalls();

	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).ExpectedTimesExactly(sizeof(test_sasl_frame_value));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_mechanisms_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_init_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_received(NULL, TEST_AMQP_VALUE));

	// act
	saved_on_frame_received(saved_callback_context, NULL, 0, test_sasl_frame_value, sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_009: [The frame body of a SASL frame MUST contain exactly one AMQP type, whose type encoding MUST have provides=�sasl-frame�.] */
TEST_FUNCTION(when_a_sasl_challenge_frame_is_received_decoding_it_succeeds)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, NULL);
	mocks.ResetAllCalls();

	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).ExpectedTimesExactly(sizeof(test_sasl_frame_value));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_mechanisms_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_init_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_challenge_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_received(NULL, TEST_AMQP_VALUE));

	// act
	saved_on_frame_received(saved_callback_context, NULL, 0, test_sasl_frame_value, sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_009: [The frame body of a SASL frame MUST contain exactly one AMQP type, whose type encoding MUST have provides=�sasl-frame�.] */
TEST_FUNCTION(when_a_sasl_response_frame_is_received_decoding_it_succeeds)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, NULL);
	mocks.ResetAllCalls();

	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).ExpectedTimesExactly(sizeof(test_sasl_frame_value));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_mechanisms_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_init_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_challenge_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_response_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_received(NULL, TEST_AMQP_VALUE));

	// act
	saved_on_frame_received(saved_callback_context, NULL, 0, test_sasl_frame_value, sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_009: [The frame body of a SASL frame MUST contain exactly one AMQP type, whose type encoding MUST have provides=�sasl-frame�.] */
TEST_FUNCTION(when_a_sasl_outcome_frame_is_received_decoding_it_succeeds)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, NULL);
	mocks.ResetAllCalls();

	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).ExpectedTimesExactly(sizeof(test_sasl_frame_value));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_mechanisms_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_init_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_challenge_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_response_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_outcome_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE));
	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_received(NULL, TEST_AMQP_VALUE));

	// act
	saved_on_frame_received(saved_callback_context, NULL, 0, test_sasl_frame_value, sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_009: [The frame body of a SASL frame MUST contain exactly one AMQP type, whose type encoding MUST have provides=�sasl-frame�.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_049: [If any error occurs while decoding a frame, the decoder shall call the error_callback and pass to it the callback_context, both of those being the ones given to sasl_frame_codec_create.] */
TEST_FUNCTION(when_an_AMQP_value_that_is_not_a_sasl_frame_is_decoded_then_decoding_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	EXPECTED_CALL(mocks, amqpvalue_decode_bytes(TEST_DECODER_HANDLE, IGNORED_PTR_ARG, IGNORED_NUM_ARG))
		.ValidateArgument(1).ExpectedTimesExactly(sizeof(test_sasl_frame_value));
	STRICT_EXPECTED_CALL(mocks, amqpvalue_get_inplace_descriptor(TEST_AMQP_VALUE));
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_mechanisms_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_init_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_challenge_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_response_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);
	STRICT_EXPECTED_CALL(amqp_definitions_mocks, is_sasl_outcome_type_by_descriptor(TEST_DESCRIPTOR_AMQP_VALUE))
		.SetReturn(false);

	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_codec_error(TEST_CONTEXT));

	// act
	saved_on_frame_received(saved_callback_context, NULL, 0, test_sasl_frame_value, sizeof(test_sasl_frame_value));

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

/* Tests_SRS_SASL_FRAME_CODEC_01_010: [Receipt of an empty frame is an irrecoverable error.] */
/* Tests_SRS_SASL_FRAME_CODEC_01_049: [If any error occurs while decoding a frame, the decoder shall call the error_callback and pass to it the callback_context, both of those being the ones given to sasl_frame_codec_create.] */
TEST_FUNCTION(when_an_empty_frame_is_received_decoding_fails)
{
	// arrange
	sasl_frame_codec_mocks mocks;
	amqp_definitions_mocks amqp_definitions_mocks;
	SASL_FRAME_CODEC_HANDLE sasl_frame_codec = sasl_frame_codec_create(TEST_FRAME_CODEC_HANDLE, test_on_sasl_frame_received, test_on_sasl_frame_codec_error, TEST_CONTEXT);
	mocks.ResetAllCalls();

	STRICT_EXPECTED_CALL(mocks, test_on_sasl_frame_codec_error(TEST_CONTEXT));

	// act
	saved_on_frame_received(saved_callback_context, NULL, 0, test_sasl_frame_value, 0);

	// assert
	mocks.AssertActualAndExpectedCalls();

	// cleanup
	sasl_frame_codec_destroy(sasl_frame_codec);
}

END_TEST_SUITE(sasl_frame_codec_unittests)
