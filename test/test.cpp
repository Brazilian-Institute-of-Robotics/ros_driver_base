#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MAIN
#define BOOST_TEST_MODULE "iodrivers"
#define BOOST_AUTO_TEST_MAIN
#include <boost/test/auto_unit_test.hpp>
#include <boost/test/unit_test.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <iodrivers_base/Driver.hpp>
#include <iostream>
using namespace std;
using namespace iodrivers_base;

class DriverTest : public Driver
{
public:
    DriverTest()
        : Driver(100) {}

    int extractPacket(uint8_t const* buffer, size_t buffer_size) const
    {
        if (buffer[0] != 0)
            return -1;
        else if (buffer_size < 4)
            return 0;
        else if (buffer[3] == 0)
            return 4;
        else
            return -4;
    }
};

int setupDriver(Driver& driver)
{
    int pipes[2];
    pipe(pipes);
    int rx = pipes[0];
    int tx = pipes[1];

    long fd_flags = fcntl(rx, F_GETFL);
    fcntl(rx, F_SETFL, fd_flags | O_NONBLOCK);

    driver.setFileDescriptor(rx, true);
    return tx;
}

void writeToDriver(Driver& driver, int tx, uint8_t* data, int size)
{
    if (driver.isValid())
        write(tx, data, size);
    else
        driver.pushInputRaw(data, size);
}


BOOST_AUTO_TEST_CASE(test_FileGuard)
{
    int tx = open("/dev/zero", O_RDONLY);
    BOOST_REQUIRE( tx != -1 );

    { FileGuard guard(tx); }
    BOOST_REQUIRE_EQUAL(-1, close(tx));
    BOOST_REQUIRE_EQUAL(EBADF, errno);
}

void common_rx_timeout(DriverTest& test, int tx)
{
    uint8_t buffer[100];
    BOOST_REQUIRE_THROW(test.readPacket(buffer, 100, 10), TimeoutError);

    uint8_t data[1] = { 'a' };
    writeToDriver(test, tx, data, 1);
    BOOST_REQUIRE_THROW(test.readPacket(buffer, 100, 10), TimeoutError);
}
BOOST_AUTO_TEST_CASE(test_rx_timeout)
{
    DriverTest test;
    int tx = setupDriver(test);
    FileGuard tx_guard(tx);
    common_rx_timeout(test, tx);
}
BOOST_AUTO_TEST_CASE(test_rx_timeout_raw_channel)
{
    DriverTest test;
    common_rx_timeout(test, 0);
}

BOOST_AUTO_TEST_CASE(test_rx_first_byte_timeout)
{
    DriverTest test;
    int tx = setupDriver(test);
    FileGuard tx_guard(tx);

    uint8_t buffer[100];
    try
    {
        test.readPacket(buffer, 100, 10, 1);
        BOOST_REQUIRE(false);
    }
    catch(TimeoutError const& e)
    {
        BOOST_REQUIRE_EQUAL(TimeoutError::FIRST_BYTE, e.type);
    }

    write(tx, "a", 1);
    try
    {
        test.readPacket(buffer, 100, 10, 1);
        BOOST_REQUIRE(false);
    }
    catch(TimeoutError const& e)
    {
        BOOST_REQUIRE_EQUAL(TimeoutError::PACKET, e.type);
    }

    try
    {
        test.readPacket(buffer, 100, 10, 1);
        BOOST_REQUIRE(false);
    }
    catch(TimeoutError const& e)
    {
        BOOST_REQUIRE_EQUAL(TimeoutError::FIRST_BYTE, e.type);
    }
}

BOOST_AUTO_TEST_CASE(test_open_sets_nonblock)
{
    DriverTest test;

    int pipes[2];
    pipe(pipes);
    int rx = pipes[0];
    int tx = pipes[1];
    test.setFileDescriptor(rx, true);

    FileGuard tx_guard(tx);

    uint8_t buffer[100];
    BOOST_REQUIRE_THROW(test.readPacket(buffer, 100, 10), TimeoutError);

    write(tx, "a", 1);
    BOOST_REQUIRE_THROW(test.readPacket(buffer, 100, 10), TimeoutError);
}

