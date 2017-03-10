//
// Copyright 2017 Memgraph
// Created by Florijan Stamenkovic on 09.03.17.
//

#include <iostream>

#include "query/backend/data_structures.hpp"
#include "query/backend/cpp/code_generator.hpp"

using std::cout;
using std::endl;

DataStructures basic_data_structures() {
  DataStructures ds;

  ds.GetPropertyIndex("age");
  ds.GetPropertyIndex("name");

  ds.GetLabelIndex("Person");
  ds.GetLabelIndex("Dog");

  ds.GetEdgeTypeIndex("Likes");
  ds.GetEdgeTypeIndex("Hates");

  return ds;
}

DataStructures basic_traversal() {
  DataStructures ds;

  // query: MATCH (p:Cute:Person)-[:Likes]-(q:Cute:Person) RETURN p.name, q.name;

  // create a pattern
  DataStructures::Node start_node(ds.GetVariableIndex("p"));
  start_node.labels_.emplace_back(ds.GetLabelIndex("Cute"));
  start_node.labels_.emplace_back(ds.GetLabelIndex("Person"));
  auto pattern = ds.AddPattern(start_node);
  pattern.second.relationships_.emplace_back(
      DataStructures::Relationship(DataStructures::Relationship::Direction::RIGHT));
  pattern.second.nodes_.emplace_back(DataStructures::Node(ds.GetVariableIndex("q")));
  pattern.second.nodes_.back().labels_.emplace_back(ds.GetLabelIndex("Cute"));
  auto match = ds.AddMatch();
  match.second.patterns_.emplace_back(pattern.first);

  // create a return statement


  // return of got property
  auto return_stmt = ds.AddReturn(false);

  for (auto node_name : {"p", "q"}) {
    // convert (node_name) into an expression
    auto node_expression_var = ds.AddExpression(DataStructures::ExpressionOp::VARIABLE);
    node_expression_var.second.operands_.emplace_back(
        DataStructures::ExpressionOperand::VARIABLE, ds.GetVariableIndex(node_name));

    // property getting node_name.name
    auto expression_prop_getter = ds.AddExpression(DataStructures::ExpressionOp::PROPERTY_GETTER);
    expression_prop_getter.second.operands_.emplace_back(
        DataStructures::ExpressionOperand::EXPRESSION, node_expression_var.first);
    expression_prop_getter.second.operands_.emplace_back(
        DataStructures::ExpressionOperand::PROPERTY, ds.GetPropertyIndex("name"));

    // adding to return
    return_stmt.second.expressions_.emplace_back(expression_prop_getter.first, -1);
  }


  return ds;
}

/**
 * For some pre-made data structures generates and prints out code.
 */
int main() {

  std::function<DataStructures()> functions[]{
      basic_data_structures,
      basic_traversal,
  };

  for (auto function : functions) {
    cout << endl << "----------------" << endl;
    cout << query::CodeGenerator(function()).code();
  }
}

