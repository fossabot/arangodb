////////////////////////////////////////////////////////////////////////////////
/// @brief simple index attribute matcher
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Jan Steemann
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_INDEXES_SIMPLE_ATTRIBUTE_EQUALITY_MATCHER_H
#define ARANGODB_INDEXES_SIMPLE_ATTRIBUTE_EQUALITY_MATCHER_H 1

#include "Basics/Common.h"
#include "Aql/Ast.h"
#include "Aql/AstNode.h"
#include "Aql/Variable.h"
#include "Basics/AttributeNameParser.h"
#include "Indexes/Index.h"
#include "VocBase/vocbase.h"

namespace triagens {
  namespace arango {

// -----------------------------------------------------------------------------
// --SECTION--                              class SimpleAttributeEqualityMatcher
// -----------------------------------------------------------------------------

    class SimpleAttributeEqualityMatcher {

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

      public:

        SimpleAttributeEqualityMatcher (std::vector<std::vector<triagens::basics::AttributeName>> const& attributes)
          : _attributes(attributes),
            _found() {

        }
         
// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief match a single of the attributes
/// this is used for the primary index and the edge index
////////////////////////////////////////////////////////////////////////////////
        
        bool matchOne (triagens::arango::Index const* index,
                       triagens::aql::AstNode const* node,
                       triagens::aql::Variable const* reference,
                       size_t itemsInIndex,
                       size_t& estimatedItems,
                       double& estimatedCost) {
          _found.clear();
                    
          for (size_t i = 0; i < node->numMembers(); ++i) {
            auto op = node->getMember(i);

            if (op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_EQ) {
              TRI_ASSERT(op->numMembers() == 2);
              // EQ is symmetric
              if (accessFitsIndex(index, op->getMember(0), op->getMember(1), op, reference) ||
                  accessFitsIndex(index, op->getMember(1), op->getMember(0), op, reference)) {
                // we can use the index
                calculateIndexCosts(index, itemsInIndex, estimatedItems, estimatedCost);
                return true;
              }
            }
            else if (op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
              TRI_ASSERT(op->numMembers() == 2);
              if (accessFitsIndex(index, op->getMember(0), op->getMember(1), op, reference)) {
                // we can use the index
                // use slightly different cost calculation for IN that for EQ
                calculateIndexCosts(index, itemsInIndex, estimatedItems, estimatedCost);
                estimatedItems *= op->getMember(1)->numMembers();
                estimatedCost  *= op->getMember(1)->numMembers();
                return true;
              }
            }
          }

          // set to defaults
          estimatedItems = itemsInIndex;
          estimatedCost  = static_cast<double>(estimatedItems);
          return false;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief match all of the attributes, in any order
/// this is used for the hash index
////////////////////////////////////////////////////////////////////////////////
        
        bool matchAll (triagens::arango::Index const* index,
                       triagens::aql::AstNode const* node,
                       triagens::aql::Variable const* reference,
                       size_t itemsInIndex,
                       size_t& estimatedItems,
                       double& estimatedCost) {
          _found.clear();
          size_t values = 0;

          for (size_t i = 0; i < node->numMembers(); ++i) {
            auto op = node->getMember(i);

            if (op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_EQ) {
              TRI_ASSERT(op->numMembers() == 2);
              if (accessFitsIndex(index, op->getMember(0), op->getMember(1), op, reference) ||
                  accessFitsIndex(index, op->getMember(1), op->getMember(0), op, reference)) {
                if (_found.size() == _attributes.size()) {
                  // got enough attributes
                  break;
                }
              }
            }
            else if (op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
              TRI_ASSERT(op->numMembers() == 2);
              if (accessFitsIndex(index, op->getMember(0), op->getMember(1), op, reference)) {
                values += op->getMember(1)->numMembers();
                if (_found.size() == _attributes.size()) {
                  // got enough attributes
                  break;
                }
              }
            }
          }

          if (_found.size() == _attributes.size()) {
            // can only use this index if all index attributes are covered by the condition
            if (values == 0) {
              values = 1;
            }
            calculateIndexCosts(index, itemsInIndex, estimatedItems, estimatedCost);
            estimatedItems *= static_cast<double>(values);
            estimatedCost  *= static_cast<double>(values);
            return true;
          }

          // set to defaults
          estimatedItems = itemsInIndex;
          estimatedCost  = static_cast<double>(estimatedItems);
          return false;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief get the condition parts that the index is responsible for
/// this is used for the primary index and the edge index
/// requires that a previous matchOne() returned true
/// the caller must not free the returned AstNode*, as it belongs to the ast
////////////////////////////////////////////////////////////////////////////////
        
        triagens::aql::AstNode* getOne (triagens::aql::Ast* ast,
                                        triagens::arango::Index const* index,
                                        triagens::aql::AstNode const* node,
                                        triagens::aql::Variable const* reference) {
          _found.clear();
                    
          for (size_t i = 0; i < node->numMembers(); ++i) {
            auto op = node->getMember(i);

            if (op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_EQ ||
                op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
              TRI_ASSERT(op->numMembers() == 2);
              // note: accessFitsIndex will increase _found in case of a condition match
              bool matches = accessFitsIndex(index, op->getMember(0), op->getMember(1), op, reference);

              if (! matches && op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_EQ) {
                matches = accessFitsIndex(index, op->getMember(1), op->getMember(0), op, reference);
              }

              if (matches) {
                // we can use the index
                std::unique_ptr<triagens::aql::AstNode> eqNode(ast->clone(op));
                auto node = ast->createNodeNaryOperator(triagens::aql::NODE_TYPE_OPERATOR_NARY_AND, eqNode.get());
                eqNode.release();
                return node;
              }
            }
          }

          TRI_ASSERT(false);
          return nullptr;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief get the condition parts that the index is responsible for
/// this is used for the hash index
/// requires that a previous matchAll() returned true
/// the caller must not free the returned AstNode*, as it belongs to the ast
////////////////////////////////////////////////////////////////////////////////
        
        triagens::aql::AstNode* getAll (triagens::aql::Ast* ast,
                                        triagens::arango::Index const* index,
                                        triagens::aql::AstNode const* node,
                                        triagens::aql::Variable const* reference) {
          _found.clear();
          std::vector<triagens::aql::AstNode const*> parts;

          for (size_t i = 0; i < node->numMembers(); ++i) {
            auto op = node->getMember(i);

            if (op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_EQ ||
                op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
              TRI_ASSERT(op->numMembers() == 2);
              // note: accessFitsIndex will increase _found in case of a condition match
              bool matches = accessFitsIndex(index, op->getMember(0), op->getMember(1), op, reference);

              if (! matches && op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_EQ) {
                // EQ is symmetric
                matches = accessFitsIndex(index, op->getMember(1), op->getMember(0), op, reference);
              }

              if (matches) {
                parts.emplace_back(op);

                if (_found.size() == _attributes.size()) {
                  // got enough matches
                  std::unique_ptr<triagens::aql::AstNode> node(ast->createNodeNaryOperator(triagens::aql::NODE_TYPE_OPERATOR_NARY_AND));

                  for (auto& it : parts) {
                    node->addMember(ast->clone(it));
                  }

                  // done
                  return node.release();
                }
              }
            }
          }

          TRI_ASSERT(false);
          return nullptr;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief specialize the condition for the index
/// this is used for the primary index and the edge index
/// requires that a previous matchOne() returned true
////////////////////////////////////////////////////////////////////////////////
        
        triagens::aql::AstNode* specializeOne (triagens::arango::Index const* index,
                                               triagens::aql::AstNode* node,
                                               triagens::aql::Variable const* reference) {
          _found.clear();

          size_t const n = node->numMembers();

          for (size_t i = 0; i < n; ++i) {
            auto op = node->getMember(i);

            if (op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_EQ) {
              TRI_ASSERT(op->numMembers() == 2);
              // EQ is symmetric
              if (accessFitsIndex(index, op->getMember(0), op->getMember(1), op, reference) ||
                  accessFitsIndex(index, op->getMember(1), op->getMember(0), op, reference)) {
                // we can use the index
                // now return only the child node we need
                while (node->numMembers() > 0) {
                  node->removeMemberUnchecked(0);
                }
                node->addMember(op);

                return node;
              }
            }
            else if (op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
              TRI_ASSERT(op->numMembers() == 2);
              if (accessFitsIndex(index, op->getMember(0), op->getMember(1), op, reference)) {
                // we can use the index
                // now return only the child node we need
                while (node->numMembers() > 0) {
                  node->removeMemberUnchecked(0);
                }
                node->addMember(op);

                return node;
              }
            }
          }

          TRI_ASSERT(false);
          return node;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief specialize the condition for the index
/// this is used for the hash index
/// requires that a previous matchAll() returned true
////////////////////////////////////////////////////////////////////////////////
        
        triagens::aql::AstNode* specializeAll (triagens::arango::Index const* index,
                                               triagens::aql::AstNode* node,
                                               triagens::aql::Variable const* reference) {
          std::vector<triagens::aql::AstNode const*> children;
          _found.clear();
          
          size_t const n = node->numMembers();

          for (size_t i = 0; i < n; ++i) {
            auto op = node->getMember(i);

            if (op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_EQ) {
              TRI_ASSERT(op->numMembers() == 2);
              if (accessFitsIndex(index, op->getMember(0), op->getMember(1), op, reference) ||
                  accessFitsIndex(index, op->getMember(1), op->getMember(0), op, reference)) {
                children.emplace_back(op);
                if (_found.size() == _attributes.size()) {
                  // got enough attributes
                  break;
                }
              }
            }
            else if (op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
              TRI_ASSERT(op->numMembers() == 2);
              if (accessFitsIndex(index, op->getMember(0), op->getMember(1), op, reference)) {
                children.emplace_back(op);
                if (_found.size() == _attributes.size()) {
                  // got enough attributes
                  break;
                }
              }
            }
          }

          if (_found.size() == _attributes.size()) {
            // remove node's existing members
            while (node->numMembers() > 0) {
              node->removeMemberUnchecked(0);
            }

            // now re-add only those members we found in this method
            for (auto& it : children) {
              node->addMember(it);
            }

            return node;
          }

          TRI_ASSERT(false);
          return node;
        }

// -----------------------------------------------------------------------------
// --SECTION--                                                   private methods
// -----------------------------------------------------------------------------

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief determine the costs of using this index
/// costs returned are scaled from 0.0 to 1.0, with 0.0 being the lowest cost
////////////////////////////////////////////////////////////////////////////////

        void calculateIndexCosts (triagens::arango::Index const* index,
                                  size_t itemsInIndex,
                                  size_t& estimatedItems,
                                  double& estimatedCost) const {
          static double const EqualityReductionFactor = 100.0;

          if (index->unique()) {
            // index is unique, and the condition covers all attributes
            // now use a low value for the costs
            estimatedItems = 1;
            estimatedCost = 1.0;
          }
          else if (index->hasSelectivityEstimate()) {
            // use index selectivity estimate
            double estimate = index->selectivityEstimate();
            if (estimate <= 0.0) {
              // prevent division by zero
              estimatedItems = itemsInIndex;
              // the more attributes are contained in the index, the more specific the lookup will be
              for (size_t i = 0; i < index->fields().size(); ++i) {
                estimatedItems /= static_cast<size_t>(EqualityReductionFactor);
              }
            }
            else {
              estimatedItems = static_cast<size_t>(itemsInIndex * (1.0 / estimate));
            }

            estimatedItems = (std::max)(estimatedItems, static_cast<size_t>(1));
            // the more attributes are covered by an index, the more accurate it
            // is considered to be
            estimatedCost = static_cast<double>(estimatedItems) - index->fields().size() * 0.01;
          }
          else {
            // no such index should exist
            TRI_ASSERT(false);
          }
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief whether or not the access fits
////////////////////////////////////////////////////////////////////////////////

        bool accessFitsIndex (triagens::arango::Index const* index,
                              triagens::aql::AstNode const* access,
                              triagens::aql::AstNode const* other,
                              triagens::aql::AstNode const* op,
                              triagens::aql::Variable const* reference) {
          if (! index->canUseConditionPart(access, other, op, reference)) {
            return false;
          }

          triagens::aql::AstNode const* what;
          if (op->type == triagens::aql::NODE_TYPE_OPERATOR_BINARY_IN &&
              other->type == triagens::aql::NODE_TYPE_EXPANSION) {
            what = other;
          }
          else {
            what = access;
          }

          std::pair<triagens::aql::Variable const*, std::vector<triagens::basics::AttributeName>> attributeData;

          if (! what->isAttributeAccessForVariable(attributeData) ||
              attributeData.first != reference) {
            // this access is not referencing this collection
            return false;
          }

          std::vector<triagens::basics::AttributeName> const& fieldNames = attributeData.second;
    
          for (size_t i = 0; i < _attributes.size(); ++i) {
            if (_attributes[i].size() != fieldNames.size()) {
              // attribute path length differs
              continue;
            }

            bool match = true;
            for (size_t j = 0; j < _attributes[i].size(); ++j) {
              if (_attributes[i][j] != fieldNames[j]) {
                match = false;
                break;
              }
            }

            if (match) {
              // mark ith attribute as being covered
              _found.emplace(i);
              return true;
            }
          }

          return false;
        }

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------
      
      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief array of attributes used for comparisons
////////////////////////////////////////////////////////////////////////////////

        std::vector<std::vector<triagens::basics::AttributeName>> _attributes;

////////////////////////////////////////////////////////////////////////////////
/// @brief an internal map to mark which condition parts were useful and 
/// covered by the index
////////////////////////////////////////////////////////////////////////////////
          
        std::unordered_set<size_t> _found;

    };
               
  }
}

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
