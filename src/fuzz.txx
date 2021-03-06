/* SPDX-License-Identifier: MIT */
/* Copyright © 2020 Max Bachmann */
/* Copyright © 2011 Adam Cohen */

#include "details/SentenceView.hpp"
#include "fuzz.hpp"
#include "levenshtein.hpp"

#include <algorithm>
#include <cmath>
#include <difflib/difflib.h>
#include <iostream>
#include <iterator>
#include <tuple>
#include <vector>

namespace rapidfuzz {

template <typename Sentence1, typename Sentence2>
percent fuzz::ratio(const Sentence1& s1, const Sentence2& s2, const percent score_cutoff)
{
  double result = levenshtein::normalized_weighted_distance(
      utils::to_string_view(s1), utils::to_string_view(s2), score_cutoff / 100);
  return result * 100;
}

template <typename Sentence1, typename Sentence2, typename CharT>
percent fuzz::partial_ratio(const Sentence1& s1, const Sentence2& s2, percent score_cutoff)
{
  using std::get;

  if (score_cutoff > 100) {
    return 0;
  }

  auto s1_view = utils::to_string_view(s1);
  auto s2_view = utils::to_string_view(s2);

  if (s1_view.empty() || s2_view.empty()) {
    return static_cast<double>(s1_view.empty() && s2_view.empty()) * 100.0;
  }

  // when both strings have the same length there is only one possible alignment
  if (s1_view.length() == s2_view.length()) {
    return ratio(s1_view, s2_view, score_cutoff);
  }

  if (s1_view.length() > s2_view.length()) {
    std::swap(s1_view, s2_view);
  }

  size_t short_len = s1_view.length();

  // TODO: This can be done based on the levenshtein distance aswell, which
  // should be faster
  // TODO: this should accept different char types
  auto matcher = difflib::SequenceMatcher<basic_string_view<CharT>>(s1_view, s2_view);
  auto blocks = matcher.get_matching_blocks();

  // when there is a full match exit early
  for (const auto& block : blocks) {
    if (get<2>(block) == short_len) {
      return 100;
    }
  }

  double max_ratio = 0;
  for (const auto& block : blocks) {
    size_t long_start = (get<1>(block) > get<0>(block)) ? get<1>(block) - get<0>(block) : 0;
    auto long_substr = s2_view.substr(long_start, short_len);

    double ls_ratio = ratio(s1_view, long_substr, score_cutoff);

    if (ls_ratio > 99.5) {
      return 100;
    }

    if (ls_ratio > max_ratio) {
      score_cutoff = max_ratio = ls_ratio;
    }
  }

  return max_ratio;
}

template <typename Sentence1, typename Sentence2, typename CharT1, typename CharT2>
percent fuzz::token_sort_ratio(const Sentence1& s1, const Sentence2& s2, percent score_cutoff)
{
  if (score_cutoff > 100) return 0;

  return ratio(SentenceView<CharT1>(s1).sorted_split().join(),
               SentenceView<CharT2>(s2).sorted_split().join(), score_cutoff);
}

template <typename Sentence1, typename Sentence2, typename CharT1, typename CharT2>
percent fuzz::partial_token_sort_ratio(const Sentence1& s1, const Sentence2& s2,
                                       percent score_cutoff)
{
  if (score_cutoff > 100) return 0;

  return partial_ratio(SentenceView<CharT1>(s1).sorted_split().join(),
                       SentenceView<CharT2>(s2).sorted_split().join(), score_cutoff);
}

template <typename Sentence1, typename Sentence2>
percent fuzz::token_set_ratio(const Sentence1& s1, const Sentence2& s2, const percent score_cutoff)
{
  if (score_cutoff > 100) return 0;

  auto tokens_a = SentenceView<char_type<Sentence1>>(s1).sorted_split();
  auto tokens_b = SentenceView<char_type<Sentence2>>(s2).sorted_split();

  auto decomposition = utils::set_decomposition(tokens_a, tokens_b);
  auto intersect = decomposition.intersection;
  auto diff_ab = decomposition.difference_ab;
  auto diff_ba = decomposition.difference_ba;

  // one sentence is part of the other one
  if (!intersect.empty() && (diff_ab.empty() || diff_ba.empty())) {
    return 100;
  }

  auto diff_ab_joined = diff_ab.join();
  auto diff_ba_joined = diff_ba.join();

  size_t ab_len = diff_ab_joined.length();
  size_t ba_len = diff_ba_joined.length();
  size_t sect_len = intersect.length();

  // string length sect+ab <-> sect and sect+ba <-> sect
  size_t sect_ab_len = sect_len + !!sect_len + ab_len;
  size_t sect_ba_len = sect_len + !!sect_len + ba_len;

  auto lev_filter = levenshtein::detail::quick_lev_filter(utils::to_string_view(diff_ab_joined),
                                                          utils::to_string_view(diff_ba_joined),
                                                          score_cutoff / 100);

  percent result = 0;
  if (lev_filter.not_zero) {
    size_t dist = levenshtein::weighted_distance(lev_filter.s1_view, lev_filter.s2_view);
    result = utils::norm_distance(dist, sect_ab_len + sect_ba_len, score_cutoff);
  }

  // exit early since the other ratios are 0
  if (intersect.empty()) {
    return result;
  }

  // levenshtein distance sect+ab <-> sect and sect+ba <-> sect
  // since only sect is similar in them the distance can be calculated based on
  // the length difference
  std::size_t sect_ab_dist = !!sect_len + ab_len;
  percent sect_ab_ratio = utils::norm_distance(sect_ab_dist, sect_len + sect_ab_len, score_cutoff);

  std::size_t sect_ba_dist = !!sect_len + ba_len;
  percent sect_ba_ratio = utils::norm_distance(sect_ba_dist, sect_len + sect_ba_len, score_cutoff);

  return std::max({result, sect_ab_ratio, sect_ba_ratio});
}

template <typename Sentence1, typename Sentence2, typename CharT1, typename CharT2>
percent fuzz::partial_token_set_ratio(const Sentence1& s1, const Sentence2& s2,
                                      percent score_cutoff)
{
  if (score_cutoff > 100) return 0;

  auto decomposition = utils::set_decomposition(SentenceView<CharT1>(s1).sorted_split(),
                                                SentenceView<CharT2>(s2).sorted_split());

  // exit early when there is a common word in both sequences
  if (!decomposition.intersection.empty()) return 100;

  return partial_ratio(decomposition.difference_ab.join(), decomposition.difference_ab.join(),
                       score_cutoff);
}

template <typename Sentence1, typename Sentence2>
percent fuzz::token_ratio(const Sentence1& s1, const Sentence2& s2, percent score_cutoff)
{
  if (score_cutoff > 100) return 0;

  auto tokens_a = SentenceView<char_type<Sentence1>>(s1).sorted_split();
  auto tokens_b = SentenceView<char_type<Sentence2>>(s2).sorted_split();

  auto decomposition = utils::set_decomposition(tokens_a, tokens_b);
  auto intersect = decomposition.intersection;
  auto diff_ab = decomposition.difference_ab;
  auto diff_ba = decomposition.difference_ba;

  if (!intersect.empty() && (diff_ab.empty() || diff_ba.empty())) {
    return 100;
  }

  auto diff_ab_joined = diff_ab.join();
  auto diff_ba_joined = diff_ba.join();

  size_t ab_len = diff_ab_joined.length();
  size_t ba_len = diff_ba_joined.length();
  size_t sect_len = intersect.length();

  percent result = ratio(tokens_a.join(), tokens_b.join(), score_cutoff);

  // string length sect+ab <-> sect and sect+ba <-> sect
  size_t sect_ab_len = sect_len + !!sect_len + ab_len;
  size_t sect_ba_len = sect_len + !!sect_len + ba_len;

  auto lev_filter = levenshtein::detail::quick_lev_filter(utils::to_string_view(diff_ab_joined),
                                                          utils::to_string_view(diff_ba_joined),
                                                          score_cutoff / 100);

  if (lev_filter.not_zero) {
    size_t dist = levenshtein::weighted_distance(lev_filter.s1_view, lev_filter.s2_view);
    result = std::max(result, utils::norm_distance(dist, 2 * sect_ba_len, score_cutoff));
  }

  // exit early since the other ratios are 0
  if (!sect_len) {
    return result;
  }

  // levenshtein distance sect+ab <-> sect and sect+ba <-> sect
  // since only sect is similar in them the distance can be calculated based on
  // the length difference
  std::size_t sect_ab_dist = !!sect_len + ab_len;
  percent sect_ab_ratio = utils::norm_distance(sect_ab_dist, sect_len + sect_ab_len, score_cutoff);

  std::size_t sect_ba_dist = !!sect_len + ba_len;
  percent sect_ba_ratio = utils::norm_distance(sect_ba_dist, sect_len + sect_ba_len, score_cutoff);

  return std::max({result, sect_ab_ratio, sect_ba_ratio});
}

template <typename Sentence1, typename Sentence2, typename CharT1, typename CharT2>
percent fuzz::partial_token_ratio(const Sentence1& s1, const Sentence2& s2, percent score_cutoff)
{
  if (score_cutoff > 100) return 0;

  auto tokens_a = SentenceView<char_type<Sentence1>>(s1).sorted_split();
  auto tokens_b = SentenceView<char_type<Sentence2>>(s2).sorted_split();

  auto decomposition = utils::set_decomposition(tokens_a, tokens_b);

  // exit early when there is a common word in both sequences
  if (!decomposition.intersection.empty()) return 100;

  auto diff_ab = decomposition.difference_ab;
  auto diff_ba = decomposition.difference_ba;

  percent result = partial_ratio(tokens_a.join(), tokens_b.join(), score_cutoff);

  // do not calculate the same partial_ratio twice
  if (tokens_a.word_count() == diff_ab.word_count() &&
      tokens_b.word_count() == diff_ba.word_count()) {
    return result;
  }

  score_cutoff = std::max(score_cutoff, result);
  return std::max(result, partial_ratio(diff_ab.join(), diff_ba.join(), score_cutoff));
}

template <typename Sentence1, typename Sentence2>
percent fuzz::quick_lev_ratio(const Sentence1& s1, const Sentence2& s2, percent score_cutoff)
{
  if (utils::is_zero(length_ratio(s1, s2, score_cutoff))) {
    return 0;
  }
  size_t distance = utils::count_uncommon_chars(s1, s2);
  size_t lensum = s1.length() + s2.length();
  return utils::norm_distance(distance, lensum, score_cutoff);
}

template <typename Sentence1, typename Sentence2>
percent fuzz::length_ratio(const Sentence1& s1, const Sentence2& s2, percent score_cutoff)
{
  size_t s1_len = s1.length();
  size_t s2_len = s2.length();
  size_t distance = (s1_len > s2_len) ? s1_len - s2_len : s2_len - s1_len;

  size_t lensum = s1_len + s2_len;
  return utils::norm_distance(distance, lensum, score_cutoff);
}

template <typename Sentence1, typename Sentence2>
percent fuzz::WRatio(const Sentence1& s1, const Sentence2& s2, percent score_cutoff)
{
  if (score_cutoff > 100) return 0;

  constexpr double UNBASE_SCALE = 0.95;

  auto s1_view = utils::to_string_view(s1);
  auto s2_view = utils::to_string_view(s2);

  size_t len_a = s1_view.length();
  size_t len_b = s2_view.length();
  double len_ratio = (len_a > len_b) ? static_cast<double>(len_a) / static_cast<double>(len_b)
                                     : static_cast<double>(len_b) / static_cast<double>(len_a);

  if (len_ratio < 1.5) {
    auto lev_filter = levenshtein::detail::quick_lev_filter(s1_view, s2_view, score_cutoff / 100);

    // ratio and token_sort ratio are not required so token_set_ratio /
    // partial_token_set_ratio is enough
    if (!lev_filter.not_zero) {
      return token_set_ratio(s1, s2, score_cutoff / UNBASE_SCALE) * UNBASE_SCALE;
    }

    size_t dist = levenshtein::weighted_distance(lev_filter.s1_view, lev_filter.s2_view);
    percent end_ratio = utils::norm_distance(dist, len_a + len_b, score_cutoff);

    score_cutoff = std::max(score_cutoff, end_ratio + 0.00001) / UNBASE_SCALE;
    return std::max(end_ratio, token_ratio(s1_view, s2_view, score_cutoff) * UNBASE_SCALE);
  }

  percent end_ratio = ratio(s1, s2, score_cutoff);

  const double PARTIAL_SCALE = (len_ratio < 8.0) ? 0.9 : 0.6;

  // increase the score_cutoff by a small step so it might be able to exit early
  score_cutoff = std::max(score_cutoff, end_ratio + 0.00001) / PARTIAL_SCALE;
  end_ratio = std::max(end_ratio, partial_ratio(s1, s2, score_cutoff) * PARTIAL_SCALE);

  score_cutoff = std::max(score_cutoff, end_ratio + 0.00001) / UNBASE_SCALE;
  return std::max(end_ratio,
                  partial_token_ratio(s1, s2, score_cutoff) * UNBASE_SCALE * PARTIAL_SCALE);
}

} // namespace rapidfuzz
