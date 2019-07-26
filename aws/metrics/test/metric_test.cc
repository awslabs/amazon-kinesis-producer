/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <aws/utils/logging.h>

#include <aws/metrics/metric.h>
#include <aws/metrics/metrics_finder.h>
#include <aws/metrics/metrics_index.h>
#include <aws/utils/concurrent_linked_queue.h>
#include <aws/utils/utils.h>

namespace {

using Dimensions = std::list<std::pair<std::string, std::string>>;

}

BOOST_AUTO_TEST_SUITE(Metric)

BOOST_AUTO_TEST_CASE(AddRoot) {
  aws::metrics::MetricsIndex mi;

  aws::metrics::MetricsFinder mf;
  mf.push_dimension("a", "1");
  auto m = mi.get_metric(mf);

  BOOST_REQUIRE(m);
  BOOST_REQUIRE_EQUAL(mi.get_all().size(), 1);

  BOOST_CHECK(mi.get_all().front() == m);

  BOOST_CHECK_EQUAL(m->dimension().first, "a");
  BOOST_CHECK_EQUAL(m->dimension().second, "1");
}

BOOST_AUTO_TEST_CASE(AddChild) {
  aws::metrics::MetricsIndex mi;

  aws::metrics::MetricsFinder mf;
  mf.push_dimension("a", "1");
  auto m = mi.get_metric(mf);
  mf.push_dimension("b", "2");
  auto m2 = mi.get_metric(mf);

  BOOST_REQUIRE(m);
  BOOST_REQUIRE(m2);
  BOOST_REQUIRE_EQUAL(mi.get_all().size(), 2);

  BOOST_CHECK_EQUAL(m->dimension().first, "a");
  BOOST_CHECK_EQUAL(m->dimension().second, "1");
  BOOST_CHECK_EQUAL(m2->dimension().first, "b");
  BOOST_CHECK_EQUAL(m2->dimension().second, "2");

  BOOST_CHECK_EQUAL(m2->parent(), m);
}

BOOST_AUTO_TEST_CASE(AddDeepChild) {
  aws::metrics::MetricsIndex mi;

  aws::metrics::MetricsFinder mf;
  mf.push_dimension("a", "1");
  mf.push_dimension("b", "2");
  mf.push_dimension("c", "3");
  auto c = mi.get_metric(mf);

  BOOST_REQUIRE(c);
  BOOST_REQUIRE_EQUAL(mi.get_all().size(), 3);

  mf.pop_dimension();
  auto b = mi.get_metric(mf);

  BOOST_REQUIRE(b);
  BOOST_REQUIRE_EQUAL(b->all_dimensions().size(), 2);

  BOOST_CHECK_EQUAL(b->all_dimensions()[0].first, "a");
  BOOST_CHECK_EQUAL(b->all_dimensions()[0].second, "1");
  BOOST_CHECK_EQUAL(b->all_dimensions()[1].first, "b");
  BOOST_CHECK_EQUAL(b->all_dimensions()[1].second, "2");

  BOOST_CHECK_EQUAL(c->parent(), b);

  mf.pop_dimension();
  auto a = mi.get_metric(mf);

  BOOST_REQUIRE(a);
  BOOST_REQUIRE_EQUAL(a->all_dimensions().size(), 1);

  BOOST_CHECK_EQUAL(a->all_dimensions()[0].first, "a");
  BOOST_CHECK_EQUAL(a->all_dimensions()[0].second, "1");
  BOOST_CHECK_EQUAL(b->all_dimensions()[1].first, "b");

  BOOST_CHECK(!a->parent());
}

