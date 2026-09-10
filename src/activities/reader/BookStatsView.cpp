#include "BookStatsView.h"

#include <FreeInkUICore.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/TouchActionButtons.h"
#include "components/TouchHeaderBackButton.h"
#include "components/TouchRegistry.h"
#include "components/UITheme.h"
#include "components/icons/listIcons.h"
#include "fontIds.h"

namespace {
constexpr int kStatsButtonHintTopGap = 10;
constexpr int kHeatmapTargetWeeks = 18;
constexpr int kHeatmapMaxWeeks = (READING_HISTORY_DAYS + 6) / 7;
constexpr int kHeatmapMinCellStride = 8;
constexpr int kHeatmapMaxCellStride = 26;
constexpr int kHeatmapSidePadding = 12;
constexpr int kHeatmapDayLabelGap = 8;
constexpr int kHeatmapLegendCell = 12;
constexpr int kHeatmapLegendTopGap = 8;
constexpr int kHeatmapLegendLabelGap = 6;
constexpr int kHeatmapLegendItemGap = 18;
constexpr int kMonthSidePadding = 12;
constexpr int kMonthHeaderGap = 8;
constexpr int kMonthCellGap = 5;
constexpr int kMonthMinCellStride = 18;
constexpr int kMonthMaxRowStride = 60;
constexpr int kStreakSummaryCardPadding = 28;
constexpr int kStandaloneNoRtcMaxTopCardHeightDivisor = 2;
constexpr int kStandaloneNoRtcMaxVerticalOffset = 32;
constexpr int kPerBookRtcTopCardMaxExtra = 84;

struct StatsLayout {
  int headerHeight;
  int headerDrawHeight;
  int topGap;
  int cardGap;
  int topCardTitleH;
  int topCardH;
  int globalCardH;
  int sectionTitleH;
  int sectionTitleFontId;
  int chartLabelFontId;
  int chartLabelW;
  int barH;
  int barGap;
  int chartTopPadding;
  int chartBottomPadding;
};

constexpr StatsLayout kDefaultLayout = {
    .headerHeight = 78,
    .headerDrawHeight = 67,
    .topGap = 8,
    .cardGap = 26,
    .topCardTitleH = 36,
    .topCardH = 214,
    .globalCardH = 154,
    .sectionTitleH = 34,
    .sectionTitleFontId = UI_10_FONT_ID,
    .chartLabelFontId = UI_10_FONT_ID,
    .chartLabelW = 88,
    .barH = 22,
    .barGap = 12,
    .chartTopPadding = 14,
    .chartBottomPadding = 14,
};

constexpr StatsLayout kCompactLayout = {
    .headerHeight = 67,
    .headerDrawHeight = 67,
    .topGap = 6,
    .cardGap = 8,
    .topCardTitleH = 30,
    .topCardH = 156,
    .globalCardH = 110,
    .sectionTitleH = 30,
    .sectionTitleFontId = UI_10_FONT_ID,
    .chartLabelFontId = SMALL_FONT_ID,
    .chartLabelW = 78,
    .barH = 16,
    .barGap = 8,
    .chartTopPadding = 8,
    .chartBottomPadding = 8,
};

constexpr std::array<StrId, READING_TIME_BUCKET_COUNT> TIME_BUCKET_LABELS = {
    StrId::STR_STATS_MORNING, StrId::STR_STATS_AFTERNOON, StrId::STR_STATS_EVENING, StrId::STR_STATS_NIGHT};
constexpr std::array<StrId, READING_DAY_OF_WEEK_COUNT> DAY_LABELS = {
    StrId::STR_STATS_MON, StrId::STR_STATS_TUE, StrId::STR_STATS_WED, StrId::STR_STATS_THU,
    StrId::STR_STATS_FRI, StrId::STR_STATS_SAT, StrId::STR_STATS_SUN};

const char* dayCountText(const uint16_t days) { return days == 1 ? tr(STR_STATS_DAY) : tr(STR_STATS_DAYS); }

void formatStreakDayCount(const uint16_t days, char* buf, const size_t len) {
  if (days > 0) {
    snprintf(buf, len, "%u", static_cast<unsigned>(days));
  } else {
    snprintf(buf, len, "-");
  }
}

int sectionCardHeight(const StatsLayout& layout, const int rowCount) {
  if (rowCount <= 0) {
    return layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding;
  }
  const int rowStride = layout.barH + layout.barGap;
  return layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding + layout.barH +
         (rowCount - 1) * rowStride;
}

bool shouldShowRtcBasedStats() { return halClock.isAvailable(); }

int noRtcCardBaseHeight(const StatsLayout& layout) { return layout.globalCardH; }

int statsHeaderHeight(const ThemeMetrics& metrics, const StatsLayout& layout, const MappedInputManager* mappedInput) {
  if (mappedInput && mappedInput->hasTouchHardware()) {
    return CompactHeader::height(metrics);
  }
  return std::min(metrics.headerHeight, layout.headerHeight);
}

int statsContentHeight(const StatsLayout& layout, const int headerHeight, const bool globalPage,
                       const bool showRtcStats) {
  const int topCardH = globalPage ? layout.globalCardH : layout.topCardH;
  if (!showRtcStats) {
    return headerHeight + layout.topGap + topCardH;
  }
  const int timeOfDayH = sectionCardHeight(layout, static_cast<int>(TIME_BUCKET_LABELS.size()));
  const int dayOfWeekH = sectionCardHeight(layout, static_cast<int>(DAY_LABELS.size()));
  return headerHeight + layout.topGap + topCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
}

int noRtcCombinedContentHeight(const StatsLayout& layout, const int headerHeight, const bool showAllDevicesStats) {
  const int cardBaseH = noRtcCardBaseHeight(layout);
  return headerHeight + layout.topGap + cardBaseH + layout.cardGap + layout.globalCardH +
         (showAllDevicesStats ? layout.cardGap + layout.globalCardH : 0);
}

int statsBottomInset(const ThemeMetrics& metrics, const bool showButtonHints) {
  return metrics.verticalSpacing + (showButtonHints ? metrics.buttonHintsHeight + kStatsButtonHintTopGap : 0);
}

int perBookRtcTopCardHeight(const StatsLayout& layout, const int extraHeight) {
  return layout.topCardH + std::min(extraHeight, kPerBookRtcTopCardMaxExtra);
}

int globalRtcCardHeightForPerBookRowSpacing(const StatsLayout& layout, const int perBookExtraHeight) {
  constexpr int perBookDataRowCount = 3;
  constexpr int globalDataRowCount = 2;
  const int perBookDataRowH =
      (perBookRtcTopCardHeight(layout, perBookExtraHeight) - layout.topCardTitleH) / perBookDataRowCount;
  return std::max(layout.globalCardH, layout.topCardTitleH + perBookDataRowH * globalDataRowCount);
}

const StatsLayout& getStatsLayout(const GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                  const bool globalPage, const bool showButtonHints, const bool showRtcStats) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  const int defaultHeaderHeight = statsHeaderHeight(metrics, kDefaultLayout, mappedInput);
  const bool defaultFitsCurrentPage =
      statsContentHeight(kDefaultLayout, defaultHeaderHeight, globalPage, showRtcStats) <= availableHeight;
  const bool defaultMatchesPerBookCharts =
      !globalPage || !showRtcStats ||
      statsContentHeight(kDefaultLayout, defaultHeaderHeight, false, showRtcStats) <= availableHeight;
  if (defaultFitsCurrentPage && defaultMatchesPerBookCharts) {
    return kDefaultLayout;
  }
  return kCompactLayout;
}

