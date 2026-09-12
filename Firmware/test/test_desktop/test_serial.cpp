#include <unity.h>

#include "fakes/FakeSerial.h"

// Verifies that begin records the selected baud rate and can be called again.
void test_serial_begin_records_baud_rate()
{
  FakeSerial serial;

  serial.begin(115200);
  serial.begin(9600);

  TEST_ASSERT_EQUAL_UINT(2, serial.beginCallCount);
  TEST_ASSERT_EQUAL_UINT32(9600, serial.lastBaudRate);
}

// Verifies output is safely discarded when no USB host is connected.
void test_serial_disconnected_does_not_block_or_record_output()
{
  FakeSerial serial;
  serial.connected = false;

  TEST_ASSERT_FALSE(serial.isConnected());
  TEST_ASSERT_EQUAL_UINT(0, serial.print("ignored"));
  TEST_ASSERT_EQUAL_UINT(0, serial.println(42));
  TEST_ASSERT_EQUAL_INT(0, serial.printf("ignored %d", 7));
  TEST_ASSERT_EQUAL_STRING("", serial.output.c_str());
  TEST_ASSERT_EQUAL_UINT(0, serial.writeCallCount);
}

// Verifies output resumes when a disconnected endpoint becomes available.
void test_serial_reconnects_after_starting_disconnected()
{
  FakeSerial serial;
  serial.connected = false;

  TEST_ASSERT_EQUAL_UINT(0, serial.println("ignored"));
  serial.connected = true;

  TEST_ASSERT_TRUE(serial.isConnected());
  TEST_ASSERT_EQUAL_UINT(5, serial.println("work"));
  TEST_ASSERT_EQUAL_STRING("work\n", serial.output.c_str());
}

// Verifies output stops during disconnection and resumes after reconnection.
void test_serial_disconnect_and_reconnect_preserve_state_safely()
{
  FakeSerial serial;

  TEST_ASSERT_TRUE(serial.isConnected());
  TEST_ASSERT_EQUAL_UINT(6, serial.println("first"));

  serial.connected = false;
  TEST_ASSERT_FALSE(serial.isConnected());
  TEST_ASSERT_EQUAL_UINT(0, serial.println("ignored"));

  serial.connected = true;
  TEST_ASSERT_TRUE(serial.isConnected());
  TEST_ASSERT_EQUAL_UINT(7, serial.println("second"));
  TEST_ASSERT_EQUAL_STRING("first\nsecond\n", serial.output.c_str());
  TEST_ASSERT_EQUAL_UINT(2, serial.writeCallCount);
}

// Verifies that print and println preserve the exact order of writes.
void test_serial_print_and_println_preserve_output_order()
{
  FakeSerial serial;

  serial.print("first");
  serial.println("-line");
  serial.print("second");
  serial.println();

  TEST_ASSERT_EQUAL_STRING("first-line\nsecond\n", serial.output.c_str());
  TEST_ASSERT_EQUAL_UINT(4, serial.writeCallCount);
}

// Verifies that printf uses normal formatting and participates in write order.
void test_serial_printf_preserves_formatting_and_order()
{
  FakeSerial serial;

  serial.print("value=");
  serial.printf("%d/%s", 7, "ready");
  serial.println("!");

  TEST_ASSERT_EQUAL_STRING("value=7/ready!\n", serial.output.c_str());
}

// Verifies signed, unsigned, boolean, and floating-point output.
void test_serial_print_numeric_values()
{
  FakeSerial serial;

  serial.print(-12);
  serial.print("|");
  serial.print(static_cast<unsigned int>(34));
  serial.print("|");
  serial.print(static_cast<long>(-56));
  serial.print("|");
  serial.print(static_cast<unsigned long>(78));
  serial.print("|");
  serial.print(true);
  serial.print("|");
  serial.print(false);
  serial.print("|");
  serial.print(3.14159);
  serial.print("|");
  serial.println(static_cast<double>(2.5));

  TEST_ASSERT_EQUAL_STRING("-12|34|-56|78|1|0|3.14|2.50\n",
                           serial.output.c_str());
}

// Verifies println adds exactly one line ending for numeric values.
void test_serial_println_numeric_values_add_one_line_ending()
{
  FakeSerial serial;

  serial.println(-1);
  serial.println(static_cast<unsigned long>(0));
  serial.println(2.75);

  TEST_ASSERT_EQUAL_STRING("-1\n0\n2.75\n", serial.output.c_str());
}

// Verifies empty and null strings are safe and produce no unexpected text.
void test_serial_string_edge_cases_are_safe()
{
  FakeSerial serial;

  serial.print("");
  serial.println("");
  serial.print(nullptr);
  serial.println(nullptr);

  TEST_ASSERT_EQUAL_STRING("\n\n", serial.output.c_str());
}

// Verifies printf handles a null format pointer without changing output.
void test_serial_printf_null_format_is_ignored()
{
  FakeSerial serial;

  TEST_ASSERT_EQUAL_INT(0, serial.printf(nullptr));
  TEST_ASSERT_EQUAL_STRING("", serial.output.c_str());
}

// Verifies failed printf formatting does not append an unrelated line.
void test_serial_printf_empty_format_is_ordered()
{
  FakeSerial serial;

  serial.print("before");
  serial.printf("");
  serial.println("after");

  TEST_ASSERT_EQUAL_STRING("beforeafter\n", serial.output.c_str());
}
