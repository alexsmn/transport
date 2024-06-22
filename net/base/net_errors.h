#pragma once

#include <boost/system/errc.hpp>

namespace net {

using Error = boost::system::error_code;

constexpr Error OK =
    boost::system::errc::make_error_code(boost::system::errc::success);
constexpr Error ERR_FAILED =
    boost::system::errc::make_error_code(boost::system::errc::io_error);
constexpr Error ERR_ABORTED = boost::system::errc::make_error_code(
    boost::system::errc::operation_canceled);
constexpr Error ERR_INVALID_ARGUMENT =
    boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
constexpr Error ERR_ACCESS_DENIED = boost::system::errc::make_error_code(
    boost::system::errc::permission_denied);
constexpr Error ERR_ADDRESS_IN_USE =
    boost::system::errc::make_error_code(boost::system::errc::address_in_use);
constexpr Error ERR_CONNECTION_CLOSED =
    boost::system::errc::make_error_code(boost::system::errc::connection_reset);
constexpr Error ERR_INVALID_HANDLE = boost::system::errc::make_error_code(
    boost::system::errc::bad_file_descriptor);
constexpr Error ERR_IO_PENDING = boost::system::errc::make_error_code(
    boost::system::errc::resource_unavailable_try_again);
constexpr Error ERR_NOT_IMPLEMENTED = boost::system::errc::make_error_code(
    boost::system::errc::function_not_supported);
constexpr Error ERR_TIMED_OUT =
    boost::system::errc::make_error_code(boost::system::errc::timed_out);

#if 0
constexpr Error ERR_CONNECTION_REFUSED =
    boost::system::errc::make_error_code(boost::system::errc::connection_refused);
constexpr Error ERR_NOT_FOUND =
    boost::system::errc::make_error_code(boost::system::errc::no_such_file_or_directory);
constexpr Error ERR_NOT_SUPPORTED =
    boost::system::errc::make_error_code(boost::system::errc::operation_not_supported);
constexpr Error ERR_FAILED =
    boost::system::errc::operation_canceled;
constexpr Error ERR_INTERNAL =
    boost::system::errc::make_error_code(boost::system::errc::bad_message);
constexpr Error ERR_FAILED_PRECONDITION =
    boost::system::errc::make_error_code(boost::system::errc::bad_address);
constexpr Error ERR_OUT_OF_RANGE =
    boost::system::errc::make_error_code(boost::system::errc::result_out_of_range);
#endif

inline std::string ErrorToString(Error error) {
  return error.message();
}

inline std::string ErrorToShortString(Error error) {
  return error.message();
}

}  // namespace net
