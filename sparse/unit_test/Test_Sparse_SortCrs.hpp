//@HEADER
// ************************************************************************
//
//                        Kokkos v. 4.0
//       Copyright (2022) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Part of Kokkos, under the Apache License v2.0 with LLVM Exceptions.
// See https://kokkos.org/LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//@HEADER

/// \file Test_Sparse_SortCrs.hpp
/// \brief Tests for sort_crs_matrix and sort_crs_graph in
/// KokkosSparse_SortCrs.hpp

#ifndef KOKKOSSPARSE_SORTCRSTEST_HPP
#define KOKKOSSPARSE_SORTCRSTEST_HPP

#include <Kokkos_Core.hpp>
#include <Kokkos_Sort.hpp>
#include <KokkosKernels_Utils.hpp>
#include "KokkosSparse_IOUtils.hpp"
#include <KokkosSparse_SortCrs.hpp>
#include <KokkosKernels_default_types.hpp>
#include <KokkosSparse_CrsMatrix.hpp>
#include <Kokkos_ArithTraits.hpp>
#include <Kokkos_Complex.hpp>
#include <cstdlib>

template <typename exec_space>
void testSortCRS(default_lno_t numRows, default_lno_t numCols,
                 default_size_type nnz, bool doValues, bool doStructInterface,
                 bool useExecInstance) {
  using scalar_t  = default_scalar;
  using lno_t     = default_lno_t;
  using size_type = default_size_type;
  using mem_space = typename exec_space::memory_space;
  using device_t  = Kokkos::Device<exec_space, mem_space>;
  using crsMat_t =
      KokkosSparse::CrsMatrix<scalar_t, lno_t, device_t, void, size_type>;
  using rowmap_t  = typename crsMat_t::row_map_type;
  using entries_t = typename crsMat_t::index_type;
  using values_t  = typename crsMat_t::values_type;
  // Create a random matrix on device
  // IMPORTANT: kk_generate_sparse_matrix does not sort the rows, if it did this
  // wouldn't test anything
  crsMat_t A = KokkosSparse::Impl::kk_generate_sparse_matrix<crsMat_t>(
      numRows, numCols, nnz, 2, numCols / 2);
  auto rowmap  = A.graph.row_map;
  auto entries = A.graph.entries;
  auto values  = A.values;
  Kokkos::View<size_type*, Kokkos::HostSpace> rowmapHost("rowmap host",
                                                         numRows + 1);
  Kokkos::View<lno_t*, Kokkos::HostSpace> entriesHost("sorted entries host",
                                                      nnz);
  Kokkos::View<scalar_t*, Kokkos::HostSpace> valuesHost("sorted values host",
                                                        nnz);
  Kokkos::deep_copy(rowmapHost, rowmap);
  Kokkos::deep_copy(entriesHost, entries);
  Kokkos::deep_copy(valuesHost, values);
  struct ColValue {
    ColValue() {}
    ColValue(lno_t c, scalar_t v) : col(c), val(v) {}
    bool operator<(const ColValue& rhs) const { return col < rhs.col; }
    bool operator==(const ColValue& rhs) const {
      return col == rhs.col && val == rhs.val;
    }
    lno_t col;
    scalar_t val;
  };
  // sort one row at a time on host using STL.
  {
    for (lno_t i = 0; i < numRows; i++) {
      std::vector<ColValue> rowCopy;
      for (size_type j = rowmapHost(i); j < rowmapHost(i + 1); j++)
        rowCopy.emplace_back(entriesHost(j), valuesHost(j));
      std::sort(rowCopy.begin(), rowCopy.end());
      // write sorted row back
      for (size_t j = 0; j < rowCopy.size(); j++) {
        entriesHost(rowmapHost(i) + j) = rowCopy[j].col;
        valuesHost(rowmapHost(i) + j)  = rowCopy[j].val;
      }
    }
  }
  // call the actual sort routine being tested
  if (doValues) {
    if (doStructInterface) {
      if (useExecInstance) {
        KokkosSparse::sort_crs_matrix(exec_space(), A);
      } else {
        KokkosSparse::sort_crs_matrix(A);
      }
    } else {
      if (useExecInstance) {
        KokkosSparse::sort_crs_matrix(exec_space(), A.graph.row_map,
                                      A.graph.entries, A.values);
      } else {
        KokkosSparse::sort_crs_matrix<exec_space, rowmap_t, entries_t,
                                      values_t>(A.graph.row_map,
                                                A.graph.entries, A.values);
      }
    }
  } else {
    if (doStructInterface) {
      if (useExecInstance) {
        KokkosSparse::sort_crs_graph(exec_space(), A.graph);
      } else {
        KokkosSparse::sort_crs_graph(A.graph);
      }
    } else {
      if (useExecInstance) {
        KokkosSparse::sort_crs_graph(exec_space(), A.graph.row_map,
                                     A.graph.entries);
      } else {
        KokkosSparse::sort_crs_graph<exec_space, rowmap_t, entries_t>(
            A.graph.row_map, A.graph.entries);
      }
    }
  }
  // Copy to host and compare
  Kokkos::View<lno_t*, Kokkos::HostSpace> entriesOut("sorted entries host",
                                                     nnz);
  Kokkos::View<scalar_t*, Kokkos::HostSpace> valuesOut("sorted values host",
                                                       nnz);
  Kokkos::deep_copy(entriesOut, entries);
  Kokkos::deep_copy(valuesOut, values);
  for (size_type i = 0; i < nnz; i++) {
    EXPECT_EQ(entriesHost(i), entriesOut(i))
        << "Sorted column indices are wrong!";
    if (doValues) {
      EXPECT_EQ(valuesHost(i), valuesOut(i)) << "Sorted values are wrong!";
    }
  }
}