// Test a tree with the following shape:
//
//           a
//           |
//     ------------
//     |          |
//     b          e
//     |          |
//  ------      ------
//  |    |      |    |
//  c    d      f    g
//
BOOST_AUTO_TEST_CASE(Tree) {
  aws::metrics::MetricsIndex mi;

  aws::metrics::MetricsFinder mf;
  mf.push_dimension("a", "_");
  mf.push_dimension("b", "_");
  mf.push_dimension("c", "_");
  auto c = mi.get_metric(mf);

  mf.pop_dimension();
  mf.push_dimension("d", "_");
  auto d = mi.get_metric(mf);

  mf.pop_dimension();
  mf.pop_dimension();
  mf.push_dimension("e", "_");
  mf.push_dimension("f", "_");
  auto f = mi.get_metric(mf);

  mf.pop_dimension();
  mf.push_dimension("g", "_");
  auto g = mi.get_metric(mf);

  mf.pop_dimension();
  auto e = mi.get_metric(mf);

  mf.pop_dimension();
  auto a = mi.get_metric(mf);

  mf.push_dimension("b", "_");
  auto b = mi.get_metric(mf);

  BOOST_REQUIRE(a);
  BOOST_REQUIRE(b);
  BOOST_REQUIRE(c);
  BOOST_REQUIRE(d);
  BOOST_REQUIRE(e);
  BOOST_REQUIRE(f);
  BOOST_REQUIRE(g);

  BOOST_REQUIRE_EQUAL(mi.get_all().size(), 7);

  BOOST_CHECK(!a->parent());
  BOOST_CHECK_EQUAL(b->parent(), a);
  BOOST_CHECK_EQUAL(e->parent(), a);
  BOOST_CHECK_EQUAL(c->parent(), b);
  BOOST_CHECK_EQUAL(d->parent(), b);
  BOOST_CHECK_EQUAL(f->parent(), e);
  BOOST_CHECK_EQUAL(g->parent(), e);

  BOOST_CHECK_EQUAL(a->all_dimensions().size(), 1);
  BOOST_CHECK_EQUAL(a->all_dimensions()[0].first, "a");

  BOOST_CHECK_EQUAL(b->all_dimensions().size(), 2);
  BOOST_CHECK_EQUAL(b->all_dimensions()[0].first, "a");
  BOOST_CHECK_EQUAL(b->all_dimensions()[1].first, "b");

  BOOST_CHECK_EQUAL(c->all_dimensions().size(), 3);
  BOOST_CHECK_EQUAL(c->all_dimensions()[0].first, "a");
  BOOST_CHECK_EQUAL(c->all_dimensions()[1].first, "b");
  BOOST_CHECK_EQUAL(c->all_dimensions()[2].first, "c");

  BOOST_CHECK_EQUAL(d->all_dimensions().size(), 3);
  BOOST_CHECK_EQUAL(d->all_dimensions()[0].first, "a");
  BOOST_CHECK_EQUAL(d->all_dimensions()[1].first, "b");
  BOOST_CHECK_EQUAL(d->all_dimensions()[2].first, "d");

  BOOST_CHECK_EQUAL(e->all_dimensions().size(), 2);
  BOOST_CHECK_EQUAL(e->all_dimensions()[0].first, "a");
  BOOST_CHECK_EQUAL(e->all_dimensions()[1].first, "e");

  BOOST_CHECK_EQUAL(f->all_dimensions().size(), 3);
  BOOST_CHECK_EQUAL(f->all_dimensions()[0].first, "a");
  BOOST_CHECK_EQUAL(f->all_dimensions()[1].first, "e");
  BOOST_CHECK_EQUAL(f->all_dimensions()[2].first, "f");

  BOOST_CHECK_EQUAL(g->all_dimensions().size(), 3);
  BOOST_CHECK_EQUAL(g->all_dimensions()[0].first, "a");
  BOOST_CHECK_EQUAL(g->all_dimensions()[1].first, "e");
  BOOST_CHECK_EQUAL(g->all_dimensions()[2].first, "g");

  c->put(1);
  d->put(2);
  f->put(3);
  g->put(4);

  BOOST_CHECK_EQUAL(a->accumulator().sum(), 10);
  BOOST_CHECK_EQUAL(b->accumulator().sum(), 3);
  BOOST_CHECK_EQUAL(c->accumulator().sum(), 1);
  BOOST_CHECK_EQUAL(d->accumulator().sum(), 2);
  BOOST_CHECK_EQUAL(e->accumulator().sum(), 7);
  BOOST_CHECK_EQUAL(f->accumulator().sum(), 3);
  BOOST_CHECK_EQUAL(g->accumulator().sum(), 4);
}

