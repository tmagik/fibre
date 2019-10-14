

#include <fibre/basic_codecs.hpp>
#include <fibre/named_tuple.hpp>
#include <fibre/print_utils.hpp>
#include "test_utils.hpp"

#include <stdlib.h>

using namespace fibre;

struct test_case_t {
    uint8_t encoded[10];
    size_t length;
    uint32_t decoded;
};


// Feed the decoder byte by byte
template<typename TDecoder, typename TVal>
TestContext decoder_test_bytewise(TDecoder decoder_prototype, const uint8_t (&encoded)[], size_t length, TVal decoded) {
    TestContext context;
    TDecoder decoder = decoder_prototype;
    size_t processed_bytes = 0;

    static_assert(std::is_base_of<typename fibre::Decoder<TVal>, TDecoder>::value, "TDecoder must inherit Decoder<TVal>");

    TEST_EQUAL(decoder.process_bytes_({nullptr, 0}, nullptr), StreamSink::OK);
    for (size_t i = 0; i < length - 1; ++i) {
        TEST_EQUAL(decoder.process_bytes_({encoded + i, 1}, &processed_bytes), StreamSink::OK);
        TEST_EQUAL(decoder.process_bytes_({encoded + i, 0}, &processed_bytes), StreamSink::OK);
        TEST_EQUAL(processed_bytes, i + 1);
        TEST_ZERO(decoder.get());
    }
    TEST_EQUAL(decoder.process_bytes_({encoded + length - 1, 1}, &processed_bytes), StreamSink::CLOSED);
    TEST_EQUAL(processed_bytes, length);
    TEST_EQUAL(decoder.process_bytes_({encoded + length - 1, 0}, &processed_bytes), StreamSink::CLOSED);
    TEST_EQUAL(decoder.process_bytes_({encoded + length - 1, 1}, &processed_bytes), StreamSink::CLOSED);
    TEST_EQUAL(processed_bytes, length);

    TEST_NOT_NULL(decoder.get());
    TEST_EQUAL(*decoder.get(), decoded);
    
    return context;
}

// Feed the decoder in largest possible chunks. Feed it 1 more byte than
// required to try to confuse it.
template<typename TDecoder, typename TVal>
TestContext decoder_test_chunkwise(TDecoder decoder_prototype, const uint8_t (&encoded)[], size_t length, TVal decoded) {
    TestContext context;
    TDecoder decoder = decoder_prototype;
    size_t processed_bytes = 0;

    static_assert(std::is_base_of<typename fibre::Decoder<TVal>, TDecoder>::value, "TDecoder must inherit Decoder<TVal>");

    StreamSink::status_t status = StreamSink::CLOSED;
    decoder = decoder_prototype;
    uint8_t encoded_longer[length + 1];
    memcpy(encoded_longer, encoded, length);
    cbufptr_t bufptr = {encoded_longer, length + 1};

    while (bufptr.length > 1) {
        TEST_ZERO(decoder.get());
        size_t prev_length = bufptr.length;
        status = decoder.process_bytes(bufptr);
        if (status == StreamSink::OK) {
            TEST_ASSERT(prev_length > bufptr.length);
            TEST_ASSERT(bufptr.length > 1);
        } else {
            TEST_EQUAL(status, StreamSink::CLOSED);
            TEST_EQUAL(bufptr.length, (size_t)1);
        }
    }

    TEST_NOT_NULL(decoder.get());
    TEST_EQUAL(*decoder.get(), decoded);
    
    return context;
}


// Query the encoder byte by byte
template<typename TEncoder, typename TVal>
TestContext encoder_test_bytewise(TEncoder encoder_prototype, const uint8_t (&encoded)[], size_t length, TVal decoded) {
    TestContext context;
    TEncoder encoder = encoder_prototype;
    size_t generated_bytes = 0;

    static_assert(std::is_base_of<typename fibre::Encoder<TVal>, TEncoder>::value, "TEncoder must inherit Decoder<TVal>");

    TEST_ASSERT(encoder.get_bytes_({nullptr, 0}, nullptr), StreamSource::CLOSED);
    encoder.set(&decoded);

    uint8_t encoded_out[length + 1];
    for (size_t i = 0; i < length - 1; ++i) {
        TEST_EQUAL(encoder.get_bytes_({encoded_out + i, 1}, &generated_bytes), StreamSource::OK);
        TEST_EQUAL(encoder.get_bytes_({encoded_out + i, 0}, &generated_bytes), StreamSource::OK);
        TEST_EQUAL(generated_bytes, i + 1);
    }
    TEST_EQUAL(encoder.get_bytes_({encoded_out + length - 1, 1}, &generated_bytes), StreamSource::CLOSED);
    TEST_EQUAL(generated_bytes, length);
    TEST_EQUAL(encoder.get_bytes_({encoded_out + length - 1, 0}, &generated_bytes), StreamSource::CLOSED);
    TEST_EQUAL(encoder.get_bytes_({encoded_out + length - 1, 1}, &generated_bytes), StreamSource::CLOSED);
    TEST_EQUAL(generated_bytes, length);

    TEST_ZERO(memcmp(encoded, encoded_out, length));
    
    return context;
}

