#include "../include/ops_omp.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <span>
#include <vector>

#include "core/util/include/util.hpp"

namespace {
bool CheckCollinearity(std::span<double> raw_points) {
  const auto points_count = raw_points.size() / 2;
  if (points_count < 3) {
    return true;
  }
  const auto dx = raw_points[2] - raw_points[0];
  const auto dy = raw_points[3] - raw_points[1];
  for (size_t i = 2; i < points_count; i++) {
    const auto dx_i = raw_points[(i * 2)] - raw_points[0];
    const auto dy_i = raw_points[(i * 2) + 1] - raw_points[1];
    if (std::fabs((dx * dy_i) - (dy * dx_i)) > 1e-9) {
      return false;
    }
  }
  return true;
}
bool ComparePoints(const Point &p0, const Point &p1, const Point &p2) {
  const auto dx1 = p1[0] - p0[0];
  const auto dy1 = p1[1] - p0[1];
  const auto dx2 = p2[0] - p0[0];
  const auto dy2 = p2[1] - p0[1];
  const auto cross = (dx1 * dy2) - (dy1 * dx2);
  if (std::abs(cross) < 1e-9) {
    return (dx1 * dx1 + dy1 * dy1) < (dx2 * dx2 + dy2 * dy2);
  }
  return cross > 0;
}
double CrossProduct(const Point &p0, const Point &p1, const Point &p2) {
  return ((p1[0] - p0[0]) * (p2[1] - p0[1])) - ((p1[1] - p0[1]) * (p2[0] - p0[0]));
}
}  // namespace

namespace shvedova_v_graham_convex_hull_omp {

bool GrahamConvexHullOMP::ValidationImpl() {
  return (task_data->inputs.size() == 1 && task_data->inputs_count.size() == 1 && task_data->outputs.size() == 2 &&
          task_data->outputs_count.size() == 2 && (task_data->inputs_count[0] % 2 == 0) &&
          (task_data->inputs_count[0] / 2 > 2) && (task_data->outputs_count[0] == 1) &&
          (task_data->outputs_count[1] >= task_data->inputs_count[0])) &&
         !CheckCollinearity({reinterpret_cast<double *>(task_data->inputs[0]), task_data->inputs_count[0]});
}

bool GrahamConvexHullOMP::PreProcessingImpl() {
  points_count_ = static_cast<int>(task_data->inputs_count[0] / 2);
  input_.resize(points_count_, Point{});

  auto *p_src = reinterpret_cast<double *>(task_data->inputs[0]);
  for (int i = 0; i < points_count_ * 2; i += 2) {
    input_[i / 2][0] = p_src[i];
    input_[i / 2][1] = p_src[i + 1];
  }

  res_.clear();
  res_.reserve(points_count_);

  return true;
}

void GrahamConvexHullOMP::PerformSort() {
  const auto pivot = *std::ranges::min_element(input_, [](auto &a, auto &b) { return a[1] < b[1]; });

  const int threadsnum = std::min(points_count_, ppc::util::GetPPCNumThreads());
  const int perthread = points_count_ / threadsnum;
  const int unfit = points_count_ % threadsnum;

  std::vector<std::span<Point>> fragments(threadsnum);
  auto it = input_.begin();
  for (int i = 0; i < threadsnum; i++) {
    auto nit = std::next(it, perthread + ((i < unfit) ? 1 : 0));
    fragments[i] = std::span{it, nit};
    it = nit;
  }

  const auto comp = [&](const Point &p1, const Point &p2) { return ComparePoints(pivot, p1, p2); };
  const auto merge = [&](std::span<Point> &primary, std::span<Point> &follower) {
    std::inplace_merge(primary.begin(), follower.begin(), follower.end(), comp);
    primary = std::span{primary.begin(), follower.end()};
  };

#pragma omp parallel for
  for (int i = 0; i < threadsnum; i++) {
    std::ranges::sort(fragments[i], comp);
  }

  for (std::size_t i = 1, iter = threadsnum; iter > 1; i *= 2, iter /= 2) {
    const auto factor = iter / 2;
#pragma omp parallel for if (fragments.front().size() > 24)
    for (int k = 0; k < static_cast<int>(factor); ++k) {
      merge(fragments[2 * i * k], fragments[(2 * i * k) + i]);
    }
    if ((iter / 2) == 1) {
      merge(fragments.front(), fragments.back());
    } else if ((iter / 2) % 2 != 0) {
      merge(fragments[2 * i * (factor - 2)], fragments[2 * i * (factor - 1)]);
    }
  }
}

bool GrahamConvexHullOMP::RunImpl() {
  PerformSort();
  res_.push_back(input_[0]);
  res_.push_back(input_[1]);
  for (int i = 2; i < points_count_; ++i) {
    while (res_.size() > 1 && CrossProduct(res_[res_.size() - 2], res_.back(), input_[i]) <= 0) {
      res_.pop_back();
    }
    res_.push_back(input_[i]);
  }
  return true;
}

bool GrahamConvexHullOMP::PostProcessingImpl() {
  int res_points_count = static_cast<int>(res_.size());
  *reinterpret_cast<int *>(task_data->outputs[0]) = res_points_count;
  auto *p_out = reinterpret_cast<double *>(task_data->outputs[1]);
  for (int i = 0; i < res_points_count; i++) {
    p_out[2 * i] = res_[i][0];
    p_out[(2 * i) + 1] = res_[i][1];
  }
  return true;
}

}  // namespace shvedova_v_graham_convex_hull_omp