// Make sure the tree is correct even with concurrent access
BOOST_AUTO_TEST_CASE(ConcurrentAdd) {
  aws::metrics::MetricsIndex mi;

  auto start =
      std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(5);

  size_t num_threads = aws::thread::hardware_concurrency();

  using MetricPtrVec = std::vector<std::shared_ptr<aws::metrics::Metric>>;
  aws::utils::ConcurrentLinkedQueue<MetricPtrVec> results;

  std::vector<aws::thread> threads;
  for (size_t i = 0; i < num_threads; i++) {
    threads.emplace_back([&] {
      aws::utils::sleep_until(start);

      aws::metrics::MetricsFinder mf;
      mf.push_dimension("a", "_");
      mf.push_dimension("b", "_");
      mf.push_dimension("c", "_");
      auto c = mi.get_metric(mf);

      mf.pop_dimension();
      mf.push_dimension("d", "_");
      auto d = mi.get_metric(mf);

      mf.pop_dimension();
      mf.pop_dimension();
      mf.push_dimension("e", "_");
      mf.push_dimension("f", "_");
      auto f = mi.get_metric(mf);

      mf.pop_dimension();
      mf.push_dimension("g", "_");
      auto g = mi.get_metric(mf);

      mf.pop_dimension();
      auto e = mi.get_metric(mf);

      mf.pop_dimension();
      auto a = mi.get_metric(mf);

      mf.push_dimension("b", "_");
      auto b = mi.get_metric(mf);

      MetricPtrVec v({a, b, c, d, e, f, g});
      results.put(std::move(v));

      c->put(1);
      d->put(2);
      f->put(3);
      g->put(4);
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  BOOST_REQUIRE_EQUAL(mi.get_all().size(), 7);

  int count = 0;
  MetricPtrVec tmp;
  while (results.try_take(tmp)) {
    BOOST_REQUIRE_EQUAL(tmp.size(), 7);
    count++;

    auto a = tmp.at(0);
    auto b = tmp.at(1);
    auto c = tmp.at(2);
    auto d = tmp.at(3);
    auto e = tmp.at(4);
    auto f = tmp.at(5);
    auto g = tmp.at(6);

    BOOST_REQUIRE(a);
    BOOST_REQUIRE(b);
    BOOST_REQUIRE(c);
    BOOST_REQUIRE(d);
    BOOST_REQUIRE(e);
    BOOST_REQUIRE(f);
    BOOST_REQUIRE(g);

    BOOST_REQUIRE(!a->parent());
    BOOST_REQUIRE_EQUAL(b->parent(), a);
    BOOST_REQUIRE_EQUAL(e->parent(), a);
    BOOST_REQUIRE_EQUAL(c->parent(), b);
    BOOST_REQUIRE_EQUAL(d->parent(), b);
    BOOST_REQUIRE_EQUAL(f->parent(), e);
    BOOST_REQUIRE_EQUAL(g->parent(), e);
  }
  BOOST_REQUIRE_EQUAL(count, threads.size());

  aws::metrics::MetricsFinder mf;
  mf.push_dimension("a", "_");
  mf.push_dimension("b", "_");
  mf.push_dimension("c", "_");
  auto c = mi.get_metric(mf);

  mf.pop_dimension();
  mf.push_dimension("d", "_");
  auto d = mi.get_metric(mf);

  mf.pop_dimension();
  mf.pop_dimension();
  mf.push_dimension("e", "_");
  mf.push_dimension("f", "_");
  auto f = mi.get_metric(mf);

  mf.pop_dimension();
  mf.push_dimension("g", "_");
  auto g = mi.get_metric(mf);

  mf.pop_dimension();
  auto e = mi.get_metric(mf);

  mf.pop_dimension();
  auto a = mi.get_metric(mf);

  mf.push_dimension("b", "_");
  auto b = mi.get_metric(mf);

  BOOST_CHECK_EQUAL(a->accumulator().sum(), num_threads * 10);
  BOOST_CHECK_EQUAL(b->accumulator().sum(), num_threads * 3);
  BOOST_CHECK_EQUAL(c->accumulator().sum(), num_threads * 1);
  BOOST_CHECK_EQUAL(d->accumulator().sum(), num_threads * 2);
  BOOST_CHECK_EQUAL(e->accumulator().sum(), num_threads * 7);
  BOOST_CHECK_EQUAL(f->accumulator().sum(), num_threads * 3);
  BOOST_CHECK_EQUAL(g->accumulator().sum(), num_threads * 4);
}

BOOST_AUTO_TEST_SUITE_END()