template<typename TDecoder, typename TVal>
TestContext codec_test(TDecoder decoder_prototype, const uint8_t (&encoded)[], size_t length, TVal decoded) {
    TestContext context;

    TEST_ADD(decoder_test_bytewise<TDecoder>(decoder_prototype, encoded, length, decoded));
    TEST_ADD(decoder_test_chunkwise<TDecoder>(decoder_prototype, encoded, length, decoded));
    
    return context;
}

TestContext varint_codec_test(const uint8_t (&encoded)[], size_t length, uint32_t decoded) {
    TestContext context;
    TEST_ADD(decoder_test_bytewise(VarintDecoder<uint32_t>(), encoded, length, decoded));
    TEST_ADD(decoder_test_chunkwise(VarintDecoder<uint32_t>(), encoded, length, decoded));
    TEST_ADD(encoder_test_bytewise(VarintEncoder<uint32_t>(), encoded, length, decoded));
    return context;
}

TestContext fixedint_le_codec_test(const uint8_t (&encoded)[], size_t length, uint32_t decoded) {
    TestContext context;
    TEST_ADD(decoder_test_bytewise(FixedIntDecoder<uint32_t, false>(), encoded, length, decoded));
    TEST_ADD(decoder_test_chunkwise(FixedIntDecoder<uint32_t, false>(), encoded, length, decoded));
    TEST_ADD(encoder_test_bytewise(FixedIntEncoder<uint32_t, false>(), encoded, length, decoded));
    return context;
}

TestContext fixedint_be_codec_test(const uint8_t (&encoded)[], size_t length, uint32_t decoded) {
    TestContext context;
    TEST_ADD(decoder_test_bytewise(FixedIntDecoder<uint32_t, true>(), encoded, length, decoded));
    TEST_ADD(decoder_test_chunkwise(FixedIntDecoder<uint32_t, true>(), encoded, length, decoded));
    TEST_ADD(encoder_test_bytewise(FixedIntEncoder<uint32_t, true>(), encoded, length, decoded));
    return context;
}

TestContext utf8_codec_test(const uint8_t (&encoded)[], size_t length, std::string decoded) {
    TestContext context;

    constexpr size_t max_size = 128;
    std::tuple<std::array<char, max_size>, size_t> decoded2;
    memcpy(std::get<0>(decoded2).data(), decoded.c_str(), std::min(max_size, decoded.size()));
    std::get<1>(decoded2) = decoded.size();
    codec_test<UTF8Decoder<char, max_size>>(UTF8Decoder<char, max_size>(), encoded, length, decoded2);

    TEST_ADD(decoder_test_bytewise(UTF8Decoder<char, max_size>(), encoded, length, decoded2));
    TEST_ADD(decoder_test_chunkwise(UTF8Decoder<char, max_size>(), encoded, length, decoded2));
    TEST_ADD(encoder_test_bytewise(UTF8Encoder<char, max_size>(), encoded, length, decoded2));
    return context;
}


int main(int argc, const char** argv) {
    TestContext context;

    test_case_t test_case; // encoded, length, decoded

    TEST_ADD(varint_codec_test({0x00}, 1, 0));
    
    TEST_ADD(varint_codec_test({0x01}, 1, 1));
    TEST_ADD(varint_codec_test({0xff, 0x01}, 2, 0xff));
    TEST_ADD(varint_codec_test({0xAC, 0x02}, 2, 300));
    TEST_ADD(varint_codec_test({0xff, 0xff, 0xff, 0xff, 0xf}, 5, 0xffffffff));

    TEST_ADD(fixedint_le_codec_test({0x00, 0x00, 0x00, 0x00}, 4, 0));
    TEST_ADD(fixedint_le_codec_test({0x12, 0x34, 0x56, 0x78}, 4, 0x78563412));
    TEST_ADD(fixedint_le_codec_test({0xFF, 0xFF, 0xFF, 0xFF}, 4, 0xFFFFFFFF));

    TEST_ADD(fixedint_be_codec_test({0x00, 0x00, 0x00, 0x00}, 4, 0));
    TEST_ADD(fixedint_be_codec_test({0x12, 0x34, 0x56, 0x78}, 4, 0x12345678));
    TEST_ADD(fixedint_be_codec_test({0xFF, 0xFF, 0xFF, 0xFF}, 4, 0xFFFFFFFF));
    TEST_ADD(fixedint_be_codec_test({0xFF, 0xFF, 0xFF, 0xFF}, 4, 0xFFFFFFFF));

    TEST_ADD(utf8_codec_test({0x03, 0x61, 0x62, 0x63}, 4, "abc"));
    // TODO: test for more strings (especially multibyte chars)

    VerboseNamedTupleDecoderV1<
        std::tuple<MAKE_SSTRING("arg1"), MAKE_SSTRING("arg2")>,
        std::tuple<uint32_t, uint32_t>> asdf{{}, {0, 0}};
        
    TEST_ADD(codec_test<>(asdf, {0x04, 0x61, 0x72, 0x67, 0x31, 0x01, 0x04, 0x61, 0x72, 0x67, 0x32, 0x02}, 12, std::tuple<uint32_t, uint32_t>(1, 2)));
    TEST_ADD(codec_test<>(asdf, {0x04, 0x61, 0x72, 0x67, 0x32, 0x02, 0x04, 0x61, 0x72, 0x67, 0x31, 0x01}, 12, std::tuple<uint32_t, uint32_t>(1, 2)));
    
    return context.summarize();
}