template <typename exec_space>
void testSortCRSUnmanaged(bool doValues, bool doStructInterface) {
  // This test is about bug #960.
  using scalar_t  = default_scalar;
  using lno_t     = default_lno_t;
  using size_type = default_size_type;
  using mem_space = typename exec_space::memory_space;
  using device_t  = Kokkos::Device<exec_space, mem_space>;
  using crsMat_t =
      KokkosSparse::CrsMatrix<scalar_t, lno_t, device_t,
                              Kokkos::MemoryTraits<Kokkos::Unmanaged>,
                              size_type>;
  using crsMat_Managed_t =
      KokkosSparse::CrsMatrix<scalar_t, lno_t, device_t, void, size_type>;
  using rowmap_t      = typename crsMat_t::row_map_type;
  using entries_t     = typename crsMat_t::index_type;
  using values_t      = typename crsMat_t::values_type;
  const lno_t numRows = 50;
  const lno_t numCols = numRows;
  size_type nnz       = numRows * 5;
  // Create a random matrix on device
  // IMPORTANT: kk_generate_sparse_matrix does not sort the rows, if it did this
  // wouldn't test anything
  crsMat_Managed_t A_managed =
      KokkosSparse::Impl::kk_generate_sparse_matrix<crsMat_Managed_t>(
          numRows, numCols, nnz, 2, numCols / 2);
  crsMat_t A(A_managed);
  auto rowmap  = A.graph.row_map;
  auto entries = A.graph.entries;
  auto values  = A.values;
  if (doValues) {
    if (doStructInterface) {
      KokkosSparse::sort_crs_matrix(A);
    } else {
      KokkosSparse::sort_crs_matrix<exec_space, rowmap_t, entries_t, values_t>(
          A.graph.row_map, A.graph.entries, A.values);
    }
  } else {
    if (doStructInterface) {
      KokkosSparse::sort_crs_graph(A.graph);
    } else {
      KokkosSparse::sort_crs_graph<exec_space, rowmap_t, entries_t>(
          A.graph.row_map, A.graph.entries);
    }
  }
}

