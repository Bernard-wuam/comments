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

namespace DeleteComments {
inline auto deleteComments = [](GetRequest &req,
                                boost::mysql::connection_pool &con)
    -> boost::asio::awaitable<
        boost::beast::http::response<boost::beast::http::string_body>,
        boost::asio::strand<boost::asio::io_context::executor_type>> {
  std::error_code ec;

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

  // server.delete_("/posts/:publicId/comments/:commentsPublicId",
  // DeleteComments::DeleteComments); get the params id i.e pubblicId;

  auto params = req.params(ec);

  // if this route does not have a parameter
  // return a json error.
  if (ec) {
    co_return JsonResponse::sendJsonBadResponse(
        req.version(), false, makeErrorCode(ServerError::Server_Failed));
  }

  auto postPublicId = params.value().at("publicId");
  auto commentPublicId = params.value().at("commentsPublicId");

  // userPublicId, is the only value inside the json object
  auto userPublicId = jwtToken.value().at("userPublicId").as_string();

  // check if the userPublicId has this post
  // before attenting to delete it.

  boost::mysql::diagnostics diag;
  boost::mysql::static_results<std::tuple<>, std::tuple<std::uint64_t>,
                               std::tuple<std::uint64_t>,
                               std::tuple<std::uint64_t, std::uint64_t>>
      userIdPublicIdResult;

  co_await connecton->async_execute(
      boost::mysql::with_params(R"(  
        START TRANSACTION;
        SELECT userId FROM users WHERE publicUserId = {0};
        SELECT id FROM posts WHERE publicId = {1};   
        SELECT comments.id,comments.userId FROM comments 
        WHERE comments.publicId = {2};     
        )",
                                userPublicId, postPublicId, commentPublicId),
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

  if (userIdPublicIdResult.rows<3>().empty()) {
    co_return JsonResponse::sendJsonBadResponse(
        req.version(), false, makeErrorCode(ServerError::Comment_Not_Found));
  }

  auto userId = std::get<0>(userIdPublicIdResult.rows<1>()[0]);
  auto commentId = std::get<0>(userIdPublicIdResult.rows<3>()[0]);

  // check if the user from the from the accestoken, is the user that has the
  // post if not send illegal comment operations

  auto fff = std::get<1>(userIdPublicIdResult.rows<3>()[0]);

  if (userId != std::get<1>(userIdPublicIdResult.rows<3>()[0])) {
    co_return JsonResponse::sendJsonBadResponse(
        req.version(), false,
        makeErrorCode(ServerError::Illegal_Comment_Operation));
  }

  boost::mysql::results result;

  co_await connecton->async_execute(boost::mysql::with_params(R"(       
        DELETE FROM comments WHERE comments.id = {};
        COMMIT;
        )",
                                                              commentId),
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
}; // namespace DeleteComments