const StatsLayout& getNoRtcCombinedLayout(const GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                          const bool showButtonHints, const bool showAllDevicesStats) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  if (noRtcCombinedContentHeight(kDefaultLayout, statsHeaderHeight(metrics, kDefaultLayout, mappedInput),
                                 showAllDevicesStats) <= availableHeight) {
    return kDefaultLayout;
  }
  return kCompactLayout;
}

bool fallbackEstimatedTimeLeft(const BookReadingStats& stats, const float progressPercent, uint32_t& seconds) {
  seconds = 0;
  if (progressPercent <= 0.0f || progressPercent >= 100.0f || stats.totalReadingSeconds < 120) {
    return false;
  }

  const float progress = progressPercent / 100.0f;
  const float estimate = (static_cast<float>(stats.totalReadingSeconds) * (1.0f - progress)) / progress;
  if (estimate <= 0.0f) {
    return false;
  }
  seconds = static_cast<uint32_t>(estimate + 0.5f);
  return seconds > 0;
}

bool cachedEstimatedTimeLeft(const BookReadingStats& stats, uint32_t& seconds) {
  seconds = stats.estimatedTimeLeftSeconds;
  return seconds > 0;
}

bool estimateFinishDateFromDailyPace(const BookReadingStats& stats, const ReadingStatsDateTime& today,
                                     const uint32_t estimatedReadingSeconds, ReadingStatsDate& outDate) {
  outDate = {};
  if (!today.isValid() || !stats.startDate.isValid() || estimatedReadingSeconds == 0 ||
      stats.totalReadingSeconds == 0) {
    return false;
  }

  const uint16_t elapsedDays = readingSpanDaysElapsed(stats.startDate, today.date);
  const uint16_t readingDays = std::max<uint16_t>(1, elapsedDays);

  // Convert remaining reading time into calendar time using the book's average reading seconds per calendar day.
  const uint64_t estimatedCalendarSeconds =
      (static_cast<uint64_t>(estimatedReadingSeconds) * static_cast<uint64_t>(readingDays) * 86400ULL +
       static_cast<uint64_t>(stats.totalReadingSeconds) / 2ULL) /
      static_cast<uint64_t>(stats.totalReadingSeconds);
  if (estimatedCalendarSeconds == 0) {
    return false;
  }

  ReadingStatsDateTime estimatedFinish = today;
  addSecondsToReadingStatsDateTime(estimatedFinish,
                                   static_cast<uint32_t>(std::min<uint64_t>(estimatedCalendarSeconds, UINT32_MAX)));
  outDate = estimatedFinish.date;
  return outDate.isValid();
}

float pagesPerMinute(const uint32_t totalPagesTurned, const uint32_t totalReadingSeconds) {
  if (totalReadingSeconds <= 60) {
    return 0.0f;
  }
  return static_cast<float>(totalPagesTurned) * 60.0f / static_cast<float>(totalReadingSeconds);
}

