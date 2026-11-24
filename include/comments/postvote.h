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
#include <boost/json/value.hpp>
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

namespace PostVote {
inline auto postVote = [](GetRequest &req, boost::mysql::connection_pool &con)
    -> boost::asio::awaitable<
        boost::beast::http::response<boost::beast::http::string_body>,
        boost::asio::strand<boost::asio::io_context::executor_type>> {
  std::error_code ec;

  auto jwtToken = req.verifyJwt(SECRETKEY, "server", "blog", "userInfo", ec);

  if (!jwtToken.has_value()) {
    co_return JsonResponse::sendJsonResponse(req.version(), true,
                                             boost::json::value(nullptr));
  }

  // userPublicId, is the only value inside the json object
  auto userPublicId = jwtToken.value().at("userPublicId").as_string();

  auto paramsList = req.params(ec);
  if (ec)
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Convert_Body_To_Object_Failed));

  const auto publicIdValue = paramsList.value().at("publicId");
  const auto commentPublicIdValue = paramsList.value().at("commentsPublicId");

  auto body = req.body(ec);

  if (ec) {
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Server_Failed));
  };

  auto commentVoteTypePublicId =
      std::string(body.value()["commentVoteTypePublicId"].as_string());

  boost::mysql::error_code ecc;
  auto connecton =
      co_await con.async_get_connection(boost::asio::redirect_error(ecc));

  if (ecc) {
    std::cerr << ecc.what() << std::endl;
    // log error....
    co_return JsonResponse::sendJsonBadResponse(
        req.version(), false, makeErrorCode(ServerError::Server_Failed));
  }

  boost::mysql::static_results<
      std::tuple<>, std::tuple<std::uint64_t>, std::tuple<std::uint64_t>,
      std::tuple<std::uint64_t>, std::tuple<std::uint64_t>>
      result;
  //   boost::mysql::results result;

  boost::mysql::diagnostics diag;

  co_await connecton->async_execute(
      boost::mysql::with_params(R"(
        START TRANSACTION;
        SELECT id FROM posts WHERE posts.publicId = {};
        SELECT id FROM comments WHERE comments.publicId = {};
        SELECT users.userId FROM users WHERE users.publicUserId ={};
        SELECT commentVoteTypeId FROM commentVoteType WHERE commentVoteTypePublicId = {};
        )",
                                publicIdValue, commentPublicIdValue,
                                userPublicId, commentVoteTypePublicId),
      result, boost::asio::redirect_error(ecc));

  if (ecc) {
    std::cerr << ecc.what() << std::endl;
    // log error....
    co_return JsonResponse::sendJsonBadResponse(
        req.version(), false, makeErrorCode(ServerError::Server_Failed));
  }

  if (result.rows<1>().empty()) {
    co_return JsonResponse::sendJsonBadResponse(
        req.version(), false, makeErrorCode(ServerError::Post_Not_Found));
  }

  auto postId = std::get<0>(result.rows<1>()[0]);

  if (result.rows<2>().empty()) {
    co_return JsonResponse::sendJsonBadResponse(
        req.version(), false, makeErrorCode(ServerError::Comment_Not_Found));
  }

  auto commentId = std::get<0>(result.rows<2>()[0]);

  if (result.rows<3>().empty()) {
    co_return JsonResponse::sendJsonBadResponse(
        req.version(), false, makeErrorCode(ServerError::Comment_Not_Found));
  }

  auto userId = std::get<0>(result.rows<3>()[0]);

  if (result.rows<4>().empty()) {
    co_return JsonResponse::sendJsonBadResponse(
        req.version(), false, makeErrorCode(ServerError::Server_Failed));
  }

  auto commentVoteTypeId = std::get<0>(result.rows<4>()[0]);

  boost::mysql::static_results<std::tuple<std::string>> reslt;

  co_await connecton->async_execute(boost::mysql::with_params(
                                        R"(  
                                          SELECT cvt.commentVoteTypePublicId FROM commentVoteType cvt 
                                          INNER JOIN commentVotes cv ON cv.commentVoteTypeId = cvt.commentVoteTypeId
                                          WHERE cv.userId = {} AND cv.commentId = {}; 
                                        )",
                                        userId, commentId),
                                    reslt, boost::asio::redirect_error(ecc));
  // connecton.return_without_reset();

  if (ecc) {
    std::cerr << ecc.what() << std::endl;
    // log error....
    co_return JsonResponse::sendJsonBadResponse(
        11, false, makeErrorCode(ServerError::Server_Failed));
  }

  // case one.

  if (reslt.rows<0>().empty()) {

    // if the voteTypePublicId is not found in the database
    //  we add a new publicId into the database.

    boost::mysql::static_results<std::tuple<>, std::tuple<>> reslt;

    co_await connecton->async_execute(boost::mysql::with_params(
                                          R"(  
                                                    INSERT INTO commentVotes
                                                    (userId,commentId,commentVoteTypeId) VALUES({},{},{});
                                                    COMMIT;
                                                  )",
                                          userId, commentId, commentVoteTypeId),
                                      reslt, boost::asio::redirect_error(ecc));
    connecton.return_without_reset();

    if (ecc) {
      std::cerr << ecc.what() << "error from inserting vote type into database"
                << std::endl;
      // log error....
      co_return JsonResponse::sendJsonBadResponse(
          11, false, makeErrorCode(ServerError::Server_Failed));
    }

    co_return JsonResponse::sendJsonResponse(req.version(), true,
                                             boost::json::value(true));
  }

  auto commentVoteTypePublicIdDb = std::get<0>(reslt.rows<0>()[0]);

  // case two
  //  commentVoteTypePublicIdDb i.e from the database is thesame as the
  // commentVotyTypePublicId i.e from the request body
  // we do nothing

  if (commentVoteTypePublicIdDb == commentVoteTypePublicId) {

    boost::mysql::static_results<std::tuple<>, std::tuple<>> reslt;

    co_await connecton->async_execute(boost::mysql::with_params(
                                          R"(  
                                                    DELETE FROM commentVotes WHERE commentVotes.userId = {}
                                                    AND commentVotes.commentId = {};
                                                    COMMIT;
                                                  )",
                                          userId, commentId),
                                      reslt, boost::asio::redirect_error(ecc));
    connecton.return_without_reset();

    if (ecc) {
      std::cerr << ecc.what() << "error deleting vote into database"
                << std::endl;
      // log error....
      co_return JsonResponse::sendJsonBadResponse(
          11, false, makeErrorCode(ServerError::Server_Failed));
    }

    co_return JsonResponse::sendJsonResponse(req.version(), true,
                                             boost::json::value(true));
  }

  // case three
  // if when update the data with the current value
  // of commentVoteTypeId

  {
    // we create a scope with the curly braces
    // to localize the variable reslt;
    // here we update the commentVoteTypePublicId int the data base.

    boost::mysql::static_results<std::tuple<>, std::tuple<>> reslt;

    co_await connecton->async_execute(boost::mysql::with_params(
                                          R"(  
                                                    UPDATE commentVotes cv SET cv.commentVoteTypeId = 
                                                    (SELECT commentVoteTypeId FROM commentVoteType 
                                                    WHERE commentVoteTypePublicId = {})
                                                    WHERE cv.userId = {} AND cv.commentId = {};
                                                    COMMIT;
                                                  )",
                                          commentVoteTypePublicId, userId,
                                          commentId),
                                      reslt, boost::asio::redirect_error(ecc));
    connecton.return_without_reset();

    if (ecc) {
      std::cerr << ecc.what() << "error updating vote into database"
                << std::endl;
      // log error....
      co_return JsonResponse::sendJsonBadResponse(
          11, false, makeErrorCode(ServerError::Server_Failed));
    }

    co_return JsonResponse::sendJsonResponse(req.version(), true,
                                             boost::json::value(true));
  }

  // auto executor = co_await boost::asio::this_coro::executor;

  // boost::asio::steady_timer t(executor.get_inner_executor(),
  //                             std::chrono::seconds(3));

  // co_await t.async_wait();
};
}; // namespace PostVote