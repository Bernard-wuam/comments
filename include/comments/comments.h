#pragma once
#include "jsonresponse/jsonresponse.h"
#include "server/server.h"
#include "servererror/servererror.h"
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json.hpp>
#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/mysql.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/static_results.hpp>
#include <boost/mysql/with_params.hpp>
#include <optional>
#include <system_error>
#include <tuple>

namespace Comments {
inline auto getComments = [](GetRequest &req,
                             boost::mysql::connection_pool &con)
    -> boost::asio::awaitable<
        boost::beast::http::response<boost::beast::http::string_body>,
        boost::asio::strand<boost::asio::io_context::executor_type>> {
  std::error_code ec;
  auto paramsList = req.params(ec);
  if (ec)
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Convert_Body_To_Object_Failed));
  const auto publicIdValue = paramsList.value().at("publicId");

  boost::mysql::error_code ecc;
  auto connecton =
      co_await con.async_get_connection(boost::asio::redirect_error(ecc));

  if (ecc) {
    std::cerr << ecc.what() << std::endl;
    // log error....
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Server_Failed));
  }

  boost::mysql::static_results<std::tuple<>, std::tuple<std::uint64_t>> result;
  //   boost::mysql::results result;

  boost::mysql::diagnostics diag;

  co_await connecton->async_execute(boost::mysql::with_params(R"(
        START TRANSACTION;
        SELECT id FROM posts WHERE posts.publicId = {};
        )",
                                                              publicIdValue),
                                    result, boost::asio::redirect_error(ecc));

  if (ecc) {
    std::cerr << ecc.what() << std::endl;
    // log error....
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Post_Not_Found));
  }

  auto f = std::get<0>(result.rows<1>().front());

  boost::mysql::results reslt;

  co_await connecton->async_execute(boost::mysql::with_params(R"(       
         
SELECT JSON_ARRAYAGG(
	JSON_OBJECT(
    "message",userComment.message,
    "commentCreatedAt",userComment.commentCreatedAt,
    "commentPublicId",userComment.commentPublicId,
    "userPublicId",userComment.publicUserId,
    "userName",userComment.userName,
    "profileImage",userComment.profileImage,
    "email",userComment.email)
) AS userCommentJson FROM
 (
 SELECT c.message,
  c.createdAt as commentCreatedAt,
  c.publicId as commentPublicId,
  u.userName,
  u.publicUserId,
  u.profileImage,
  u.email FROM comments c 
  INNER JOIN users u  
  ON c.userId = u.userId
  WHERE c.postId = {} ORDER BY  c.createdAt DESC,c.publicId DESC) AS userComment;  
  COMMIT;
        )",
                                                              f),
                                    reslt, boost::asio::redirect_error(ecc));
  connecton.return_without_reset();

  if (ecc) {
    std::cerr << ecc.what() << std::endl;
    // log error....
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Server_Failed));
  }

  if (reslt.rows().at(0).at(0).is_null()) {
    co_return JsonResponse::sendJsonResponse(11, true, boost::json::array());
  }

  auto val = reslt.rows().at(0).at(0).as_string();
  auto resVal = boost::json::parse(val, ecc);

  if (ecc) {
    std::cerr << ecc.what() << std::endl;
    // log error....
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Server_Failed));
  }

  // auto executor = co_await boost::asio::this_coro::executor;

  // boost::asio::steady_timer t(executor.get_inner_executor(),
  //                             std::chrono::seconds(3));

  // co_await t.async_wait();

  co_return JsonResponse::sendJsonResponse(11, true, resVal.as_array());
};
}; // namespace Comments