template <typename exec_space>
void testSortAndMerge(bool justGraph, bool useExecInstance,
                      bool doStructInterface, int testCase) {
  using size_type = default_size_type;
  using lno_t     = default_lno_t;
  using scalar_t  = default_scalar;
  using mem_space = typename exec_space::memory_space;
  using device_t  = Kokkos::Device<exec_space, mem_space>;
  using crsMat_t =
      KokkosSparse::CrsMatrix<scalar_t, lno_t, device_t, void, size_type>;
  using graph_t   = typename crsMat_t::staticcrsgraph_type;
  using rowmap_t  = typename crsMat_t::row_map_type::non_const_type;
  using entries_t = typename crsMat_t::index_type;
  using values_t  = typename crsMat_t::values_type;
  using Kokkos::HostSpace;
  using Kokkos::MemoryTraits;
  using Kokkos::Unmanaged;
  // Select a test case: matrices and correct ouptut are hardcoded for each
  std::vector<size_type> inRowmap;
  std::vector<lno_t> inEntries;
  std::vector<scalar_t> inValues;
  std::vector<size_type> goldRowmap;
  std::vector<lno_t> goldEntries;
  std::vector<scalar_t> goldValues;
  lno_t nrows = 0;
  lno_t ncols = 0;
  switch (testCase) {
    case 0: {
      // Two merges take place, and one depends on sorting being done correctly
      nrows     = 5;
      ncols     = 7;
      inRowmap  = {0, 4, 4, 5, 7, 10};
      inEntries = {
          4, 3, 5, 3,  // row 0
                       // row 1 has no entries
          6,           // row 2
          2, 2,        // row 3
          0, 1, 2      // row 4
      };
      // note: choosing values that can be represented exactly by float
      inValues = {
          1.5, 4, 1, -3,  // row 0
                          // row 1
          2,              // row 2
          -1, -2,         // row 3
          0, 3.5, -2.25   // row 4
      };
      // Expect 2 merges to have taken place
      goldRowmap  = {0, 3, 3, 4, 5, 8};
      goldEntries = {
          3, 4, 5,  // row 0
                    // row 1 has no entries
          6,        // row 2
          2,        // row 3
          0, 1, 2   // row 4
      };
      goldValues = {
          1, 1.5, 1,     // row 0
                         // row 1
          2,             // row 2
          -3,            // row 3
          0, 3.5, -2.25  // row 4
      };
      break;
    }
    case 1: {
      // Same as above, but no merges take place
      nrows     = 5;
      ncols     = 7;
      inRowmap  = {0, 3, 3, 4, 5, 8};
      inEntries = {
          4, 5, 3,  // row 0
                    // row 1 has no entries
          6,        // row 2
          2,        // row 3
          0, 1, 2   // row 4
      };
      inValues = {
          1.5, 4, 1,     // row 0
                         // row 1
          2,             // row 2
          -1,            // row 3
          0, 3.5, -2.25  // row 4
      };
      // Expect 2 merges to have taken place
      goldRowmap  = {0, 3, 3, 4, 5, 8};
      goldEntries = {
          3, 4, 5,  // row 0
                    // row 1 has no entries
          6,        // row 2
          2,        // row 3
          0, 1, 2   // row 4
      };
      goldValues = {
          1, 1.5, 4,     // row 0
                         // row 1
          2,             // row 2
          -1,            // row 3
          0, 3.5, -2.25  // row 4
      };
      break;
    }
    case 2: {
      // Nonzero dimensions but no entries
      nrows      = 5;
      ncols      = 7;
      inRowmap   = {0, 0, 0, 0, 0, 0};
      goldRowmap = inRowmap;
      break;
    }
    case 3: {
      // Zero rows, length-zero rowmap
      break;
    }
    case 4: {
      // Zero rows, length-one rowmap
      inRowmap   = {0};
      goldRowmap = {0};
      break;
    }
  }
  size_type nnz = inEntries.size();
  Kokkos::View<size_type*, HostSpace, MemoryTraits<Unmanaged>> hostInRowmap(
      inRowmap.data(), inRowmap.size());
  Kokkos::View<lno_t*, HostSpace, MemoryTraits<Unmanaged>> hostInEntries(
      inEntries.data(), nnz);
  Kokkos::View<scalar_t*, HostSpace, MemoryTraits<Unmanaged>> hostInValues(
      inValues.data(), nnz);
  rowmap_t devInRowmap("in rowmap", inRowmap.size());
  entries_t devInEntries("in entries", nnz);
  values_t devInValues("in values", nnz);
  Kokkos::deep_copy(devInRowmap, hostInRowmap);
  Kokkos::deep_copy(devInEntries, hostInEntries);
  Kokkos::deep_copy(devInValues, hostInValues);
  crsMat_t input("Input", nrows, ncols, nnz, devInValues, devInRowmap,
                 devInEntries);
  crsMat_t output;
  if (justGraph) {
    graph_t outputGraph;
    // Testing sort_and_merge_graph
    if (doStructInterface) {
      if (useExecInstance) {
        outputGraph =
            KokkosSparse::sort_and_merge_graph(exec_space(), input.graph);
      } else {
        outputGraph = KokkosSparse::sort_and_merge_graph(input.graph);
      }
    } else {
      rowmap_t devOutRowmap;
      entries_t devOutEntries;
      if (useExecInstance) {
        KokkosSparse::sort_and_merge_graph(exec_space(), input.graph.row_map,
                                           input.graph.entries, devOutRowmap,
                                           devOutEntries);
      } else {
        KokkosSparse::sort_and_merge_graph<exec_space>(
            input.graph.row_map, input.graph.entries, devOutRowmap,
            devOutEntries);
      }
      outputGraph = graph_t(devOutEntries, devOutRowmap);
    }
    // Construct output using the output graph, leaving values zero-initialized
    output = crsMat_t("Output", outputGraph, ncols);
  } else {
    // Testing sort_and_merge_matrix
    if (doStructInterface) {
      if (useExecInstance) {
        output = KokkosSparse::sort_and_merge_matrix(exec_space(), input);
      } else {
        output = KokkosSparse::sort_and_merge_matrix(input);
      }
    } else {
      rowmap_t devOutRowmap;
      entries_t devOutEntries;
      values_t devOutValues;
      if (useExecInstance) {
        KokkosSparse::sort_and_merge_matrix(
            exec_space(), input.graph.row_map, input.graph.entries,
            input.values, devOutRowmap, devOutEntries, devOutValues);
      } else {
        KokkosSparse::sort_and_merge_matrix<exec_space>(
            input.graph.row_map, input.graph.entries, input.values,
            devOutRowmap, devOutEntries, devOutValues);
      }
      // and then construct output from views
      output = crsMat_t("Output", nrows, ncols, devOutValues.extent(0),
                        devOutValues, devOutRowmap, devOutEntries);
    }
    EXPECT_EQ(output.numRows(), nrows);
    EXPECT_EQ(output.numCols(), ncols);
  }
  auto outRowmap  = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(),
                                                       output.graph.row_map);
  auto outEntries = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(),
                                                        output.graph.entries);
  auto outValues =
      Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace(), output.values);
  EXPECT_EQ(goldRowmap.size(), outRowmap.extent(0));
  EXPECT_EQ(goldEntries.size(), outEntries.extent(0));
  if (!justGraph) {
    EXPECT_EQ(goldValues.size(), outValues.extent(0));
    EXPECT_EQ(goldValues.size(), output.nnz());
  }
  for (size_t i = 0; i < goldRowmap.size(); i++)
    EXPECT_EQ(goldRowmap[i], outRowmap(i));
  for (size_t i = 0; i < goldEntries.size(); i++) {
    EXPECT_EQ(goldEntries[i], outEntries(i));
    if (!justGraph) {
      EXPECT_EQ(goldValues[i], outValues(i));
    }
  }
}