void drawCenteredLabel(const GfxRenderer& renderer, const int fontId, const int x, const int w, const int y,
                       const char* text, const bool bold = false) {
  const int textWidth = renderer.getTextWidth(fontId, text, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  renderer.drawText(fontId, x + (w - textWidth) / 2, y, text, true,
                    bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}

void drawStatCell(const GfxRenderer& renderer, const int x, const int w, const int y, const int h, const char* value,
                  const char* label) {
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int totalTextH = valueLineH + 4 + labelLineH;
  const int textY = y + (h - totalTextH) / 2;
  drawCenteredLabel(renderer, UI_12_FONT_ID, x, w, textY, value, true);
  drawCenteredLabel(renderer, SMALL_FONT_ID, x, w, textY + valueLineH + 4, label);
}

void drawSectionCard(const GfxRenderer& renderer, const int x, const int y, const int w, const int h, const char* title,
                     const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.sectionTitleH, x + w, y + layout.sectionTitleH);
  drawCenteredLabel(renderer, layout.sectionTitleFontId, x, w,
                    y + (layout.sectionTitleH - renderer.getLineHeight(layout.sectionTitleFontId)) / 2, title, true);
}

template <size_t N>
void drawHorizontalBars(GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                        const std::array<uint32_t, N>& values, const std::array<StrId, N>& labels,
                        const StatsLayout& layout) {
  constexpr int labelLeftPadding = 10;
  constexpr int labelRightPadding = 18;
  constexpr int barLeftGap = 8;
  constexpr int rightPadding = 18;
  const uint32_t maxValue = *std::max_element(values.begin(), values.end());
  const int labelLineH = renderer.getLineHeight(layout.chartLabelFontId);
  const int rowContentH = std::max(labelLineH, layout.barH);
  const int baseContentH = layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding + rowContentH +
                           (static_cast<int>(N) - 1) * (rowContentH + layout.barGap);
  const int extraHeight = std::max(0, h - baseContentH);
  const int spacingSlotCount = static_cast<int>(N) + 1;
  const int extraPerSlot = spacingSlotCount > 0 ? extraHeight / spacingSlotCount : 0;
  const int extraRemainder = spacingSlotCount > 0 ? extraHeight % spacingSlotCount : 0;
  const int topPadding = layout.chartTopPadding + extraPerSlot + (extraRemainder > 0 ? 1 : 0);
  const int rowGap = layout.barGap + extraPerSlot;
  const int contentTop = y + layout.sectionTitleH + topPadding;
  const int rowStride = rowContentH + rowGap;
  int maxLabelW = 0;
  for (size_t i = 0; i < N; ++i) {
    maxLabelW = std::max(maxLabelW, renderer.getTextWidth(layout.chartLabelFontId, I18N.get(labels[i])));
  }
  const int labelColumnW = std::max(layout.chartLabelW, labelLeftPadding + maxLabelW + labelRightPadding);
  const int barX = x + labelColumnW + barLeftGap;
  const int barW = std::max(0, w - labelColumnW - barLeftGap - rightPadding);
  for (size_t i = 0; i < N; ++i) {
    const int rowTop = contentTop + static_cast<int>(i) * rowStride;
    const int labelY = rowTop + (rowContentH - labelLineH) / 2;
    const int barY = rowTop + (rowContentH - layout.barH) / 2;
    renderer.drawText(layout.chartLabelFontId, x + labelLeftPadding, labelY, I18N.get(labels[i]));
    if (maxValue > 0 && values[i] > 0) {
      const int fillW = std::max(2, static_cast<int>((static_cast<uint64_t>(barW) * values[i]) / maxValue));
      renderer.fillRect(barX, barY, fillW, layout.barH, true);
    }
  }
}

void drawPerBookStatsCard(GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                          const std::string& bookTitle, const BookReadingStats& stats, const float progressPercent,
                          const bool hasEstimatedTimeLeft, const uint32_t estimatedTimeLeftSeconds,
                          const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.topCardTitleH, x + w, y + layout.topCardTitleH);
  const std::string visibleTitle =
      renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(), w - 20, EpdFontFamily::BOLD);
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, w,
                    y + (layout.topCardTitleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, visibleTitle.c_str(), true);

  const bool showRtcStats = shouldShowRtcBasedStats();
  const int thirdW = w / 3;
  const int halfW = w / 2;
  const int rowCount = showRtcStats ? 3 : 2;
  const int rowH = (h - layout.topCardTitleH) / rowCount;
  char buf[40];

  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(stats.sessionCount));
  drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_SESSIONS_LBL));

  BookReadingStats::formatDuration(stats.totalReadingSeconds, buf, sizeof(buf));
  drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_TIME_LBL));

  if (progressPercent >= 0.0f) {
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(progressPercent + 0.5f));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, x + thirdW * 2, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_PROGRESS_LBL));

  const uint32_t avgSecs = stats.sessionCount > 0 ? stats.totalReadingSeconds / stats.sessionCount : 0;
  BookReadingStats::formatDuration(avgSecs, buf, sizeof(buf));
  drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_AVG_SESSION_LBL));

  uint32_t fallbackEstimateSeconds = 0;
  uint32_t cachedEstimateSeconds = 0;
  const bool hasCachedEstimate = cachedEstimatedTimeLeft(stats, cachedEstimateSeconds);
  const bool hasFallbackEstimate = fallbackEstimatedTimeLeft(stats, progressPercent, fallbackEstimateSeconds);
  if (!stats.isCompleted && (hasEstimatedTimeLeft || hasCachedEstimate || hasFallbackEstimate)) {
    formatCompactReadingDuration(hasEstimatedTimeLeft ? estimatedTimeLeftSeconds
                                 : hasCachedEstimate  ? cachedEstimateSeconds
                                                      : fallbackEstimateSeconds,
                                 buf, sizeof(buf));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_TIME_LEFT));

  snprintf(buf, sizeof(buf), "%.1f", pagesPerMinute(stats.totalPagesTurned, stats.totalReadingSeconds));
  drawStatCell(renderer, x + thirdW * 2, thirdW, y + layout.topCardTitleH + rowH, rowH, buf,
               tr(STR_STATS_PAGES_PER_MIN));

  if (!showRtcStats) {
    return;
  }

  ReadingStatsDateTime today;
  const bool hasToday = getCurrentLocalReadingStatsDateTime(today);
  const ReadingStatsDate endDate = stats.isCompleted && stats.finishedDate.isValid()
                                       ? stats.finishedDate
                                       : (hasToday ? today.date : ReadingStatsDate{});
  const bool hasDaySpan = stats.startDate.isValid() && endDate.isValid();
  const uint16_t daysReading = hasDaySpan ? readingSpanDaysElapsed(stats.startDate, endDate) : 0;
  if (hasDaySpan) {
    snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(daysReading), dayCountText(daysReading));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  char startedLabel[32];
  char dateBuf[24];
  formatReadingStatsShortDate(stats.startDate, dateBuf, sizeof(dateBuf));
  snprintf(startedLabel, sizeof(startedLabel), "%s %s", tr(STR_STATS_STARTED), dateBuf);
  const int startedY = y + layout.topCardTitleH + rowH * 2;
  TouchRegistry::getInstance().add(Rect(x, startedY, halfW, rowH), BookStatsTouchTarget::StartedDaysStat,
                                   TouchRegistry::Item);
  drawStatCell(renderer, x, halfW, startedY, rowH, buf, startedLabel);

  ReadingStatsDate finishDisplayDate;
  bool finished = stats.isCompleted;
  if (finished) {
    finishDisplayDate = stats.finishedDate;
  } else if (hasToday && (hasEstimatedTimeLeft || hasCachedEstimate || hasFallbackEstimate)) {
    const uint32_t remainingReadingSeconds = hasEstimatedTimeLeft ? estimatedTimeLeftSeconds
                                             : hasCachedEstimate  ? cachedEstimateSeconds
                                                                  : fallbackEstimateSeconds;
    if (!estimateFinishDateFromDailyPace(stats, today, remainingReadingSeconds, finishDisplayDate)) {
      ReadingStatsDateTime estimatedFinish = today;
      addSecondsToReadingStatsDateTime(estimatedFinish, remainingReadingSeconds);
      finishDisplayDate = estimatedFinish.date;
    }
  }
  formatReadingStatsShortDate(finishDisplayDate, buf, sizeof(buf));
  drawStatCell(renderer, x + halfW, halfW, y + layout.topCardTitleH + rowH * 2, rowH, buf,
               finished ? tr(STR_STATS_FINISHED_DATE) : tr(STR_STATS_EST_FINISH_DATE));
}

void drawGlobalStatsCard(GfxRenderer& renderer, const int x, const int y, const int w, const int h, const char* title,
                         const GlobalReadingStats& stats, const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.topCardTitleH, x + w, y + layout.topCardTitleH);
  const bool showRtcStats = shouldShowRtcBasedStats();
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, w,
                    y + (layout.topCardTitleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, title, true);

  const int thirdW = w / 3;
  const int halfW = w / 2;
  const int rowH = (h - layout.topCardTitleH) / 2;
  char buf[40];

  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.totalSessions));
  drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_SESSIONS_LBL));

  BookReadingStats::formatDuration(stats.totalReadingSeconds, buf, sizeof(buf));
  drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_TIME_LBL));

  snprintf(buf, sizeof(buf), "%.1f", pagesPerMinute(stats.totalPagesTurned, stats.totalReadingSeconds));
  drawStatCell(renderer, x + thirdW * 2, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_PAGES_PER_MIN));

  const uint32_t avgSecs = stats.totalSessions > 0 ? stats.totalReadingSeconds / stats.totalSessions : 0;
  BookReadingStats::formatDuration(avgSecs, buf, sizeof(buf));
  if (showRtcStats) {
    drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_AVG_SESSION_LBL));
  } else {
    drawStatCell(renderer, x, halfW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_AVG_SESSION_LBL));
  }

  if (showRtcStats) {
    ReadingStatsDateTime today;
    const bool hasToday = getCurrentLocalReadingStatsDateTime(today);
    const uint16_t currentStreak = hasToday ? stats.currentReadingStreak(&today.date) : 0;
    if (currentStreak > 0) {
      snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(currentStreak), dayCountText(currentStreak));
    } else {
      snprintf(buf, sizeof(buf), "-");
    }
    drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH + rowH, rowH, buf,
                 tr(STR_STATS_READING_STREAK_LBL));
  }

  if (stats.completedBooks > 0) {
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.completedBooks));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, showRtcStats ? x + thirdW * 2 : x + halfW, showRtcStats ? thirdW : halfW,
               y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_COMPLETED_LBL));
}

void drawDateField(const GfxRenderer& renderer, const int x, const int y, const int w, const char* text,
                   const bool selected, const int touchTarget = -1) {
  const int h = renderer.getLineHeight(UI_12_FONT_ID) + 10;
  if (touchTarget >= 0) {
    TouchRegistry::getInstance().add(Rect(x, y, w, h), touchTarget, TouchRegistry::Item);
  }
  renderer.fillRectDither(x, y, w, h, selected ? Color::LightGray : Color::White);
  renderer.drawRect(x, y, w, h, true);
  if (selected) {
    renderer.drawRect(x + 1, y + 1, w - 2, h - 2, true);
  }
  drawCenteredLabel(renderer, UI_12_FONT_ID, x, w, y + 5, text);
}

