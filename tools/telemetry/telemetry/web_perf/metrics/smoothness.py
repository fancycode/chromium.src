# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from telemetry.perf_tests_helper import FlattenList
from telemetry.util import statistics
from telemetry.value import list_of_scalar_values
from telemetry.value import scalar
from telemetry.web_perf.metrics import rendering_stats
from telemetry.web_perf.metrics import timeline_based_metric


class SmoothnessMetric(timeline_based_metric.TimelineBasedMetric):
  def __init__(self):
    super(SmoothnessMetric, self).__init__()

  def AddResults(self, model, renderer_thread, interaction_records, results):
    self.VerifyNonOverlappedRecords(interaction_records)
    renderer_process = renderer_thread.parent
    stats = rendering_stats.RenderingStats(
      renderer_process, model.browser_process,
      [r.GetBounds() for r in interaction_records])

    input_event_latency = FlattenList(stats.input_event_latency)
    if input_event_latency:
      mean_input_event_latency = statistics.ArithmeticMean(
        input_event_latency)
      input_event_latency_discrepancy = statistics.DurationsDiscrepancy(
        input_event_latency)
      results.AddValue(scalar.ScalarValue(
          results.current_page, 'mean_input_event_latency', 'ms',
          round(mean_input_event_latency, 3)))
      results.AddValue(scalar.ScalarValue(
          results.current_page, 'input_event_latency_discrepancy', 'ms',
          round(input_event_latency_discrepancy, 4)))
    scroll_update_latency = FlattenList(stats.scroll_update_latency)
    if scroll_update_latency:
      mean_scroll_update_latency = statistics.ArithmeticMean(
        scroll_update_latency)
      scroll_update_latency_discrepancy = statistics.DurationsDiscrepancy(
        scroll_update_latency)
      results.AddValue(scalar.ScalarValue(
          results.current_page, 'mean_scroll_update_latency', 'ms',
          round(mean_scroll_update_latency, 3)))
      results.AddValue(scalar.ScalarValue(
          results.current_page, 'scroll_update_latency_discrepancy', 'ms',
          round(scroll_update_latency_discrepancy, 4)))
    gesture_scroll_update_latency = FlattenList(
        stats.gesture_scroll_update_latency)
    if gesture_scroll_update_latency:
      results.AddValue(scalar.ScalarValue(
          results.current_page, 'first_gesture_scroll_update_latency', 'ms',
          round(gesture_scroll_update_latency[0], 4)))

    # List of queueing durations.
    frame_queueing_durations = FlattenList(stats.frame_queueing_durations)
    if frame_queueing_durations:
      results.AddValue(list_of_scalar_values.ListOfScalarValues(
          results.current_page, 'queueing_durations', 'ms',
          frame_queueing_durations))

    # List of raw frame times.
    frame_times = FlattenList(stats.frame_times)
    results.AddValue(list_of_scalar_values.ListOfScalarValues(
        results.current_page, 'frame_times', 'ms', frame_times,
        description='List of raw frame times, helpful to understand the other '
                    'metrics.'))

    # Arithmetic mean of frame times.
    mean_frame_time = statistics.ArithmeticMean(frame_times)
    results.AddValue(scalar.ScalarValue(
        results.current_page, 'mean_frame_time', 'ms',
        round(mean_frame_time, 3),
        description='Arithmetic mean of frame times.'))

    # Absolute discrepancy of frame time stamps.
    frame_discrepancy = statistics.TimestampsDiscrepancy(
      stats.frame_timestamps)
    results.AddValue(scalar.ScalarValue(
        results.current_page, 'jank', 'ms', round(frame_discrepancy, 4),
        description='Absolute discrepancy of frame time stamps, where '
                    'discrepancy is a measure of irregularity. It quantifies '
                    'the worst jank. For a single pause, discrepancy '
                    'corresponds to the length of this pause in milliseconds. '
                    'Consecutive pauses increase the discrepancy. This metric '
                    'is important because even if the mean and 95th '
                    'percentile are good, one long pause in the middle of an '
                    'interaction is still bad.'))

    # Are we hitting 60 fps for 95 percent of all frames?
    # We use 19ms as a somewhat looser threshold, instead of 1000.0/60.0.
    percentile_95 = statistics.Percentile(frame_times, 95.0)
    results.AddValue(scalar.ScalarValue(
        results.current_page, 'mostly_smooth', 'score',
        1.0 if percentile_95 < 19.0 else 0.0,
        description='Were 95 percent of the frames hitting 60 fps?'
                    'boolean value (1/0).'))

    # Mean percentage of pixels approximated (missing tiles, low resolution
    # tiles, non-ideal resolution tiles).
    results.AddValue(scalar.ScalarValue(
        results.current_page, 'mean_pixels_approximated', 'percent',
        round(statistics.ArithmeticMean(
            FlattenList(stats.approximated_pixel_percentages)), 3),
        description='Percentage of pixels that were approximated '
                    '(checkerboarding, low-resolution tiles, etc.).'))
