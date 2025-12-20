// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#include "wire_decoder.hpp"

#include <sstream>

namespace vep::exporter {

DecodedQuality decode_quality(vep::transfer::Quality pb_quality) {
    switch (pb_quality) {
        case vep::transfer::QUALITY_VALID:
            return DecodedQuality::VALID;
        case vep::transfer::QUALITY_INVALID:
            return DecodedQuality::INVALID;
        case vep::transfer::QUALITY_NOT_AVAILABLE:
            return DecodedQuality::NOT_AVAILABLE;
        default:
            return DecodedQuality::UNKNOWN;
    }
}

// Helper to decode struct value
static DecodedStruct decode_struct_value(const vep::transfer::StructValue& pb_struct) {
    DecodedStruct result;
    result.type_name = pb_struct.type_name();

    for (const auto& pb_field : pb_struct.fields()) {
        DecodedStructField field;
        field.name = pb_field.name();

        // Decode field value based on type
        switch (pb_field.value_case()) {
            case vep::transfer::StructField::kBoolVal:
                field.value = pb_field.bool_val();
                break;
            case vep::transfer::StructField::kInt32Val:
                field.value = pb_field.int32_val();
                break;
            case vep::transfer::StructField::kInt64Val:
                field.value = pb_field.int64_val();
                break;
            case vep::transfer::StructField::kUint32Val:
                field.value = pb_field.uint32_val();
                break;
            case vep::transfer::StructField::kUint64Val:
                field.value = pb_field.uint64_val();
                break;
            case vep::transfer::StructField::kFloatVal:
                field.value = pb_field.float_val();
                break;
            case vep::transfer::StructField::kDoubleVal:
                field.value = pb_field.double_val();
                break;
            case vep::transfer::StructField::kStringVal:
                field.value = pb_field.string_val();
                break;
            case vep::transfer::StructField::kStructVal:
                field.value = decode_struct_value(pb_field.struct_val());
                break;
            default:
                field.value = std::monostate{};
                break;
        }

        result.fields.push_back(std::move(field));
    }

    return result;
}

DecodedSignal decode_signal(const vep::transfer::Signal& pb_signal,
                            int64_t timestamp_ms) {
    DecodedSignal signal;

    // Get path (either full path or interned ID)
    if (pb_signal.has_path()) {
        signal.path = pb_signal.path();
    } else {
        // Path ID - would need dictionary lookup
        signal.path = "<path_id:" + std::to_string(pb_signal.path_id()) + ">";
    }

    // Use the pre-computed timestamp from the TransferItem
    signal.timestamp_ms = timestamp_ms;
    signal.quality = decode_quality(pb_signal.quality());

    // Decode value based on type
    switch (pb_signal.value_case()) {
        case vep::transfer::Signal::kBoolVal:
            signal.value = pb_signal.bool_val();
            break;
        case vep::transfer::Signal::kInt32Val:
            signal.value = pb_signal.int32_val();
            break;
        case vep::transfer::Signal::kInt64Val:
            signal.value = pb_signal.int64_val();
            break;
        case vep::transfer::Signal::kUint32Val:
            signal.value = pb_signal.uint32_val();
            break;
        case vep::transfer::Signal::kUint64Val:
            signal.value = pb_signal.uint64_val();
            break;
        case vep::transfer::Signal::kFloatVal:
            signal.value = pb_signal.float_val();
            break;
        case vep::transfer::Signal::kDoubleVal:
            signal.value = pb_signal.double_val();
            break;
        case vep::transfer::Signal::kStringVal:
            signal.value = pb_signal.string_val();
            break;
        case vep::transfer::Signal::kBoolArray: {
            std::vector<bool> arr;
            for (bool v : pb_signal.bool_array().values()) {
                arr.push_back(v);
            }
            signal.value = std::move(arr);
            break;
        }
        case vep::transfer::Signal::kInt32Array: {
            std::vector<int32_t> arr;
            for (int32_t v : pb_signal.int32_array().values()) {
                arr.push_back(v);
            }
            signal.value = std::move(arr);
            break;
        }
        case vep::transfer::Signal::kInt64Array: {
            std::vector<int64_t> arr;
            for (int64_t v : pb_signal.int64_array().values()) {
                arr.push_back(v);
            }
            signal.value = std::move(arr);
            break;
        }
        case vep::transfer::Signal::kUint32Array: {
            std::vector<uint32_t> arr;
            for (uint32_t v : pb_signal.uint32_array().values()) {
                arr.push_back(v);
            }
            signal.value = std::move(arr);
            break;
        }
        case vep::transfer::Signal::kUint64Array: {
            std::vector<uint64_t> arr;
            for (uint64_t v : pb_signal.uint64_array().values()) {
                arr.push_back(v);
            }
            signal.value = std::move(arr);
            break;
        }
        case vep::transfer::Signal::kFloatArray: {
            std::vector<float> arr;
            for (float v : pb_signal.float_array().values()) {
                arr.push_back(v);
            }
            signal.value = std::move(arr);
            break;
        }
        case vep::transfer::Signal::kDoubleArray: {
            std::vector<double> arr;
            for (double v : pb_signal.double_array().values()) {
                arr.push_back(v);
            }
            signal.value = std::move(arr);
            break;
        }
        case vep::transfer::Signal::kStringArray: {
            std::vector<std::string> arr;
            for (const auto& v : pb_signal.string_array().values()) {
                arr.push_back(v);
            }
            signal.value = std::move(arr);
            break;
        }
        case vep::transfer::Signal::kStructVal:
            signal.value = decode_struct_value(pb_signal.struct_val());
            break;
        case vep::transfer::Signal::kStructArray: {
            std::vector<DecodedStruct> arr;
            for (const auto& s : pb_signal.struct_array().values()) {
                arr.push_back(decode_struct_value(s));
            }
            signal.value = std::move(arr);
            break;
        }
        default:
            signal.value = std::monostate{};
            break;
    }

    return signal;
}

DecodedEvent decode_event(const vep::transfer::Event& pb_event,
                          int64_t timestamp_ms) {
    DecodedEvent event;
    event.event_id = pb_event.event_id();
    event.timestamp_ms = timestamp_ms;
    event.category = pb_event.category();
    event.event_type = pb_event.event_type();
    event.severity = static_cast<int32_t>(pb_event.severity());

    // Copy payload bytes
    const auto& payload = pb_event.payload();
    event.payload.assign(payload.begin(), payload.end());

    return event;
}

DecodedMetric decode_metric(const vep::transfer::Metric& pb_metric,
                            int64_t timestamp_ms) {
    DecodedMetric metric;
    metric.name = pb_metric.name();
    metric.timestamp_ms = timestamp_ms;

    // Determine metric type from oneof
    switch (pb_metric.metric_type_case()) {
        case vep::transfer::Metric::kGauge:
            metric.type = MetricType::GAUGE;
            metric.value = pb_metric.gauge();
            break;
        case vep::transfer::Metric::kCounter:
            metric.type = MetricType::COUNTER;
            metric.value = pb_metric.counter();
            break;
        case vep::transfer::Metric::kHistogram:
            metric.type = MetricType::HISTOGRAM;
            metric.value = 0.0;  // No single value for histogram
            metric.sample_count = pb_metric.histogram().sample_count();
            metric.sample_sum = pb_metric.histogram().sample_sum();
            for (double bound : pb_metric.histogram().bucket_bounds()) {
                metric.bucket_bounds.push_back(bound);
            }
            for (uint64_t count : pb_metric.histogram().bucket_counts()) {
                metric.bucket_counts.push_back(count);
            }
            break;
        default:
            metric.type = MetricType::GAUGE;
            metric.value = 0.0;
            break;
    }

    // Decode labels from parallel arrays
    int label_count = std::min(pb_metric.label_keys_size(), pb_metric.label_values_size());
    for (int i = 0; i < label_count; ++i) {
        metric.labels[pb_metric.label_keys(i)] = pb_metric.label_values(i);
    }

    return metric;
}

DecodedLogEntry decode_log(const vep::transfer::LogEntry& pb_log,
                           int64_t timestamp_ms) {
    DecodedLogEntry entry;
    entry.timestamp_ms = timestamp_ms;
    entry.level = static_cast<LogLevel>(pb_log.level());
    entry.component = pb_log.component();
    entry.message = pb_log.message();

    // Decode attributes from parallel arrays
    int attr_count = std::min(pb_log.attr_keys_size(), pb_log.attr_values_size());
    for (int i = 0; i < attr_count; ++i) {
        entry.attributes[pb_log.attr_keys(i)] = pb_log.attr_values(i);
    }

    return entry;
}

std::optional<DecodedTransferBatch> decode_transfer_batch(const std::vector<uint8_t>& data) {
    vep::transfer::TransferBatch pb_batch;
    if (!pb_batch.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        return std::nullopt;
    }

    DecodedTransferBatch batch;
    batch.source_id = pb_batch.source_id();
    batch.sequence = pb_batch.sequence();
    batch.base_timestamp_ms = pb_batch.base_timestamp_ms();

    for (const auto& pb_item : pb_batch.items()) {
        DecodedItem item;
        item.timestamp_ms = batch.base_timestamp_ms + pb_item.timestamp_delta_ms();

        switch (pb_item.item_case()) {
            case vep::transfer::TransferItem::kSignal:
                item.type = DecodedItemType::SIGNAL;
                item.signal = decode_signal(pb_item.signal(), item.timestamp_ms);
                break;
            case vep::transfer::TransferItem::kEvent:
                item.type = DecodedItemType::EVENT;
                item.event = decode_event(pb_item.event(), item.timestamp_ms);
                break;
            case vep::transfer::TransferItem::kMetric:
                item.type = DecodedItemType::METRIC;
                item.metric = decode_metric(pb_item.metric(), item.timestamp_ms);
                break;
            case vep::transfer::TransferItem::kLog:
                item.type = DecodedItemType::LOG;
                item.log = decode_log(pb_item.log(), item.timestamp_ms);
                break;
            default:
                item.type = DecodedItemType::UNKNOWN;
                break;
        }

        batch.items.push_back(std::move(item));
    }

    return batch;
}

size_t DecodedTransferBatch::signal_count() const {
    size_t count = 0;
    for (const auto& item : items) {
        if (item.type == DecodedItemType::SIGNAL) ++count;
    }
    return count;
}

size_t DecodedTransferBatch::event_count() const {
    size_t count = 0;
    for (const auto& item : items) {
        if (item.type == DecodedItemType::EVENT) ++count;
    }
    return count;
}

size_t DecodedTransferBatch::metric_count() const {
    size_t count = 0;
    for (const auto& item : items) {
        if (item.type == DecodedItemType::METRIC) ++count;
    }
    return count;
}

size_t DecodedTransferBatch::log_count() const {
    size_t count = 0;
    for (const auto& item : items) {
        if (item.type == DecodedItemType::LOG) ++count;
    }
    return count;
}

const char* quality_to_string(DecodedQuality quality) {
    switch (quality) {
        case DecodedQuality::VALID: return "VALID";
        case DecodedQuality::INVALID: return "INVALID";
        case DecodedQuality::NOT_AVAILABLE: return "NOT_AVAILABLE";
        default: return "UNKNOWN";
    }
}

const char* metric_type_to_string(MetricType type) {
    switch (type) {
        case MetricType::GAUGE: return "gauge";
        case MetricType::COUNTER: return "counter";
        case MetricType::HISTOGRAM: return "histogram";
        default: return "unknown";
    }
}

const char* log_level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char* item_type_to_string(DecodedItemType type) {
    switch (type) {
        case DecodedItemType::SIGNAL: return "signal";
        case DecodedItemType::EVENT: return "event";
        case DecodedItemType::METRIC: return "metric";
        case DecodedItemType::LOG: return "log";
        default: return "unknown";
    }
}

std::string value_type_name(const DecodedValue& value) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) return "empty";
        else if constexpr (std::is_same_v<T, bool>) return "bool";
        else if constexpr (std::is_same_v<T, int8_t>) return "int8";
        else if constexpr (std::is_same_v<T, int16_t>) return "int16";
        else if constexpr (std::is_same_v<T, int32_t>) return "int32";
        else if constexpr (std::is_same_v<T, int64_t>) return "int64";
        else if constexpr (std::is_same_v<T, uint8_t>) return "uint8";
        else if constexpr (std::is_same_v<T, uint16_t>) return "uint16";
        else if constexpr (std::is_same_v<T, uint32_t>) return "uint32";
        else if constexpr (std::is_same_v<T, uint64_t>) return "uint64";
        else if constexpr (std::is_same_v<T, float>) return "float";
        else if constexpr (std::is_same_v<T, double>) return "double";
        else if constexpr (std::is_same_v<T, std::string>) return "string";
        else if constexpr (std::is_same_v<T, std::vector<bool>>) return "bool[]";
        else if constexpr (std::is_same_v<T, std::vector<int8_t>>) return "int8[]";
        else if constexpr (std::is_same_v<T, std::vector<int16_t>>) return "int16[]";
        else if constexpr (std::is_same_v<T, std::vector<int32_t>>) return "int32[]";
        else if constexpr (std::is_same_v<T, std::vector<int64_t>>) return "int64[]";
        else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) return "uint8[]";
        else if constexpr (std::is_same_v<T, std::vector<uint16_t>>) return "uint16[]";
        else if constexpr (std::is_same_v<T, std::vector<uint32_t>>) return "uint32[]";
        else if constexpr (std::is_same_v<T, std::vector<uint64_t>>) return "uint64[]";
        else if constexpr (std::is_same_v<T, std::vector<float>>) return "float[]";
        else if constexpr (std::is_same_v<T, std::vector<double>>) return "double[]";
        else if constexpr (std::is_same_v<T, std::vector<std::string>>) return "string[]";
        else if constexpr (std::is_same_v<T, DecodedStruct>) return "struct";
        else if constexpr (std::is_same_v<T, std::vector<DecodedStruct>>) return "struct[]";
        else return "unknown";
    }, value);
}

std::string value_to_string(const DecodedValue& value) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return "<empty>";
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return "\"" + arg + "\"";
        } else if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < arg.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << (arg[i] ? "true" : "false");
            }
            oss << "]";
            return oss.str();
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < arg.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << "\"" << arg[i] << "\"";
            }
            oss << "]";
            return oss.str();
        } else if constexpr (std::is_same_v<T, DecodedStruct>) {
            std::ostringstream oss;
            oss << "{" << arg.type_name << ": ...}";
            return oss.str();
        } else if constexpr (std::is_same_v<T, std::vector<DecodedStruct>>) {
            return "[struct x" + std::to_string(arg.size()) + "]";
        } else {
            // Vector of arithmetic types
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < arg.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << arg[i];
            }
            oss << "]";
            return oss.str();
        }
    }, value);
}

}  // namespace vep::exporter