void drawDateAdjustButton(const GfxRenderer& renderer, const int x, const int y, const int size,
                          const freeink::Icon& icon, const int touchTarget) {
  TouchRegistry::getInstance().add(Rect(x, y, size, size), touchTarget, TouchRegistry::Item);
  renderer.drawRect(x, y, size, size, true);
  const freeink::ui::BitmapRef bitmap{icon.bits, icon.w, icon.h, freeink::ui::BitmapFormat::Mask1, true};
  freeink::ui::forEachBitmapPixel(
      freeink::ui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(size),
                        static_cast<int16_t>(size)},
      bitmap, freeink::ui::BitmapMode::Center,
      [&renderer](const int16_t px, const int16_t py) { renderer.drawPixel(px, py, true); });
}

// Rolling day-history heatmap: one column per week, one row per weekday, most
// recent week last. The stored history is one bit per day, so a cell is either
// "read" (solid) or "not read" (light gray).
struct HeatmapGeometry {
  int weeks = 0;
  int cellStride = 0;
  int cellSize = 0;
  int gridX = 0;
  int gridTop = 0;  // Top of the weekday rows, below the month labels.
  int dayLabelX = 0;
  int monthLabelH = 0;
  int legendY = 0;
  bool valid = false;
};

// Current-month calendar: seven weekday columns, one row per calendar week.
struct MonthGridGeometry {
  int columnStride = 0;
  int rowStride = 0;
  int cellSize = 0;
  int rows = 0;
  int firstDayColumn = 0;  // Weekday column (Monday = 0) holding day 1.
  int daysInCurrentMonth = 0;
  int gridX = 0;
  int gridTop = 0;
  int headerY = 0;
  bool valid = false;
};

int heatmapDayLabelColumnWidth(const GfxRenderer& renderer, const StatsLayout& layout) {
  int maxLabelW = 0;
  for (const StrId label : DAY_LABELS) {
    maxLabelW = std::max(maxLabelW, renderer.getTextWidth(layout.chartLabelFontId, I18N.get(label)));
  }
  return maxLabelW + kHeatmapDayLabelGap;
}

// Derived from the card width alone so the card height can be budgeted before
// the grid is laid out. Every weekday keeps its own label, so a row can never
// be shorter than one line of the label font.
int heatmapCellStrideForWidth(const GfxRenderer& renderer, const int w, const StatsLayout& layout) {
  const int gridAvailW = w - kHeatmapSidePadding * 2 - heatmapDayLabelColumnWidth(renderer, layout);
  const int minStride = std::max(kHeatmapMinCellStride, renderer.getLineHeight(layout.chartLabelFontId) + 1);
  if (gridAvailW < minStride) {
    return 0;
  }
  return std::clamp(gridAvailW / kHeatmapTargetWeeks, minStride, kHeatmapMaxCellStride);
}

int heatmapNaturalCardHeight(const GfxRenderer& renderer, const int w, const StatsLayout& layout) {
  const int cellStride = heatmapCellStrideForWidth(renderer, w, layout);
  if (cellStride == 0) {
    return sectionCardHeight(layout, 0);
  }
  const int monthLabelH = renderer.getLineHeight(layout.chartLabelFontId) + 4;
  const int legendH = renderer.getLineHeight(SMALL_FONT_ID) + kHeatmapLegendTopGap;
  return layout.sectionTitleH + layout.chartTopPadding + monthLabelH +
         static_cast<int>(READING_DAY_OF_WEEK_COUNT) * cellStride + legendH + layout.chartBottomPadding;
}

