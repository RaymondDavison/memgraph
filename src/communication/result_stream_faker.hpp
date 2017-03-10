#pragma once

#include "logging/default.hpp"
#include "query/backend/cpp/typed_value.hpp"
#include "utils/assert.hpp"

/**
 * A mocker for the data output record stream.
 * This implementation checks that messages are
 * sent to it in an acceptable order, and tracks
 * the content of those messages.
 */
class ResultStreamFaker {
 public:

  void Header(const std::vector<std::string> &fields) {
    debug_assert(current_state_ == State::Start, "Headers can only be written in the beginning");
    header_ = fields;
    current_state_ = State::WritingResults;
  }

  void Result(const std::vector<TypedValue> &values) {
    debug_assert(current_state_ == State::WritingResults, "Can't accept results before header nor after summary");
    results_.push_back((values));
  }

  void Summary(const std::map<std::string, TypedValue> &summary) {
    debug_assert(current_state_ != State::Done, "Can only send a summary once");
    summary_ = summary;
    current_state_ = State::Done;
  }

  const auto &GetHeader() {
    debug_assert(current_state_ != State::Start, "Header not written");
    return header_;
  }

  const auto &GetResults() { return results_; }

  const auto &GetSummary() {
    debug_assert(current_state_ == State::Done, "Summary not written");
    return summary_;
  }

 private:

  /**
   * Possible states of the Mocker. Used for checking if calls to
   * the Mocker as in acceptable order.
   */
  enum class State {
    Start,
    WritingResults,
    Done
  };

  // the current state
  State current_state_ = State::Start;

  // the data that the record stream can accept
  std::vector<std::string> header_;
  std::vector<std::vector<TypedValue>> results_;
  std::map<std::string, TypedValue> summary_;
};