void common_rx_first_packet_extraction(Driver& test, int tx)
{
    uint8_t buffer[100];
    uint8_t msg[4] = { 0, 'a', 'b', 0 };
    writeToDriver(test, tx, msg, 4);
    BOOST_REQUIRE_EQUAL(4, test.readPacket(buffer, 100, 10));
    BOOST_REQUIRE_EQUAL(0, test.getStats().tx);
    BOOST_REQUIRE_EQUAL(4, test.getStats().good_rx);
    BOOST_REQUIRE_EQUAL(0, test.getStats().bad_rx);
    BOOST_REQUIRE( !memcmp(msg, buffer, 4) );
}
BOOST_AUTO_TEST_CASE(test_rx_first_packet_extraction)
{
    DriverTest test;
    int tx = setupDriver(test);
    FileGuard tx_guard(tx);
    common_rx_first_packet_extraction(test, tx);
}
BOOST_AUTO_TEST_CASE(test_rx_first_packet_extraction_raw_channel)
{
    DriverTest test;
    common_rx_first_packet_extraction(test, 0);
}

void common_rx_partial_packets(Driver& test, int tx)
{
    uint8_t buffer[100];
    uint8_t msg[4] = { 0, 'a', 'b', 0 };
    writeToDriver(test, tx, msg, 2);
    BOOST_REQUIRE_THROW(test.readPacket(buffer, 100, 10), TimeoutError);
    writeToDriver(test, tx, msg + 2, 2);
    BOOST_REQUIRE_EQUAL(4, test.readPacket(buffer, 100, 10));
    BOOST_REQUIRE_EQUAL(0, test.getStats().tx);
    BOOST_REQUIRE_EQUAL(4, test.getStats().good_rx);
    BOOST_REQUIRE_EQUAL(0, test.getStats().bad_rx);
    BOOST_REQUIRE( !memcmp(msg, buffer, 4) );

    writeToDriver(test, tx, msg, 4);
    BOOST_REQUIRE_EQUAL(4, test.readPacket(buffer, 100, 10));
    BOOST_REQUIRE_EQUAL(0, test.getStats().tx);
    BOOST_REQUIRE_EQUAL(8, test.getStats().good_rx);
    BOOST_REQUIRE_EQUAL(0, test.getStats().bad_rx);
    BOOST_REQUIRE( !memcmp(msg, buffer, 4) );
}
BOOST_AUTO_TEST_CASE(test_rx_partial_packets)
{
    DriverTest test;
    int tx = setupDriver(test);
    FileGuard tx_guard(tx);
    common_rx_partial_packets(test, tx);
}
BOOST_AUTO_TEST_CASE(test_rx_partial_packets_raw_channel)
{
    DriverTest test;
    common_rx_partial_packets(test, 0);
}

void common_rx_garbage_removal(Driver& test, int tx)
{
    uint8_t buffer[100];
    uint8_t msg[16] = { 'g', 'a', 'r', 'b', 0, 'a', 'b', 0, 'b', 'a', 'g', 'e', 0, 'c', 'd', 0 };
    writeToDriver(test, tx, msg, 3);
    BOOST_REQUIRE_THROW(test.readPacket(buffer, 100, 10), TimeoutError);
    BOOST_REQUIRE_EQUAL(0, test.getStats().tx);
    BOOST_REQUIRE_EQUAL(0, test.getStats().good_rx);
    BOOST_REQUIRE_EQUAL(3, test.getStats().bad_rx);
    writeToDriver(test, tx, msg + 3, 3);
    BOOST_REQUIRE_THROW(test.readPacket(buffer, 100, 10), TimeoutError);
    BOOST_REQUIRE_EQUAL(0, test.getStats().tx);
    BOOST_REQUIRE_EQUAL(0, test.getStats().good_rx);
    BOOST_REQUIRE_EQUAL(4, test.getStats().bad_rx);
    writeToDriver(test, tx, msg + 6, 3);
    BOOST_REQUIRE_EQUAL(4, test.readPacket(buffer, 100, 10));
    BOOST_REQUIRE_EQUAL(0, test.getStats().tx);
    BOOST_REQUIRE_EQUAL(4, test.getStats().good_rx);
    BOOST_REQUIRE_EQUAL(4, test.getStats().bad_rx);
    BOOST_REQUIRE( !memcmp(msg + 4, buffer, 4) );

    writeToDriver(test, tx, msg + 9, 7);
    BOOST_REQUIRE_EQUAL(4, test.readPacket(buffer, 100, 10));
    BOOST_REQUIRE_EQUAL(0, test.getStats().tx);
    BOOST_REQUIRE_EQUAL(8, test.getStats().good_rx);
    BOOST_REQUIRE_EQUAL(8, test.getStats().bad_rx);
    BOOST_REQUIRE( !memcmp(msg + 12, buffer, 4) );
}
BOOST_AUTO_TEST_CASE(test_rx_garbage_removal)
{
    DriverTest test;
    int tx = setupDriver(test);
    FileGuard tx_guard(tx);
    common_rx_garbage_removal(test, tx);
}
BOOST_AUTO_TEST_CASE(test_rx_garbage_removal_raw_channel)
{
    DriverTest test;
    common_rx_garbage_removal(test, 0);
}

