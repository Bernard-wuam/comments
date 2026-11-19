#pragma once
#include "jsonresponse/jsonresponse.h"
#include "server/server.h"
#include "servererror/servererror.h"
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/mysql.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/static_results.hpp>
#include <boost/mysql/with_params.hpp>
#include <cstdint>
#include <optional>
#include <system_error>
#include <tuple>

// this function get the comment from the body of the request and
// insert it into the database

namespace PostComments {
inline auto postComments = [](GetRequest &req,
                              boost::mysql::connection_pool &con)
    -> boost::asio::awaitable<
        boost::beast::http::response<boost::beast::http::string_body>,
        boost::asio::strand<boost::asio::io_context::executor_type>> {
  std::error_code ec;
  auto body = req.body(ec);

  if (ec)
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Convert_Body_To_Object_Failed));

  auto message = body.value()["message"].as_string();

  boost::mysql::error_code ecc;
  auto connecton =
      co_await con.async_get_connection(boost::asio::redirect_error(ecc));

  if (ecc) {
    std::cerr << ecc.what() << std::endl;
    // log error....
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Server_Failed));
  }

  // check if the request has a Bearer token
  // that is check if the user is authorize to send a comment

  // auto accessToken = Security::createJwt("userInfo", payLoad, expiresIn,
  //                                        "server", "blog", SECRETKEY);

  auto jwtToken = req.verifyJwt(SECRETKEY, "server", "blog", "userInfo", ec);

  if (!jwtToken.has_value()) {
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Unauthorize_Access));
  }

  // server.post("/posts/:publicId/comments", PostComments::PostComments);
  // get the params id i.e pubblicId;

  auto params = req.params(ec);

  // if this route does not have a parameter
  // return a json error.
  if (ec) {
    co_return JsonResponse::sendJsonBadResponse(
        req.version(), false, makeErrorCode(ServerError::Server_Failed));
  }

  auto postPublicId = params.value().at("publicId");

  // userPublicId, is the only value inside the json object
  auto userPublicId = jwtToken.value().at("userPublicId").as_string();

  // check if the user and the post exist in the data base
  // then insert the post into the database.

  boost::mysql::diagnostics diag;
  boost::mysql::static_results<std::tuple<>, std::tuple<std::uint64_t>,
                               std::tuple<uint64_t>>
      userIdPublicIdResult;

  co_await connecton->async_execute(
      boost::mysql::with_params(R"(  
        START TRANSACTION;
        SELECT userId FROM users WHERE publicUserId = {};
        SELECT id FROM posts WHERE publicId = {};       
        )",
                                userPublicId, postPublicId),
      userIdPublicIdResult, boost::asio::redirect_error(ecc));

  if (ecc) {
    if (!diag.server_message().empty()) {
      std::cout << diag.client_message() << std::endl;
    }
    std::cerr << ecc.what() << " fro.1" << std::endl;
    // log error....
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Server_Failed));
  }

  if (userIdPublicIdResult.rows<1>().empty()) {
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::User_Not_Found));
  }

  if (userIdPublicIdResult.rows<2>().empty()) {
    co_return JsonResponse::sendJsonBadResponse(
        req.version(), false, makeErrorCode(ServerError::Post_Not_Found));
  }

  auto userId = std::get<0>(userIdPublicIdResult.rows<1>()[0]);
  auto postId = std::get<0>(userIdPublicIdResult.rows<2>()[0]);

  boost::mysql::results result;

  co_await connecton->async_execute(boost::mysql::with_params(R"(       
        INSERT INTO comments(userId,postId,message) VALUES({},{},{});
        COMMIT;
        )",
                                                              userId, postId,
                                                              message),
                                    result, boost::asio::redirect_error(ecc));
  connecton.return_without_reset();

  if (ecc) {
    if (!diag.server_message().empty()) {
      std::cout << diag.client_message() << std::endl;
    }
    std::cerr << ecc.what() << std::endl;
    // log error....
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Server_Failed));
  }

  if (ecc) {
    std::cerr << ecc.what() << std::endl;
    // log error....
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Server_Failed));
  }

  // simulate a delay

  //   auto executor = co_await boost::asio::this_coro::executor;

  //   boost::asio::steady_timer t(executor.get_inner_executor(),
  //                               std::chrono::seconds(1));

  //   co_await t.async_wait();

  co_return JsonResponse::sendJsonResponse(
      11, true, boost::json::object{{"inserted", "true"}});
};
}; // namespace PostComments