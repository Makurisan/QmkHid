
#include <format> // For std::format
#include <mpack.h>
#include "msgpack.h"
#include "hid.h"

void mpack_assert_fail(const char* message) {
    printf("MessagePack assertion failed: %s\n", message);
    while(1) {} // Halt on assertion failure
}

// Implementation
void init_msgpack(msgpack_t * km) {
    km->count = 0;
    // Initialize all pairs to 0
    memset(km->pairs, 0, sizeof(msgpack_pair_t) * MSGPACK_PAIR_ARRAY_SIZE);
}

// Helper function to add a pair
bool add_msgpack_pair(msgpack_t * km, uint8_t key, uint8_t value) {
    if (km->count >= 10) return false;  // Array full

    km->pairs[km->count].key = key;
    km->pairs[km->count].value = value;
    km->count++;
    return true;
}

void send_msgpack(msgpack_t * km) {
    char buffer[RAW_EPSIZE];
    mpack_writer_t writer;

    mpack_writer_init(&writer, buffer, sizeof(buffer));

    // Write format identifier string "MPACK"
    mpack_write_cstr(&writer, "QMV1");

    // Start writing map with number of pairs
    mpack_start_map(&writer, km->count);
    // Loop through all pairs
    for (size_t i = 0; i < km->count; i++) {
        mpack_write_uint(&writer, km->pairs[i].key);
        mpack_write_uint(&writer, km->pairs[i].value);
    }

    mpack_finish_map(&writer);
    mpack_finish_array(&writer);

    if (mpack_writer_destroy(&writer) == mpack_ok) {
        //raw_hid_send((uint8_t*)buffer, RAW_EPSIZE);
        printf("Sent %d key-value pairs\n", km->count);
    }
}

bool read_msgpack(msgpack_t * km, std::vector<uint8_t>& data) {
    mpack_reader_t reader;
    bool success = false;

    // Read raw HID data
    if (data.size() < RAW_EPSIZE) return false;

    mpack_reader_init_data(&reader, (char*)data.data(), data.size());

    // Check format identifier
    char format[5];
    mpack_expect_cstr(&reader, format, sizeof(format));
    if (strcmp(format, "QMV1") != 0) {
        printf("Invalid format identifier\n");
        mpack_reader_destroy(&reader);
        return false;
    }

    // Read map
    uint32_t count = mpack_expect_map(&reader);
    if (count > MSGPACK_PAIR_ARRAY_SIZE) {
        printf("Too many pairs for the given buffer received: %lu\n", count);
        mpack_reader_destroy(&reader);
        return false;
    }

    // Initialize msgpack structure
    init_msgpack(km);

    // Read all key-value pairs
    for (uint32_t i = 0; i < count; i++) {
        uint8_t key = mpack_expect_uint(&reader);
        uint8_t value = mpack_expect_uint(&reader);
        add_msgpack_pair(km, key, value);
    }

    mpack_done_map(&reader);
    printf("Received %d key-value pairs\n", km->count);
    mpack_reader_destroy(&reader);

    return true;
}

bool msgpack_log(msgpack_t* km) {
    std::string outmsg;

    for (uint32_t i = 0; i < km->count; i++) {
        outmsg += std::format("Key: {}, Value: {}\n", km->pairs[i].key, km->pairs[i].value);
    }


    OutputDebugString(outmsg.c_str());

    return true;
}