TEST_F(TestCategory, common_sort_crsgraph) {
  for (int doStructInterface = 0; doStructInterface < 2; doStructInterface++) {
    for (int useExecInstance = 0; useExecInstance < 2; useExecInstance++) {
      testSortCRS<TestExecSpace>(10, 10, 20, false, doStructInterface,
                                 useExecInstance);
      testSortCRS<TestExecSpace>(100, 100, 2000, false, doStructInterface,
                                 useExecInstance);
      testSortCRS<TestExecSpace>(1000, 1000, 30000, false, doStructInterface,
                                 useExecInstance);
    }
    testSortCRSUnmanaged<TestExecSpace>(false, doStructInterface);
  }
}

TEST_F(TestCategory, common_sort_crsmatrix) {
  for (int doStructInterface = 0; doStructInterface < 2; doStructInterface++) {
    for (int useExecInstance = 0; useExecInstance < 2; useExecInstance++) {
      testSortCRS<TestExecSpace>(10, 10, 20, true, doStructInterface,
                                 useExecInstance);
      testSortCRS<TestExecSpace>(100, 100, 2000, true, doStructInterface,
                                 useExecInstance);
      testSortCRS<TestExecSpace>(1000, 1000, 30000, true, doStructInterface,
                                 useExecInstance);
    }
    testSortCRSUnmanaged<TestExecSpace>(true, doStructInterface);
  }
}

TEST_F(TestCategory, common_sort_crs_longrows) {
  testSortCRS<TestExecSpace>(1, 50000, 10000, false, false, false);
  testSortCRS<TestExecSpace>(1, 50000, 10000, true, false, false);
  testSortCRS<TestExecSpace>(1, 50000, 10000, false, false, true);
  testSortCRS<TestExecSpace>(1, 50000, 10000, true, false, true);
}

TEST_F(TestCategory, common_sort_merge_crsmatrix) {
  for (int testCase = 0; testCase < 5; testCase++) {
    testSortAndMerge<TestExecSpace>(false, false, false, testCase);
    testSortAndMerge<TestExecSpace>(false, false, true, testCase);
    testSortAndMerge<TestExecSpace>(false, true, false, testCase);
    testSortAndMerge<TestExecSpace>(false, true, true, testCase);
  }
}

TEST_F(TestCategory, common_sort_merge_crsgraph) {
  for (int testCase = 0; testCase < 5; testCase++) {
    testSortAndMerge<TestExecSpace>(true, false, false, testCase);
    testSortAndMerge<TestExecSpace>(true, false, true, testCase);
    testSortAndMerge<TestExecSpace>(true, true, false, testCase);
    testSortAndMerge<TestExecSpace>(true, true, true, testCase);
  }
}

#endif  // KOKKOSSPARSE_SORTCRSTEST_HPP
