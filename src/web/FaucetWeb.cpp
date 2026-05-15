#ifndef NATIVE_BUILD

#include "web/FaucetWeb.h"

#include "app/AppController.h"
#include "app/AppConfig.h"
#include "app/ConfigStore.h"
#include "app/FilterStore.h"
#include "app/WaterRecordStore.h"
#include "web/FaucetWebJson.h"
#include "web/FaucetWebParsing.h"
#include "web/FaucetWebRoutes.h"

#include <Esp32Base.h>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace faucet {
namespace {

constexpr std::uint32_t kChartDays = 14;
FaucetWebContext g_context{};

bool requireContext();
bool getParam(const char* name, char* out, std::size_t len);

Esp32BaseWeb::Method toBaseMethod(FaucetWebMethod method) {
    switch (method) {
        case FaucetWebMethod::Get:
            return Esp32BaseWeb::METHOD_GET;
        case FaucetWebMethod::Post:
            return Esp32BaseWeb::METHOD_POST;
        case FaucetWebMethod::Any:
        default:
            return Esp32BaseWeb::METHOD_ANY;
    }
}

void sendFmt(const char* fmt, ...) {
    char buffer[256]{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Esp32BaseWeb::sendChunk(buffer);
}

void sendHtmlAttrEscaped(const char* text) {
    for (const char* p = text ? text : ""; *p; ++p) {
        switch (*p) {
            case '&':
                Esp32BaseWeb::sendChunk("&amp;");
                break;
            case '<':
                Esp32BaseWeb::sendChunk("&lt;");
                break;
            case '>':
                Esp32BaseWeb::sendChunk("&gt;");
                break;
            case '"':
                Esp32BaseWeb::sendChunk("&quot;");
                break;
            case '\'':
                Esp32BaseWeb::sendChunk("&#39;");
                break;
            default:
                sendFmt("%c", *p);
                break;
        }
    }
}

void formatLiters(std::uint32_t ml, char* out, std::size_t len) {
    const std::uint32_t centiliters = (ml + 5UL) / 10UL;
    std::snprintf(out, len, "%lu.%02lu L", static_cast<unsigned long>(centiliters / 100UL),
                  static_cast<unsigned long>(centiliters % 100UL));
}

bool isLeapYear(std::uint16_t year) {
    return (year % 4U == 0 && year % 100U != 0) || year % 400U == 0;
}

void dateFromDayIndex(std::uint32_t day, std::uint16_t& year, std::uint8_t& month, std::uint8_t& monthDay) {
    year = 2000;
    while (true) {
        const std::uint16_t yearDays = isLeapYear(year) ? 366 : 365;
        if (day < yearDays) {
            break;
        }
        day -= yearDays;
        ++year;
    }
    static constexpr std::uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    month = 1;
    while (month <= 12) {
        std::uint8_t days = monthDays[month - 1];
        if (month == 2 && isLeapYear(year)) {
            days = 29;
        }
        if (day < days) {
            break;
        }
        day -= days;
        ++month;
    }
    monthDay = static_cast<std::uint8_t>(day + 1);
}

void formatDayLabel(std::uint32_t day, char* out, std::size_t len) {
    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t monthDay = 0;
    dateFromDayIndex(day, year, month, monthDay);
    std::snprintf(out, len, "%02u-%02u", static_cast<unsigned>(month), static_cast<unsigned>(monthDay));
}

std::uint32_t filterProgressPercent(const FilterRecord& filter, std::uint32_t usedDays) {
    if (filter.recommendDays == 0) {
        return 0;
    }
    const std::uint64_t percent = (static_cast<std::uint64_t>(usedDays) * 100ULL) / filter.recommendDays;
    return percent > 100ULL ? 100UL : static_cast<std::uint32_t>(percent);
}

std::uint32_t daysToMonths(std::uint32_t days) {
    return (days + kDaysPerLifeMonth - 1UL) / kDaysPerLifeMonth;
}

std::uint32_t monthsToDays(std::uint32_t months) {
    const std::uint32_t maxMonths = kMaxFilterLifeDays / kDaysPerLifeMonth;
    if (months > maxMonths) {
        months = maxMonths;
    }
    return months * kDaysPerLifeMonth;
}

void formatLifeRange(const FilterRecord& filter, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (filter.recommendDays == 0) {
        std::snprintf(out, len, "未设置天数");
        return;
    }
    const std::uint32_t minMonths = daysToMonths(filter.recommendDays);
    const std::uint32_t maxMonths = daysToMonths(filter.maxDays);
    if (maxMonths > minMonths) {
        std::snprintf(out, len, "%lu～%lu个月", static_cast<unsigned long>(minMonths),
                      static_cast<unsigned long>(maxMonths));
    } else {
        std::snprintf(out, len, "%lu个月", static_cast<unsigned long>(minMonths));
    }
}

const char* filterStatusText(FilterLifeStatus status) {
    switch (status) {
        case FilterLifeStatus::RecommendReplace:
            return "建议更换";
        case FilterLifeStatus::Expired:
            return "已超期";
        case FilterLifeStatus::Normal:
        default:
            return "正常";
    }
}

const char* filterDisplayStatusText(const FilterRecord& filter, std::uint32_t usedDays) {
    if (!filter.enabled) {
        return "停用";
    }
    return filterStatusText(filterLifeStatus(filter, usedDays));
}

void sendLiters(std::uint32_t ml) {
    char text[24]{};
    formatLiters(ml, text, sizeof(text));
    Esp32BaseWeb::sendChunk(text);
}

void sendTargetValue(const WaterRecord& record) {
    if (record.mode == WaterMode::Time) {
        sendFmt("%lu 秒", static_cast<unsigned long>(record.targetValue));
        return;
    }
    sendLiters(record.targetValue);
}

bool waterRecordCanCalibrate(const WaterRecord& record) {
    return record.pulseCount > 0 &&
           (record.result == WaterResult::Completed || record.result == WaterResult::StoppedByUser);
}

void sendMetricCard(const char* label, const char* value) {
    Esp32BaseWeb::sendChunk("<section class='metric-card'><span>");
    Esp32BaseWeb::sendChunk(label);
    Esp32BaseWeb::sendChunk("</span><strong>");
    Esp32BaseWeb::sendChunk(value);
    Esp32BaseWeb::sendChunk("</strong></section>");
}

std::uint32_t percentOf(std::uint32_t value, std::uint32_t total) {
    if (total == 0) {
        return 0;
    }
    const std::uint64_t percent = (static_cast<std::uint64_t>(value) * 100ULL) / total;
    return percent > 100ULL ? 100UL : static_cast<std::uint32_t>(percent);
}

void sendStatBar(const char* label, const char* value, std::uint32_t percent) {
    sendFmt("<div class='stat-bar'><div class='stat-bar-head'><span>%s</span><strong>%s</strong></div>"
            "<div class='progress'><span style='width:%lu%%'></span></div></div>",
            label,
            value,
            static_cast<unsigned long>(percent));
}

struct DailyBucket {
    std::uint32_t dayIndex = 0;
    std::uint32_t volumeMl = 0;
    std::uint32_t durationSec = 0;
};

struct RecordAggregates {
    DailyBucket days[kChartDays]{};
    std::uint32_t todayMl = 0;
    std::uint32_t weekMl = 0;
    std::uint32_t monthMl = 0;
    std::uint32_t totalMl = 0;
    std::uint32_t unknownMl = 0;
    std::uint32_t unknownDurationSec = 0;
    std::uint32_t unknownCount = 0;
};

void addSaturating(std::uint32_t& target, std::uint32_t value) {
    const std::uint32_t max = UINT32_MAX;
    target = max - target < value ? max : target + value;
}

RecordAggregates aggregateRecords(std::uint32_t nowSeconds) {
    RecordAggregates aggregates{};
    if (!g_context.records || !g_context.records->ready()) {
        return aggregates;
    }
    const bool hasRealNow = nowSeconds >= kMinRealDateSeconds;
    const std::uint32_t todayDay = hasRealNow ? nowSeconds / 86400UL : 0;
    const std::uint32_t firstChartDay = hasRealNow && todayDay >= kChartDays - 1 ? todayDay - (kChartDays - 1) : 0;
    for (std::size_t i = 0; i < kChartDays; ++i) {
        aggregates.days[i].dayIndex = firstChartDay + i;
    }

    WaterRecord records[kMaxRecordPageSize]{};
    const std::size_t total = g_context.records->count();
    for (std::size_t offset = 0; offset < total; offset += kMaxRecordPageSize) {
        const std::size_t page = offset / kMaxRecordPageSize;
        const std::size_t count = g_context.records->readPage(page, kMaxRecordPageSize, records, kMaxRecordPageSize);
        if (count == 0) {
            break;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const WaterRecord& record = records[i];
            if (!waterRecordHasRealTime(record)) {
                ++aggregates.unknownCount;
                addSaturating(aggregates.unknownMl, record.volumeMl);
                addSaturating(aggregates.unknownDurationSec, record.durationSec);
                continue;
            }
            addSaturating(aggregates.totalMl, record.volumeMl);
            if (!hasRealNow) {
                continue;
            }
            const std::uint32_t day = record.startTime / 86400UL;
            if (day == todayDay) {
                addSaturating(aggregates.todayMl, record.volumeMl);
            }
            if (day <= todayDay && todayDay - day < 7) {
                addSaturating(aggregates.weekMl, record.volumeMl);
            }
            if (day <= todayDay && todayDay - day < 30) {
                addSaturating(aggregates.monthMl, record.volumeMl);
            }
            if (day >= firstChartDay && day <= todayDay) {
                DailyBucket& bucket = aggregates.days[day - firstChartDay];
                addSaturating(bucket.volumeMl, record.volumeMl);
                addSaturating(bucket.durationSec, record.durationSec);
            }
        }
    }
    return aggregates;
}

std::uint32_t sumRealRecordVolumeSince(std::uint32_t startTime) {
    if (!g_context.records || !g_context.records->ready() || startTime < kMinRealDateSeconds) {
        return 0;
    }
    std::uint32_t total = 0;
    WaterRecord records[kMaxRecordPageSize]{};
    const std::size_t count = g_context.records->count();
    for (std::size_t offset = 0; offset < count; offset += kMaxRecordPageSize) {
        const std::size_t page = offset / kMaxRecordPageSize;
        const std::size_t read = g_context.records->readPage(page, kMaxRecordPageSize, records, kMaxRecordPageSize);
        if (read == 0) {
            break;
        }
        for (std::size_t i = 0; i < read; ++i) {
            if (waterRecordHasRealTime(records[i]) && records[i].startTime >= startTime && records[i].volumeMl > 0) {
                addSaturating(total, records[i].volumeMl);
            }
        }
    }
    return total;
}

void sendDailyChart(const DailyBucket (&days)[kChartDays]) {
    std::uint32_t maxVolume = 0;
    std::uint32_t maxDuration = 0;
    for (const DailyBucket& day : days) {
        if (day.volumeMl > maxVolume) {
            maxVolume = day.volumeMl;
        }
        if (day.durationSec > maxDuration) {
            maxDuration = day.durationSec;
        }
    }
    Esp32BaseWeb::sendChunk("<section class='daily-chart'><h3>最近 14 天出水趋势</h3>"
                            "<p class='hint'>柱状图为每日出水量 L，折线为每日出水总时长。</p>"
                            "<svg viewBox='0 0 700 260' role='img' aria-label='最近14天出水趋势'>");
    constexpr std::uint32_t chartTop = 20;
    constexpr std::uint32_t chartHeight = 150;
    constexpr std::uint32_t baseY = chartTop + chartHeight;
    constexpr std::uint32_t left = 36;
    constexpr std::uint32_t step = 46;
    constexpr std::uint32_t barWidth = 22;
    Esp32BaseWeb::sendChunk("<line class='axis' x1='30' y1='170' x2='680' y2='170'></line>");
    char points[360]{};
    points[0] = '\0';
    for (std::size_t i = 0; i < kChartDays; ++i) {
        const std::uint32_t x = left + static_cast<std::uint32_t>(i) * step;
        const std::uint32_t barHeight = maxVolume == 0 ? 0 : (days[i].volumeMl * chartHeight) / maxVolume;
        const std::uint32_t y = baseY - barHeight;
        sendFmt("<rect class='bar' x='%lu' y='%lu' width='%lu' height='%lu'></rect>",
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(y),
                static_cast<unsigned long>(barWidth),
                static_cast<unsigned long>(barHeight));
        char label[8]{};
        formatDayLabel(days[i].dayIndex, label, sizeof(label));
        sendFmt("<text class='x-label' x='%lu' y='202'>%s</text>",
                static_cast<unsigned long>(x + barWidth / 2),
                label);
        const std::uint32_t lineY =
            maxDuration == 0 ? baseY : baseY - ((days[i].durationSec * chartHeight) / maxDuration);
        char point[24]{};
        std::snprintf(point,
                      sizeof(point),
                      "%s%lu,%lu",
                      i == 0 ? "" : " ",
                      static_cast<unsigned long>(x + barWidth / 2),
                      static_cast<unsigned long>(lineY));
        std::strncat(points, point, sizeof(points) - std::strlen(points) - 1);
    }
    Esp32BaseWeb::sendChunk("<polyline class='duration-line' points='");
    Esp32BaseWeb::sendChunk(points);
    Esp32BaseWeb::sendChunk("'></polyline></svg></section>");
}

void sendTimeUnsyncedChartNotice() {
    Esp32BaseWeb::sendChunk("<section class='daily-chart'><h3>最近 14 天出水趋势</h3>"
                            "<p class='hint'>时间未同步，暂不生成按真实日期统计的图表；同步后会自动显示今日往前 14 天。</p>"
                            "</section>");
}

void formatRecordTime(std::uint32_t seconds, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (seconds >= kMinRealDateSeconds) {
        char date[16]{};
        formatDate(seconds, date, sizeof(date));
        const std::uint32_t daySecond = seconds % 86400UL;
        std::snprintf(out, len, "%s %02lu:%02lu:%02lu",
                      date,
                      static_cast<unsigned long>(daySecond / 3600UL),
                      static_cast<unsigned long>((daySecond / 60UL) % 60UL),
                      static_cast<unsigned long>(daySecond % 60UL));
        return;
    }
    std::snprintf(out, len, "开机+%lu s", static_cast<unsigned long>(seconds));
}

void formatWaterRecordTime(const WaterRecord& record, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (waterRecordHasRealTime(record)) {
        formatRecordTime(record.startTime, out, len);
        return;
    }
    if (waterRecordHasBootRelativeTime(record)) {
        std::snprintf(out, len, "未知时间 / 本次开机+%lu s", static_cast<unsigned long>(record.startTime));
        return;
    }
    std::snprintf(out, len, "历史未知时间");
}

void sendNoticeFromQuery() {
    char text[32]{};
    if (getParam("saved", text, sizeof(text))) {
        Esp32BaseWeb::sendChunk("<p class='ok'>已保存。</p>");
        return;
    }
    if (getParam("reset", text, sizeof(text))) {
        Esp32BaseWeb::sendChunk("<p class='ok'>已重置。</p>");
        return;
    }
    if (!getParam("error", text, sizeof(text))) {
        return;
    }
    const char* message = "操作失败，请检查输入后重试。";
    if (std::strcmp(text, "busy") == 0) {
        message = "设备正在出水或显示结果，请回到待机后再保存。";
    } else if (std::strcmp(text, "invalid_index") == 0) {
        message = "编号无效，请返回列表重新选择。";
    } else if (std::strcmp(text, "invalid_value") == 0) {
        message = "数值超出允许范围，请按页面提示填写。";
    } else if (std::strcmp(text, "invalid_date") == 0) {
        message = "日期格式无效，请重新选择日期。";
    } else if (std::strcmp(text, "save_failed") == 0) {
        message = "保存失败，请稍后重试。";
    } else if (std::strcmp(text, "no_calibration_record") == 0) {
        message = "最新记录没有可用的原始脉冲，不能用于校准。";
    } else if (std::strcmp(text, "calibration_drift") == 0) {
        message = "新系数和旧系数偏差过大，请重新接水测量。";
    }
    Esp32BaseWeb::sendChunk("<p class='err'>");
    Esp32BaseWeb::sendChunk(message);
    Esp32BaseWeb::sendChunk("</p>");
}

void sendAppStyles() {
    Esp32BaseWeb::sendChunk("<style>"
                            "body{background:#f4f7f6;color:#202428;max-width:1080px;padding:10px 14px 14px;line-height:1.45;font-size:14px}"
                            "h1{color:#111;font-size:1rem;font-weight:400;margin:0 0 10px}"
                            "h2{font-size:.98rem;font-weight:400;margin:0 0 10px;color:#111}"
                            "h3{font-size:.94rem;font-weight:400;margin:0;color:#111}"
                            "p{margin:5px 0}"
                            "strong,b,th{font-weight:400}"
                            "nav{display:flex;align-items:center;gap:5px;margin:0 0 12px;padding:10px 12px;background:#fff;border:1px solid #e1e7e5;border-radius:8px;box-shadow:0 1px 2px rgba(21,35,34,.04);overflow-x:auto}"
                            "nav a{display:inline-flex;align-items:center;min-height:34px;padding:0 11px;margin:0;background:transparent;color:#2f3947;border-radius:9px;text-decoration:none;font-size:.98rem;font-weight:600;white-space:nowrap}"
                            "nav a.brand{background:transparent;color:#2f3947}"
                            "nav a.active{background:#e9f3ef;color:#226b5f}"
                            "a,button,input[type=submit],input[type=button]{border-radius:4px;box-shadow:none}"
                            "a{background:transparent;color:#2d6f7a;padding:0;margin:0;text-decoration:none}"
                            "button,input[type=submit],input[type=button]{display:inline-flex;align-items:center;justify-content:center;min-height:34px;padding:0 14px;background:#2f6f73;border:1px solid #2f6f73;color:#fff;cursor:pointer;font-size:.92rem;line-height:1.2;box-sizing:border-box;vertical-align:middle}"
                            ".faucet-actions{display:flex;flex-wrap:wrap;gap:5px;margin:0 0 10px}"
                            ".faucet-actions a{display:inline-flex;align-items:center;min-height:28px;line-height:1.2;margin:0;white-space:nowrap}"
                            "input:not([type]),input[type=text],input[type=password],input[type=number],input[type=email],input[type=url],input[type=tel],input[type=search],input[type=date],select{width:100%;height:30px;padding:4px 8px;margin:0;border:1px solid #d7dde2;border-radius:4px;box-sizing:border-box;background:#fbfcfc;color:#202428;font-size:.94rem}"
                            "select{margin:0}"
                            ".panel{border:1px solid #e1e7e5;border-radius:8px;padding:8px 10px;margin:0 0 8px;background:#fff;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            ".panel h3{padding-bottom:6px;margin-bottom:7px;border-bottom:1px solid #edf0f2}"
                            ".panel-head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:7px;padding-bottom:6px;border-bottom:1px solid #edf0f2}"
                            ".panel-head h3{padding:0;margin:0;border:0}"
                            ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:7px 10px}"
                            ".form-grid{display:grid;grid-template-columns:repeat(12,1fr);gap:9px 12px;align-items:start}"
                            ".span-2{grid-column:span 2}.span-3{grid-column:span 3}.span-4{grid-column:span 4}.span-5{grid-column:span 5}.span-6{grid-column:span 6}.span-8{grid-column:span 8}.span-12{grid-column:1/-1}"
                            ".field{display:block;margin:0}"
                            ".field span{display:block;font-size:.82em;color:#56616b;margin-bottom:3px}"
                            ".field input{margin:0}"
                            ".hint{display:block;color:#69727a;font-size:.76em;margin:2px 0 0}"
                            ".status-pill{display:inline-block;padding:1px 7px;border-radius:999px;background:#f2f5f4;color:#4c565d;font-size:.76rem;line-height:1.35;white-space:nowrap}"
                            ".muted{color:#69727a}"
                            ".metric-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:7px;margin:0 0 10px}"
                            ".metric-card{background:#fff;border:1px solid #e1e7e5;border-radius:8px;padding:7px 9px;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            ".metric-card span{display:block;color:#69727a;font-size:.8rem;margin-bottom:2px}"
                            ".metric-card strong{display:block;color:#202428;font-size:.94rem;line-height:1.3}"
                            ".stats-layout{display:grid;grid-template-columns:minmax(0,1fr) minmax(240px,.75fr);gap:10px;align-items:start}"
                            ".stat-bars{background:#fff;border:1px solid #e1e7e5;border-radius:8px;padding:10px;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            ".stat-bars .hint{margin:0 0 8px}"
                            ".stat-bar{margin:0 0 10px}.stat-bar:last-child{margin-bottom:0}"
                            ".stat-bar-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:4px;color:#56616b;font-size:.86rem}"
                            ".stat-bar-head strong{color:#202428}"
                            ".daily-chart{grid-column:1/-1;background:#fff;border:1px solid #e1e7e5;border-radius:8px;padding:10px;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            ".daily-chart svg{display:block;width:100%;height:auto;max-height:300px}.daily-chart .bar{fill:#6b9b96}.daily-chart .duration-line{fill:none;stroke:#c77f2f;stroke-width:3;stroke-linejoin:round;stroke-linecap:round}.daily-chart .axis{stroke:#d9e0df;stroke-width:1}.daily-chart .x-label{font-size:11px;text-anchor:middle;fill:#69727a}"
                            ".filter-cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:8px;margin:0 0 10px}"
                            ".filter-card{background:#fff;border:1px solid #e1e7e5;border-radius:8px;padding:8px 10px;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            ".filter-head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:6px}"
                            ".filter-head strong{color:#111}"
                            ".filter-meta{display:grid;grid-template-columns:1fr 1fr;gap:3px 8px;color:#56616b;font-size:.82rem;margin-top:6px}"
                            ".progress{height:5px;background:#e4e9ed;border-radius:999px;overflow:hidden}"
                            ".progress span{display:block;height:100%;background:#4f8a86;border-radius:999px}"
                            ".check-field{display:block;margin:0}"
                            ".check-title{display:block;font-size:.82em;color:#56616b;margin-bottom:3px}"
                            ".check-line{display:inline-flex;align-items:center;gap:6px;min-height:28px;padding:0 8px;border:1px solid #d7dde2;border-radius:4px;background:#fff;box-sizing:border-box;color:#202428;font-size:.92rem;white-space:nowrap}"
                            ".check-line input[type=checkbox]{margin:0;flex:0 0 auto}"
                            ".form-actions{display:flex;align-items:center;justify-content:flex-start;gap:6px;margin-top:8px;flex-wrap:wrap}"
                            ".form-actions form{margin:0}"
                            "form input[type=submit]{min-height:34px;padding:0 14px;margin:0;font-size:.92rem;line-height:1.2}"
                            ".form-actions a,.btn-link{display:inline-flex;align-items:center;justify-content:center;min-height:34px;padding:0 12px;border:1px solid #d7dde2;border-radius:4px;background:#f7f9fa;color:#355e66;font-size:.92rem;line-height:1.2;box-sizing:border-box}"
                            ".form-actions input.secondary{background:#f7f9fa;border-color:#d7dde2;color:#4c565d}"
                            ".compact-table td,.compact-table th{padding:6px 8px}"
                            ".filters-table th:first-child{width:22%}.filters-table th:last-child{width:150px}"
                            ".row-actions{display:flex;gap:5px;align-items:center;justify-content:flex-start;flex-wrap:wrap}"
                            ".row-actions a,.row-actions input[type=submit]{display:inline-flex;align-items:center;justify-content:center;min-width:48px;min-height:30px;padding:0 10px;border:1px solid #d7dde2;border-radius:4px;background:#f7f9fa;color:#355e66;font-size:.86rem;box-sizing:border-box}"
                            "table{width:100%;border-collapse:collapse;margin:0 0 10px;background:#fff;border:1px solid #e1e7e5;border-radius:8px;overflow:hidden;font-size:.92rem;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            "td,th{padding:5px 8px;border-bottom:1px solid #edf0f2;text-align:left;vertical-align:middle}"
                            "th{background:#fafafa;color:#56616b}"
                            "tr:last-child td{border-bottom:0}"
                            "td .form-actions{margin-top:0;gap:5px;flex-wrap:nowrap}"
                            "td .form-actions a,td .form-actions input[type=submit]{min-height:30px;padding:0 10px;font-size:.86rem}"
                            ".pager{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;margin:0 0 8px}"
                            ".pager-links{display:flex;align-items:center;gap:5px;flex-wrap:wrap}"
                            ".page-link,.page-current{display:inline-flex;align-items:center;justify-content:center;min-height:30px;padding:0 10px;border:1px solid #d7dde2;border-radius:4px;background:#fff;color:#355e66;font-size:.88rem;box-sizing:border-box}"
                            ".page-current{background:#e9f3ef;color:#226b5f;border-color:#cfe4dc}"
                            ".page-disabled{color:#9aa3aa;background:#f4f6f6;pointer-events:none}"
                            ".page-size{display:flex;align-items:center;gap:6px;color:#56616b;font-size:.86rem}.page-size select{width:auto;min-width:80px}"
                            ".kv{margin:0 0 10px;background:#fff;border:1px solid #e1e7e5;border-radius:8px;box-shadow:0 1px 2px rgba(21,35,34,.04);overflow:hidden}"
                            ".kv th,.kv td{padding:5px 8px;border-bottom:1px solid #edf0f2}.kv th{width:26%;color:#56616b;background:#fbfcfc}"
                            "form[action^='/esp32base'],#f,details,pre{background:#fff;border:1px solid #e1e7e5;border-radius:8px;padding:8px 10px;margin:0 0 8px;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            "form[action^='/esp32base'] input:not([type]),form[action^='/esp32base'] input[type=text],form[action^='/esp32base'] input[type=password],#f input:not([type]){margin:3px 0 7px}"
                            "input[type=file]{margin:3px 8px 7px 0;font-size:.9rem}"
                            "body>h3{background:#fff;border:1px solid #e1e7e5;border-bottom:0;border-radius:8px 8px 0 0;padding:8px 10px;margin:8px 0 0;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            "body>h3+p{background:#fff;border-left:1px solid #e1e7e5;border-right:1px solid #e1e7e5;margin:0;padding:0 10px 6px}"
                            "body>h3+p+form[action^='/esp32base/tools']{border-top:0;border-radius:0 0 8px 8px;margin-top:0}"
                            ".ok,.err{display:block;background:#fff;border:1px solid #e1e7e5;border-radius:8px;padding:6px 10px;margin:0 0 8px;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            ".footerbar{margin-top:10px;padding:6px 10px;background:#fff;border:1px solid #e1e7e5;border-radius:8px;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            "@media(max-width:720px){.stats-layout{grid-template-columns:1fr}.form-grid{grid-template-columns:1fr}.span-2,.span-3,.span-4,.span-5,.span-6,.span-8,.span-12{grid-column:1/-1}}"
                            "@media(max-width:520px){body{padding:8px 10px 12px}nav{padding:8px 10px}.grid,.metric-grid,.filter-cards{grid-template-columns:1fr}.panel{padding:8px}.form-actions{gap:5px}.kv th{width:34%}.pager{align-items:flex-start}.page-size{width:100%}}"
                            "</style>");
}

bool sendPageStart(const char* title) {
    if (!Esp32BaseWeb::checkAuth()) {
        return false;
    }
    Esp32BaseWeb::sendHeader(title);
    return true;
}

void sendPageEnd() {
    Esp32BaseWeb::sendFooter();
}

const char* stateText(WaterState state) {
    switch (state) {
        case WaterState::Idle:
            return "待机";
        case WaterState::Confirm:
            return "确认";
        case WaterState::Running:
            return "出水中";
        case WaterState::Paused:
            return "暂停";
        case WaterState::Error:
            return "异常";
    }
    return "未知";
}

const char* modeText(WaterMode mode) {
    switch (mode) {
        case WaterMode::Volume:
            return "容量";
        case WaterMode::Time:
            return "时间";
    }
    return "未知";
}

const char* resultText(WaterResult result) {
    switch (result) {
        case WaterResult::Completed:
            return "完成";
        case WaterResult::StoppedByUser:
            return "手动停止";
        case WaterResult::SafetyStopped:
            return "安全停止";
        case WaterResult::FlowError:
            return "流量异常";
        case WaterResult::PauseTimeout:
            return "暂停超时";
    }
    return "未知";
}

void sendTextInput(const char* label, const char* name, unsigned long value) {
    sendFmt("<label class='field'><span>%s</span><input name='%s' value='%lu'></label>", label, name, value);
}

void sendVolumeInput(const char* label, const char* name, std::uint32_t value) {
    char liters[24]{};
    formatLiters(value, liters, sizeof(liters));
    sendFmt("<label class='field'><span>%s</span><input name='%s' value='%lu'><small class='hint'>当前 %s</small></label>",
            label,
            name,
            static_cast<unsigned long>(value),
            liters);
}

void sendMonthInput(const char* label, const char* name, std::uint32_t days) {
    sendFmt("<label class='field'><span>%s</span><input name='%s' value='%lu'><small class='hint'>按 30 天/月计算</small></label>",
            label,
            name,
            static_cast<unsigned long>(daysToMonths(days)));
}

void sendDateInput(const char* label, const char* name, std::uint32_t seconds) {
    char date[16]{};
    formatDate(seconds >= kMinRealDateSeconds ? seconds : 0, date, sizeof(date));
    sendFmt("<label class='field'><span>%s</span><input type='date' name='%s' value='%s'></label>",
            label,
            name,
            date);
}

void sendCheckbox(const char* label, const char* name, bool checked) {
    sendFmt("<label class='check-field'><span class='check-title'>%s</span><span class='check-line'><input type='checkbox' name='%s' value='1'%s>启用</span></label>",
            label,
            name,
            checked ? " checked" : "");
}

void handleFaucetPage() {
    if (!sendPageStart("出水龙头")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const AppSnapshot snapshot = g_context.app->snapshot();
    char currentPreset[16]{};
    std::snprintf(currentPreset, sizeof(currentPreset), "%u", static_cast<unsigned>(snapshot.water.selectedPreset + 1));
    char targetValue[24]{};
    if (snapshot.water.mode == WaterMode::Time) {
        std::snprintf(targetValue, sizeof(targetValue), "%lu 秒", static_cast<unsigned long>(snapshot.water.targetValue));
    } else {
        formatLiters(snapshot.water.targetValue, targetValue, sizeof(targetValue));
    }
    char today[24]{};
    char week[24]{};
    char month[24]{};
    char total[24]{};
    formatLiters(snapshot.statistics.todayMl, today, sizeof(today));
    formatLiters(snapshot.statistics.weekMl, week, sizeof(week));
    formatLiters(snapshot.statistics.monthMl, month, sizeof(month));
    formatLiters(snapshot.statistics.totalMl, total, sizeof(total));

    Esp32BaseWeb::sendChunk("<h2>状态</h2><div class='metric-grid'>");
    sendMetricCard("运行状态", stateText(snapshot.water.state));
    sendMetricCard("当前预设", currentPreset);
    sendMetricCard("目标值", targetValue);
    sendMetricCard("今日用水", today);
    Esp32BaseWeb::sendChunk("</div><h2>统计</h2><div class='metric-grid'>");
    sendMetricCard("今日", today);
    sendMetricCard("本周", week);
    sendMetricCard("本月", month);
    sendMetricCard("总累计", total);
    Esp32BaseWeb::sendChunk("</div><h2>滤芯</h2><div class='filter-cards'>");
    bool anyFilter = false;
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const FilterRecord& filter = g_context.filters->record(i);
        if (!filter.enabled) {
            continue;
        }
        anyFilter = true;
        const std::uint32_t usedDays =
            filter.startTime >= kMinRealDateSeconds ? g_context.filters->usedDays(i, g_context.nowSeconds()) : 0;
        char life[32]{};
        formatLifeRange(filter, life, sizeof(life));
        char usedFlow[24]{};
        formatLiters(filter.usedMl, usedFlow, sizeof(usedFlow));
        const std::uint32_t progress = filterProgressPercent(filter, usedDays);
        Esp32BaseWeb::sendChunk("<section class='filter-card'><div class='filter-head'><strong>");
        Esp32BaseWeb::writeHtmlEscaped(filter.name);
        sendFmt("</strong><span class='status-pill'>%s</span></div>",
                filterDisplayStatusText(filter, usedDays));
        if (filter.recommendDays > 0) {
            sendFmt("<div class='progress'><span style='width:%lu%%'></span></div>",
                    static_cast<unsigned long>(progress));
            sendFmt("<div class='filter-meta'><span>已用 %lu 天</span><span>寿命 %s</span><span>流量 %s</span><span>进度 %lu%%</span></div></section>",
                    static_cast<unsigned long>(usedDays),
                    life,
                    usedFlow,
                    static_cast<unsigned long>(progress));
        } else {
            Esp32BaseWeb::sendChunk("<p class='hint'>未设置周期</p>");
            sendFmt("<div class='filter-meta'><span>已用 %lu 天</span><span>寿命 %s</span><span>流量 %s</span></div></section>",
                    static_cast<unsigned long>(usedDays),
                    life,
                    usedFlow);
        }
    }
    if (!anyFilter) {
        Esp32BaseWeb::sendChunk("<section class='filter-card muted'>当前没有启用的滤芯。</section>");
    }
    Esp32BaseWeb::sendChunk("</div>");
    sendPageEnd();
}

void handlePresetsPage() {
    if (!sendPageStart("出水预设")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }

    char text[24]{};
    std::uint32_t editIndex = UINT32_MAX;
    if (getParam("index", text, sizeof(text))) {
        parseU32(text, editIndex);
    }
    if (editIndex < kPresetCount) {
        const PresetConfig& preset = g_context.config->presets[editIndex];
        const bool timePreset = preset.type == PresetType::Time;
        sendFmt("<h2>预设 %u</h2><section class='panel'>"
                "<form method='post' action='/api/faucet/presets' onsubmit='return once(this)'>"
                "<input type='hidden' name='index' value='%u'><input type='hidden' name='return' value='/faucet/presets'>"
                "<div class='form-grid'>",
                static_cast<unsigned>(editIndex + 1),
                static_cast<unsigned>(editIndex));
        sendNoticeFromQuery();
        Esp32BaseWeb::sendChunk("<div class='span-2'>");
        sendCheckbox("启用", "enabled", preset.enabled);
        Esp32BaseWeb::sendChunk("</div><label class='field span-5'><span>名称</span><input name='name' maxlength='15' value='");
        sendHtmlAttrEscaped(preset.name);
        Esp32BaseWeb::sendChunk("'><small class='hint'>最多 15 个字符</small></label><label class='field span-2'><span>类型</span><select id='presetType' name='type' onchange='presetTypeChanged()'>");
        sendFmt("<option value='volume'%s>容量</option>", preset.type == PresetType::Volume ? " selected" : "");
        sendFmt("<option value='time'%s>时间</option>", preset.type == PresetType::Time ? " selected" : "");
        Esp32BaseWeb::sendChunk("</select></label>");
        Esp32BaseWeb::sendChunk("<label class='field span-3'><span id='presetValueLabel'>");
        Esp32BaseWeb::sendChunk(timePreset ? "时长（秒）" : "出水量（ml）");
        sendFmt("</span><input id='presetValue' name='value' type='number' min='%lu' max='%lu' step='%lu' value='%lu'>",
                static_cast<unsigned long>(timePreset ? kMinTimePresetSec : kMinVolumePresetMl),
                static_cast<unsigned long>(timePreset ? kMaxTimePresetSec : kMaxVolumePresetMl),
                static_cast<unsigned long>(timePreset ? 1UL : 100UL),
                static_cast<unsigned long>(preset.value));
        Esp32BaseWeb::sendChunk("<small id='presetValueHint' class='hint'>");
        Esp32BaseWeb::sendChunk(timePreset ? "按秒计时，到时自动关阀" : "按毫升计量，到量自动关阀");
        Esp32BaseWeb::sendChunk("</small></label>");
        Esp32BaseWeb::sendChunk("</div><div class='form-actions'><input type='submit' value='保存'>"
                                "<a href='/faucet/presets'>取消</a></div></form></section>"
                                "<script>"
                                "function presetTypeChanged(){"
                                "var t=document.getElementById('presetType').value;"
                                "var v=document.getElementById('presetValue');"
                                "document.getElementById('presetValueLabel').textContent=t=='time'?'时长（秒）':'出水量（ml）';"
                                "document.getElementById('presetValueHint').textContent=t=='time'?'按秒计时，到时自动关阀':'按毫升计量，到量自动关阀';"
                                "v.min=t=='time'?'5':'100';v.max=t=='time'?'1800':'20000';v.step=t=='time'?'1':'100';"
                                "}"
                                "</script>");
        sendPageEnd();
        return;
    }

    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<h2>预设</h2><table class='compact-table'><tr>"
                            "<th>序号</th><th>名称</th><th>类型</th><th>数值</th><th>状态</th><th>操作</th></tr>");
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        const PresetConfig& preset = g_context.config->presets[i];
        sendFmt("<tr><td>%u</td><td>",
                static_cast<unsigned>(i + 1));
        Esp32BaseWeb::writeHtmlEscaped(preset.name);
        sendFmt("</td><td>%s</td><td>",
                preset.type == PresetType::Time ? "时间" : "容量");
        if (preset.type == PresetType::Time) {
            sendFmt("%lu 秒", static_cast<unsigned long>(preset.value));
        } else {
            sendLiters(preset.value);
        }
        sendFmt("</td><td><span class='status-pill'>%s</span></td>"
                "<td><div class='row-actions'><a href='/faucet/presets?index=%u'>编辑</a></div></td></tr>",
                preset.enabled ? "启用" : "停用",
                static_cast<unsigned>(i));
    }
    Esp32BaseWeb::sendChunk("</table>");
    sendPageEnd();
}

void handleStatsPage() {
    if (!sendPageStart("用水统计")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const std::uint32_t now = g_context.nowSeconds();
    const RecordAggregates aggregates = aggregateRecords(now);
    char today[24]{};
    char week[24]{};
    char month[24]{};
    char total[24]{};
    char weekAvg[24]{};
    char monthAvg[24]{};
    char unknown[24]{};
    char unknownDuration[24]{};
    formatLiters(aggregates.todayMl, today, sizeof(today));
    formatLiters(aggregates.weekMl, week, sizeof(week));
    formatLiters(aggregates.monthMl, month, sizeof(month));
    formatLiters(g_context.app->snapshot().statistics.totalMl, total, sizeof(total));
    formatLiters((aggregates.weekMl + 3UL) / 7UL, weekAvg, sizeof(weekAvg));
    formatLiters((aggregates.monthMl + 14UL) / 30UL, monthAvg, sizeof(monthAvg));
    formatLiters(aggregates.unknownMl, unknown, sizeof(unknown));
    std::snprintf(unknownDuration,
                  sizeof(unknownDuration),
                  "%lu s / %lu 条",
                  static_cast<unsigned long>(aggregates.unknownDurationSec),
                  static_cast<unsigned long>(aggregates.unknownCount));
    Esp32BaseWeb::sendChunk("<h2>统计</h2><div class='stats-layout'><div><div class='metric-grid'>");
    sendMetricCard("今日", today);
    sendMetricCard("本周", week);
    sendMetricCard("本月", month);
    sendMetricCard("总累计", total);
    Esp32BaseWeb::sendChunk("</div><div class='metric-grid'>");
    sendMetricCard("本周日均", weekAvg);
    sendMetricCard("本月日均", monthAvg);
    char todayWeek[16]{};
    char weekMonth[16]{};
    std::snprintf(todayWeek, sizeof(todayWeek), "%lu%%", static_cast<unsigned long>(percentOf(aggregates.todayMl, aggregates.weekMl)));
    std::snprintf(weekMonth, sizeof(weekMonth), "%lu%%", static_cast<unsigned long>(percentOf(aggregates.weekMl, aggregates.monthMl)));
    sendMetricCard("今日占本周", todayWeek);
    sendMetricCard("本周占本月", weekMonth);
    sendMetricCard("未知时间出水量", unknown);
    sendMetricCard("未知时间时长", unknownDuration);
    Esp32BaseWeb::sendChunk("</div></div>");
    if (now >= kMinRealDateSeconds) {
        sendDailyChart(aggregates.days);
    } else {
        sendTimeUnsyncedChartNotice();
    }
    Esp32BaseWeb::sendChunk("</div>");
    sendPageEnd();
}

void handleRecordsPage() {
    if (!sendPageStart("出水记录")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    char text[24]{};
    std::uint32_t page = 0;
    if (getParam("page", text, sizeof(text))) {
        parseU32(text, page);
    }
    std::uint32_t requestedPageSize = kDefaultRecordPageSize;
    if (getParam("pageSize", text, sizeof(text))) {
        parseU32(text, requestedPageSize);
    }
    const std::uint16_t pageSize = sanitizeRecordPageSize(static_cast<std::uint16_t>(requestedPageSize));
    WaterRecord records[kMaxRecordPageSize]{};
    const bool ready = g_context.records->ready();
    const std::size_t total = ready ? g_context.records->count() : 0;
    const std::uint32_t maxPage = total == 0 ? 0 : static_cast<std::uint32_t>((total - 1) / pageSize);
    if (page > maxPage) {
        page = maxPage;
    }
    const std::size_t count = ready ? g_context.records->readPage(page, pageSize, records, pageSize) : 0;
    Esp32BaseWeb::sendChunk("<h2>记录</h2>");
    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<div class='pager'><div class='pager-links'>");
    const bool hasPrev = ready && total > 0 && page > 0;
    const bool hasNext = ready && total > 0 && page < maxPage;
    if (hasPrev) {
        sendFmt("<a class='page-link' href='/faucet/records?page=0&pageSize=%u'>首页</a>"
                "<a class='page-link' href='/faucet/records?page=%lu&pageSize=%u'>上一页</a>",
                static_cast<unsigned>(pageSize),
                static_cast<unsigned long>(page - 1),
                static_cast<unsigned>(pageSize));
    } else {
        Esp32BaseWeb::sendChunk("<span class='page-link page-disabled'>首页</span><span class='page-link page-disabled'>上一页</span>");
    }
    sendFmt("<span class='page-current'>第 %lu / %lu 页</span>",
            static_cast<unsigned long>(page + 1),
            static_cast<unsigned long>(maxPage + 1));
    if (hasNext) {
        sendFmt("<a class='page-link' href='/faucet/records?page=%lu&pageSize=%u'>下一页</a>"
                "<a class='page-link' href='/faucet/records?page=%lu&pageSize=%u'>末页</a>",
                static_cast<unsigned long>(page + 1),
                static_cast<unsigned>(pageSize),
                static_cast<unsigned long>(maxPage),
                static_cast<unsigned>(pageSize));
    } else {
        Esp32BaseWeb::sendChunk("<span class='page-link page-disabled'>下一页</span><span class='page-link page-disabled'>末页</span>");
    }
    sendFmt("</div><form class='page-size' method='get' action='/faucet/records'>"
            "<input type='hidden' name='page' value='0'><span>每页</span><select name='pageSize' onchange='this.form.submit()'>");
    constexpr std::uint16_t sizes[] = {20, 30, 50, 100, 200};
    for (std::uint16_t size : sizes) {
        sendFmt("<option value='%u'%s>%u</option>",
                static_cast<unsigned>(size),
                pageSize == size ? " selected" : "",
                static_cast<unsigned>(size));
    }
    sendFmt("</select><span>条</span></form></div><p class='hint'>共 %lu 条记录</p>",
            static_cast<unsigned long>(total));
    if (!ready) {
        Esp32BaseWeb::sendChunk("<p class='err'>记录存储不可用。</p>");
    } else if (total == 0) {
        Esp32BaseWeb::sendChunk("<p class='ok'>暂无出水记录。</p>");
    }
    Esp32BaseWeb::sendChunk("<table><tr><th>开始时间</th><th>目标</th><th>出水量 (L)</th><th>脉冲数</th>"
                            "<th>过滤脉冲</th><th>当时系数</th><th>持续时间 (s)</th><th>预设模式</th><th>结束原因</th><th>操作</th></tr>");
    for (std::size_t i = 0; i < count; ++i) {
        char startTime[40]{};
        formatWaterRecordTime(records[i], startTime, sizeof(startTime));
        const bool latestRecord = page == 0 && i == 0;
        const bool canCalibrate = latestRecord && waterRecordCanCalibrate(records[i]);
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::sendChunk(startTime);
        Esp32BaseWeb::sendChunk("</td><td>");
        sendTargetValue(records[i]);
        Esp32BaseWeb::sendChunk("</td><td>");
        sendLiters(records[i].volumeMl);
        sendFmt("</td><td>%lu</td><td>%lu</td><td>%.3f</td><td>%u s</td><td>%s</td><td>%s</td><td>",
                static_cast<unsigned long>(records[i].pulseCount),
                static_cast<unsigned long>(records[i].rejectedPulseCount),
                static_cast<double>(records[i].pulsePerMlAtRun),
                static_cast<unsigned>(records[i].durationSec),
                modeText(records[i].mode),
                resultText(records[i].result));
        if (canCalibrate) {
            sendFmt("<form method='post' action='/api/faucet/records/calibration' onsubmit='return once(this)'>"
                    "<label><span>量杯实际水量</span><input name='actualMl' type='number' min='%lu' max='%lu' step='10' value='%lu'></label>"
                    "<input type='submit' value='校准'></form>",
                    static_cast<unsigned long>(kMinVolumePresetMl),
                    static_cast<unsigned long>(kMaxVolumePresetMl),
                    static_cast<unsigned long>(records[i].volumeMl));
        } else if (!latestRecord) {
            Esp32BaseWeb::sendChunk("<span class='muted'>仅最新记录</span>");
        } else {
            Esp32BaseWeb::sendChunk("<span class='muted'>不可校准</span>");
        }
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table>");
    sendPageEnd();
}

void handleFiltersPage() {
    if (!sendPageStart("滤芯状态")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const std::uint32_t now = g_context.nowSeconds();
    char todayDate[16]{};
    formatDate(now >= kMinRealDateSeconds ? now : 0, todayDate, sizeof(todayDate));
    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<h2>滤芯</h2><table class='filters-table'><tr><th>名称</th><th>启用状态</th><th>已用天数 (天)</th><th>已用流量 (L)</th><th>寿命规则</th><th>状态</th><th>操作</th></tr>");
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const FilterRecord& filter = g_context.filters->record(i);
        const std::uint32_t usedDays = filter.startTime >= kMinRealDateSeconds ? g_context.filters->usedDays(i, now) : 0;
        char life[32]{};
        formatLifeRange(filter, life, sizeof(life));
        sendFmt("<tr data-filter-start='%lu' data-filter-boot='%lu' data-filter-enabled='%u' data-filter-recommend-days='%lu' data-filter-max-days='%lu' data-filter-life-ml='%lu' data-filter-used-ml='%lu'><td>",
                static_cast<unsigned long>(filter.startTime),
                static_cast<unsigned long>(filter.startBootId),
                filter.enabled ? 1U : 0U,
                static_cast<unsigned long>(filter.recommendDays),
                static_cast<unsigned long>(filter.maxDays),
                static_cast<unsigned long>(filter.lifeMl),
                static_cast<unsigned long>(filter.usedMl));
        Esp32BaseWeb::writeHtmlEscaped(filter.name);
        sendFmt("</td><td>%s</td><td class='filter-used-days'>%lu 天</td><td>",
                filter.enabled ? "启用" : "停用",
                static_cast<unsigned long>(usedDays));
        sendLiters(filter.usedMl);
        sendFmt("</td><td>%s", life);
        Esp32BaseWeb::sendChunk(" / ");
        if (filter.lifeMl > 0) {
            sendLiters(filter.lifeMl);
        } else {
            Esp32BaseWeb::sendChunk("未设置流量");
        }
        sendFmt("</td><td><span class='status-pill filter-status'>%s</span></td><td><div class='row-actions'><a href='/faucet/filters/edit?index=%u'>设置</a>",
                filterDisplayStatusText(filter, usedDays),
                static_cast<unsigned>(i));
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/faucet/filters/reset' data-filter-name='");
        sendHtmlAttrEscaped(filter.name);
        Esp32BaseWeb::sendChunk("' data-reset-date='");
        sendHtmlAttrEscaped(todayDate);
        Esp32BaseWeb::sendChunk("' onsubmit=\"return confirmFilterReset(this)&&once(this)\">");
        sendFmt("<input type='hidden' name='index' value='%u'>", static_cast<unsigned>(i));
        Esp32BaseWeb::sendChunk("<input class='secondary' type='submit' value='重置'></form></div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table><script>"
                            "function confirmFilterReset(form){"
                            "var name=form.getAttribute('data-filter-name')||'滤芯';"
                            "var date=form.getAttribute('data-reset-date')||'';"
                            "var msg=date?('确认将【'+name+'】更换日期重置为 '+date+'，并清空已用流量？'):"
                            "('当前时间未同步。确认将【'+name+'】更换日期记录为本次开机未知时间，并清空已用流量？');"
                            "return confirm(msg);"
                            "}"
                            "(function(){"
                            "var unix2000=946684800;"
                            "document.querySelectorAll('tr[data-filter-start]').forEach(function(row){"
                            "var start=parseInt(row.getAttribute('data-filter-start')||'0',10);"
                            "var boot=parseInt(row.getAttribute('data-filter-boot')||'0',10);"
                            "if(boot){return;}"
                            "if(!start){return;}"
                            "var now=Math.floor(Date.now()/1000)-unix2000;"
                            "if(!isFinite(now)||now<=start){return;}"
                            "var days=Math.floor((now-start)/86400);"
                            "var used=row.querySelector('.filter-used-days');"
                            "if(used){used.textContent=days+' 天';}"
                            "var status=row.querySelector('.filter-status');"
                            "if(!status){return;}"
                            "var enabled=row.getAttribute('data-filter-enabled')==='1';"
                            "var rec=parseInt(row.getAttribute('data-filter-recommend-days')||'0',10);"
                            "var max=parseInt(row.getAttribute('data-filter-max-days')||'0',10);"
                            "var life=parseInt(row.getAttribute('data-filter-life-ml')||'0',10);"
                            "var flow=parseInt(row.getAttribute('data-filter-used-ml')||'0',10);"
                            "var text='正常';"
                            "if(!enabled){text='停用';}"
                            "else if((life>0&&flow>=life)||(max>0&&days>=max)){text='已超期';}"
                            "else if(rec>0&&days>=rec){text='建议更换';}"
                            "status.textContent=text;"
                            "});"
                            "})();"
                            "</script>");
    sendPageEnd();
}

void handleFilterEditPage() {
    if (!sendPageStart("滤芯设置")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }

    char text[24]{};
    std::uint32_t index = 0;
    if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kFilterCount) {
        Esp32BaseWeb::sendChunk("<h2>滤芯设置</h2><section class='panel'><p>滤芯编号无效。</p>"
                                "<p><a href='/faucet/filters'>返回滤芯列表</a></p></section>");
        sendPageEnd();
        return;
    }

    const FilterRecord& filter = g_context.filters->record(index);
    sendNoticeFromQuery();
    sendFmt("<h2>第 %u 级滤芯设置</h2>"
            "<section class='panel'><form method='post' action='/api/faucet/filters' onsubmit='return once(this)'>"
            "<input type='hidden' name='index' value='%u'><input type='hidden' name='return' value='/faucet/filters'>"
            "<div class='form-grid'>",
            static_cast<unsigned>(index + 1),
            static_cast<unsigned>(index));
    Esp32BaseWeb::sendChunk("<div class='span-2'>");
    sendCheckbox("启用", "enabled", filter.enabled);
    Esp32BaseWeb::sendChunk("</div><label class='field span-6'><span>名称</span><input name='name' maxlength='");
    sendFmt("%u", static_cast<unsigned>(kFilterNameMaxChars));
    Esp32BaseWeb::sendChunk("' value='");
    sendHtmlAttrEscaped(filter.name);
    Esp32BaseWeb::sendChunk("'><small class='hint'>最多 30 个字符，用于列表和状态页显示</small></label>"
                            "<div class='span-4'></div>");
    Esp32BaseWeb::sendChunk("<div class='span-3'>");
    sendMonthInput("建议更换周期（月）", "recommendMonths", filter.recommendDays);
    Esp32BaseWeb::sendChunk("</div><div class='span-3'>");
    sendMonthInput("最长使用周期（月）", "maxMonths", filter.maxDays);
    Esp32BaseWeb::sendChunk("</div><div class='span-3'>");
    sendVolumeInput("寿命流量（ml）", "lifeMl", filter.lifeMl);
    Esp32BaseWeb::sendChunk("<small class='hint'>0 表示不按流量判断</small></div><div class='span-3'>");
    sendDateInput("上次更换日期", "startDate", filter.startTime);
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::sendChunk("</div><div class='form-actions'><input type='submit' value='保存'>"
                            "<a href='/faucet/filters'>取消</a></div></form></section>");
    sendPageEnd();
}

void handleApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendJson(200, "{\"ok\":true,\"waterControl\":false}");
}

bool sendJsonBuffer(bool ok, const char* json) {
    if (!ok) {
        Esp32BaseWeb::sendJson(500, "{\"error\":\"buffer_too_small\"}");
        return false;
    }
    Esp32BaseWeb::sendJson(200, json);
    return true;
}

bool requireContext() {
    if (!g_context.config || !g_context.configStore || !g_context.app || !g_context.filters || !g_context.records ||
        !g_context.nowSeconds) {
        Esp32BaseWeb::sendJson(503, "{\"error\":\"context_not_ready\"}");
        return false;
    }
    return true;
}

bool getParam(const char* name, char* out, std::size_t len) {
    return Esp32BaseWeb::hasParam(name) && Esp32BaseWeb::getParam(name, out, len);
}

void applyU32Param(const char* name, std::uint32_t& value) {
    char text[24]{};
    std::uint32_t parsed = 0;
    if (getParam(name, text, sizeof(text)) && parseU32(text, parsed)) {
        value = parsed;
    }
}

bool checkboxParam(const char* name) {
    return Esp32BaseWeb::hasParam(name);
}


bool persistConfig(const SystemConfig& config) {
    if (!g_context.app->canApplyConfig()) {
        return false;
    }

    SystemConfig safe = config;
    sanitizeConfig(safe);
    if (!g_context.configStore->saveSystemConfig(safe)) {
        return false;
    }
    if (!g_context.app->applyConfig(safe)) {
        return false;
    }
    *g_context.config = safe;
    if (g_context.applySettings) {
        g_context.applySettings(*g_context.config);
    }
    return true;
}

bool saveConfigAndReply(const SystemConfig& config) {
    if (!g_context.app->canApplyConfig()) {
        Esp32BaseWeb::sendJson(409, "{\"error\":\"busy\",\"restartRecommended\":true}");
        return false;
    }
    const bool ok = persistConfig(config);
    Esp32BaseWeb::sendJson(ok ? 200 : 500,
                           ok ? "{\"ok\":true,\"restartRecommended\":true}" : "{\"error\":\"save_failed\"}");
    return ok;
}

bool persistFilterConfig(const FilterRecord& record, std::size_t index) {
    if (!g_context.app->canApplyConfig() || index >= kFilterCount) {
        return false;
    }

    SystemConfig* safe = new (std::nothrow) SystemConfig(*g_context.config);
    if (!safe) {
        return false;
    }
    safe->filters[index] = record;
    sanitizeConfig(*safe);

    FilterRecord runtime[kFilterCount]{};
    const FilterRecord* current = g_context.filters->records();
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        runtime[i] = current[i];
    }
    runtime[index] = safe->filters[index];

    if (!g_context.configStore->saveSystemConfig(*safe) || !g_context.configStore->saveFilterRuntime(runtime)) {
        delete safe;
        return false;
    }
    if (!g_context.filters->updateFilter(index, safe->filters[index]) || !g_context.app->applyConfig(*safe)) {
        delete safe;
        return false;
    }
    *g_context.config = *safe;
    delete safe;
    if (g_context.applySettings) {
        g_context.applySettings(*g_context.config);
    }
    return true;
}

void handleStatusApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char json[256]{};
    sendJsonBuffer(writeStatusJson(g_context.app->snapshot(), json, sizeof(json)), json);
}

void handlePresetsApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        const bool browserForm = Esp32BaseWeb::hasParam("return");
        if (!g_context.app->canApplyConfig()) {
            if (browserForm) {
                Esp32BaseWeb::redirectSeeOther("/faucet/presets?error=busy");
                return;
            }
            Esp32BaseWeb::sendJson(409, "{\"error\":\"busy\",\"restartRecommended\":true}");
            return;
        }
        char text[24]{};
        std::uint32_t index = 0;
        if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kPresetCount) {
            if (browserForm) {
                Esp32BaseWeb::redirectSeeOther("/faucet/presets?error=invalid_index");
                return;
            }
            Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_index\"}");
            return;
        }
        SystemConfig* candidate = new (std::nothrow) SystemConfig(*g_context.config);
        if (!candidate) {
            if (browserForm) {
                Esp32BaseWeb::redirectSeeOther("/faucet/presets?error=save_failed");
                return;
            }
            Esp32BaseWeb::sendJson(500, "{\"error\":\"oom\"}");
            return;
        }
        PresetConfig& preset = candidate->presets[index];
        preset.enabled = checkboxParam("enabled");
        if (getParam("type", text, sizeof(text))) {
            preset.type = std::strcmp(text, "time") == 0 ? PresetType::Time : PresetType::Volume;
        }
        std::uint32_t value = 0;
        const bool validValue = getParam("value", text, sizeof(text)) && parseU32(text, value) &&
                                ((preset.type == PresetType::Time && value >= kMinTimePresetSec &&
                                  value <= kMaxTimePresetSec) ||
                                 (preset.type == PresetType::Volume && value >= kMinVolumePresetMl &&
                                  value <= kMaxVolumePresetMl));
        if (!validValue) {
            delete candidate;
            if (browserForm) {
                char location[80]{};
                std::snprintf(location,
                              sizeof(location),
                              "/faucet/presets?index=%lu&error=invalid_value",
                              static_cast<unsigned long>(index));
                Esp32BaseWeb::redirectSeeOther(location);
                return;
            }
            Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_value\"}");
            return;
        }
        preset.value = value;
        Esp32BaseWeb::getParam("name", preset.name, sizeof(preset.name));
        if (browserForm) {
            const bool ok = persistConfig(*candidate);
            delete candidate;
            Esp32BaseWeb::redirectSeeOther(ok ? "/faucet/presets?saved=1" : "/faucet/presets?error=save_failed");
        } else {
            saveConfigAndReply(*candidate);
            delete candidate;
        }
        return;
    }
    char json[1536]{};
    sendJsonBuffer(writePresetsJson(g_context.config->presets, json, sizeof(json)), json);
}

void handleRecordsApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char text[24]{};
    std::uint32_t page = 0;
    std::uint32_t pageSize = kDefaultRecordPageSize;
    if (getParam("page", text, sizeof(text))) {
        parseU32(text, page);
    }
    if (getParam("pageSize", text, sizeof(text))) {
        parseU32(text, pageSize);
    }
    if (pageSize > kMaxRecordPageSize) {
        pageSize = kMaxRecordPageSize;
    }

    const std::uint16_t sanitizedPageSize = sanitizeRecordPageSize(static_cast<std::uint16_t>(pageSize));
    WaterRecord* records = new (std::nothrow) WaterRecord[kMaxRecordPageSize]{};
    char* json = new (std::nothrow) char[32768]{};
    if (!records || !json) {
        delete[] records;
        delete[] json;
        Esp32BaseWeb::sendJson(500, "{\"error\":\"oom\"}");
        return;
    }
    const bool ready = g_context.records->ready();
    const std::size_t readCount = ready ? g_context.records->readPage(page, sanitizedPageSize, records, kMaxRecordPageSize) : 0;
    const std::size_t totalCount = ready ? g_context.records->count() : 0;
    sendJsonBuffer(writeWaterRecordsJson(records,
                                      readCount,
                                      page,
                                      sanitizedPageSize,
                                      totalCount,
                                      g_context.records->storageName(),
                                      json,
                                      32768),
                   json);
    delete[] records;
    delete[] json;
}

void handleStatsApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char json[256]{};
    sendJsonBuffer(writeStatsJson(g_context.app->snapshot().statistics, json, sizeof(json)), json);
}

void handleRecordCalibrationApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }

    WaterRecord record{};
    if (!g_context.records || !g_context.records->ready() || g_context.records->readPage(0, 1, &record, 1) != 1 ||
        !waterRecordCanCalibrate(record)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/records?error=no_calibration_record");
        return;
    }

    char text[32]{};
    std::uint32_t actualMl = 0;
    if (!getParam("actualMl", text, sizeof(text)) || !parseU32(text, actualMl)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/records?error=invalid_value");
        return;
    }

    const CalibrationApplyResult result = g_context.app->applyCalibrationFromRecord(record, actualMl);
    if (result == CalibrationApplyResult::Saved) {
        *g_context.config = g_context.app->config();
        if (!g_context.configStore->saveSystemConfig(*g_context.config)) {
            Esp32BaseWeb::redirectSeeOther("/faucet/records?error=save_failed");
            return;
        }
        Esp32BaseWeb::redirectSeeOther("/faucet/records?saved=1");
        return;
    }

    const char* error = result == CalibrationApplyResult::TooMuchDrift       ? "calibration_drift"
                        : result == CalibrationApplyResult::NotAvailable    ? "busy"
                        : result == CalibrationApplyResult::InvalidRecord   ? "no_calibration_record"
                                                                             : "invalid_value";
    char url[80]{};
    std::snprintf(url, sizeof(url), "/faucet/records?error=%s", error);
    Esp32BaseWeb::redirectSeeOther(url);
}

void handleFiltersApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!g_context.app->canApplyConfig()) {
            Esp32BaseWeb::redirectSeeOther("/faucet/filters?error=busy");
            return;
        }
        char text[24]{};
        std::uint32_t index = 0;
        if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kFilterCount) {
            Esp32BaseWeb::redirectSeeOther("/faucet/filters?error=invalid_index");
            return;
        }

        FilterRecord record = g_context.filters->record(index);
        record.enabled = checkboxParam("enabled");
        Esp32BaseWeb::getParam("name", record.name, sizeof(record.name));
        std::uint32_t months = 0;
        if (getParam("recommendMonths", text, sizeof(text)) && parseU32(text, months)) {
            record.recommendDays = monthsToDays(months);
        }
        if (getParam("maxMonths", text, sizeof(text)) && parseU32(text, months)) {
            record.maxDays = monthsToDays(months);
        }
        applyU32Param("lifeMl", record.lifeMl);
        if (getParam("startDate", text, sizeof(text))) {
            if (!parseDate(text, record.startTime)) {
                char location[80]{};
                std::snprintf(location,
                              sizeof(location),
                              "/faucet/filters/edit?index=%lu&error=invalid_date",
                              static_cast<unsigned long>(index));
                Esp32BaseWeb::redirectSeeOther(location);
                return;
            }
            record.startBootId = 0;
            record.usedMl = sumRealRecordVolumeSince(record.startTime);
        }
        record.name[kFilterNameLength - 1] = '\0';

        const bool ok = persistFilterConfig(record, index);
        Esp32BaseWeb::redirectSeeOther(ok ? "/faucet/filters?saved=1" : "/faucet/filters?error=save_failed");
        return;
    }
    char json[1536]{};
    sendJsonBuffer(writeFiltersJson(g_context.filters->records(), json, sizeof(json)), json);
}

void handleFiltersResetApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char text[24]{};
    std::uint32_t index = 0;
    if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kFilterCount) {
        Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_index\"}");
        return;
    }

    const std::uint32_t now = g_context.nowSeconds();
    FilterRecord record = g_context.filters->record(index);
    record.startTime = now;
    record.usedMl = 0;
    record.startBootId = now >= kMinRealDateSeconds ? 0 : (g_context.bootId ? g_context.bootId() : 0);
    if (!g_context.filters->updateFilter(index, record)) {
        Esp32BaseWeb::sendJson(500, "{\"error\":\"reset_failed\"}");
        return;
    }
    const bool ok = g_context.configStore->saveFilterRuntime(g_context.filters->records());
    Esp32BaseWeb::redirectSeeOther(ok ? "/faucet/filters?reset=1" : "/faucet/filters?error=save_failed");
}

Esp32BaseWeb::Handler handlerFor(const FaucetWebRoute& route) {
    if (route.kind == FaucetWebRouteKind::Page) {
        if (std::strcmp(route.path, "/faucet") == 0) {
            return handleFaucetPage;
        }
        if (std::strcmp(route.path, "/faucet/presets") == 0) {
            return handlePresetsPage;
        }
        if (std::strcmp(route.path, "/faucet/records") == 0) {
            return handleRecordsPage;
        }
        if (std::strcmp(route.path, "/faucet/stats") == 0) {
            return handleStatsPage;
        }
        if (std::strcmp(route.path, "/faucet/filters") == 0) {
            return handleFiltersPage;
        }
        return handleFaucetPage;
    }
    if (std::strcmp(route.path, "/api/faucet/status") == 0) {
        return handleStatusApi;
    }
    if (std::strcmp(route.path, "/faucet/filters/edit") == 0) {
        return handleFilterEditPage;
    }
    if (std::strcmp(route.path, "/api/faucet/presets") == 0) {
        return handlePresetsApi;
    }
    if (std::strcmp(route.path, "/api/faucet/records") == 0) {
        return handleRecordsApi;
    }
    if (std::strcmp(route.path, "/api/faucet/stats") == 0) {
        return handleStatsApi;
    }
    if (std::strcmp(route.path, "/api/faucet/filters") == 0) {
        return handleFiltersApi;
    }
    if (std::strcmp(route.path, "/api/faucet/filters/reset") == 0) {
        return handleFiltersResetApi;
    }
    if (std::strcmp(route.path, "/api/faucet/records/calibration") == 0) {
        return handleRecordCalibrationApi;
    }
    return handleApi;
}

}  // namespace

void setFaucetWebContext(const FaucetWebContext& context) {
    g_context = context;
}

bool registerFaucetWeb() {
    if (!faucetWebRoutesFitEsp32Base()) {
        return false;
    }

    Esp32BaseWeb::setHeadExtraCallback(sendAppStyles);

    bool ok = true;
    const FaucetWebRoute* routes = faucetWebRoutes();
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (!faucetWebRouteAllowed(routes[i].path)) {
            ok = false;
            continue;
        }
        if (routes[i].kind == FaucetWebRouteKind::Page) {
            ok = Esp32BaseWeb::addPage(routes[i].path, routes[i].title, handlerFor(routes[i])) && ok;
        } else {
            ok = Esp32BaseWeb::addRoute(routes[i].path, toBaseMethod(routes[i].method), handlerFor(routes[i])) && ok;
        }
    }
    return ok;
}

}  // namespace faucet

#endif