HeatmapGeometry computeHeatmapGeometry(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                                       const StatsLayout& layout) {
  constexpr int dayRows = static_cast<int>(READING_DAY_OF_WEEK_COUNT);
  HeatmapGeometry geometry;
  const int cellStride = heatmapCellStrideForWidth(renderer, w, layout);
  if (cellStride == 0) {
    return geometry;
  }

  const int labelLineH = renderer.getLineHeight(layout.chartLabelFontId);
  const int dayLabelColumnW = heatmapDayLabelColumnWidth(renderer, layout);
  const int legendH = renderer.getLineHeight(SMALL_FONT_ID) + kHeatmapLegendTopGap;
  geometry.monthLabelH = labelLineH + 4;

  const int contentTop = y + layout.sectionTitleH + layout.chartTopPadding;
  const int contentBottom = y + h - layout.chartBottomPadding - legendH;
  const int gridAvailW = w - kHeatmapSidePadding * 2 - dayLabelColumnW;
  const int gridAvailH = contentBottom - contentTop - geometry.monthLabelH;
  if (gridAvailH < dayRows * kHeatmapMinCellStride) {
    return geometry;
  }

  geometry.cellStride = std::min(cellStride, gridAvailH / dayRows);
  geometry.weeks = std::clamp(gridAvailW / geometry.cellStride, 1, kHeatmapMaxWeeks);
  const int cellGap = std::max(1, geometry.cellStride / 7);
  geometry.cellSize = geometry.cellStride - cellGap;

  const int gridW = geometry.weeks * geometry.cellStride - cellGap;
  const int gridH = dayRows * geometry.cellStride - cellGap;
  const int blockX = x + (w - dayLabelColumnW - gridW) / 2;
  geometry.dayLabelX = blockX;
  geometry.gridX = blockX + dayLabelColumnW;
  geometry.gridTop = contentTop + geometry.monthLabelH + std::max(0, (gridAvailH - gridH) / 2);
  geometry.legendY = contentBottom + (legendH - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
  geometry.valid = true;
  return geometry;
}

void drawHeatmapMonthLabels(const GfxRenderer& renderer, const HeatmapGeometry& geometry,
                            const uint32_t firstMondayDay, const StatsLayout& layout) {
  const int labelY = geometry.gridTop - geometry.monthLabelH;
  int lastLabelRight = 0;
  uint8_t previousMonth = 0;
  char monthBuf[8];
  for (int week = 0; week < geometry.weeks; ++week) {
    ReadingStatsDate weekStart;
    if (!readingStatsDateFromDayIndex(firstMondayDay + static_cast<uint32_t>(week) * 7u, weekStart)) {
      continue;
    }
    if (weekStart.month == previousMonth) {
      continue;
    }
    previousMonth = weekStart.month;

    const int labelX = geometry.gridX + week * geometry.cellStride;
    if (lastLabelRight > 0 && labelX < lastLabelRight) {
      continue;
    }
    formatReadingStatsMonthToken(weekStart, monthBuf, sizeof(monthBuf));
    renderer.drawText(layout.chartLabelFontId, labelX, labelY, monthBuf);
    lastLabelRight = labelX + renderer.getTextWidth(layout.chartLabelFontId, monthBuf) + kHeatmapDayLabelGap;
  }
}

void drawHeatmapCell(const GfxRenderer& renderer, const int x, const int y, const int size, const bool wasRead,
                     const bool isToday) {
  if (wasRead) {
    renderer.fillRect(x, y, size, size, true);
  } else {
    renderer.fillRectDither(x, y, size, size, Color::LightGray);
  }
  if (!isToday) {
    return;
  }
  // Today keeps a ring in the opposite shade so it stays visible either way.
  if (wasRead) {
    renderer.drawRect(x + 1, y + 1, size - 2, size - 2, false);
  } else {
    renderer.fillRect(x, y, size, size, false);
    renderer.drawRect(x, y, size, size, true);
  }
}

void drawHeatmapLegend(const GfxRenderer& renderer, const int x, const int w, const HeatmapGeometry& geometry) {
  const int legendLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int cellY = geometry.legendY + (legendLineH - kHeatmapLegendCell) / 2;
  const char* readLabel = tr(STR_STATS_READ_DAY);
  const char* todayLabel = tr(STR_STATS_TODAY);
  const int readLabelW = renderer.getTextWidth(SMALL_FONT_ID, readLabel);
  const int todayLabelW = renderer.getTextWidth(SMALL_FONT_ID, todayLabel);
  const int totalW =
      (kHeatmapLegendCell + kHeatmapLegendLabelGap) * 2 + readLabelW + todayLabelW + kHeatmapLegendItemGap;
  int legendX = x + (w - totalW) / 2;

  drawHeatmapCell(renderer, legendX, cellY, kHeatmapLegendCell, true, false);
  legendX += kHeatmapLegendCell + kHeatmapLegendLabelGap;
  renderer.drawText(SMALL_FONT_ID, legendX, geometry.legendY, readLabel);
  legendX += readLabelW + kHeatmapLegendItemGap;
  drawHeatmapCell(renderer, legendX, cellY, kHeatmapLegendCell, false, true);
  legendX += kHeatmapLegendCell + kHeatmapLegendLabelGap;
  renderer.drawText(SMALL_FONT_ID, legendX, geometry.legendY, todayLabel);
}

void drawReadingHeatmap(const GfxRenderer& renderer, const int x, const int w, const GlobalReadingStats& stats,
                        const ReadingStatsDate& today, const HeatmapGeometry& geometry, const StatsLayout& layout) {
  const uint32_t todayDay = readingStatsDayIndex(today);
  const uint32_t currentWeekMondayDay = todayDay - readingStatsDayOfWeekIndex(today);
  const uint32_t weeksBack = static_cast<uint32_t>(geometry.weeks - 1) * 7u;
  const uint32_t firstMondayDay = currentWeekMondayDay > weeksBack ? currentWeekMondayDay - weeksBack : 0u;
  const int labelLineH = renderer.getLineHeight(layout.chartLabelFontId);

  drawHeatmapMonthLabels(renderer, geometry, firstMondayDay, layout);

  for (size_t row = 0; row < READING_DAY_OF_WEEK_COUNT; ++row) {
    const int rowY = geometry.gridTop + static_cast<int>(row) * geometry.cellStride;
    renderer.drawText(layout.chartLabelFontId, geometry.dayLabelX, rowY + (geometry.cellSize - labelLineH) / 2,
                      I18N.get(DAY_LABELS[row]));
    for (int week = 0; week < geometry.weeks; ++week) {
      const uint32_t dayIndex = firstMondayDay + static_cast<uint32_t>(week) * 7u + static_cast<uint32_t>(row);
      if (dayIndex > todayDay) {
        continue;
      }
      drawHeatmapCell(renderer, geometry.gridX + week * geometry.cellStride, rowY, geometry.cellSize,
                      stats.didReadOnDay(dayIndex), dayIndex == todayDay);
    }
  }

  drawHeatmapLegend(renderer, x, w, geometry);
}

int monthGridRowCount(const ReadingStatsDate& today) {
  const ReadingStatsDate firstOfMonth{today.year, today.month, 1};
  const int leadingDays = readingStatsDayOfWeekIndex(firstOfMonth);
  const int totalCells = leadingDays + daysInMonth(today.year, today.month);
  return (totalCells + 6) / 7;
}

int monthGridCardHeight(const GfxRenderer& renderer, const StatsLayout& layout, const ReadingStatsDate& today,
                        const int rowStride) {
  const int headerH = renderer.getLineHeight(layout.chartLabelFontId) + kMonthHeaderGap;
  return layout.sectionTitleH + layout.chartTopPadding + headerH + monthGridRowCount(today) * rowStride +
         layout.chartBottomPadding;
}

MonthGridGeometry computeMonthGridGeometry(const GfxRenderer& renderer, const int x, const int y, const int w,
                                           const int h, const StatsLayout& layout, const ReadingStatsDate& today) {
  MonthGridGeometry geometry;
  const int headerH = renderer.getLineHeight(layout.chartLabelFontId) + kMonthHeaderGap;
  const int contentTop = y + layout.sectionTitleH + layout.chartTopPadding;
  const int contentBottom = y + h - layout.chartBottomPadding;
  const int gridAvailW = w - kMonthSidePadding * 2;
  const int gridAvailH = contentBottom - contentTop - headerH;
  geometry.rows = monthGridRowCount(today);
  if (gridAvailW < kMonthMinCellStride * 7 || gridAvailH < kMonthMinCellStride * geometry.rows) {
    return geometry;
  }

  geometry.columnStride = gridAvailW / 7;
  geometry.rowStride = std::clamp(gridAvailH / geometry.rows, kMonthMinCellStride, kMonthMaxRowStride);
  geometry.cellSize = std::min(geometry.columnStride, geometry.rowStride) - kMonthCellGap;
  geometry.firstDayColumn = readingStatsDayOfWeekIndex(ReadingStatsDate{today.year, today.month, 1});
  geometry.daysInCurrentMonth = daysInMonth(today.year, today.month);
  geometry.gridX = x + (w - geometry.columnStride * 7) / 2;
  geometry.gridTop = contentTop + headerH + std::max(0, (gridAvailH - geometry.rows * geometry.rowStride) / 2);
  geometry.headerY = geometry.gridTop - headerH + (kMonthHeaderGap / 2);
  geometry.valid = true;
  return geometry;
}

void drawMonthDayCell(const GfxRenderer& renderer, const int x, const int y, const int size, const int day,
                      const bool wasRead, const bool isToday) {
  if (wasRead) {
    renderer.fillRect(x, y, size, size, true);
  } else {
    renderer.drawRect(x, y, size, size, true);
  }
  if (isToday) {
    renderer.drawRect(x + 2, y + 2, size - 4, size - 4, !wasRead);
  }

  char dayBuf[4];
  snprintf(dayBuf, sizeof(dayBuf), "%d", day);
  const int textW = renderer.getTextWidth(SMALL_FONT_ID, dayBuf);
  const int textH = renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, x + (size - textW) / 2, y + (size - textH) / 2, dayBuf, !wasRead);
}

void drawMonthGrid(const GfxRenderer& renderer, const GlobalReadingStats& stats, const ReadingStatsDate& today,
                   const MonthGridGeometry& geometry, const StatsLayout& layout) {
  const uint32_t todayDay = readingStatsDayIndex(today);
  const uint32_t firstDayIndex = readingStatsDayIndex(ReadingStatsDate{today.year, today.month, 1});
  const int cellOffset = (geometry.columnStride - geometry.cellSize) / 2;

  for (size_t column = 0; column < READING_DAY_OF_WEEK_COUNT; ++column) {
    const int columnX = geometry.gridX + static_cast<int>(column) * geometry.columnStride;
    drawCenteredLabel(renderer, layout.chartLabelFontId, columnX, geometry.columnStride, geometry.headerY,
                      I18N.get(DAY_LABELS[column]));
  }

  for (int day = 1; day <= geometry.daysInCurrentMonth; ++day) {
    const int cellIndex = geometry.firstDayColumn + day - 1;
    const int column = cellIndex % 7;
    const int row = cellIndex / 7;
    const uint32_t dayIndex = firstDayIndex + static_cast<uint32_t>(day - 1);
    drawMonthDayCell(renderer, geometry.gridX + column * geometry.columnStride + cellOffset,
                     geometry.gridTop + row * geometry.rowStride + (geometry.rowStride - geometry.cellSize) / 2,
                     geometry.cellSize, day, stats.didReadOnDay(dayIndex), dayIndex == todayDay);
  }
}

