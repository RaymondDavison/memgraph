#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "communication/rpc/server.hpp"
#include "database/graph_db.hpp"
#include "database/graph_db_accessor.hpp"
#include "distributed/plan_consumer.hpp"
#include "query/context.hpp"
#include "query/frontend/semantic/symbol_table.hpp"
#include "query/interpret/frame.hpp"
#include "query/parameters.hpp"
#include "query/plan/operator.hpp"
#include "query/typed_value.hpp"
#include "transactions/engine.hpp"
#include "transactions/type.hpp"

namespace distributed {

/// Handles the execution of a plan on the worker, requested by the remote
/// master. Assumes that (tx_id, plan_id) uniquely identifies an execution, and
/// that there will never be parallel requests for the same execution thus
/// identified.
class ProduceRpcServer {
  /// Encapsulates a Cursor execution in progress. Can be used for pulling a
  /// single result from the execution, or pulling all and accumulating the
  /// results. Accumulations are used for synchronizing updates in distributed
  /// MG (see query::plan::Synchronize).
  class OngoingProduce {
   public:
    OngoingProduce(database::GraphDb &db, tx::TransactionId tx_id,
                   std::shared_ptr<query::plan::LogicalOperator> op,
                   query::SymbolTable symbol_table, Parameters parameters,
                   std::vector<query::Symbol> pull_symbols);

    /// Returns a vector of typed values (one for each `pull_symbol`), and an
    /// indication of the pull result. The result data is valid only if the
    /// returned state is CURSOR_IN_PROGRESS.
    std::pair<std::vector<query::TypedValue>, PullState> Pull();

    /// Accumulates all the frames pulled from the cursor and returns
    /// CURSOR_EXHAUSTED. If an error occurs, an appropriate value is returned.
    PullState Accumulate();

   private:
    database::GraphDbAccessor dba_;
    query::Context context_;
    std::vector<query::Symbol> pull_symbols_;
    query::Frame frame_;
    PullState cursor_state_{PullState::CURSOR_IN_PROGRESS};
    std::vector<std::vector<query::TypedValue>> accumulation_;
    std::unique_ptr<query::plan::Cursor> cursor_;

    /// Pulls and returns a single result from the cursor.
    std::pair<std::vector<query::TypedValue>, PullState> PullOneFromCursor();
  };

 public:
  ProduceRpcServer(database::GraphDb &db, tx::Engine &tx_engine,
                   communication::rpc::Server &server,
                   const distributed::PlanConsumer &plan_consumer);

  /// Finish and clear ongoing produces for all plans that are tied to a
  /// transaction with tx_id.
  void FinishAndClearOngoingProducePlans(tx::TransactionId tx_id);

 private:
  std::mutex ongoing_produces_lock_;
  /// Mapping of (tx id, plan id) to OngoingProduce.
  std::map<std::pair<tx::TransactionId, int64_t>, OngoingProduce>
      ongoing_produces_;
  database::GraphDb &db_;
  communication::rpc::Server &produce_rpc_server_;
  const distributed::PlanConsumer &plan_consumer_;
  tx::Engine &tx_engine_;

  /// Gets an ongoing produce for the given pull request. Creates a new one if
  /// there is none currently existing.
  OngoingProduce &GetOngoingProduce(const PullReq &req);

  /// Performs a single remote pull for the given request.
  PullResData Pull(const PullReq &req);
};

}  // namespace distributed