void common_rx_packet_extraction_mode(Driver& test, int tx)
{
    uint8_t buffer[100];
    uint8_t msg[16] = { 'g', 'a', 'r', 'b', 0, 'a', 'b', 0, 'b', 'a', 'g', 'e', 0, 'c', 'd', 0 };
    writeToDriver(test, tx, msg, 16);
    test.setExtractLastPacket(false);

    BOOST_REQUIRE_EQUAL(4, test.readPacket(buffer, 100, 10));
    BOOST_REQUIRE_EQUAL(0, test.getStats().tx);
    BOOST_REQUIRE_EQUAL(4, test.getStats().good_rx);
    BOOST_REQUIRE_EQUAL(4, test.getStats().bad_rx);
    BOOST_REQUIRE( !memcmp(msg + 4, buffer, 4) );
    BOOST_REQUIRE_EQUAL(4, test.readPacket(buffer, 100, 10));
    BOOST_REQUIRE_EQUAL(0, test.getStats().tx);
    BOOST_REQUIRE_EQUAL(8, test.getStats().good_rx);
    BOOST_REQUIRE_EQUAL(8, test.getStats().bad_rx);
    BOOST_REQUIRE( !memcmp(msg + 12, buffer, 4) );
    BOOST_REQUIRE_EQUAL(8, test.getStats().good_rx);
    BOOST_REQUIRE_EQUAL(8, test.getStats().bad_rx);

    writeToDriver(test, tx, msg, 16);
    test.setExtractLastPacket(true);

    BOOST_REQUIRE_EQUAL(4, test.readPacket(buffer, 100, 10));
    BOOST_REQUIRE_EQUAL(0, test.getStats().tx);
    // 16 bytes: even though one package has not been returned, it should still
    // be counted
    BOOST_REQUIRE_EQUAL(16, test.getStats().good_rx);
    BOOST_REQUIRE_EQUAL(16, test.getStats().bad_rx);
    BOOST_REQUIRE( !memcmp(msg + 12, buffer, 4) );

    writeToDriver(test, tx, msg, 16);
    test.setExtractLastPacket(false);
    BOOST_REQUIRE_EQUAL(4, test.readPacket(buffer, 100, 10));
    BOOST_REQUIRE_EQUAL(0, test.getStats().tx);
    BOOST_REQUIRE_EQUAL(20, test.getStats().good_rx);
    BOOST_REQUIRE_EQUAL(20, test.getStats().bad_rx);
    BOOST_REQUIRE( !memcmp(msg + 4, buffer, 4) );
    writeToDriver(test, tx, msg, 14);
    // We have now one packet from the first write and one packet from the 2nd
    // write. We should get the packet from the second write
    test.setExtractLastPacket(true);
    BOOST_REQUIRE_EQUAL(4, test.readPacket(buffer, 100, 10));
    BOOST_REQUIRE_EQUAL(0, test.getStats().tx);
    BOOST_REQUIRE_EQUAL(28, test.getStats().good_rx);
    if (test.isValid())
        BOOST_REQUIRE_EQUAL(32, test.getStats().bad_rx);
    else
        BOOST_REQUIRE_EQUAL(36, test.getStats().bad_rx);
    BOOST_REQUIRE( !memcmp(msg + 4, buffer, 4) );

    if (test.isValid())
    {
        // The garbage that was at the end of the second write should have been
        // removed as well
        BOOST_REQUIRE_EQUAL(-1, read(test.getFileDescriptor(), buffer, 1));
        BOOST_REQUIRE_EQUAL(EAGAIN, errno);
        writeToDriver(test, tx, msg + 14, 2);
        BOOST_REQUIRE_EQUAL(4, test.readPacket(buffer, 100, 10));
        BOOST_REQUIRE( !memcmp(msg + 12, buffer, 4) );
    }
}
BOOST_AUTO_TEST_CASE(test_rx_packet_extraction_mode)
{
    DriverTest test;
    int tx = setupDriver(test);
    FileGuard tx_guard(tx);
    common_rx_packet_extraction_mode(test, tx);
}
BOOST_AUTO_TEST_CASE(test_rx_packet_extraction_mode_raw_channel)
{
    DriverTest test;
    common_rx_packet_extraction_mode(test, 0);
}