namespace shvedova_v_graham_convex_hull_seq {

bool GrahamConvexHullSequential::ValidationImpl() {
  return (task_data->inputs.size() == 1 && task_data->inputs_count.size() == 1 && task_data->outputs.size() == 2 &&
          task_data->outputs_count.size() == 2 && (task_data->inputs_count[0] % 2 == 0) &&
          (task_data->inputs_count[0] / 2 > 2) && (task_data->outputs_count[0] == 1) &&
          (task_data->outputs_count[1] >= task_data->inputs_count[0])) &&
         !CheckCollinearity({reinterpret_cast<double *>(task_data->inputs[0]), task_data->inputs_count[0]});
}

bool GrahamConvexHullSequential::PreProcessingImpl() {
  points_count_ = static_cast<int>(task_data->inputs_count[0] / 2);
  input_.resize(points_count_, Point{});

  auto *p_src = reinterpret_cast<double *>(task_data->inputs[0]);
  for (int i = 0; i < points_count_ * 2; i += 2) {
    input_[i / 2][0] = p_src[i];
    input_[i / 2][1] = p_src[i + 1];
  }

  res_.clear();
  res_.reserve(points_count_);

  return true;
}

void GrahamConvexHullSequential::PerformSort() {  // NOLINT(*cognit*)
  const auto cmp = [](const Point &p0, const Point &p1, const Point &p2) {
    const auto calc_ang = [](const Point &o, const Point &p) {
      const auto dx = p[0] - o[0];
      const auto dy = p[1] - o[1];
      if (dx == 0. && dy == 0.) {
        return -1.;
      }
      return (dy >= 0) ? (dx >= 0 ? dy / (dx + dy)  // NOLINT(*nest*)
                                  : 1 - (dx / (-dx + dy)))
                       : (dx < 0 ? 2 - (dy / (-dx - dy)) : 3 + (dx / (dx - dy)));
    };
    const auto ang1 = calc_ang(p0, p1);
    const auto ang2 = calc_ang(p0, p2);
    double exp1 = std::pow(p1[0] - p0[0], 2);
    double exp2 = std::pow(p1[1] - p0[1], 2);
    double exp3 = std::pow(p2[0] - p0[0], 2);
    double exp4 = std::pow(p2[1] - p0[1], 2);

    return (ang1 < ang2) || ((ang1 > ang2) ? false : (exp1 + exp2 - exp3 - exp4 > 0));
  };

  const auto pivot = *std::ranges::min_element(input_, [](auto &a, auto &b) { return a[1] < b[1]; });
  for (int pt = 0; pt < points_count_; pt++) {
    const bool ev = pt % 2 == 0;
    const auto [shift, revshift] = ev ? std::make_pair(0, -1) : std::make_pair(-1, 0);
    for (int i = 1; i < points_count_ + shift; i += 2) {
      if (cmp(pivot, input_[i - shift], input_[i + revshift])) {
        std::swap(input_[i], input_[i - (ev ? 1 : -1)]);
      }
    }
  }
}

bool GrahamConvexHullSequential::RunImpl() {
  PerformSort();

  for (int i = 0; i < 3; i++) {
    res_.push_back(input_[i]);
  }

  for (int i = 3; i < points_count_; ++i) {
    while (res_.size() > 1) {
      const auto &pv = res_.back();
      const auto dx1 = res_.rbegin()[1][0] - pv[0];
      const auto dy1 = res_.rbegin()[1][1] - pv[1];
      const auto dx2 = input_[i][0] - pv[0];
      const auto dy2 = input_[i][1] - pv[1];
      if (dx1 * dy2 < dy1 * dx2) {
        break;
      }
      res_.pop_back();
    }
    res_.push_back(input_[i]);
  }

  return true;
}

bool GrahamConvexHullSequential::PostProcessingImpl() {
  int res_points_count = static_cast<int>(res_.size());
  *reinterpret_cast<int *>(task_data->outputs[0]) = res_points_count;
  auto *p_out = reinterpret_cast<double *>(task_data->outputs[1]);
  for (int i = 0; i < res_points_count; i++) {
    p_out[2 * i] = res_[i][0];
    p_out[(2 * i) + 1] = res_[i][1];
  }
  return true;
}

}  // namespace shvedova_v_graham_convex_hull_seq
