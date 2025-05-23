/*
 * Copyright 2023 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

// Pull in flat-index related functionaliry
#include "svs/index/flat/flat.h"

// svs
#include "svs/core/data.h"
#include "svs/core/distance.h"
#include "svs/core/graph.h"
#include "svs/lib/preprocessor.h"
#include "svs/lib/threads.h"

#include "svs/orchestrators/manager.h"

// stdlib
#include <memory>

namespace svs {

/////
///// Type-erased stack implementation.
/////

// Additional API exposed by exhaustive search.
class FlatInterface {
  public:
    using search_parameters_type = svs::index::flat::FlatParameters;

    // Non-templated virtual method for distance calculation
    virtual double get_distance(size_t id, const AnonymousArray<1>& query) const = 0;
};

template <lib::TypeList QueryTypes, typename Impl, typename IFace = FlatInterface>
class FlatImpl : public manager::ManagerImpl<QueryTypes, Impl, FlatInterface> {
  public:
    using base_type = manager::ManagerImpl<QueryTypes, Impl, FlatInterface>;
    using base_type::impl;

    ///
    /// Construct a FlatImpl implementation from the implementation's move constructor.
    ///
    explicit FlatImpl(Impl impl)
        : base_type{std::move(impl)} {}

    ///
    /// Construct a FlatImpl implementation by calling its actual implementation's
    /// constructor directly.
    ///
    template <typename... Args>
    explicit FlatImpl(Args&&... args)
        : base_type{std::forward<Args>(args)...} {}

    ///// Distance
    double get_distance(size_t id, const AnonymousArray<1>& query) const override {
        return svs::lib::match(
            QueryTypes{},
            query.type(),
            [&]<typename T>(svs::lib::Type<T>) {
                auto query_span = std::span<const T>(get<T>(query), query.size(0));
                return impl().get_distance(id, query_span);
            }
        );
    }
};

// Forward Declarations
class Flat;
template <lib::TypeList QueryTypes, typename... Args> Flat make_flat(Args&&... args);

/// @brief Type erased container for the Flat index.
class Flat : public manager::IndexManager<FlatInterface> {
  public:
    /// Internal dispatch tag.
    struct AssembleTag {};
    using base_type = manager::IndexManager<FlatInterface>;

    explicit Flat(std::unique_ptr<manager::ManagerInterface<FlatInterface>> impl)
        : base_type{std::move(impl)} {}

    ///// Loading

    ///
    /// @brief Load a Flat Index from an existing dataset.
    ///
    /// @tparam QueryType The element type of the vectors that will be used for querying.
    ///
    /// @param data_loader A compatible class capable of load data. See expanded notes.
    /// @param distance A distance functor to use or a ``svs::DistanceType`` enum.
    /// @param threadpool_proto Precursor for the thread pool to use. Can either be an
    /// acceptable thread pool
    ///     instance or an integer specifying the number of threads to use. In the latter
    ///     case, a new default thread pool will be constructed using ``threadpool_proto``
    ///     as the number of threads to create.
    ///
    /// @copydoc hidden_flat_auto_assemble
    ///
    /// @copydoc threadpool_requirements
    ///
    template <
        manager::QueryTypeDefinition QueryTypes,
        typename DataLoader,
        typename Distance,
        typename ThreadPoolProto>
    static Flat assemble(
        DataLoader&& data_loader, Distance distance, ThreadPoolProto threadpool_proto
    ) {
        if constexpr (std::is_same_v<std::decay_t<Distance>, DistanceType>) {
            auto dispatcher = DistanceDispatcher{distance};
            return dispatcher([&](auto distance_function) {
                return make_flat<manager::as_typelist<QueryTypes>>(
                    AssembleTag(),
                    std::forward<DataLoader>(data_loader),
                    std::move(distance_function),
                    threads::as_threadpool(std::move(threadpool_proto))
                );
            });
        } else {
            return make_flat<manager::as_typelist<QueryTypes>>(
                AssembleTag(),
                std::forward<DataLoader>(data_loader),
                std::move(distance),
                threads::as_threadpool(std::move(threadpool_proto))
            );
        }
    }

    ///// Distance
    /// @brief Get the distance between a vector in the index and a query vector
    /// @tparam Query The query vector type
    /// @param id The ID of the vector in the index
    /// @param query The query vector
    template <typename Query> double get_distance(size_t id, const Query& query) const {
        // Create AnonymousArray from the query
        AnonymousArray<1> query_array{query.data(), query.size()};
        return impl_->get_distance(id, query_array);
    }
};

template <lib::TypeList QueryTypes, typename... Args> Flat make_flat(Args&&... args) {
    using Impl = decltype(index::flat::FlatIndex(std::forward<Args>(args)...));
    return Flat{std::make_unique<FlatImpl<QueryTypes, Impl>>(std::forward<Args>(args)...)};
}

template <lib::TypeList QueryTypes, typename... Args>
Flat make_flat(Flat::AssembleTag SVS_UNUSED(tag) /*unused*/, Args&&... args) {
    return make_flat<QueryTypes>(index::flat::auto_assemble(std::forward<Args>(args)...));
}
} // namespace svs
