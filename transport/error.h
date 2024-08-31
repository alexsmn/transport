#pragma once

#include <boost/system/errc.hpp>

namespace transport {

using error_code = boost::system::error_code;

constexpr error_code OK =
    boost::system::errc::make_error_code(boost::system::errc::success);
constexpr error_code ERR_FAILED =
    boost::system::errc::make_error_code(boost::system::errc::io_error);
constexpr error_code ERR_ABORTED = boost::system::errc::make_error_code(
    boost::system::errc::operation_canceled);
constexpr error_code ERR_INVALID_ARGUMENT =
    boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
constexpr error_code ERR_ACCESS_DENIED = boost::system::errc::make_error_code(
    boost::system::errc::permission_denied);
constexpr error_code ERR_ADDRESS_IN_USE =
    boost::system::errc::make_error_code(boost::system::errc::address_in_use);
constexpr error_code ERR_CONNECTION_CLOSED =
    boost::system::errc::make_error_code(boost::system::errc::connection_reset);
constexpr error_code ERR_INVALID_HANDLE = boost::system::errc::make_error_code(
    boost::system::errc::bad_file_descriptor);
constexpr error_code ERR_IO_PENDING = boost::system::errc::make_error_code(
    boost::system::errc::resource_unavailable_try_again);
constexpr error_code ERR_NOT_IMPLEMENTED = boost::system::errc::make_error_code(
    boost::system::errc::function_not_supported);
constexpr error_code ERR_TIMED_OUT =
    boost::system::errc::make_error_code(boost::system::errc::timed_out);
constexpr error_code ERR_SSL_BAD_PEER_PUBLIC_KEY =
    boost::system::errc::make_error_code(boost::system::errc::bad_message);

#if 0
constexpr error_code ERR_CONNECTION_REFUSED =
    boost::system::errc::make_error_code(boost::system::errc::connection_refused);
constexpr error_code ERR_NOT_FOUND =
    boost::system::errc::make_error_code(boost::system::errc::no_such_file_or_directory);
constexpr error_code ERR_NOT_SUPPORTED =
    boost::system::errc::make_error_code(boost::system::errc::operation_not_supported);
constexpr error_code ERR_FAILED =
    boost::system::errc::operation_canceled;
constexpr error_code ERR_INTERNAL =
    boost::system::errc::make_error_code(boost::system::errc::bad_message);
constexpr error_code ERR_FAILED_PRECONDITION =
    boost::system::errc::make_error_code(boost::system::errc::bad_address);
constexpr error_code ERR_OUT_OF_RANGE =
    boost::system::errc::make_error_code(boost::system::errc::result_out_of_range);
#endif

inline std::string ErrorToString(error_code error) {
  return error.message();
}

inline std::string ErrorToShortString(error_code error) {
  return error.message();
}

}  // namespace transport