int streakSummaryCardHeight(const GfxRenderer& renderer, const StatsLayout& layout) {
  return layout.topCardTitleH + renderer.getLineHeight(UI_12_FONT_ID) + 4 + renderer.getLineHeight(SMALL_FONT_ID) +
         kStreakSummaryCardPadding;
}

void drawStreakSummaryCard(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                           const char* title, const GlobalReadingStats& stats, const ReadingStatsDate* today,
                           const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.topCardTitleH, x + w, y + layout.topCardTitleH);
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, w,
                    y + (layout.topCardTitleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, title, true);

  const int thirdW = w / 3;
  const int rowY = y + layout.topCardTitleH;
  const int rowH = h - layout.topCardTitleH;
  char buf[16];

  formatStreakDayCount(stats.currentReadingStreak(today), buf, sizeof(buf));
  drawStatCell(renderer, x, thirdW, rowY, rowH, buf, tr(STR_STATS_READING_STREAK_LBL));

  formatStreakDayCount(stats.displayLongestReadingStreak(), buf, sizeof(buf));
  drawStatCell(renderer, x + thirdW, thirdW, rowY, rowH, buf, tr(STR_STATS_LONGEST_STREAK_LBL));

  formatStreakDayCount(stats.totalReadingDays(), buf, sizeof(buf));
  drawStatCell(renderer, x + thirdW * 2, thirdW, rowY, rowH, buf, tr(STR_STATS_DAYS_READ_LBL));
}
}  // namespace

void renderPerBookStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                            const BookReadingStats& stats, const float progressPercent, const bool hasEstimatedTimeLeft,
                            const uint32_t estimatedTimeLeftSeconds, const bool showButtonHints,
                            const bool showEditButton, const bool showMoreButton) {
  renderer.clearScreen();
  const bool showRtcStats = shouldShowRtcBasedStats();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getStatsLayout(renderer, mappedInput, false, showButtonHints, showRtcStats);
  if (mappedInput && mappedInput->hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, tr(STR_READING_STATS), false, true);
  } else {
    CompactHeader::drawTitle(renderer, tr(STR_READING_STATS), true);
  }
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  int topCardH = layout.topCardH;
  const int headerHeight = statsHeaderHeight(metrics, layout, mappedInput);
  int y = metrics.topPadding + headerHeight + layout.topGap;

  if (showRtcStats) {
    const int timeOfDayH = sectionCardHeight(layout, static_cast<int>(TIME_BUCKET_LABELS.size()));
    const int dayOfWeekH = sectionCardHeight(layout, static_cast<int>(DAY_LABELS.size()));
    const int compactContentHeight =
        headerHeight + layout.topGap + layout.topCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
    const int extraHeight = std::max(0, availableHeight - compactContentHeight);
    const int extraTopCardHeight = std::min(extraHeight, kPerBookRtcTopCardMaxExtra);
    const int remainingExtraHeight = extraHeight - extraTopCardHeight;
    const int timeOfDayExtraHeight = (remainingExtraHeight * 4) / 11;
    const int dayOfWeekExtraHeight = remainingExtraHeight - timeOfDayExtraHeight;
    const int timeOfDayCardH = timeOfDayH + timeOfDayExtraHeight;
    const int dayOfWeekCardH = dayOfWeekH + dayOfWeekExtraHeight;
    topCardH += extraTopCardHeight;

    drawPerBookStatsCard(renderer, cardX, y, cardW, topCardH, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                         estimatedTimeLeftSeconds, layout);
    y += topCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, timeOfDayCardH, tr(STR_STATS_TIME_OF_DAY), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, timeOfDayCardH, stats.timeOfDaySeconds, TIME_BUCKET_LABELS, layout);
    y += timeOfDayCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, dayOfWeekCardH, tr(STR_STATS_DAY_OF_WEEK), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, dayOfWeekCardH, stats.dayOfWeekSeconds, DAY_LABELS, layout);
  } else {
    const int compactContentHeight = headerHeight + layout.topGap + layout.topCardH;
    const int extraHeight = std::max(0, availableHeight - compactContentHeight);
    if (showButtonHints) {
      topCardH += extraHeight;
    } else {
      // The sleep-screen variant has no footer controls, so on tall portrait displays the
      // single card can balloon and create huge internal gaps between the two stat rows.
      // Cap the card growth and spend the rest as outer margin instead.
      const int maxStandaloneCardHeight =
          std::max(layout.topCardH, renderer.getScreenHeight() / kStandaloneNoRtcMaxTopCardHeightDivisor);
      topCardH = std::min(layout.topCardH + extraHeight, maxStandaloneCardHeight);
      const int unusedExtraHeight = extraHeight - (topCardH - layout.topCardH);
      y += std::min(unusedExtraHeight / 3, kStandaloneNoRtcMaxVerticalOffset);
    }
    drawPerBookStatsCard(renderer, cardX, y, cardW, topCardH, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                         estimatedTimeLeftSeconds, layout);
  }

  if (showButtonHints && mappedInput) {
    const auto labels =
        mappedInput->mapLabels(mappedInput->withBackArrow(tr(STR_BACK)), showEditButton ? tr(STR_EDIT) : "", "",
                               showMoreButton ? tr(STR_MORE) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderGlobalStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const char* screenTitle,
                           const GlobalReadingStats& stats, const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  const bool showRtcStats = shouldShowRtcBasedStats();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getStatsLayout(renderer, mappedInput, true, showButtonHints, showRtcStats);
  if (mappedInput && mappedInput->hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, screenTitle, false);
  } else {
    CompactHeader::drawTitle(renderer, screenTitle);
  }
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  int globalCardH = layout.globalCardH;
  const int headerHeight = statsHeaderHeight(metrics, layout, mappedInput);
  int y = metrics.topPadding + headerHeight + layout.topGap;

  if (showRtcStats) {
    const int timeOfDayH = sectionCardHeight(layout, static_cast<int>(TIME_BUCKET_LABELS.size()));
    const int dayOfWeekH = sectionCardHeight(layout, static_cast<int>(DAY_LABELS.size()));
    const int compactContentHeight =
        headerHeight + layout.topGap + layout.globalCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
    const int extraHeight = std::max(0, availableHeight - compactContentHeight);
    const int perBookCompactContentHeight =
        headerHeight + layout.topGap + layout.topCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
    const int perBookExtraHeight = std::max(0, availableHeight - perBookCompactContentHeight);
    const int targetGlobalCardH = globalRtcCardHeightForPerBookRowSpacing(layout, perBookExtraHeight);
    const int extraTopCardHeight = std::min(extraHeight, std::max(0, targetGlobalCardH - layout.globalCardH));
    const int remainingExtraHeight = extraHeight - extraTopCardHeight;
    const int timeOfDayExtraHeight = (remainingExtraHeight * 4) / 11;
    const int dayOfWeekExtraHeight = remainingExtraHeight - timeOfDayExtraHeight;
    const int timeOfDayCardH = timeOfDayH + timeOfDayExtraHeight;
    const int dayOfWeekCardH = dayOfWeekH + dayOfWeekExtraHeight;
    globalCardH += extraTopCardHeight;

    drawGlobalStatsCard(renderer, cardX, y, cardW, globalCardH, tr(STR_STATS_ALL_TIME), stats, layout);
    y += globalCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, timeOfDayCardH, tr(STR_STATS_TIME_OF_DAY), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, timeOfDayCardH, stats.timeOfDaySeconds, TIME_BUCKET_LABELS, layout);
    y += timeOfDayCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, dayOfWeekCardH, tr(STR_STATS_DAY_OF_WEEK), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, dayOfWeekCardH, stats.dayOfWeekSeconds, DAY_LABELS, layout);
  } else {
    const int compactContentHeight = headerHeight + layout.topGap + layout.globalCardH;
    globalCardH += std::max(0, availableHeight - compactContentHeight);
    drawGlobalStatsCard(renderer, cardX, y, cardW, globalCardH, tr(STR_STATS_ALL_TIME), stats, layout);
  }

  if (showButtonHints && mappedInput) {
    const auto labels =
        mappedInput->mapLabels(mappedInput->withBackArrow(tr(STR_EXIT)), "", mappedInput->withBackArrow(tr(STR_BACK)),
                               showMoreButton ? tr(STR_MORE) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderReadingHistoryPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const char* screenTitle,
                              const GlobalReadingStats& stats, const bool showButtonHints) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getStatsLayout(renderer, mappedInput, true, showButtonHints, true);
  if (mappedInput && mappedInput->hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, tr(STR_STATS_READING_HISTORY), false);
  } else {
    CompactHeader::drawTitle(renderer, tr(STR_STATS_READING_HISTORY));
  }

  const int cardX = metrics.contentSidePadding;
  const int cardW = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  const int headerHeight = statsHeaderHeight(metrics, layout, mappedInput);

  ReadingStatsDateTime now;
  ReadingStatsDate today;
  if (getCurrentLocalReadingStatsDateTime(now)) {
    today = now.date;
  } else if (!readingStatsDateFromDayIndex(stats.readingHistoryAnchorDay, today)) {
    today.clear();
  }

  const int summaryCardH = streakSummaryCardHeight(renderer, layout);
  int y = metrics.topPadding + headerHeight + layout.topGap;
  drawStreakSummaryCard(renderer, cardX, y, cardW, summaryCardH, screenTitle, stats, today.isValid() ? &today : nullptr,
                        layout);
  y += summaryCardH + layout.cardGap;

  // The heatmap keeps its natural height and the current-month calendar takes
  // whatever is left, so both stay readable instead of one card ballooning.
  const int remainingHeight = std::max(sectionCardHeight(layout, 0), availableHeight - (y - metrics.topPadding));
  const int heatmapNaturalH = heatmapNaturalCardHeight(renderer, cardW, layout);
  const int monthMinCardH = today.isValid() ? monthGridCardHeight(renderer, layout, today, kMonthMinCellStride) : 0;
  const bool hasMonthCard =
      today.isValid() && remainingHeight >= heatmapNaturalH + layout.cardGap + monthMinCardH;
  const int heatmapCardH = hasMonthCard ? heatmapNaturalH : remainingHeight;

  const HeatmapGeometry heatmapGeometry = computeHeatmapGeometry(renderer, cardX, y, cardW, heatmapCardH, layout);
  const bool canDrawHeatmap = heatmapGeometry.valid && today.isValid();

  char cardTitle[40];
  if (canDrawHeatmap) {
    snprintf(cardTitle, sizeof(cardTitle), tr(STR_STATS_LAST_WEEKS_FORMAT),
             static_cast<unsigned>(heatmapGeometry.weeks));
  } else {
    snprintf(cardTitle, sizeof(cardTitle), "%s", tr(STR_STATS_READING_HISTORY));
  }
  drawSectionCard(renderer, cardX, y, cardW, heatmapCardH, cardTitle, layout);

  if (canDrawHeatmap) {
    drawReadingHeatmap(renderer, cardX, cardW, stats, today, heatmapGeometry, layout);
  } else {
    const int messageY = y + layout.sectionTitleH + (heatmapCardH - layout.sectionTitleH) / 2 -
                         renderer.getLineHeight(UI_10_FONT_ID) / 2;
    drawCenteredLabel(renderer, UI_10_FONT_ID, cardX, cardW, messageY, tr(STR_STATS_NO_HISTORY));
  }

  if (hasMonthCard) {
    y += heatmapCardH + layout.cardGap;
    const int monthCardH = remainingHeight - heatmapCardH - layout.cardGap;
    char monthTitle[24];
    char monthToken[8];
    formatReadingStatsMonthToken(today, monthToken, sizeof(monthToken));
    snprintf(monthTitle, sizeof(monthTitle), "%s %u", monthToken, static_cast<unsigned>(today.year));
    drawSectionCard(renderer, cardX, y, cardW, monthCardH, monthTitle, layout);

    const MonthGridGeometry monthGeometry =
        computeMonthGridGeometry(renderer, cardX, y, cardW, monthCardH, layout, today);
    if (monthGeometry.valid) {
      drawMonthGrid(renderer, stats, today, monthGeometry, layout);
    }
  }

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(mappedInput->withBackArrow(tr(STR_EXIT)), "",
                                               mappedInput->withBackArrow(tr(STR_BACK)), "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderNoRtcCombinedStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                  const std::string& bookTitle, const BookReadingStats& bookStats,
                                  const float progressPercent, const bool hasEstimatedTimeLeft,
                                  const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& deviceStats,
                                  const GlobalReadingStats* allDevicesStats, const bool showButtonHints) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getNoRtcCombinedLayout(renderer, mappedInput, showButtonHints, allDevicesStats != nullptr);
  if (mappedInput && mappedInput->hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, tr(STR_READING_STATS), false);
  } else {
    CompactHeader::drawTitle(renderer, tr(STR_READING_STATS));
  }
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  const int headerHeight = statsHeaderHeight(metrics, layout, mappedInput);
  const int compactContentHeight = noRtcCombinedContentHeight(layout, headerHeight, allDevicesStats != nullptr);
  const int extraHeight = std::max(0, availableHeight - compactContentHeight);
  const int visibleCardCount = allDevicesStats ? 3 : 2;
  const int extraPerCard = visibleCardCount > 0 ? extraHeight / visibleCardCount : 0;
  const int extraRemainder = visibleCardCount > 0 ? extraHeight % visibleCardCount : 0;
  const int perBookExtraHeight = extraPerCard + (extraRemainder > 0 ? 1 : 0);
  const int deviceExtraHeight = extraPerCard + (extraRemainder > 1 ? 1 : 0);
  const int allDevicesExtraHeight = allDevicesStats ? extraPerCard : 0;
  const int perBookCardH = noRtcCardBaseHeight(layout) + perBookExtraHeight;
  const int deviceCardH = layout.globalCardH + deviceExtraHeight;
  const int allDevicesCardH = layout.globalCardH + allDevicesExtraHeight;

  int y = metrics.topPadding + headerHeight + layout.topGap;
  drawPerBookStatsCard(renderer, cardX, y, cardW, perBookCardH, bookTitle, bookStats, progressPercent,
                       hasEstimatedTimeLeft, estimatedTimeLeftSeconds, layout);
  y += perBookCardH + layout.cardGap;

  drawGlobalStatsCard(renderer, cardX, y, cardW, deviceCardH, tr(STR_STATS_THIS_DEVICE_SCREEN), deviceStats, layout);
  y += deviceCardH;

  if (allDevicesStats) {
    y += layout.cardGap;
    drawGlobalStatsCard(renderer, cardX, y, cardW, allDevicesCardH, tr(STR_STATS_ALL_DEVICES_SCREEN), *allDevicesStats,
                        layout);
  }

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(mappedInput->withBackArrow(tr(STR_BACK)), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderEditBookDatesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                             const BookReadingStats& stats, const int selectedField, const bool showButtonHints) {
  renderer.clearScreen();
  if (mappedInput && mappedInput->hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, tr(STR_READING_STATS), false);
  } else {
    CompactHeader::drawTitle(renderer, tr(STR_READING_STATS));
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int cardW = pageWidth - 120;
  const int cardH = 250;
  const int cardX = (pageWidth - cardW) / 2;
  const int cardY = 138;

  const std::string visibleTitle =
      renderer.truncatedText(UI_12_FONT_ID, bookTitle.c_str(), pageWidth - 80, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, 96, visibleTitle.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawRect(cardX, cardY, cardW, cardH);

  const int sectionGap = 104;
  const int row1Y = cardY + 66;
  const int row2Y = row1Y + sectionGap;
  const int monthW = 52;
  const int dayW = 46;
  const int yearW = 68;
  const int gap = 14;
  const int totalFieldW = monthW + gap + dayW + gap + yearW;
#if CROSSINK_APP_CAP_TOUCH
  const bool showTouchControls = mappedInput && mappedInput->hasTouch();
  constexpr int adjustButtonSize = 60;
  constexpr int adjustButtonRightPadding = 34;
  constexpr int adjustButtonGap = 24;
  const int adjustButtonX = cardX + cardW - adjustButtonRightPadding - adjustButtonSize;
  const int fieldAreaW = showTouchControls ? adjustButtonX - cardX - adjustButtonGap : cardW;
#else
  const int fieldAreaW = cardW;
#endif
  const int fieldStartX = cardX + (std::max(totalFieldW, fieldAreaW) - totalFieldW) / 2;

  char monthBuf[8];
  char dayBuf[8];
  char yearBuf[8];

  drawCenteredLabel(renderer, UI_10_FONT_ID, cardX, cardW, cardY + 24, tr(STR_STATS_START_DATE), true);
  formatReadingStatsMonthToken(stats.startDate, monthBuf, sizeof(monthBuf));
  snprintf(dayBuf, sizeof(dayBuf), "%s", stats.startDate.isValid() ? "" : "-");
  if (stats.startDate.isValid()) {
    snprintf(dayBuf, sizeof(dayBuf), "%02u", static_cast<unsigned>(stats.startDate.day));
    snprintf(yearBuf, sizeof(yearBuf), "%u", static_cast<unsigned>(stats.startDate.year));
  } else {
    snprintf(dayBuf, sizeof(dayBuf), "-");
    snprintf(yearBuf, sizeof(yearBuf), "-");
  }
  drawDateField(renderer, fieldStartX, row1Y, monthW, monthBuf, selectedField == 0, BookStatsTouchTarget::dateField(0));
  drawDateField(renderer, fieldStartX + monthW + gap, row1Y, dayW, dayBuf, selectedField == 1,
                BookStatsTouchTarget::dateField(1));
  drawDateField(renderer, fieldStartX + monthW + gap + dayW + gap, row1Y, yearW, yearBuf, selectedField == 2,
                BookStatsTouchTarget::dateField(2));

  drawCenteredLabel(renderer, UI_10_FONT_ID, cardX, cardW, cardY + 24 + sectionGap, tr(STR_STATS_FINISHED_DATE), true);
  const bool showFinishedFields = stats.isCompleted && stats.finishedDate.isValid();
  formatReadingStatsMonthToken(showFinishedFields ? stats.finishedDate : ReadingStatsDate{}, monthBuf,
                               sizeof(monthBuf));
  if (showFinishedFields) {
    snprintf(dayBuf, sizeof(dayBuf), "%02u", static_cast<unsigned>(stats.finishedDate.day));
    snprintf(yearBuf, sizeof(yearBuf), "%u", static_cast<unsigned>(stats.finishedDate.year));
  } else {
    snprintf(dayBuf, sizeof(dayBuf), "-");
    snprintf(yearBuf, sizeof(yearBuf), "-");
  }
  drawDateField(renderer, fieldStartX, row2Y, monthW, monthBuf, selectedField == 3, BookStatsTouchTarget::dateField(3));
  drawDateField(renderer, fieldStartX + monthW + gap, row2Y, dayW, dayBuf, selectedField == 4,
                BookStatsTouchTarget::dateField(4));
  drawDateField(renderer, fieldStartX + monthW + gap + dayW + gap, row2Y, yearW, yearBuf, selectedField == 5,
                BookStatsTouchTarget::dateField(5));

#if CROSSINK_APP_CAP_TOUCH
  if (showTouchControls) {
    const int fieldH = renderer.getLineHeight(UI_12_FONT_ID) + 10;
    drawDateAdjustButton(renderer, adjustButtonX, row1Y + (fieldH - adjustButtonSize) / 2, adjustButtonSize,
                         icon_chevron_up_32, BookStatsTouchTarget::DateAdjustUp);
    drawDateAdjustButton(renderer, adjustButtonX, row2Y + (fieldH - adjustButtonSize) / 2, adjustButtonSize,
                         icon_chevron_down_32, BookStatsTouchTarget::DateAdjustDown);

    constexpr int actionHeight = TouchActionButtons::kDefaultHeight;
    constexpr int actionGap = TouchActionButtons::kDefaultGap;
    constexpr int actionTotalHeight = actionHeight * 2 + actionGap;
    const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const Rect actionArea{safeArea.x + metrics.contentSidePadding,
                          safeArea.y + safeArea.height - metrics.verticalSpacing - actionTotalHeight,
                          safeArea.width - metrics.contentSidePadding * 2, actionTotalHeight};
    const auto actions = TouchActionButtons::vertical(actionArea, 2);
    const char* labels[] = {tr(STR_SAVE), tr(STR_CANCEL)};
    TouchActionButtons::draw(renderer, actions, labels, 0, -1, UI_10_FONT_ID);
    TouchRegistry::getInstance().add(actions.buttons[0], BookStatsTouchTarget::DateSave, TouchRegistry::Item);
    TouchRegistry::getInstance().add(actions.buttons[1], BookStatsTouchTarget::DateCancel, TouchRegistry::Item);
  }
#endif

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(mappedInput->withBackArrow(tr(STR_BACK)), tr(STR_NEXT_FIELD),
                                               tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}
